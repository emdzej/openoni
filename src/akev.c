/* AKEV environment: see akev.h. */
#include "akev.h"

#include "tm.h"

#include <math.h>
#include <string.h>

enum {
    ARRAY_COUNT = 0x14,     /* PLEA, TXCA, AGQG, AGQR, TXMA: 20 bytes of padding, the count, the elements */
    PNTA_COUNT = 0x34,      /* PNTA: padding, box, sphere, then the count (as in m3gm.c) */
    QUAD_SIZE = 0x38,       /* AGQG: 4 point indices, 4 texcoord indices, 4 shades, flags, object id */
    QUAD_FLAGS = 0x30,
    QUAD_OBJECT = 0x34,
    RENDER_SIZE = 4,        /* AGQR: u16 texture index, u16 padding */
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static const uint8_t *array(const void *env, unsigned field, unsigned count_at, uint32_t *count)
{
    const uint8_t *a = tm_link((const uint8_t *)env + field);
    *count = a ? rd32(a + count_at) : 0;
    return a ? a + count_at + 4 : NULL;
}

const uint8_t *akev_points(const void *e, uint32_t *n) { return array(e, AKEV_POINTS, PNTA_COUNT, n); }
const uint8_t *akev_texcoords(const void *e, uint32_t *n) { return array(e, AKEV_TEXCOORDS, ARRAY_COUNT, n); }
const uint8_t *akev_planes(const void *e, uint32_t *n) { return array(e, AKEV_PLANES, ARRAY_COUNT, n); }
const uint8_t *akev_quad_records(const void *e, uint32_t *n) { return array(e, AKEV_QUADS, ARRAY_COUNT, n); }
const uint8_t *akev_collision_records(const void *e, uint32_t *n) { return array(e, AKEV_QUAD_COLLISION, ARRAY_COUNT, n); }
const uint8_t *akev_render_records(const void *e, uint32_t *n) { return array(e, AKEV_QUAD_RENDER, ARRAY_COUNT, n); }

void akev_bounds(const void *env, float box[6]) { memcpy(box, (const uint8_t *)env + AKEV_BOUNDS, 6 * sizeof(float)); }

uint32_t akev_quad_count(const void *env)
{
    uint32_t n;
    array(env, AKEV_QUADS, ARRAY_COUNT, &n);
    return n;
}

/* The corner count is 4 less the triangle flag, as the walks over a quad's corners take it (0x507080,
   0x5071e0, and the clipper in MSrGeomContext_Method_Env_DrawGQList 0x40ff30). */
bool akev_quad(const void *env, uint32_t i, AkevQuad *q)
{
    uint32_t nq, nr, np, nt;
    const uint8_t *g = array(env, AKEV_QUADS, ARRAY_COUNT, &nq);
    const uint8_t *r = array(env, AKEV_QUAD_RENDER, ARRAY_COUNT, &nr);
    akev_points(env, &np);
    akev_texcoords(env, &nt);
    if (i >= nq || i >= nr) return false;
    g += (size_t)i * QUAD_SIZE;
    q->flags = rd32(g + QUAD_FLAGS);
    q->object = rd32(g + QUAD_OBJECT);
    q->corners = q->flags & AKEV_QUAD_TRIANGLE ? 3 : 4;
    for (int k = 0; k < 4; k++) {
        q->point[k] = rd32(g + 4 * k);
        q->texcoord[k] = rd32(g + 16 + 4 * k);
        q->shade[k] = rd32(g + 32 + 4 * k);
        if (q->point[k] >= np || q->texcoord[k] >= nt) return false;
    }
    r += (size_t)i * RENDER_SIZE;
    q->texture = (uint32_t)r[0] | (uint32_t)r[1] << 8;
    return true;
}

uint32_t akev_texture_count(const void *env)
{
    uint32_t n;
    array(env, AKEV_TEXTURES, ARRAY_COUNT, &n);
    return n;
}

void *akev_texture(const void *env, uint32_t i)
{
    uint32_t n;
    const uint8_t *t = array(env, AKEV_TEXTURES, ARRAY_COUNT, &n);
    return i < n ? tm_link(t + 4 * i) : NULL;
}

static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

bool akev_floor(const void *env, float x, float y, float z, float up, float down, float *floor_y)
{
    uint32_t nq, nc, npl, np;
    const uint8_t *col = array(env, AKEV_QUAD_COLLISION, ARRAY_COUNT, &nc);
    const uint8_t *pl = akev_planes(env, &npl), *pts = akev_points(env, &np);
    nq = akev_quad_count(env);
    bool found = false;
    float best = y - down;
    for (uint32_t i = 0; i < nq && i < nc; i++) {
        const uint8_t *c = col + 28u * i;
        /* AGQC: plane index (bit 31: negated), then the box: min xyz, max xyz */
        if (x < rdf(c + 4) || x > rdf(c + 16) || z < rdf(c + 12) || z > rdf(c + 24)) continue;
        if (rdf(c + 8) > y + up || rdf(c + 20) < y - down) continue;
        uint32_t pi = rd32(c), k = pi & 0x7fffffffu;
        if (k >= npl) continue;
        float sgn = pi & 0x80000000u ? -1.0f : 1.0f;
        float a = sgn * rdf(pl + 16 * k), b = sgn * rdf(pl + 16 * k + 4), cc = sgn * rdf(pl + 16 * k + 8), d = sgn * rdf(pl + 16 * k + 12);
        if (b <= 0.5f) continue;
        AkevQuad q;
        if (!akev_quad(env, i, &q) || (q.flags & AKEV_QUAD_GHOST)) continue;
        /* inside the corners, seen from above (the winding's sign either way) */
        int pos = 0, neg = 0;
        for (uint32_t e = 0; e < q.corners; e++) {
            const uint8_t *p0 = pts + 12 * q.point[e], *p1 = pts + 12 * q.point[(e + 1) % q.corners];
            float cr = (rdf(p1) - rdf(p0)) * (z - rdf(p0 + 8)) - (rdf(p1 + 8) - rdf(p0 + 8)) * (x - rdf(p0));
            pos += cr > 0, neg += cr < 0;
        }
        if (pos && neg) continue;
        float h = -(a * x + cc * z + d) / b;
        if (h > y + up || h < best) continue;
        best = h;
        found = true;
    }
    if (found) *floor_y = best;
    return found;
}

/* Whether p, on the quad's plane (normal n), is inside its corners, grown by margin. */
static bool inside(const uint8_t *pts, const AkevQuad *q, const float n[3], const float p[3], float margin)
{
    for (uint32_t e = 0; e < q->corners; e++) {
        const uint8_t *a = pts + 12 * q->point[e], *b = pts + 12 * q->point[(e + 1) % q->corners];
        float ax = rdf(a), ay = rdf(a + 4), az = rdf(a + 8);
        float ex = rdf(b) - ax, ey = rdf(b + 4) - ay, ez = rdf(b + 8) - az;
        /* the edge's inward normal: n x edge (the quads wind counter-clockwise about their normal) */
        float ix = n[1] * ez - n[2] * ey, iy = n[2] * ex - n[0] * ez, iz = n[0] * ey - n[1] * ex;
        float len = sqrtf(ix * ix + iy * iy + iz * iz);
        if (len <= 0) continue;
        if (((p[0] - ax) * ix + (p[1] - ay) * iy + (p[2] - az) * iz) / len < -margin) return false;
    }
    return true;
}

bool akev_segment_hits(const void *env, const float p[3], const float v[3], uint32_t skip)
{
    uint32_t nc, npl, np, nq = akev_quad_count(env);
    const uint8_t *col = array(env, AKEV_QUAD_COLLISION, ARRAY_COUNT, &nc);
    const uint8_t *pl = akev_planes(env, &npl), *pts = akev_points(env, &np);
    float lo[3], hi[3];
    for (int k = 0; k < 3; k++) lo[k] = fminf(p[k], p[k] + v[k]), hi[k] = fmaxf(p[k], p[k] + v[k]);
    for (uint32_t i = 0; i < nq && i < nc; i++) {
        const uint8_t *c = col + 28u * i;
        if (hi[0] < rdf(c + 4) || lo[0] > rdf(c + 16) || hi[1] < rdf(c + 8) || lo[1] > rdf(c + 20) ||
            hi[2] < rdf(c + 12) || lo[2] > rdf(c + 24)) continue;
        uint32_t pi = rd32(c), k = pi & 0x7fffffffu;
        if (k >= npl) continue;
        float sgn = pi & 0x80000000u ? -1.0f : 1.0f;
        float n[3] = { sgn * rdf(pl + 16 * k), sgn * rdf(pl + 16 * k + 4), sgn * rdf(pl + 16 * k + 8) };
        float d = sgn * rdf(pl + 16 * k + 12);
        float d0 = n[0] * p[0] + n[1] * p[1] + n[2] * p[2] + d, dv = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
        float d1 = d0 + dv;
        if ((d0 > 0.0f && d1 > 0.0f) || (d0 < 0.0f && d1 < 0.0f) || dv == 0.0f) continue;
        AkevQuad q;
        if (!akev_quad(env, i, &q) || (q.flags & AKEV_QUAD_GHOST) || (q.flags & skip)) continue;
        float t = -d0 / dv, at[3] = { p[0] + v[0] * t, p[1] + v[1] * t, p[2] + v[2] * t };
        if (inside(pts, &q, n, at, 0.0f)) return true;
    }
    return false;
}

static uint32_t flags_serial;

uint32_t akev_flags_serial(void) { return flags_serial; }

uint32_t akev_script_count(const void *env)
{
    uint32_t nq, ni;
    array(env, AKEV_SCRIPT_QUADS, ARRAY_COUNT, &nq);
    array(env, AKEV_SCRIPT_IDS, ARRAY_COUNT, &ni);
    return nq < ni ? nq : ni;
}

bool akev_script_entry(const void *env, uint32_t i, uint32_t *quad, uint32_t *id)
{
    uint32_t nq, ni;
    const uint8_t *q = array(env, AKEV_SCRIPT_QUADS, ARRAY_COUNT, &nq), *d = array(env, AKEV_SCRIPT_IDS, ARRAY_COUNT, &ni);
    if (i >= nq || i >= ni) return false;
    *quad = rd32(q + 4 * i);
    *id = rd32(d + 4 * i);
    return true;
}

void akev_quad_set_flags(void *env, uint32_t quad, uint32_t set, uint32_t clear)
{
    uint32_t nq;
    uint8_t *g = (uint8_t *)array(env, AKEV_QUADS, ARRAY_COUNT, &nq);
    if (!g || quad >= nq) return;
    uint8_t *f = g + (size_t)quad * QUAD_SIZE + QUAD_FLAGS;
    uint32_t v = (rd32(f) | set) & ~clear;
    f[0] = (uint8_t)v, f[1] = (uint8_t)(v >> 8), f[2] = (uint8_t)(v >> 16), f[3] = (uint8_t)(v >> 24);
    flags_serial++;
}

void akev_quad_set_shade(void *env, uint32_t quad, uint32_t argb)
{
    uint32_t nq;
    uint8_t *g = (uint8_t *)array(env, AKEV_QUADS, ARRAY_COUNT, &nq);
    if (!g || quad >= nq) return;
    uint8_t *q = g + (size_t)quad * QUAD_SIZE;
    unsigned corners = rd32(q + QUAD_FLAGS) & AKEV_QUAD_TRIANGLE ? 3 : 4;
    for (unsigned k = 0; k < corners; k++) {
        uint8_t *f = q + 32 + 4 * k;
        f[0] = (uint8_t)argb, f[1] = (uint8_t)(argb >> 8), f[2] = (uint8_t)(argb >> 16), f[3] = (uint8_t)(argb >> 24);
    }
    flags_serial++;
}

bool akev_quad_plane(const void *env, uint32_t quad, float plane[4])
{
    uint32_t nc, npl;
    const uint8_t *col = array(env, AKEV_QUAD_COLLISION, ARRAY_COUNT, &nc);
    const uint8_t *pl = akev_planes(env, &npl);
    if (!col || !pl || quad >= nc) return false;
    uint32_t pi = rd32(col + 28u * quad), k = pi & 0x7fffffffu;
    if (k >= npl) return false;
    float sgn = pi & 0x80000000u ? -1.0f : 1.0f;
    for (int i = 0; i < 4; i++) plane[i] = sgn * rdf(pl + 16 * k + 4 * i);
    return true;
}

bool akev_quad_corner(const void *env, uint32_t quad, unsigned k, float out[3])
{
    AkevQuad q;
    uint32_t np;
    const uint8_t *pts = akev_points(env, &np);
    if (!pts || k > 3 || !akev_quad(env, quad, &q) || q.point[k] >= np) return false;
    for (int i = 0; i < 3; i++) out[i] = rdf(pts + 12u * q.point[k] + 4 * i);
    return true;
}
