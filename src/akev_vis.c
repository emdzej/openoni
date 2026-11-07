/* The environment's visibility: see akev_vis.h. */
#include "akev_vis.h"

#include "akev.h"
#include "akev_collide.h"
#include "exe.h"
#include "persist.h"
#include "tm.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static const uint8_t *elements(const uint8_t *a, unsigned count_at, uint32_t *n)
{
    *n = a ? rd32(a + count_at) : 0;
    return a ? a + count_at + 4 : NULL;
}

/* The environment's private data (TMrTemplate_PrivateData_GetDataPtr's for the AKEV): +0 the quads tested
   by this pass, +4 those found facing away, +8 the quads' marks and +0xc the leaves' (2 bits each), +0x1c
   the sky's frames. */
static struct {
    const void *env;
    const uint8_t *points, *planes, *quads, *qcoll, *nodes, *leaves, *qtn, *index;
    uint32_t nquads, nqcoll, nplanes, npoints, nnodes, nleaves, nqtn, nindex;
    uint8_t *tested, *away, *quad_mark, *leaf_mark;
    uint32_t sky;               /* +0x1c */
    float pattern[20 * 20][2];  /* 0x5ccef8 */
    uint32_t nrays;             /* 0x5cdb80 */
    float jitter[25][2];        /* 0x6a8fa0 */
} V;

static uint32_t still;          /* 0x5cdbcc: the frames the camera has kept still */
static uint32_t age_step;       /* 0x5cdbd0 */
static uint32_t passes;         /* 0x5cdb88 */
static float last[9];           /* 0x5ccec8..0x5ccef0: the camera's last view, location */

static bool mark(const uint8_t *v, uint32_t i) { return v[i >> 2] & (3u << ((i & 3) << 1)); }
static void set_mark(uint8_t *v, uint32_t i) { v[i >> 2] |= (uint8_t)(3u << ((i & 3) << 1)); }
static bool test_set(uint8_t *bits, uint32_t i)
{
    bool was = bits[i >> 3] >> (i & 7) & 1;
    bits[i >> 3] |= (uint8_t)(1u << (i & 7));
    return was;
}

/* 0x424530 with the table at 0x532260: each mark one step down (3, 2, 1, 0, 0). */
static void age(uint8_t *v, uint32_t n)
{
    for (uint32_t b = 0; b < (n + 3) / 4; b++) {
        uint8_t x = v[b], y = 0;
        for (int k = 0; k < 4; k++) {
            unsigned m = x >> (2 * k) & 3;
            y |= (uint8_t)((m ? m - 1 : 0) << (2 * k));
        }
        v[b] = y;
    }
}

static void free_all(void)
{
    free(V.tested), free(V.away), free(V.quad_mark), free(V.leaf_mark);
    V.tested = V.away = V.quad_mark = V.leaf_mark = NULL;
}

/* AKrRayCastCoefficients_Initialize 0x442f50: n by n rays over the screen, each cell's ray put in by a
   quarter step of the order 0, 2, 1, 3 along both sides; the 25 jitters (0x534cd0, in cells) per pass. */
static void coefficients(uint32_t n)
{
    if (n > 20) n = 20;
    static const unsigned order[4] = { 0, 2, 1, 3 };
    float cell = 1.0f / (float)n;
    V.nrays = n * n;
    float (*p)[2] = V.pattern;
    for (uint32_t i = 0; i < n; i++) {
        float shift = (float)order[i & 3] * cell * 0.25f;
        for (uint32_t j = 0; j < n; j++, p++) {
            (*p)[0] = (float)order[j & 3] * cell * 0.25f + (float)i * cell;
            (*p)[1] = (float)j * cell + shift;
        }
    }
    const uint8_t *t = exe_data(0x534cd0, 25 * 8);
    for (int k = 0; k < 25; k++) {
        V.jitter[k][0] = t ? cell * rdf(t + 8 * k) : 0.0f;
        V.jitter[k][1] = t ? cell * rdf(t + 8 * k + 4) : 0.0f;
    }
}

