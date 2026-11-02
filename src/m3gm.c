/* M3GM geometry: see m3gm.h. */
#include "m3gm.h"

#include "tm.h"

#include <string.h>

enum {
    ARRAY_COUNT = 0x14,     /* VCRA, TXCA, IDXA: 20 bytes of padding, the count, the elements */
    PNTA_BOUNDS = 0x0c,     /* PNTA: 12 bytes of padding, box (6 floats), sphere (4), count, elements */
    PNTA_COUNT = 0x34,
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static const uint8_t *array(const void *geom, unsigned field, unsigned count_at, uint32_t *count)
{
    const uint8_t *a = tm_link((const uint8_t *)geom + field);
    *count = a ? rd32(a + count_at) : 0;
    return a ? a + count_at + 4 : NULL;
}

const uint8_t *m3gm_points(const void *g, uint32_t *n) { return array(g, M3GM_POINTS, PNTA_COUNT, n); }
const uint8_t *m3gm_vertex_normals(const void *g, uint32_t *n) { return array(g, M3GM_VERTEX_NORMALS, ARRAY_COUNT, n); }
const uint8_t *m3gm_face_normals(const void *g, uint32_t *n) { return array(g, M3GM_FACE_NORMALS, ARRAY_COUNT, n); }
const uint8_t *m3gm_texcoords(const void *g, uint32_t *n) { return array(g, M3GM_TEXCOORDS, ARRAY_COUNT, n); }

bool m3gm_bounds(const void *geom, float box[6], float sphere[4])
{
    const uint8_t *p = tm_link((const uint8_t *)geom + M3GM_POINTS);
    if (!p) return false;
    memcpy(box, p + PNTA_BOUNDS, 6 * sizeof(float));
    memcpy(sphere, p + PNTA_BOUNDS + 24, 4 * sizeof(float));
    return true;
}

/* The strip walk of MSrGeomContext_Method_Geometry_Draw 0x410930 (the method at +0x3c): a value with
   bit 31 set starts a strip with it and the next two; every other one makes a triangle with the two
   before it. The order isn't flipped on every other triangle: the draw culls by the face normals, not
   the winding. */
int32_t m3gm_triangles(const void *geom, uint32_t *tris, uint32_t *normals, int32_t cap)
{
    uint32_t n_idx, n_fn, n_points;
    const uint8_t *idx = array(geom, M3GM_STRIPS, ARRAY_COUNT, &n_idx);
    const uint8_t *fn = array(geom, M3GM_FACE_NORMAL_INDEX, ARRAY_COUNT, &n_fn);
    m3gm_points(geom, &n_points);
    int32_t n = 0;
    uint32_t a = 0, b = 0;
    for (uint32_t k = 0; k < n_idx;) {
        uint32_t v = rd32(idx + 4 * k), c;
        if (v & 0x80000000u) {
            if (k + 3 > n_idx) return -1;
            a = v & 0x7fffffffu;
            b = rd32(idx + 4 * (k + 1));
            c = rd32(idx + 4 * (k + 2));
            k += 3;
        } else {
            c = v;
            k++;
        }
        if (a >= n_points || b >= n_points || c >= n_points) return -1;
        if (tris && n < cap) {
            tris[3 * n] = a;
            tris[3 * n + 1] = b;
            tris[3 * n + 2] = c;
        }
        if (normals && n < cap) normals[n] = (uint32_t)n < n_fn ? rd32(fn + 4 * (uint32_t)n) : 0;
        n++;
        a = b;
        b = c;
    }
    return (uint32_t)n == n_fn ? n : -1;
}
