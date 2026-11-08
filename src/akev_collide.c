/* The environment's collisions: see akev_collide.h. */
#include "akev_collide.h"
#include "sort.h"

#include "akev.h"
#include "tm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static const float EPS = 0.0009765625f;     /* 0x529cc8, and less it 0x529ccc */
static const float MARGIN = -0.1f;          /* 0x5296cc */

/* ---- the environment's arrays (each instance: 0x14 bytes, the count, the elements) ---- */

static const uint8_t *elements(const uint8_t *a, unsigned count_at, uint32_t *n)
{
    *n = a ? rd32(a + count_at) : 0;
    return a ? a + count_at + 4 : NULL;
}

typedef struct {
    const uint8_t *points, *planes, *quads, *qcoll;
    uint32_t npoints, nplanes, nquads, nqcoll;
    const uint8_t *nodes, *leaves, *qtn, *index;
    uint32_t nnodes, nleaves, nqtn, nindex;
} Env;

static bool env_of(const void *e, Env *v)
{
    memset(v, 0, sizeof *v);
    const uint8_t *env = e;
    v->points = elements(tm_link(env + AKEV_POINTS), 0x34, &v->npoints);
    v->planes = elements(tm_link(env + AKEV_PLANES), 0x14, &v->nplanes);
    v->quads = elements(tm_link(env + AKEV_QUADS), 0x14, &v->nquads);
    v->qcoll = elements(tm_link(env + AKEV_QUAD_COLLISION), 0x14, &v->nqcoll);
    const uint8_t *ot = tm_link(env + AKEV_OCTREE);
    if (ot) {
        v->nodes = elements(tm_link(ot), 0x14, &v->nnodes);
        v->leaves = elements(tm_link(ot + 4), 0x14, &v->nleaves);
        v->qtn = elements(tm_link(ot + 8), 0x14, &v->nqtn);
        v->index = elements(tm_link(ot + 0xc), 0x14, &v->nindex);
    }
    return v->points && v->planes && v->quads && v->qcoll;
}

static void point(const Env *v, uint32_t i, float p[3])
{
    if (i >= v->npoints) { p[0] = p[1] = p[2] = 0.0f; return; }
    const uint8_t *q = v->points + 12u * i;
    p[0] = rdf(q), p[1] = rdf(q + 4), p[2] = rdf(q + 8);
}

static void corners(const Env *v, uint32_t quad, float c[4][3])
{
    const uint8_t *q = v->quads + 0x38u * quad;
    for (int k = 0; k < 4; k++) point(v, rd32(q + 4 * k), c[k]);
}

static uint32_t quad_flags(const Env *v, uint32_t quad) { return rd32(v->quads + 0x38u * quad + 0x30); }

/* The plane by its index; with bit 31 turned (normal and distance negated). The quad tests read it
   unturned: 16 times the index drops bit 31. */
static void plane(const Env *v, uint32_t idx, bool turn, float pl[4])
{
    uint32_t i = idx & 0x0fffffffu;
    if (i >= v->nplanes) { pl[0] = pl[1] = pl[2] = pl[3] = 0.0f; return; }
    const uint8_t *p = v->planes + 16u * i;
    for (int k = 0; k < 4; k++) pl[k] = rdf(p + 4 * k);
    if (turn && (idx & 0x80000000u))
        for (int k = 0; k < 4; k++) pl[k] = -pl[k];
}

/* ---- inside a quad ---- */

static int axis_of(float c[4][3])
{
    float lo[3], hi[3];
    for (int a = 0; a < 3; a++) {
        lo[a] = hi[a] = c[0][a];
        for (int k = 1; k < 4; k++) {
            if (c[k][a] < lo[a]) lo[a] = c[k][a];
            if (c[k][a] > hi[a]) hi[a] = c[k][a];
        }
    }
    bool fx = -EPS <= -(hi[0] - lo[0]), fy = -EPS <= -(hi[1] - lo[1]), fz = -EPS <= -(hi[2] - lo[2]);
    /* a projection is out when the quad is flat across it, or two distinct corners line up along it */
    bool no_z = fy || fx, no_y = fz || fx, no_x = fz || fy;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) {
            bool ex = fabsf(c[i][0] - c[j][0]) < EPS, ey = fabsf(c[i][1] - c[j][1]) < EPS, ez = fabsf(c[i][2] - c[j][2]) < EPS;
            if (ex && ey && ez) continue;
            no_z = no_z || (ey && ex);
            no_y = no_y || (ez && ex);
            no_x = no_x || (ez && ey);
        }
    if (!no_z) return 1;
    if (!no_y) return 2;
    return no_x ? 0 : 3;
}