void vis_level_begin(const void *env)
{
    free_all();
    memset(&V, 0, sizeof V);
    still = age_step = passes = 0;
    memset(last, 0, sizeof last);
    if (!env) return;
    const uint8_t *e = env;
    V.points = elements(tm_link(e + AKEV_POINTS), 0x34, &V.npoints);
    V.planes = elements(tm_link(e + AKEV_PLANES), 0x14, &V.nplanes);
    V.quads = elements(tm_link(e + AKEV_QUADS), 0x14, &V.nquads);
    V.qcoll = elements(tm_link(e + AKEV_QUAD_COLLISION), 0x14, &V.nqcoll);
    const uint8_t *ot = tm_link(e + AKEV_OCTREE);
    if (ot) {
        V.nodes = elements(tm_link(ot), 0x14, &V.nnodes);
        V.leaves = elements(tm_link(ot + 4), 0x14, &V.nleaves);
        V.qtn = elements(tm_link(ot + 8), 0x14, &V.nqtn);
        V.index = elements(tm_link(ot + 0xc), 0x14, &V.nindex);
    }
    if (!V.points || !V.planes || !V.quads || !V.qcoll || !V.nodes || !V.leaves) return;
    V.tested = calloc(1, V.nquads / 8 + 4);
    V.away = calloc(1, V.nquads / 8 + 4);
    V.quad_mark = calloc(1, V.nquads / 4 + 4);
    V.leaf_mark = calloc(1, V.nleaves / 4 + 4);
    if (!V.tested || !V.away || !V.quad_mark || !V.leaf_mark) { free_all(); return; }
    V.env = env;
    /* ONrMotoko_GraphicsQuality_RayCount 0x5084e0: 16, 18, 20, 20, 20 */
    static const uint32_t rays[5] = { 16, 18, 20, 20, 20 };
    int q = persist_quality();
    coefficients(rays[q < 0 ? 0 : q > 4 ? 4 : q]);
}

/* AKiOctTree_FindLeaf 0x4411a0: down from the root (half size 4096). */
static uint32_t find_leaf(const float p[3])
{
    uint32_t node = 0;
    float half = 4096.0f, cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int guard = 0; guard < 64; guard++) {
        uint32_t k = (cx < p[0]) | (cy < p[1]) << 1 | (cz < p[2]) << 2;
        if (node >= V.nnodes) return 0xffffffffu;
        node = rd32(V.nodes + 0x20u * node + 4 * k);
        if (node & 0x80000000u) return node;
        half *= 0.5f;
        cx += (k & 1) ? half : -half;
        cy += (k & 2) ? half : -half;
        cz += (k & 4) ? half : -half;
    }
    return 0xffffffffu;
}

/* A leaf's box from its packed word (+0x1c): the far corner at 16 (k - 255) along each axis (bits 18, 9,
   0, 9 each), the size 16 << the top 5 bits. */
static void leaf_box(uint32_t packed, float lo[3], float hi[3])
{
    float size = (float)(1u << (packed >> 27)) * 16.0f;
    hi[0] = ((float)(packed >> 18 & 0x1ff) - 255.0f) * 16.0f;
    hi[1] = ((float)(packed >> 9 & 0x1ff) - 255.0f) * 16.0f;
    hi[2] = ((float)(packed & 0x1ff) - 255.0f) * 16.0f;
    for (int k = 0; k < 3; k++) lo[k] = hi[k] - size;
}

static void quad_plane(uint32_t q, float pl[4])
{
    uint32_t idx = q < V.nqcoll ? rd32(V.qcoll + 0x1cu * q) : 0, i = idx & 0x0fffffffu;
    if (i >= V.nplanes) { pl[0] = pl[1] = pl[2] = pl[3] = 0.0f; return; }
    for (int k = 0; k < 4; k++) pl[k] = rdf(V.planes + 16u * i + 4 * k);
    if (idx & 0x80000000u)
        for (int k = 0; k < 4; k++) pl[k] = -pl[k];
}

