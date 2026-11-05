/* The decals: see decal.h. Notes: docs/re/decal.md. */
#include "decal.h"

#include "akev.h"
#include "akev_collide.h"
#include "particle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEMP_MAX = 0x10,            /* the three working polygons' points (P3rDecalInitialize 0x44eac0) */
    OUT_MAX = 0x200,            /* the decal's points */
    IDX_MAX = 0x200,            /* its triangles' indices (0x5ce020) */
    WORK_MAX = 64,              /* the adjacency walk's items (0x5e7428, 0x84 bytes each) */
    KIDS_MAX = 16,              /* the quads one item reaches */
    SKIP = 0x846800,            /* the quads the decal's rays and spheres pass through */
    RING_SIZE = 0x18fff,        /* the dynamic decals' ring ("decal buffer", 0x426eb0) */
    RING_SLOTS = 5000,
    DECALS_MAX = 8192,
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
/* MUrVector_CrossProduct 0x433b70: a x b */
static void cross3(const float *a, const float *b, float *o)
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - b[0] * a[1];
    o[0] = x, o[1] = y, o[2] = z;
}
static void sub3(const float *a, const float *b, float *o) { o[0] = a[0] - b[0], o[1] = a[1] - b[1], o[2] = a[2] - b[2]; }
static void copy3(float *o, const float *a) { o[0] = a[0], o[1] = a[1], o[2] = a[2]; }
/* 0x4340f0: normalized in place; true if too short to be (left as it was) */
static bool normalize_zero(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (fabsf(l) < 9.5367432e-07f) return true;
    l = 1.0f / l;
    v[0] *= l, v[1] *= l, v[2] *= l;
    return false;
}
static void bit_set(uint8_t *b, uint32_t i) { b[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static void bit_clear(uint8_t *b, uint32_t i) { b[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static bool bit_test(const uint8_t *b, uint32_t i) { return (b[i >> 3] >> (i & 7)) & 1; }
/* 0x424380: set, and whether it was */
static bool bit_test_set(uint8_t *b, uint32_t i)
{
    bool was = bit_test(b, i);
    bit_set(b, i);
    return was;
}

/* ---- the level's quads ---- */

static struct {
    const void *env;
    const uint8_t *pts, *planes, *quads, *col, *render;
    uint32_t npts, nplanes, nquads, ncol, nrender;
} E;

void decal_set_environment(const void *env)
{
    memset(&E, 0, sizeof E);
    E.env = env;
    if (!env) return;
    E.pts = akev_points(env, &E.npts);
    E.planes = akev_planes(env, &E.nplanes);
    E.quads = akev_quad_records(env, &E.nquads);
    E.col = akev_collision_records(env, &E.ncol);
    E.render = akev_render_records(env, &E.nrender);
}

static bool quad_ok(uint32_t q) { return E.quads && E.col && q < E.nquads && q < E.ncol; }
static uint32_t quad_flags(uint32_t q) { return rd32(E.quads + 0x38u * q + 0x30); }
static unsigned quad_corners(uint32_t q) { return (quad_flags(q) & 0x40) ? 3 : 4; }
static uint32_t quad_shade(uint32_t q, unsigned k) { return rd32(E.quads + 0x38u * q + 0x20 + 4 * k); }
static void quad_corner(uint32_t q, unsigned k, float o[3])
{
    uint32_t i = rd32(E.quads + 0x38u * q + 4 * k);
    if (i >= E.npts) { o[0] = o[1] = o[2] = 0.0f; return; }
    for (int c = 0; c < 3; c++) o[c] = rdf(E.pts + 12u * i + 4 * c);
}
static uint32_t quad_plane_id(uint32_t q) { return rd32(E.col + 0x1cu * q); }
/* the plane by its index, bit 31 turning it */
static void plane_of(uint32_t id, float pl[4])
{
    uint32_t k = id & 0x7fffffffu;
    float s = (id & 0x80000000u) ? -1.0f : 1.0f;
    if (!E.planes || k >= E.nplanes) { pl[0] = pl[1] = pl[2] = pl[3] = 0.0f; return; }
    for (int c = 0; c < 4; c++) pl[c] = s * rdf(E.planes + 16u * k + 4 * c);
}
static void negate4(float *p) { p[0] = -p[0], p[1] = -p[1], p[2] = -p[2], p[3] = -p[3]; }

/* ---- the working polygons ---- */

typedef struct {
    uint32_t count, max;
    float *pos;                 /* 3 a point */
    float *uv;                  /* 2 a point */
    uint8_t *edges;             /* the clipping edges each point lies on, a bit an edge */
    uint32_t *shade;
} Poly;

static float tmp_pos[3][TEMP_MAX * 3], tmp_uv[3][TEMP_MAX * 2];
static uint8_t tmp_edges[3][TEMP_MAX];
static uint32_t tmp_shade[3][TEMP_MAX];
static Poly tmp[3];             /* 0x5e9580, 0x5e9598 (the clipper's two), 0x5e95b0 */

static float out_pos[OUT_MAX * 3], out_uv[OUT_MAX * 2];
static uint8_t out_edges[OUT_MAX];
static uint32_t out_shade[OUT_MAX];
static Poly out;                /* 0x5e95c8: the decal's points as they are made */
static uint32_t out_plane[OUT_MAX];     /* 0x5e95e8: each point's quad's plane */
static uint32_t remap[OUT_MAX];         /* 0x5e95ec */
static uint8_t shared[OUT_MAX / 8], merged[OUT_MAX / 8];     /* 0x5e95e0, 0x5e95e4 */
static uint16_t idx[IDX_MAX];           /* 0x5ce020 */

static bool shaded;             /* 0x5e9528: not DECAL_UNSHADED */
static float proj[3];           /* 0x5e9530: the direction the decal is pressed in, reversed */
static float cos_wrap;          /* 0x5e957c */
static float root_c[3], root_n[3], root_b[3], root_a[3];   /* 0x5e9540..: the decal's own frame */

void decal_delete_all(void);

static void polys_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    decal_delete_all();
    for (int i = 0; i < 3; i++)
        tmp[i] = (Poly){ 0, TEMP_MAX, tmp_pos[i], tmp_uv[i], tmp_edges[i], tmp_shade[i] };
    out = (Poly){ 0, OUT_MAX, out_pos, out_uv, out_edges, out_shade };
}

static void copy_point(Poly *o, uint32_t k, const Poly *in, uint32_t i)
{
    copy3(o->pos + 3 * k, in->pos + 3 * i);
    o->uv[2 * k] = in->uv[2 * i], o->uv[2 * k + 1] = in->uv[2 * i + 1];
    o->edges[k] = in->edges[i];
}

/* 0x4340c0 with 8: s in 1/256ths, rounded as a float sum rounds */
static uint32_t fixed8(float s)
{
    float t = s + 32768.0f;
    uint32_t u;
    memcpy(&u, &t, 4);
    return u & 0x7fffff;
}

/* 0x447c10: the colours mixed by w/256, each channel */
static uint32_t shade_mix(uint32_t a, uint32_t b, uint32_t w)
{
    uint32_t r = 0;
    for (int k = 0; k < 32; k += 8) r |= ((((a >> k) & 0xff) * (256 - w) + ((b >> k) & 0xff) * w) >> 8 & 0xff) << k;
    return r;
}

/* 0x447cc0: every channel within 2 */
static bool shade_close(uint32_t a, uint32_t b)
{
    for (int k = 0; k < 32; k += 8) {
        int d = (int)((a >> k) & 0xff) - (int)((b >> k) & 0xff);
        if (d > 2 || d < -2) return false;
    }
    return true;
}

/* 0x432f10: inside the plane by more than 1/1024 */
static bool inside(const float *p, const float *pl) { return dot3(p, pl) + pl[3] < -0.0009765625f; }

/* P3iDecal_LinePlaneIntersection 0x450a90: where the segment from p0 to p1 meets the plane (its middle
   if they are parallel), the coordinates between theirs, and the shade along the edge a-b of the quad
   (e = b - a, ee its length squared) from sa to sb. */
static void line_plane(const float *p0, const float *p1, const float *uv0, const float *uv1, const float *pl, const float *a,
                       const float *e, float ee, uint32_t sa, uint32_t sb, float *o, float *ouv, uint32_t *oshade)
{
    float d[3];
    sub3(p1, p0, d);
    float dn = dot3(d, pl), t = 0.5f;
    if (fabsf(dn) >= 1e-6f) {
        t = -(dot3(p0, pl) + pl[3]) / dn;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }
    float s = 1.0f - t;
    for (int k = 0; k < 3; k++) o[k] = p0[k] + t * d[k];
    ouv[0] = t * uv1[0] + s * uv0[0], ouv[1] = t * uv1[1] + s * uv0[1];
    if (oshade) {
        float q[3];
        sub3(o, a, q);
        float f = dot3(q, e) / ee;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        *oshade = shade_mix(sa, sb, fixed8(f));
    }
}

static void overflow_points(uint32_t max) { fprintf(stderr, "P3iDecal_ClipToPlane: decal has more than %u points, overflowed output buffer\n", max); }

/* P3iDecal_ClipToPlane 0x450600: the polygon in cut by the plane through the edge a-b, the side of c (the
   rest of the quad) kept, onto o's end: the points inside, and where the polygon's edges cross the
   plane. A crossing lies on the edge (its bits, the two ends' common bits and bit) and takes its shade
   along a-b. The points added. */
static uint32_t clip_plane(uint8_t bit, const float *a, const float *b, uint32_t sa, uint32_t sb, const float *c, const Poly *in, Poly *o)
{
    uint32_t start = o->count;
    if (o->count >= o->max) return 0;
    float e[3], f[3], n1[3], n[3];
    sub3(b, a, e);
    sub3(c, b, f);
    cross3(e, f, n1);
    cross3(e, n1, n);
    if (normalize_zero(n)) {
        /* an edge of no length: all of it */
        for (uint32_t i = 0; i < in->count; i++) {
            copy_point(o, o->count, in, i);
            if (++o->count >= o->max) {
                overflow_points(o->max);
                break;
            }
        }
        return o->count - start;
    }
    float pl[4] = { n[0], n[1], n[2], -dot3(n, b) };
    float ee = dot3(e, e);
    if (!in->count) return 0;
    uint32_t prev = in->count - 1;
    bool pin = inside(in->pos + 3 * prev, pl);
    for (uint32_t i = 0; i < in->count; i++) {
        bool cin = inside(in->pos + 3 * i, pl);
        uint8_t cb = in->edges[i], pb = in->edges[prev];
        if (pin || cin) {
            if (!pin || !cin) {
                uint32_t k = o->count;
                line_plane(in->pos + 3 * prev, in->pos + 3 * i, in->uv + 2 * prev, in->uv + 2 * i, pl, a, e, ee, sa, sb,
                           o->pos + 3 * k, o->uv + 2 * k, shaded ? o->shade + k : NULL);
                o->edges[k] = (uint8_t)((cb & pb) | bit);
                o->count++;
                if (!pin && o->count >= o->max) {
                    overflow_points(o->max);
                    break;
                }
            }
            if (cin) {
                uint32_t k = o->count;
                copy_point(o, k, in, i);
                if (shaded) o->shade[k] = in->shade[i];
                o->count++;
            }
            if (o->count >= o->max) {
                overflow_points(o->max);
                break;
            }
        }
        prev = i, pin = cin;
    }
    return o->count - start;
}

/* P3iDecal_ClipToPolygon 0x4503c0: the polygon in cut to the quad of n corners pts (shades sh), edge
   by edge (edge k from corner k to k+1, its bit 1 << k) through the two working polygons, the last onto
   o's end. edges (if given) takes the bits of the points added; their count. */
static uint32_t clip_polygon(unsigned n, const float *pts, const uint32_t *sh, Poly *in, uint32_t *edges, Poly *o)
{
    uint32_t start = o->count;
    Poly cur = *in;
    memset(cur.edges, 0, cur.count);
    const float *pp = pts + 3 * (n - 2), *p = pts + 3 * (n - 1);
    uint32_t spp = sh[n - 2], sp = sh[n - 1];
    unsigned k = n - 2;
    for (unsigned i = 0; i < n; i++) {
        Poly *dst = (i == n - 1) ? o : &tmp[i & 1];
        if (i != n - 1) dst->count = 0;
        clip_plane((uint8_t)(1u << k), pp, p, spp, sp, pts + 3 * i, &cur, dst);
        pp = p, spp = sp;
        p = pts + 3 * i, sp = sh[i];
        k = (k + 1) % n;
        if (i != n - 1) cur = *dst;
    }
    if (edges) {
        uint32_t bits = 0;
        for (uint32_t i = start; i < o->count; i++) bits |= o->edges[i];
        *edges |= bits;
    }
    return o->count - start;
}

/* 0x4340a0: to the nearest */
static int round_int(float f) { return (int)lrintf(f); }

/* P3iDecal_BarycentricShadeInterpolate 0x450c30: the shade at p in the triangle t0 t1 t2 (shades s0 s1
   s2, n its face's normal), each colour weighted by the areas facing its corner; the alpha left 0. */
static uint32_t barycentric(const float *t0, const float *t1, const float *t2, uint32_t s0, uint32_t s1, uint32_t s2,
                            const float *n, const float *p)
{
    float e[3], d[3], x[3], w[3];
    const float *v[3] = { t0, t1, t2 };
    for (int i = 0; i < 3; i++) {
        const float *b = v[(i + 1) % 3], *c = v[(i + 2) % 3];
        sub3(c, b, e);
        sub3(p, b, d);
        cross3(e, d, x);
        w[i] = dot3(n, x);
    }
    float sum = w[0] + w[1] + w[2];
    uint32_t s[3] = { s0, s1, s2 }, r = 0;
    for (int k = 0; k < 24; k += 8) {
        float c = 0.0f;
        for (int i = 0; i < 3; i++) c += (float)((s[i] >> k) & 0xff) * w[i];
        int v8 = 0;
        if (c >= 0.0f) {
            v8 = round_int(c / sum);
            if (v8 > 0xff) v8 = 0xff;
        }
        r |= (uint32_t)(v8 & 0xff) << k;
    }
    return r;
}

/* The shade of a point the clipping left unshaded (0), from the quad's corners: a triangle's, or a quad's
   half the point is on (by the diagonal from corner 0 to 2). n is the face's normal from its first two
   edges, side the plane's normal the halves are told apart by. */
static uint32_t quad_point_shade(unsigned nc, const float *c, const uint32_t *s, const float *n, const float *side, const float *p)
{
    if (nc < 4) return barycentric(c, c + 3, c + 6, s[0], s[1], s[2], n, p);
    float d[3], diag[3], x[3];
    sub3(p, c, d);
    sub3(c + 6, c, diag);
    cross3(d, diag, x);
    if (dot3(x, side) <= 0.0f) return barycentric(c, c + 6, c + 9, s[0], s[2], s[3], n, p);
    return barycentric(c, c + 3, c + 6, s[0], s[1], s[2], n, p);
}

/* The decal's corners about the centre c along its axes (a by xs, b by ys), their coordinates. */
static void decal_corners(const float *c, const float *a, const float *b, float xs, float ys, float q[12], float uv[8])
{
    for (int k = 0; k < 3; k++) {
        float ax = a[k] * xs, by = b[k] * ys;
        q[k] = c[k] - ax + by;
        q[3 + k] = c[k] + ax + by;
        q[6 + k] = c[k] + ax - by;
        q[9 + k] = c[k] - ax - by;
    }
    static const float UV[8] = { 1, 0, 0, 0, 0, 1, 1, 1 };
    memcpy(uv, UV, sizeof UV);
}

/* ---- the walk over the quads (P3iDecal_ProcessAdjacency) ---- */

typedef struct Work Work;
struct Work {
    bool used;
    bool clip;                  /* +0x01: cut by the line it shares with the quad before */
    bool crosses;               /* +0x02: the next quad goes through its plane */
    uint32_t depth;             /* +0x04: the folds from the first quad */
    int32_t parent_list;        /* +0x08: the quad before, in the hits (-1: the first quad's own) */
    int32_t parent_quad;        /* +0x0c */
    int32_t list;               /* +0x10: in the hits */
    uint32_t quad;              /* +0x14 */
    float frame[6];             /* +0x18: the centre (u, v) from e0, b (u, v), a (u, v) along the edge */
    float e0[3], e1[3];         /* +0x30, +0x3c: the edge shared with the quad before */
    float t0, t1;               /* +0x48: the part of it the decal covers */
    float mid[3];               /* +0x50 */
    float de[3];                /* +0x5c */
    float pnormal[3];           /* +0x68: the quad before's normal */
    uint32_t bits_parent;       /* +0x74: the edges of the quad before it lies on */
    uint32_t bits_self;         /* +0x78: its own edges it lies on */
    Work *prev, *next;          /* +0x7c, +0x80 */
};
static Work work[WORK_MAX];
static Work *queue;             /* 0x5e7424: by depth */
static AkevHit hits[AKEV_HITS_MAX];
static unsigned nhits;
static uint8_t visited[OUT_MAX / 8];    /* 0x5cdfe0 */

/* 0x450ed0: a free item, after every queued one no deeper */
static Work *work_new(uint32_t depth)
{
    for (int i = 0; i < WORK_MAX; i++) {
        Work *w = &work[i];
        if (w->used) continue;
        Work *before = NULL, *after = queue;
        while (after && after->depth <= depth) before = after, after = after->next;
        w->prev = before;
        if (before) before->next = w;
        else queue = w;
        w->next = after;
        if (after) after->prev = w;
        w->used = true;
        return w;
    }
    return NULL;
}

static void work_free(Work *w)
{
    if (w->prev) w->prev->next = w->next;
    else queue = w->next;
    if (w->next) w->next->prev = w->prev;
    w->used = false;
}

/* Where two planes meet in a quad's outline: its corners on the other's plane (d within 0.01; on, their
   bits) and the edges whose ends lie on its two sides, walked round once and to the first corner again.
   At most two points kept; the edges they lie on into bits. The points found. */
static unsigned plane_points(unsigned n, const float *d, unsigned on, const float *c, float out2[6], uint32_t *bits)
{
    unsigned cnt = 0, prev = 0;
    bool prev_on = true;
    for (unsigned i = 0; i <= n; i++) {
        if (!(on & (1u << i))) {
            unsigned ii = i % n;
            if (!prev_on && d[ii] * d[prev] < 0.0f) {
                *bits |= 1u << prev;
                if (cnt < 2) {
                    float den = d[ii] - d[prev], f = d[ii] / den, g = -(d[prev] / den);
                    for (int k = 0; k < 3; k++) out2[3 * cnt + k] = f * c[3 * prev + k] + g * c[3 * ii + k];
                }
                cnt++;
            }
            prev_on = false;
            prev = ii;
        } else {
            if (on & (1u << ((i + 1) % n))) *bits |= 1u << i;
            if (cnt < 2) copy3(out2 + 3 * cnt, c + 3 * i);
            cnt++;
            prev_on = true;
        }
    }
    return cnt;
}

/* The corners' distances from a plane: behind (below -0.01), on (their bits), in front. */
static void classify(unsigned n, const float *c, const float *pl, float *d, unsigned *on, unsigned *bits, unsigned *front, unsigned *behind)
{
    *on = *bits = *front = *behind = 0;
    for (unsigned i = 0; i < n; i++) {
        d[i] = dot3(pl, c + 3 * i) + pl[3];
        if (d[i] >= -0.01f) {
            if (d[i] <= 0.01f) (*on)++, *bits |= 1u << i;
            else (*front)++;
        } else (*behind)++;
    }
}

/* The edge of the decal's frame for a quad reached over it: its direction u, and v = u x n on the quad. */
static void edge_axes(const float *e0, const float *e1, const float *n, float *u, float *v)
{
    sub3(e1, e0, u);
    float l = 1.0f / sqrtf(dot3(u, u));
    for (int k = 0; k < 3; k++) u[k] *= l;
    cross3(u, n, v);
    l = 1.0f / sqrtf(dot3(v, v));
    for (int k = 0; k < 3; k++) v[k] *= l;
}

/* The part of the segment from u0, v0 by du, dv in the decal's rectangle (|u| < xs, |v| < ys), as the
   segment's parameters; false if it misses. */
static bool segment_in_rect(float u0, float du, float v0, float dv, float xs, float ys, float *t0, float *t1)
{
    float ui, uo, vi, vo;
    if (du < -0.001f) {
        ui = (xs - u0) / du, uo = (-xs - u0) / du;
        if (!(ui < 1.0f && uo > 0.0f)) return false;
    } else if (du > 0.001f) {
        ui = (-xs - u0) / du, uo = (xs - u0) / du;
        if (!(ui < 1.0f && uo > 0.0f)) return false;
    } else {
        ui = -1e9f, uo = 1e9f;
        if (!(-xs < u0 && u0 < xs)) return false;
    }
    if (dv < -0.001f) {
        vi = (ys - v0) / dv, vo = (-ys - v0) / dv;
        if (vi >= 1.0f || vo <= 0.0f) return false;
    } else if (dv > 0.001f) {
        vi = (-ys - v0) / dv, vo = (ys - v0) / dv;
        if (vi >= 1.0f || vo <= 0.0f) return false;
    } else {
        vi = -1e9f, vo = 1e9f;
        if (v0 <= -ys || ys <= v0) return false;
    }
    *t0 = vi < ui ? ui : vi;
    *t1 = vo < uo ? vo : uo;
    return true;
}

/* P3iDecal_ProcessAdjacency 0x450f50: one quad of the walk. The decal unfolded onto it (the first quad's
   frame, or the one carried over the shared edge), the quads it shares an edge with (on its plane: edges
   along each other or crossing; on another: where the planes meet inside both, if that plane faces the
   decal within the wrap) queued with the decal's frame along that edge; then the decal's corners pressed
   onto its plane, cut to it (and to the lines it shares with quads that go through it), and the points
   and triangles added if they reach the edge it was reached over. Only the queued quads the cut reaches
   stay queued. */
static void walk(Work *w, unsigned flags, float xs, float ys, uint32_t *ntris, uint32_t *nidx)
{
    Work *kids[KIDS_MAX];
    unsigned nk = 0;
    bool accepted = true;
    if (w->parent_list != -1) {
        /* the quad itself: cut along its edge if it goes through the plane of the quad before */
        w->list = -1;   /* (so its own quads' children take -1 for the quad before: the original's) */
        kids[0] = w;
        w->clip = w->crosses;
        nk = 1;
    }
    uint32_t q = w->quad, qf = quad_flags(q);
    unsigned nc = quad_corners(q);
    float c[12], e[12];
    for (unsigned k = 0; k < nc; k++) quad_corner(q, k, c + 3 * k);
    for (unsigned k = 0; k < nc; k++) sub3(c + 3 * ((k + 1) % nc), c + 3 * k, e + 3 * k);
    uint32_t qpid = quad_plane_id(q);
    float P[4];
    plane_of(qpid, P);
    bool flipped = false;
    if (qf & 0x280) {
        const float *ref = w->parent_quad == -1 ? root_n : w->pnormal;
        if (dot3(P, ref) < -0.001f) negate4(P), flipped = true;
    }
    float C[3], N[3], B[3], A[3];
    if (w->parent_quad == -1) {
        copy3(C, root_c), copy3(N, root_n), copy3(B, root_b), copy3(A, root_a);
    } else {
        float u[3], v[3];
        copy3(N, P);
        edge_axes(w->e0, w->e1, P, u, v);
        for (int k = 0; k < 3; k++) {
            C[k] = w->frame[1] * v[k] + w->frame[0] * u[k] + w->e0[k];
            B[k] = w->frame[3] * v[k] + w->frame[2] * u[k];
            A[k] = w->frame[5] * v[k] + w->frame[4] * u[k];
        }
    }
    float Nq[3];
    cross3(e, e + 3, Nq);

    for (unsigned li = 0; li < nhits; li++) {
        if ((int32_t)li == w->list || (int32_t)li == w->parent_list) continue;
        uint32_t nq = hits[li].quad;
        if (!quad_ok(nq)) continue;
        unsigned nn = quad_corners(nq);
        uint32_t npid = quad_plane_id(nq);
        float n[12];
        for (unsigned k = 0; k < nn; k++) quad_corner(nq, k, n + 3 * k);
        uint32_t bits_c = 0, bits_n = 0, depth;
        bool clip = false, crosses = false;
        float seg[6];
        if (((qpid ^ npid) & 0x7fffffffu) == 0) {
            /* on the same plane: the edges that lie along each other, or that the other's cross */
            depth = w->depth;
            unsigned npts = 0;
            for (unsigned k = 0; k < nn; k++) {
                const float *nk0 = n + 3 * k, *nk1 = n + 3 * ((k + 1) % nn);
                float en[3];
                sub3(nk1, nk0, en);
                for (unsigned j = 0; j < nc; j++) {
                    const float *cj0 = c + 3 * j, *cj1 = c + 3 * ((j + 1) % nc);
                    float x[3], d0[3], d1[3];
                    cross3(en, e + 3 * j, x);
                    if (dot3(x, x) < 1e-4f) {
                        sub3(cj0, nk0, d0);
                        cross3(en, d0, x);
                        if (dot3(x, x) > 1e-4f) continue;
                        float ee = dot3(en, en), t0 = dot3(d0, en), t1;
                        sub3(cj1, nk0, d1);
                        t1 = dot3(d1, en);
                        const float *p0 = cj0, *p1 = cj1;
                        if (t0 > t1) {
                            float t = t0;
                            t0 = t1, t1 = t;
                            p0 = cj1, p1 = cj0;
                        }
                        if (t1 < 0.01f || ee - 0.01f < t0) continue;
                        bits_c |= 1u << j, bits_n |= 1u << k;
                        if (npts < 2) {
                            copy3(seg + 3 * npts++, t0 >= 0.0f ? p0 : nk0);
                            /* the far end the other way round from the near one's: the original's */
                            if (npts < 2) copy3(seg + 3 * npts++, t1 >= ee ? p1 : nk1);
                        }
                    } else {
                        float x1[3];
                        sub3(cj0, nk0, d0);
                        cross3(en, d0, x);
                        sub3(cj1, nk0, d1);
                        cross3(en, d1, x1);
                        if (dot3(x, x1) >= 0.0f) continue;
                        float a = dot3(x, Nq), b = dot3(x1, Nq), den = b - a, p[3], dp[3];
                        for (int m = 0; m < 3; m++) p[m] = (b / den) * cj0[m] + (-(a / den)) * cj1[m];
                        sub3(p, nk0, dp);
                        float s = dot3(dp, en) / dot3(en, en);
                        if (s < 0.01f || s > 0.99f) continue;
                        bits_c |= 1u << j, bits_n |= 1u << k;
                        if (npts < 2) copy3(seg + 3 * npts++, p);
                        crosses = true;
                    }
                }
            }
            if (npts < 2) continue;
        } else {
            /* on another plane: where the two meet inside both quads */
            depth = w->depth + 1;
            float NP[4];
            plane_of(npid, NP);
            bool two_sided = (quad_flags(nq) & 0x280) != 0;
            if (two_sided && dot3(NP, P) < 0.001f) negate4(NP);
            float dc[4], dn[4];
            unsigned on_c, on_cb, front_c, behind_c, on_n, on_nb, front_n, behind_n;
            classify(nc, c, NP, dc, &on_c, &on_cb, &front_c, &behind_c);
            if (!(on_c > 1 || (front_c && behind_c))) continue;
            classify(nn, n, P, dn, &on_n, &on_nb, &front_n, &behind_n);
            if (!(on_n > 1 || (front_n && behind_n))) continue;
            if ((!front_n || !front_c) && (!behind_n || !behind_c)) {
                /* a step, not a fold: only a two-sided quad, seen from its other side */
                if (!two_sided) continue;
                unsigned f0 = front_c, b0 = behind_c;
                front_c = b0, behind_c = f0;
                if (!((front_n && b0) || (behind_n && f0))) continue;
                /* the distances turned over the other quad's corner count, and its plane once for each:
                   the original's */
                for (unsigned i = 0; i < nn; i++) {
                    dc[i] = -dc[i];
                    negate4(NP);
                }
            }
            float f = dot3(NP, proj);
            if (!(((flags & DECAL_CULL) == 0 || f >= -0.001f) && f >= cos_wrap)) continue;
            float pc[6], pn[6];
            if (plane_points(nc, dc, on_cb, c, pc, &bits_c) < 2) continue;
            if (plane_points(nn, dn, on_nb, n, pn, &bits_n) < 2) continue;
            if (front_c && behind_c) bits_c = 0;
            if (front_n && behind_n) bits_n = 0;
            float L[3];
            cross3(P, NP, L);
            float tn0 = dot3(L, pn), tn1 = dot3(L, pn + 3), tc0 = dot3(L, pc), tc1 = dot3(L, pc + 3);
            if (tn1 < tn0) {
                float t = tn0, s[3];
                tn0 = tn1, tn1 = t;
                copy3(s, pn), copy3(pn, pn + 3), copy3(pn + 3, s);
            }
            if (tc1 < tc0) {
                float t = tc0, s[3];
                tc0 = tc1, tc1 = t;
                copy3(s, pc), copy3(pc, pc + 3), copy3(pc + 3, s);
            }
            if (!(tn1 - tc0 >= 0.001f && tc1 - tn0 >= 0.001f)) continue;
            clip = front_c && behind_c;
            crosses = front_n && behind_n;
            copy3(seg, tc0 <= tn0 ? pn : pc);
            copy3(seg + 3, tn1 <= tc1 ? pn + 3 : pc + 3);
        }
        /* queue it, with the decal's frame along the shared edge */
        Work *k = work_new(depth);
        if (!k) break;
        k->depth = depth;
        k->parent_quad = (int32_t)q;
        k->parent_list = w->list;
        k->quad = nq;
        k->list = (int32_t)li;
        k->clip = clip, k->crosses = crosses;
        copy3(k->e0, seg), copy3(k->e1, seg + 3);
        sub3(k->e1, k->e0, k->de);
        k->bits_parent = bits_c, k->bits_self = bits_n;
        copy3(k->pnormal, P);
        float d[3], s[4];
        for (int m = 0; m < 2; m++) {
            sub3(seg + 3 * m, C, d);
            s[2 * m] = dot3(d, A), s[2 * m + 1] = dot3(d, B);
        }
        float t0, t1;
        if (!segment_in_rect(s[0], s[2] - s[0], s[1], s[3] - s[1], xs, ys, &t0, &t1)) {
            work_free(k);
            continue;
        }
        k->t0 = t0, k->t1 = t1;
        if (t0 > t1) {
            work_free(k);
            continue;
        }
        float tm = (t1 + t0) * 0.5f, u[3], v[3];
        for (int m = 0; m < 3; m++) k->mid[m] = tm * k->de[m] + k->e0[m];
        edge_axes(k->e0, k->e1, P, u, v);
        sub3(C, k->e0, d);
        k->frame[0] = dot3(d, u), k->frame[1] = dot3(d, v);
        k->frame[2] = dot3(B, u), k->frame[3] = dot3(B, v);
        k->frame[4] = dot3(A, u), k->frame[5] = dot3(A, v);
        kids[nk++] = k;
        if (nk >= KIDS_MAX) break;
    }

    /* the decal on this quad */
    float dq[12], duv[8];
    uint32_t dsh[4] = { 0, 0, 0, 0 };
    uint8_t dedges[4];
    decal_corners(C, A, B, xs, ys, dq, duv);
    uint32_t start = out.count, edges = 0;
    float nd = dot3(N, P);
    if (fabsf(nd) >= 0.001f) {
        for (int m = 0; m < 4; m++) {
            float t = -(dot3(P, dq + 3 * m) + P[3]) / nd;
            for (int k = 0; k < 3; k++) dq[3 * m + k] += t * N[k];
        }
        Poly in4 = { 4, 4, dq, duv, dedges, dsh };
        uint32_t qs[4];
        for (unsigned m = 0; m < nc; m++) qs[m] = quad_shade(q, m);
        unsigned active = 0;
        for (unsigned i = 0; i < nk; i++) {
            if (!kids[i]->clip) continue;
            if (kids[i]->t0 < -0.1f || kids[i]->t1 > 1.1f) kids[i]->clip = false;
            else active++;
        }
        const float *third = w->parent_list == -1 ? root_c : w->mid;
        Poly *dst = &out;
        if (active) dst = &tmp[2], tmp[2].count = 0;
        uint32_t added = clip_polygon(nc, c, qs, &in4, &edges, dst);
        if (w->parent_list == -1 || w->bits_self == 0 || (edges & w->bits_self)) {
            unsigned used = 0, done = 0;
            Poly *cur = dst;
            while (added && done < active) {
                Poly *next = (done + 1 < active) ? &tmp[done & 1] : &out;
                if (done + 1 < active) next->count = 0;
                while (!kids[used]->clip)
                    if (++used >= nk) {
                        accepted = false;
                        goto finish;
                    }
                Work *kd = kids[used];
                float third_k[3];
                const float *c3 = third;
                if (kd->list == -1) {
                    for (int m = 0; m < 3; m++) third_k[m] = kd->pnormal[m] + kd->mid[m];
                    c3 = third_k;
                }
                added = clip_plane(0, kd->e0, kd->e1, 0, 0, c3, cur, next);
                used++, done++;
                cur = next;
            }
            added = out.count - start;
            if (added) {
                uint32_t id = flipped ? qpid ^ 0x80000000u : qpid;
                for (uint32_t v = start; v < out.count; v++) {
                    out_plane[v] = id;
                    if (shaded && out.shade[v] == 0) out.shade[v] = quad_point_shade(nc, c, qs, Nq, P, out.pos + 3 * v);
                }
                uint32_t t = 0, ni = *nidx;
                if (added != 2) {
                    for (; t < added - 2; t++) {
                        if (ni + 3 > IDX_MAX) {
                            fprintf(stderr, "P3iDecal_ProcessAdjacency: decal has more than %u triangles\n", IDX_MAX / 3);
                            break;
                        }
                        idx[ni] = (uint16_t)start, idx[ni + 1] = (uint16_t)(start + t + 1), idx[ni + 2] = (uint16_t)(start + t + 2);
                        ni += 3;
                    }
                }
                *ntris += t;
                *nidx = ni;
                goto finish;
            }
        }
    }
    out.count = start;
    accepted = false;
finish:
    for (unsigned i = 0; i < nk; i++) {
        Work *kd = kids[i];
        if (kd->list == -1) continue;
        if (accepted && (kd->bits_parent == 0 || (edges & kd->bits_parent))) continue;
        work_free(kd);
    }
}

/* ---- the decals ---- */

typedef struct {
    bool used, is_static;
    uint32_t id;
    const void *texture;
    int32_t quad;
    uint16_t nverts, ntris, alpha;
    uint32_t tint;
    uint8_t *block;             /* the triangles (NULL once the ring took them) */
    uint32_t owner;             /* the particle */
    int16_t slot;               /* in the ring, -1 none */
    int32_t prev, next;         /* in its list */
} Decal;

static Decal decals[DECALS_MAX];
static uint32_t next_id = 1;
static struct { int32_t head, tail; int count; } lists[2];     /* 0x6a8f70 static, 0x6a8f80 dynamic */

/* The ring (0x426eb0 and 0x427060): the dynamic decals' memory, each new block after the last and over
   the oldest, whose decals lose their triangles (and their particles die, 0x4535d0). */
static struct {
    int16_t oldest, newest;     /* +0x34, +0x36 */
    struct { bool live; int32_t owner; uint32_t off, size; } d[RING_SLOTS];
} ring;

static Decal *by_id(uint32_t id)
{
    if (!id) return NULL;
    Decal *d = &decals[id % DECALS_MAX];
    return (d->used && d->id == id) ? d : NULL;
}

static void list_add(Decal *d)
{
    int L = d->is_static ? 0 : 1, i = (int)(d - decals);
    d->prev = lists[L].tail, d->next = -1;
    if (lists[L].tail >= 0) decals[lists[L].tail].next = i;
    else lists[L].head = i;
    lists[L].tail = i;
    lists[L].count++;
}

static void list_remove(Decal *d)
{
    int L = d->is_static ? 0 : 1;
    if (d->prev >= 0) decals[d->prev].next = d->next;
    else lists[L].head = d->next;
    if (d->next >= 0) decals[d->next].prev = d->prev;
    else lists[L].tail = d->prev;
    d->prev = d->next = -1;
    lists[L].count--;
}

static void decal_free(Decal *d)
{
    free(d->block);
    memset(d, 0, sizeof *d);
}

/* 0x4535d0: the ring took the decal's memory: no triangles, and its particle dies */
static void purge(int32_t owner)
{
    if (owner < 0) return;
    Decal *d = &decals[owner];
    if (!d->used || d->slot == -1) return;
    d->slot = -1;
    free(d->block);
    d->block = NULL;
    P3Class *c;
    P3Particle *p = p3_from_handle(d->owner, &c);
    if (p) p3_kill(c, p);      /* its death deletes the decal */
    else if (d->used && !d->block) {
        list_remove(d);
        decal_free(d);
    }
}

/* 0x427060: the ring's block of size for the decal, its slot; -1 if it can never fit */
static int ring_alloc(uint32_t size, int32_t owner)
{
    size = (size + 3) & ~3u;
    if (size > RING_SIZE) return -1;
    int16_t slot = ring.newest == -1 ? 0 : (int16_t)(ring.newest + 1);
    if (slot >= RING_SLOTS) slot = 0;
    int16_t first = ring.oldest;
    uint32_t pos;
    for (;;) {
        pos = ring.newest == -1 ? 0 : ring.d[ring.newest].off + ring.d[ring.newest].size;
        if (ring.oldest != -1) {
            for (int guard = 0; guard <= RING_SLOTS; guard++) {
                int16_t o = ring.oldest;
                if (!(o == slot || (pos <= ring.d[o].off && ring.d[o].off < pos + size))) break;
                if (ring.d[o].live) {
                    int32_t ow = ring.d[o].owner;
                    ring.d[o].owner = -1;
                    ring.d[o].live = false;
                    purge(ow);
                }
                ring.oldest = (int16_t)(o + 1 >= RING_SLOTS ? 0 : o + 1);
            }
        }
        if (pos + size <= RING_SIZE) break;
        ring.newest = -1;
    }
    ring.d[slot].size = size, ring.d[slot].live = true, ring.d[slot].off = pos, ring.d[slot].owner = owner;
    ring.newest = slot;
    if (first == -1) ring.oldest = slot;
    return slot;
}

/* 0x427240 */
static void ring_free(int16_t slot)
{
    if (slot < 0 || slot >= RING_SLOTS) return;
    ring.d[slot].owner = -1;
    ring.d[slot].live = false;
}

uint32_t decal_create(const void *texture, int32_t quad, unsigned flags, float wrap, const float *dir, const float pos[3],
                      const float normal[3], const float b[3], const float a[3], float xs, float ys, uint16_t alpha,
                      uint32_t tint)
{
    if (!E.env || !E.quads) return 0;
    polys_init();
    copy3(proj, normal);
    if (dir) {
        float t[3];
        copy3(t, dir);
        if (!normalize_zero(t)) proj[0] = -t[0], proj[1] = -t[1], proj[2] = -t[2];
    }
    float w = wrap < 0.0087266462f ? 0.0087266462f : wrap > 3.1415927f ? 3.1415927f : wrap;
    cos_wrap = (float)cos((double)w);
    shaded = !(flags & DECAL_UNSHADED);
    float ray[3] = { normal[0] * -10.0f, normal[1] * -10.0f, normal[2] * -10.0f }, at[3];
    if (quad == -1) {
        AkevHit h[AKEV_HITS_MAX];
        if (!akev_collide_point(E.env, pos, ray, SKIP, 1, h)) return 0;
        quad = (int32_t)h[0].quad;
        copy3(at, h[0].point);
    } else {
        if (!quad_ok((uint32_t)quad) || (quad_flags((uint32_t)quad) & 0x800000)) return 0;
        copy3(at, pos);
    }
    float sphere[4] = { at[0], at[1], at[2], sqrtf(ys * ys + xs * xs) };
    nhits = akev_collide_sphere(E.env, sphere, ray, SKIP, 0xff, hits);
    if (!nhits) return 0;
    if (nhits > OUT_MAX) {
        fprintf(stderr, "P3rCreateDecal: found %u collisions nearby, overflowed P3cDecal_GQBitVectorSize %u\n", nhits, OUT_MAX);
        nhits = OUT_MAX;
    }
    bool planar = (flags & DECAL_PLANAR) != 0;
    out.count = 0;
    uint32_t ntris = 0, nidx = 0;
    if (!planar) {
        memset(visited, 0, sizeof visited);
        unsigned root = 0;
        while (root < nhits && hits[root].quad != (uint32_t)quad) root++;
        if (root >= nhits) root = 0;
        queue = NULL;
        Work *w = work_new(0);
        if (w) {
            w->depth = 0, w->parent_list = -1, w->parent_quad = -1, w->clip = false;
            w->list = (int32_t)root, w->quad = hits[root].quad, w->crosses = false;
            copy3(w->pnormal, normal);
        }
        copy3(root_c, pos), copy3(root_n, normal), copy3(root_b, b), copy3(root_a, a);
        while (queue) {
            w = queue;
            if (w->list < 0 || !bit_test_set(visited, (uint32_t)w->list)) {
                if (quad_ok(w->quad)) walk(w, flags, xs, ys, &ntris, &nidx);
            }
            work_free(w);
        }
    } else {
        /* the characters': the decal pressed along the normal onto each quad the sphere meets, cut to it */
        float dq0[12], duv[8];
        decal_corners(at, a, b, xs, ys, dq0, duv);
        for (unsigned h = 0; h < nhits; h++) {
            uint32_t q = hits[h].quad;
            if (!quad_ok(q)) continue;
            unsigned nc = quad_corners(q);
            float c[12], P[4], dq[12];
            uint32_t qs[4] = { 0, 0, 0, 0 }, dsh[4] = { 0, 0, 0, 0 };
            uint8_t dedges[4];
            plane_of(quad_plane_id(q), P);
            float f = dot3(normal, P);
            if ((quad_flags(q) & 0x280) && f < 0.0f) negate4(P), f = -f;
            if ((flags & DECAL_CULL) && f < 0.001f) continue;
            for (unsigned k = 0; k < nc; k++) {
                quad_corner(q, k, c + 3 * k);
                if (shaded) qs[k] = quad_shade(q, k);
            }
            memcpy(dq, dq0, sizeof dq);
            if (fabsf(f) >= 0.001f)
                for (int m = 0; m < 4; m++) {
                    float t = -(dot3(P, dq + 3 * m) + P[3]) / f;
                    for (int k = 0; k < 3; k++) dq[3 * m + k] += t * normal[k];
                }
            Poly in4 = { 4, 4, dq, duv, dedges, dsh };
            uint32_t start = out.count, added = clip_polygon(nc, c, qs, &in4, NULL, &out);
            if (!added) continue;
            float e[12], Nq[3];
            if (shaded) {
                for (unsigned k = 0; k < nc; k++) sub3(c + 3 * ((k + 1) % nc), c + 3 * k, e + 3 * k);
                cross3(e, e + 3, Nq);
            }
            for (uint32_t v = start; v < out.count; v++) {
                for (int k = 0; k < 3; k++) out.pos[3 * v + k] += normal[k] * 0.05f;
                if (shaded && out.shade[v] == 0) out.shade[v] = quad_point_shade(nc, c, qs, Nq, P, out.pos + 3 * v);
            }
            uint32_t t = 0;
            if (added != 2) {
                for (; t < added - 2; t++) {
                    if (nidx > 0x1fd) {
                        fprintf(stderr, "P3rCreateDecal: decal has more than %d triangles\n", 0xaa);
                        break;
                    }
                    idx[nidx] = (uint16_t)start, idx[nidx + 1] = (uint16_t)(start + t + 1), idx[nidx + 2] = (uint16_t)(start + t + 2);
                    nidx += 3;
                }
            }
            ntris += t;
        }
    }
    if (out.count == 0) return 0;

    /* the points welded: those at one place with the same coordinates and shade become one; those at one
       place otherwise share the first's place. Each point off the walls along its quads' normals. */
    memset(shared, 0, sizeof shared);
    memset(merged, 0, sizeof merged);
    memset(remap, 0, out.count * sizeof *remap);
    uint32_t nv = 0;
    for (uint32_t i = 0; i < out.count; i++) {
        if (bit_test(merged, i)) continue;
        uint32_t pl[16];
        unsigned npl = 0;
        if (!planar) pl[npl++] = out_plane[i];
        const float *p = out.pos + 3 * i, *uv = out.uv + 2 * i;
        for (uint32_t j = i + 1; j < out.count; j++) {
            if (bit_test(merged, j)) continue;
            const float *pj = out.pos + 3 * j;
            if (!(fabsf(pj[0] - p[0]) <= 0.1f && fabsf(pj[1] - p[1]) <= 0.1f && fabsf(pj[2] - p[2]) <= 0.1f)) continue;
            bool same = !(fabsf(out.uv[2 * j] - uv[0]) >= 0.01f || fabsf(out.uv[2 * j + 1] - uv[1]) >= 0.01f ||
                          (shaded && !shade_close(out.shade[i], out.shade[j])));
            if (!planar) pl[npl++] = out_plane[j];
            if (same) {
                bit_set(merged, j);
                remap[j] = nv;
                bit_clear(shared, j);
            } else if (!bit_test_set(shared, j)) {
                remap[j] |= i << 16;
            }
            if (!planar && npl > 15) break;
        }
        remap[i] |= nv & 0xffff;
        nv++;
        if (!planar) {
            float s[3] = { 0, 0, 0 };
            for (unsigned k = 0; k < npl; k++) {
                float P[4];
                plane_of(pl[k], P);
                s[0] += P[0], s[1] += P[1], s[2] += P[2];
            }
            float l2 = dot3(s, s);
            if (l2 > 1.0000001e-06f) {
                float f = (1.0f / sqrtf(l2)) * 0.05f;
                for (int k = 0; k < 3; k++) out.pos[3 * i + k] += s[k] * f;
            }
        }
    }
    for (uint32_t i = 0; i < out.count; i++) {
        if (!bit_test(shared, i)) continue;
        uint32_t j = i;
        bool more;
        do {
            uint32_t k = remap[j] >> 16;
            if (j <= k) break;
            more = bit_test(shared, k);
            j = k;
        } while (more);
        if (j != i) copy3(out.pos + 3 * i, out.pos + 3 * j);
    }

    /* the block: the header, the places, the coordinates, the indices (to a multiple of 4), the shades */
    uint32_t nidx4 = (nidx & 3) ? (nidx & ~3u) + 4 : nidx;
    uint32_t size = (nidx4 + nv * 12) * 2 + 0x14;
    int slot_i = -1;
    for (int k = 0; k < DECALS_MAX; k++) {
        uint32_t id = next_id + (uint32_t)k;
        if (!id) continue;
        if (!decals[id % DECALS_MAX].used) {
            slot_i = (int)(id % DECALS_MAX);
            next_id = id + 1;
            break;
        }
    }
    if (slot_i < 0) return 0;
    Decal *d = &decals[slot_i];
    memset(d, 0, sizeof *d);
    d->id = next_id - 1;
    d->slot = -1;
    d->prev = d->next = -1;
    d->is_static = (flags & (DECAL_STATIC | DECAL_INTO)) != 0;
    if (!d->is_static) {
        d->used = true;     /* (for the purge's look up) */
        int s = ring_alloc(size, slot_i);
        if (s < 0) {
            memset(d, 0, sizeof *d);
            return 0;
        }
        d->slot = (int16_t)s;
    }
    d->block = malloc(size);
    if (!d->block) {
        if (d->slot >= 0) ring_free(d->slot);
        memset(d, 0, sizeof *d);
        return 0;
    }
    d->used = true;
    d->texture = texture, d->quad = quad, d->nverts = (uint16_t)nv, d->ntris = (uint16_t)ntris;
    d->alpha = alpha, d->tint = tint;
    float *bp = (float *)(d->block + 0x14), *buv = bp + 3 * nv;
    uint16_t *bi = (uint16_t *)(buv + 2 * nv);
    uint32_t *bs = (uint32_t *)(bi + nidx4);
    memset(merged, 0, sizeof merged);
    for (uint32_t v = 0; v < out.count; v++) {
        uint32_t o = remap[v] & 0xffff;
        if (bit_test_set(merged, o)) continue;
        copy3(bp + 3 * o, out.pos + 3 * v);
        buv[2 * o] = out.uv[2 * v], buv[2 * o + 1] = out.uv[2 * v + 1];
        bs[o] = shaded ? out.shade[v] : 0xffffffffu;
    }
    for (uint32_t t = 0; t < nidx; t++) bi[t] = (uint16_t)remap[idx[t]];
    list_add(d);
    return d->id;
}

void decal_set_owner(uint32_t id, uint32_t particle)
{
    Decal *d = by_id(id);
    if (d) d->owner = particle;
}

bool decal_live(uint32_t id)
{
    Decal *d = by_id(id);
    return d && d->block;
}

void decal_delete(uint32_t id)
{
    Decal *d = by_id(id);
    if (!d) return;
    list_remove(d);
    if (!d->is_static && d->slot != -1) ring_free(d->slot);
    decal_free(d);
}

void decal_set_alpha(uint32_t id, uint16_t alpha)
{
    Decal *d = by_id(id);
    if (d && d->block) d->alpha = alpha;
}

void decal_delete_all(void)
{
    for (int i = 0; i < DECALS_MAX; i++) free(decals[i].block);
    memset(decals, 0, sizeof decals);
    lists[0].head = lists[0].tail = lists[1].head = lists[1].tail = -1;
    lists[0].count = lists[1].count = 0;
    ring.oldest = ring.newest = -1;
    memset(ring.d, 0, sizeof ring.d);
    for (int i = 0; i < RING_SLOTS; i++) ring.d[i].owner = -1;
}

void decal_each(DecalFn fn, void *user)
{
    for (int L = 0; L < 2; L++) {
        if (!lists[L].count) continue;
        for (int i = lists[L].head; i >= 0; i = decals[i].next) {
            const Decal *d = &decals[i];
            if (!d->block || !d->texture) continue;
            const float *bp = (const float *)(d->block + 0x14), *buv = bp + 3 * d->nverts;
            const uint16_t *bi = (const uint16_t *)(buv + 2 * d->nverts);
            uint32_t ni = 3u * d->ntris, ni4 = (ni & 3) ? (ni & ~3u) + 4 : ni;
            DecalDraw dd = { d->texture, d->quad, d->tint, d->alpha, d->nverts, d->ntris, bp, buv,
                             (const uint32_t *)(bi + ni4), bi };
            fn(&dd, user);
        }
    }
}

int decal_count(void) { return lists[0].count + lists[1].count; }