int akev_quad_axis(const void *env, uint32_t quad)
{
    Env v;
    if (!env_of(env, &v) || quad >= v.nquads) return 0;
    float c[4][3];
    corners(&v, quad, c);
    return axis_of(c);
}

/* The projection's coordinates (u, v) and the one dropped (w). */
static void axes(int axis, int *u, int *v, int *w)
{
    if (axis == 1) *u = 0, *v = 1, *w = 2;
    else if (axis == 2) *u = 0, *v = 2, *w = 1;
    else *u = 1, *v = 2, *w = 0;
}

/* 0x43e3b0: not more than 0.1 past every corner along the dropped axis, then on the inner side of every
   edge (or of none: either winding). */
static bool inside(float c[4][3], int axis, const float p[3])
{
    if (!axis && !(axis = axis_of(c))) return false;
    int u, v, w;
    axes(axis, &u, &v, &w);
    if (p[w] - c[0][w] < MARGIN && p[w] - c[1][w] < MARGIN && p[w] - c[2][w] < MARGIN && p[w] - c[3][w] < MARGIN) return false;
    if (c[0][w] - p[w] < MARGIN && c[1][w] - p[w] < MARGIN && c[2][w] - p[w] < MARGIN && c[3][w] - p[w] < MARGIN) return false;
    float pu = p[u], pv = p[v], e[4];
    for (int k = 0; k < 4; k++) {
        const float *a = c[k], *b = c[(k + 1) & 3];
        e[k] = (a[u] - pu) * (b[v] - pv) - (b[u] - pu) * (a[v] - pv);
    }
    if (-e[0] < EPS && -e[1] < EPS && -e[2] < EPS && -e[3] < EPS) return true;
    return e[0] < EPS && e[1] < EPS && e[2] < EPS && e[3] < EPS;
}

bool akev_corners_inside(float c[4][3], int axis, const float p[3]) { return inside(c, axis, p); }
int akev_corners_axis(float c[4][3]) { return axis_of(c); }

bool akev_point_in_quad(const void *env, uint32_t quad, int axis, const float p[3])
{
    Env v;
    if (!env_of(env, &v) || quad >= v.nquads) return false;
    float c[4][3];
    corners(&v, quad, c);
    return inside(c, axis, p);
}

/* 0x43c8c0: the move from s to e through the quad's plane. Crossing it: the crossing point inside the
   quad. Along it (both ends on the plane): the end inside, else the move across one of the edges. */
static bool crosses(float c[4][3], int axis, const float pl[4], const float s[3], const float e[3], float out[3])
{
    float d0 = s[0] * pl[0] + s[1] * pl[1] + s[2] * pl[2] + pl[3];
    float d1 = e[0] * pl[0] + e[1] * pl[1] + e[2] * pl[2] + pl[3];
    if (EPS <= fabsf(d0) || EPS <= fabsf(d1)) {
        float den = (e[0] - s[0]) * pl[0] + (e[1] - s[1]) * pl[1] + (e[2] - s[2]) * pl[2];
        if (fabsf(den) < EPS) return false;
        float num = -d0;
        if (fabsf(den) - fabsf(num) < -EPS) return false;
        if (den < -EPS && -num < -EPS) return false;
        if (-den < -EPS && num < -EPS) return false;
        float t = num / den, p[3];
        for (int k = 0; k < 3; k++) p[k] = t * (e[k] - s[k]) + s[k];
        if (!inside(c, axis, p)) return false;
        memcpy(out, p, sizeof p);
        return true;
    }
    if (!axis && !(axis = axis_of(c))) return false;
    if (inside(c, axis, e)) {
        memcpy(out, e, 3 * sizeof(float));
        return true;
    }
    int u, v, w;
    axes(axis, &u, &v, &w);
    float su = s[u], sv = s[v], du = e[u] - s[u], dv = e[v] - s[v];
    for (int k = 0; k < 4; k++) {
        const float *a = c[k], *b = c[(k + 1) & 3];
        float cu = a[u], cv = a[v], eu = b[u] - a[u], ev = b[v] - a[v];
        float t = (ev * cu + ((sv * eu - eu * cv) - su * ev)) / (du * ev - dv * eu);
        bool last = k == 3;
        if (t < 0.0f || (last ? 1.0f < t : 1.0f <= t)) {
            if (last) return false;
            continue;
        }
        float m = EPS <= fabsf(eu) ? ((t * du + su) - cu) / eu : ((t * dv + sv) - cv) / ev;
        if (m < 0.0f || (last ? 1.0f < m : 1.0f <= m)) {
            if (last) return false;
            continue;
        }
        /* the point takes the edge's fraction along the move: the original's */
        for (int j = 0; j < 3; j++) out[j] = (e[j] - s[j]) * m + s[j];
        return true;
    }
    return false;
}