/* AKiEnvironment_RayCastOctTree 0x4433f0: every ray of the pattern (jittered by the pass), from the near
   plane to the far, leaf by leaf from the camera's: each leaf met marked, then its quads: one not yet
   tested this pass, unless two-sided or the like (flags 0x2a0), found facing away from the ray's start is
   left out for the pass; the others marked, and the first opaque one (flags 0x520a0 clear) the ray
   meets within the leaf ends the ray after it. Out of the leaf by the face it reaches first, a face's
   quadtree descended at the crossing; past the far plane or out of the octree, the ray got out.
   Whether one did. (The leaves' box, kept at +0x5c, isn't.) */
static bool cast(const VisCamera *cam)
{
    bool out = false;
    const float *jit = V.jitter[passes % 25];
    memset(V.tested, 0, V.nquads / 8 + 4);       /* UUrBitVector_ClearBitAll */
    memset(V.away, 0, V.nquads / 8 + 4);
    uint32_t start_leaf = find_leaf(cam->location);
    const float (*c)[3] = cam->corners;
    float fa[3], fb[3], na[3], nb[3];
    for (int k = 0; k < 3; k++) fa[k] = c[5][k] - c[4][k], fb[k] = c[6][k] - c[4][k], na[k] = c[1][k] - c[0][k], nb[k] = c[2][k] - c[0][k];
    for (uint32_t r = 0; r < V.nrays; r++) {
        float u = V.pattern[r][0] + jit[0], w = V.pattern[r][1] + jit[1];
        float s[3], d[3], inv[3];
        for (int k = 0; k < 3; k++) {
            s[k] = u * na[k] + w * nb[k] + c[0][k];
            d[k] = u * fa[k] + w * fb[k] + c[4][k] - s[k];
            inv[k] = 1.0f / d[k];
        }
        unsigned fx = 0.0f < d[0], fy = (0.0f < d[1]) + 2, fz = (0.0f < d[2]) + 4;
        bool hit = false;
        uint32_t leaf = start_leaf;
        while (leaf != 0xffffffffu) {
            uint32_t li = leaf & 0x7fffffffu;
            if (li >= V.nleaves) break;
            const uint8_t *lf = V.leaves + 0x24u * li;
            set_mark(V.leaf_mark, li);
            float lo[3], hi[3];
            uint32_t packed = rd32(lf + 0x1c);
            leaf_box(packed, lo, hi);
            float size = (float)(1u << (packed >> 27)) * 16.0f;
            uint32_t list = rd32(lf), first = list >> 12, n = list & 0xfff;
            for (uint32_t j = first; j < first + n && j < V.nindex; j++) {
                uint32_t q = rd32(V.index + 4 * j);
                if (q >= V.nquads) continue;
                uint32_t flags = rd32(V.quads + 0x38u * q + 0x30);
                float pl[4];
                bool have = false;
                if (!test_set(V.tested, q)) {
                    if (!(flags & 0x2a0)) {
                        quad_plane(q, pl);
                        have = true;
                        uint32_t p0 = rd32(V.quads + 0x38u * q);
                        const uint8_t *pp = p0 < V.npoints ? V.points + 12u * p0 : V.points;
                        if ((s[2] - rdf(pp + 8)) * pl[2] + (s[1] - rdf(pp + 4)) * pl[1] + (s[0] - rdf(pp)) * pl[0] < 0.0f) {
                            test_set(V.away, q);
                            continue;
                        }
                    }
                } else if (V.away[q >> 3] >> (q & 7) & 1) {
                    continue;
                }
                set_mark(V.quad_mark, q);
                if ((flags & 0x520a0) || hit) continue;
                if (!have) quad_plane(q, pl);
                float den = pl[0] * d[0] + pl[1] * d[1] + pl[2] * d[2];
                if (den == 0.0f) continue;
                float t = -(pl[0] * s[0] + pl[1] * s[1] + pl[2] * s[2] + pl[3]) / den;
                if (t < 0.0f || t > 1.0f) continue;
                float at[3] = { t * d[0] + s[0], t * d[1] + s[1], t * d[2] + s[2] };
                if (lo[0] <= at[0] && at[0] <= hi[0] && lo[1] <= at[1] && at[1] <= hi[1] && lo[2] <= at[2] && at[2] <= hi[2] &&
                    akev_point_in_quad(V.env, q, (int)(flags >> 25 & 3), at))
                    hit = true;
            }
            if (hit) break;
            /* the face out: the nearest of the three the ray heads for */
            float tx = ((fx == 1 ? hi[0] : lo[0]) - s[0]) * inv[0];
            float ty = ((fy == 3 ? hi[1] : lo[1]) - s[1]) * inv[1];
            float tz = ((fz == 5 ? hi[2] : lo[2]) - s[2]) * inv[2];
            unsigned face = fx;
            float t = tx;
            if (ty < tx || tz < tx) {
                face = fy, t = ty;
                if (tz < ty) face = fz, t = tz;
            }
            if (1.0f < t) break;
            leaf = rd32(lf + 4 * (face + 1));
            if (leaf & 0x80000000u) continue;
            float half = size * 0.5f, cu, cv, pu, pv;
            if (face < 2) cu = half + lo[1], cv = half + lo[2], pu = t * d[1] + s[1], pv = t * d[2] + s[2];
            else if (face < 4) cu = half + lo[0], cv = half + lo[2], pu = t * d[0] + s[0], pv = t * d[2] + s[2];
            else cu = half + lo[0], cv = half + lo[1], pu = t * d[0] + s[0], pv = t * d[1] + s[1];
            for (int g = 0; g < 32; g++) {
                uint32_t k = (cu < pu) | (cv < pv) << 1;
                if (leaf >= V.nqtn) { leaf = 0xffffffffu; break; }
                leaf = rd32(V.qtn + 0x10u * leaf + 4 * k);
                if (leaf & 0x80000000u) break;
                half *= 0.5f;
                cu += (k & 1) ? half : -half;
                cv += (k & 2) ? half : -half;
            }
        }
        if (!hit) out = true;
    }
    return out;
}

bool vis_update(const VisCamera *cam)
{
    if (!V.env) return true;
    /* moved (its location by 0.1, its view or up by 0.05): the rays again; still for more than 25 frames,
       none (0x529e3c, 0x529e38: the squares) */
    float now[9];
    memcpy(now, cam->view, 12), memcpy(now + 3, cam->up, 12), memcpy(now + 6, cam->location, 12);
    float dl = 0.0f, dv = 0.0f, du = 0.0f;
    for (int k = 0; k < 3; k++) {
        dv += (now[k] - last[k]) * (now[k] - last[k]);
        du += (now[3 + k] - last[3 + k]) * (now[3 + k] - last[3 + k]);
        dl += (now[6 + k] - last[6 + k]) * (now[6 + k] - last[6 + k]);
    }
    if (0.01f <= dl || 0.0025f <= dv || 0.0025f <= du) still = 0;
    else if (++still > 25) return V.sky != 0;
    /* ONrMotoko_GraphicsQuality_RayCastCount 0x508440: 1, 1, 1, 1, 2 passes */
    int q = persist_quality(), n = q >= 4 ? 2 : 1;
    for (int i = 0; i < n; i++) {
        age_step = (age_step + 1) % 12;
        if (age_step == 0) age(V.quad_mark, V.nquads), age(V.leaf_mark, V.nleaves);
    }
    if (V.sky) V.sky--;
    bool out = false;
    for (int i = 0; i < n; i++) {
        out |= cast(cam);
        passes++;
    }
    if (out) V.sky = 25;
    memcpy(last, now, sizeof last);
    return V.sky != 0;
}

void vis_env_drawn(void) { passes++; }