/* ---- a sphere against a quad ---- */

static float dist2(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return z * z + y * y + x * x;
}

static bool sphere_vector(const Env *v, uint32_t quad, const float sph[4], const float mv[3], float at[3])
{
    if (quad >= v->nquads || quad >= v->nqcoll) return false;
    const uint8_t *qc = v->qcoll + 0x1cu * quad;
    uint32_t pidx = rd32(qc);
    float pl[4];
    plane(v, pidx, false, pl);
    const float *s = sph;
    float r = sph[3], e[3] = { s[0] + mv[0], s[1] + mv[1], s[2] + mv[2] };
    float d0 = s[1] * pl[1] + s[2] * pl[2] + s[0] * pl[0] + pl[3];
    float d1 = e[1] * pl[1] + e[2] * pl[2] + e[0] * pl[0] + pl[3];
    if ((r < d0 && r < d1) || (d0 < -r && d1 < -r)) return false;
    float g = r + 0.001f;   /* 0x529dfc */
    for (int k = 0; k < 3; k++) {
        float lo = rdf(qc + 4 + 4 * k) - g, hi = rdf(qc + 0x10 + 4 * k) + g;
        if ((s[k] < lo && e[k] < lo) || (hi < s[k] && hi < e[k])) return false;
    }
    float c[4][3];
    corners(v, quad, c);
    int axis = (int)(quad_flags(v, quad) >> 25 & 3);
    float p[3];
    if (fabsf(d0) <= r) {
        for (int k = 0; k < 3; k++) p[k] = s[k] - d0 * pl[k];
        if (inside(c, axis, p)) { memcpy(at, p, sizeof p); return true; }
    }
    if (fabsf(d1) <= r) {
        for (int k = 0; k < 3; k++) p[k] = e[k] - d1 * pl[k];
        if (inside(c, axis, p)) { memcpy(at, p, sizeof p); return true; }
    }
    float vv = mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2];
    float vs = mv[0] * s[0] + mv[1] * s[1] + mv[2] * s[2];
    float inv = 1.0f / vv, rr = r * r;
    /* the corners */
    for (int k = 0; k < 4; k++) {
        const float *q = c[k];
        bool hit = dist2(q, s) <= rr || dist2(q, e) <= rr;
        if (!hit) {
            float t = -(((vs - q[0] * mv[0]) - q[1] * mv[1]) - q[2] * mv[2]) * inv;
            if (0.0f <= t && t <= 1.0f) {
                float x = (t * mv[0] + s[0]) - q[0], y = (t * mv[1] + s[1]) - q[1], z = (t * mv[2] + s[2]) - q[2];
                hit = z * z + y * y + x * x <= rr;
            }
        }
        if (hit) { memcpy(at, q, 3 * sizeof(float)); return true; }
    }
    /* the edges: their nearest points to the start, to the end, then to the move (one fraction for both) */
    for (int k = 0; k < 4; k++) {
        const float *a = c[k], *b = c[(k + 1) & 3];
        float ed[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        float ee = ed[0] * ed[0] + ed[1] * ed[1] + ed[2] * ed[2], ie = 1.0f / ee;
        float ea = ed[0] * a[0] + ed[1] * a[1] + ed[2] * a[2];
        float es = ed[0] * s[0] + ed[1] * s[1] + ed[2] * s[2];
        float t = -(((ea - ed[0] * s[0]) - ed[1] * s[1]) - ed[2] * s[2]) * ie;
        (void)es;
        if (0.0f <= t && t <= 1.0f) {
            float q[3] = { ed[0] * t + a[0], ed[1] * t + a[1], ed[2] * t + a[2] };
            if (dist2(q, s) <= rr) { memcpy(at, q, sizeof q); return true; }
        }
        t = -(((ea - ed[0] * e[0]) - ed[1] * e[1]) - ed[2] * e[2]) * ie;
        if (0.0f <= t && t <= 1.0f) {
            float q[3] = { ed[0] * t + a[0], ed[1] * t + a[1], ed[2] * t + a[2] };
            if (dist2(q, e) <= rr) { memcpy(at, q, sizeof q); return true; }
        }
        float num = (((((((((vs - ed[0] * s[0]) - ed[1] * s[1]) - ed[2] * s[2]) - a[0] * mv[0]) + ed[0] * a[0]) - a[1] * mv[1])
                      + ed[1] * a[1]) - a[2] * mv[2]) + ed[2] * a[2]);
        float den = ((((vv - (ed[0] * mv[0] + ed[0] * mv[0])) + ed[0] * ed[0]) - (ed[1] * mv[1] + ed[1] * mv[1])) + ed[1] * ed[1])
                    - (ed[2] * mv[2] + ed[2] * mv[2]) + ed[2] * ed[2];
        t = -(num / den);
        if (0.0f <= t && t <= 1.0f) {
            float q[3] = { ed[0] * t + a[0], ed[1] * t + a[1], ed[2] * t + a[2] };
            float x = (t * mv[0] + s[0]) - q[0], y = (t * mv[1] + s[1]) - q[1], z = (t * mv[2] + s[2]) - q[2];
            if (z * z + y * y + x * x <= rr) { memcpy(at, q, sizeof q); return true; }
        }
    }
    float tp[4];
    plane(v, pidx, true, tp);
    return crosses(c, axis, tp, s, e, at);
}