bool vis_quad(uint32_t quad) { return !V.env || (quad < V.nquads && mark(V.quad_mark, quad)); }

bool vis_point(const float p[3])
{
    if (!V.env) return true;
    uint32_t leaf = find_leaf(p);
    return leaf != 0xffffffffu && (leaf & 0x7fffffffu) < V.nleaves && mark(V.leaf_mark, leaf & 0x7fffffffu);
}

/* 0x4409c0: the leaves under the node overlapping the box, the node's cube halved into its eight */
static void collect(const float cube[6], uint32_t node, const float box[6], uint32_t max, VisList *l, uint32_t *n)
{
    if (!(cube[0] <= box[3] && cube[1] <= box[4] && cube[2] <= box[5] && box[0] <= cube[3] && box[1] <= cube[4] && box[2] <= cube[5]))
        return;
    if (node & 0x80000000u) {
        if (*n < max) l->leaf[*n] = node & 0x7fffffffu;
        ++*n;
        return;
    }
    if (node >= V.nnodes) return;
    float half = (cube[3] - cube[0]) * 0.5f;
    for (uint32_t k = 0; k < 8; k++) {
        float c[6];
        c[0] = (k & 1) ? half + cube[0] : cube[0], c[3] = (k & 1) ? cube[3] : cube[3] - half;
        c[1] = (k & 2) ? half + cube[1] : cube[1], c[4] = (k & 2) ? cube[4] : cube[4] - half;
        c[2] = (k & 4) ? half + cube[2] : cube[2], c[5] = (k & 4) ? cube[5] : cube[5] - half;
        collect(c, rd32(V.nodes + 0x20u * node + 4 * k), box, max, l, n);
        if (max < *n) return;
    }
}

void vis_list(const float box[6], uint32_t max, VisList *l)
{
    if (!V.env || !max) return;
    if (max > VIS_LIST_MAX + 1) max = VIS_LIST_MAX + 1;
    static const float root[6] = { -4096.0f, -4096.0f, -4096.0f, 4096.0f, 4096.0f, 4096.0f };
    uint32_t n = 0;
    collect(root, 0, box, max - 1, l, &n);
    l->count = max - 1 <= n ? 0xffffffffu : n;
}

bool vis_list_visible(const VisList *l)
{
    if (!V.env || l->count == 0xffffffffu) return true;
    for (uint32_t i = 0; i < l->count && i < VIS_LIST_MAX; i++)
        if (l->leaf[i] < V.nleaves && mark(V.leaf_mark, l->leaf[i])) return true;
    return false;
}

/* 0x440c20 */
static bool any(const float cube[6], uint32_t node, const float box[6])
{
    if (!(cube[0] <= box[3] && cube[1] <= box[4] && cube[2] <= box[5] && box[0] <= cube[3] && box[1] <= cube[4] && box[2] <= cube[5]))
        return false;
    if (node & 0x80000000u) return (node & 0x7fffffffu) < V.nleaves && mark(V.leaf_mark, node & 0x7fffffffu);
    if (node >= V.nnodes) return false;
    float half = (cube[3] - cube[0]) * 0.5f;
    for (uint32_t k = 0; k < 8; k++) {
        float c[6];
        c[0] = (k & 1) ? half + cube[0] : cube[0], c[3] = (k & 1) ? cube[3] : cube[3] - half;
        c[1] = (k & 2) ? half + cube[1] : cube[1], c[4] = (k & 2) ? cube[4] : cube[4] - half;
        c[2] = (k & 4) ? half + cube[2] : cube[2], c[5] = (k & 4) ? cube[5] : cube[5] - half;
        if (any(c, rd32(V.nodes + 0x20u * node + 4 * k), box)) return true;
    }
    return false;
}

bool vis_box(const float box[6])
{
    if (!V.env) return true;
    static const float root[6] = { -4096.0f, -4096.0f, -4096.0f, 4096.0f, 4096.0f, 4096.0f };
    return any(root, 0, box);
}