bool akev_quad_sphere_vector(const void *env, uint32_t quad, const float sphere[4], const float move[3], float at[3])
{
    Env v;
    float tmp[3];
    return env_of(env, &v) && sphere_vector(&v, quad, sphere, move, at ? at : tmp);
}

/* ---- the octree ---- */

static float leaf_size(uint32_t k) { return (float)(16u << (k > 8 ? 8 : k)); }       /* 0x534d98 */
static float leaf_pos(uint32_t k) { return -4080.0f + 16.0f * (float)(k & 0x1ff); }   /* 0x534dbc */

/* The leaf holding p: down from the root (half size 4096, 0x529e24). */
static uint32_t find_leaf(const Env *v, const float p[3])
{
    uint32_t node = 0;
    float half = 4096.0f, cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int guard = 0; guard < 64; guard++) {
        uint32_t k = (cx < p[0]) | (cy < p[1]) << 1 | (cz < p[2]) << 2;
        if (node >= v->nnodes) return 0xffffffffu;
        node = rd32(v->nodes + 0x20u * node + 4 * k);
        if (node & 0x80000000u) return node;
        half *= 0.5f;
        cx += (k & 1) ? half : -half;
        cy += (k & 2) ? half : -half;
        cz += (k & 4) ? half : -half;
    }
    return 0xffffffffu;
}

typedef struct {
    uint8_t *seen;          /* the leaves met (all samples) */
    uint8_t *near;          /* the leaves within reach, this sample */
    uint32_t *list;         /* those, in order */
    uint32_t n;
} Leaves;

static bool test_set(uint8_t *bits, uint32_t i)
{
    bool was = bits[i >> 3] >> (i & 7) & 1;
    bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    return was;
}

/* The flood from the leaf holding the centre through the faces, while the leaves are within the radius. */
static void flood(const Env *v, Leaves *L, const float c[3], float r)
{
    memset(L->near, 0, (v->nleaves + 7) / 8);
    uint32_t stack[0x800], n = 0;
    stack[n++] = find_leaf(v, c);
    while (n) {
        uint32_t x = stack[--n];
        if (x == 0xffffffffu) continue;
        if (!(x & 0x80000000u)) {
            if (x >= v->nqtn) continue;
            for (int k = 0; k < 4; k++) {
                if (n >= 0x800) return;
                stack[n++] = rd32(v->qtn + 0x10u * x + 4 * k);
            }
            continue;
        }
        uint32_t i = x & 0x7fffffffu;
        if (i >= v->nleaves || (L->near[i >> 3] >> (i & 7) & 1)) continue;
        const uint8_t *lf = v->leaves + 0x24u * i;
        uint32_t box = rd32(lf + 0x1c);
        float sz = leaf_size(box >> 27);
        float hx = leaf_pos(box >> 18), hy = leaf_pos(box >> 9), hz = leaf_pos(box);
        float lo[3] = { hx - sz, hy - sz, hz - sz }, hi[3] = { hx, hy, hz }, d = 0.0f;
        for (int k = 0; k < 3; k++) {
            if (c[k] < lo[k]) d += (c[k] - lo[k]) * (c[k] - lo[k]);
            else if (hi[k] < c[k]) d += (c[k] - hi[k]) * (c[k] - hi[k]);
        }
        if (d > r * r) continue;
        test_set(L->near, i);
        if (!test_set(L->seen, i)) L->list[L->n++] = i;
        for (int k = 1; k <= 6; k++) {
            if (n >= 0x800) return;
            stack[n++] = rd32(lf + 4 * k);
        }
    }
}

/* 0x441c50: the farther (the rounded distance) after (AUrQSort_16 0x437d90 sorts the hits' indices
   with it, then 0x441b40 copies them in that order) */
static bool after_by_distance(const void *a, const void *b, void *u)
{
    (void)u;
    return ((const AkevHit *)a)->idist > ((const AkevHit *)b)->idist;
}

unsigned akev_collide_sphere(const void *env, const float sphere[4], const float move[3], uint32_t skip, unsigned limit,
                             AkevHit *hits)
{
    Env v;
    if (!env_of(env, &v) || !v.nodes || !v.leaves) return 0;
    size_t bytes = (v.nleaves + 7) / 8;
    Leaves L = { calloc(1, bytes ? bytes : 1), calloc(1, bytes ? bytes : 1), calloc(v.nleaves ? v.nleaves : 1, 4), 0 };
    uint8_t *tested = calloc(1, (v.nquads + 7) / 8 + 1);
    unsigned count = 0;
    if (!L.seen || !L.near || !L.list || !tested) goto done;
    float r = sphere[3], len = sqrtf(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
    if (r * 0.125f <= len) {       /* 0x5294bc: samples a radius apart */
        float t = 0.0f;
        for (bool last = false; !last; t += r / len) {
            if (1.0f <= t) t = 1.0f, last = true;
            float c[3] = { t * move[0] + sphere[0], t * move[1] + sphere[1], t * move[2] + sphere[2] };
            flood(&v, &L, c, r);
        }
    } else {                        /* one sphere round the move */
        float c[3] = { move[0] * 0.5f + sphere[0], move[1] * 0.5f + sphere[1], move[2] * 0.5f + sphere[2] };
        flood(&v, &L, c, len * 0.5f + r);
    }
    for (uint32_t li = 0; li < L.n; li++) {
        uint32_t packed = rd32(v.leaves + 0x24u * L.list[li]);
        uint32_t first = packed >> 12, n = packed & 0xfff;
        for (uint32_t j = first; j < first + n && j < v.nindex; j++) {
            uint32_t q = rd32(v.index + 4 * j);
            if (q >= v.nquads || (quad_flags(&v, q) & skip) || test_set(tested, q)) continue;
            float at[3];
            if (!sphere_vector(&v, q, sphere, move, at)) continue;
            if (!limit) { count = 1; goto done; }
            if (count >= AKEV_HITS_MAX - 1) goto sort;     /* 0xfe */
            AkevHit *h = &hits[count++];
            memset(h, 0, sizeof *h);
            h->dist = sqrtf(dist2(sphere, at));
            h->idist = (uint32_t)lrintf(h->dist);           /* 0x4340c0 */
            h->quad = q;
            memcpy(h->point, at, sizeof at);
            plane(&v, rd32(v.qcoll + 0x1cu * q), true, h->plane);
        }
    }
sort:
    /* 0x441b40: by the rounded distance; with a limit of 1 the nearest alone */
    if (count >= 2 && limit == 1) {
        unsigned best = 0;
        for (unsigned i = 1; i < count; i++)
            if (hits[i].idist < hits[best].idist) best = i;
        hits[0] = hits[best];
        count = 1;
    } else if (count >= 2) {
        au_qsort(hits, count, sizeof *hits, after_by_distance, NULL);
    }
done:
    free(L.seen);
    free(L.near);
    free(L.list);
    free(tested);
    return count;
}

/* AKrCollision_Point 0x4412d0 (through 0x441c80, its start and end leaves found afresh): the segment
   from p along v, leaf by leaf through the octree from the start's leaf to the end's, each leaf left
   by the face the segment meets first (its distances measured from the start), a face's quadtree
   descended at the segment's crossing. A leaf's quads not yet tested and without the skip flags are
   tried with 0x43c8c0; a limit stops the walk after the leaf that reaches it. The distance is the
   L1 distance from p. */
unsigned akev_collide_point(const void *env, const float p[3], const float v[3], uint32_t skip, unsigned limit, AkevHit *hits)
{
    Env e;
    if (!env_of(env, &e) || !e.nodes || !e.leaves) return 0;
    uint8_t *tested = calloc(1, (e.nquads + 7) / 8 + 1);
    if (!tested) return 0;
    float inv[3];
    for (int k = 0; k < 3; k++) inv[k] = v[k] != 0.0f ? 1.0f / v[k] : 0.0f;
    const float end[3] = { p[0] + v[0], p[1] + v[1], p[2] + v[2] };
    unsigned fx = 0.0f < v[0], fy = (0.0f < v[1]) + 2, fz = (0.0f < v[2]) + 4;
    uint32_t leaf = find_leaf(&e, p), last = find_leaf(&e, end);
    unsigned count = 0;
    for (int guard = 0; leaf != 0xffffffffu && guard < 0x10000; guard++) {
        uint32_t i = leaf & 0x7fffffffu;
        if (i >= e.nleaves) break;
        const uint8_t *lf = e.leaves + 0x24u * i;
        uint32_t packed = rd32(lf), first = packed >> 12, n = packed & 0xfff;
        for (uint32_t j = first; j < first + n && j < e.nindex; j++) {
            uint32_t q = rd32(e.index + 4 * j);
            if (q >= e.nquads || q >= e.nqcoll || (quad_flags(&e, q) & skip) || test_set(tested, q)) continue;
            float c[4][3], pl[4], at[3];
            corners(&e, q, c);
            plane(&e, rd32(e.qcoll + 0x1cu * q), true, pl);
            if (!crosses(c, (int)(quad_flags(&e, q) >> 25 & 3), pl, p, end, at)) continue;
            if (!limit) { count = 1; goto done; }
            if (count > 0xfe) goto sort;
            AkevHit *h = &hits[count++];
            memset(h, 0, sizeof *h);
            h->dist = fabsf(at[2] - p[2]) + fabsf(at[1] - p[1]) + fabsf(at[0] - p[0]);
            h->idist = (uint32_t)lrintf(h->dist);           /* 0x4340c0 */
            h->quad = q;
            memcpy(h->point, at, sizeof at);
            memcpy(h->plane, pl, sizeof pl);
        }
        if ((limit && limit <= count) || leaf == last) break;
        /* the face out: the nearest of the three the segment heads for */
        uint32_t box = rd32(lf + 0x1c);
        float sz = leaf_size(box >> 27);
        float mx = leaf_pos(box >> 18) - sz, my = leaf_pos(box >> 9) - sz, mz = leaf_pos(box) - sz;
        float dx = (fx == 1 ? leaf_pos(box >> 18) : mx) - p[0];
        float dy = (fy == 3 ? leaf_pos(box >> 9) : my) - p[1];
        float dz = (fz == 5 ? leaf_pos(box) : mz) - p[2];
        unsigned face;
        if (fabsf(dx * v[1]) <= fabsf(dy * v[0])) face = fabsf(dx * v[2]) <= fabsf(dz * v[0]) ? fx : fz;
        else face = fabsf(dz * v[1]) < fabsf(dy * v[2]) ? fz : fy;
        leaf = rd32(lf + 4 * (face + 1));
        if (leaf & 0x80000000u) continue;
        /* a quadtree on the face: down at the crossing */
        float half = sz * 0.5f, cu, cv, pu, pv;
        if (face < 2) cu = my + half, cv = mz + half, pu = dx * inv[0] * v[1] + p[1], pv = dx * inv[0] * v[2] + p[2];
        else if (face < 4) cu = mx + half, cv = mz + half, pu = dy * inv[1] * v[0] + p[0], pv = dy * inv[1] * v[2] + p[2];
        else cu = mx + half, cv = my + half, pu = dz * inv[2] * v[0] + p[0], pv = dz * inv[2] * v[1] + p[1];
        for (int g = 0; g < 32; g++) {
            uint32_t k = (cu < pu) | (cv < pv) << 1;
            if (leaf >= e.nqtn) { leaf = 0xffffffffu; break; }
            leaf = rd32(e.qtn + 0x10u * leaf + 4 * k);
            if (leaf & 0x80000000u) break;
            half *= 0.5f;
            cu += (k & 1) ? half : -half;
            cv += (k & 2) ? half : -half;
        }
    }
sort:
    if (count >= 2 && limit == 1) {
        unsigned best = 0;
        for (unsigned i = 1; i < count; i++)
            if (hits[i].idist < hits[best].idist) best = i;
        hits[0] = hits[best];
        count = 1;
    } else if (count >= 2) {
        au_qsort(hits, count, sizeof *hits, after_by_distance, NULL);
    }
done:
    free(tested);
    return count;
}
