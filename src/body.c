/* Character bodies: see body.h. */
#include "body.h"

#include "tm.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* TRGA, TRTA, TRIA, TRMA: 22 bytes of padding, the u16 count, the elements. */
static const uint8_t *var_array(const void *inst, unsigned field, uint32_t *count)
{
    const uint8_t *a = tm_link((const uint8_t *)inst + field);
    *count = a ? rd16(a + 0x16) : 0;
    return a ? a + 0x18 : NULL;
}

void *body_of_class(const void *oncc, int detail)
{
    const uint8_t *set = tm_link((const uint8_t *)oncc + ONCC_BODY_SET);
    return set && detail >= 0 && detail < 5 ? tm_link(set + 4 * detail) : NULL;
}

void *body_texture(const void *oncc, uint32_t part)
{
    uint32_t n;
    const uint8_t *t = var_array(oncc, ONCC_TEXTURES, &n);
    return part < n ? tm_link(t + 4 * part) : NULL;
}

uint32_t body_bones(const void *trcm) { return rd16((const uint8_t *)trcm + TRCM_BONES); }

void *body_geometry(const void *trcm, uint32_t bone)
{
    uint32_t n;
    const uint8_t *g = var_array(trcm, TRCM_GEOMETRIES, &n);
    return bone < n ? tm_link(g + 4 * bone) : NULL;
}

void body_quat_to_matrix(const float q[4], float m[9])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float s = 2.0f / (w * w + z * z + y * y + x * x);
    float ys = s * y, zs = s * z, xw = s * x * w, xx = s * x * x;
    m[0] = 1.0f - (zs * z + ys * y);
    m[1] = ys * x - zs * w;
    m[2] = zs * x + ys * w;
    m[3] = ys * x + zs * w;
    m[4] = 1.0f - (zs * z + xx);
    m[5] = zs * y - xw;
    m[6] = zs * x - ys * w;
    m[7] = zs * y + xw;
    m[8] = 1.0f - (ys * y + xx);
}

void body_transform(const BodyMatrix m, const float p[3], float o[3])
{
    for (int r = 0; r < 3; r++) o[r] = m[r] * p[0] + m[3 + r] * p[1] + m[6 + r] * p[2] + m[9 + r];
}

typedef struct {
    const uint8_t *tree, *trans;
    uint32_t bones;
    const float *quats;
    BodyMatrix *out;
    int depth;
} Build;

static float rdf(const uint8_t *p) { uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; float f; memcpy(&f, &v, 4); return f; }

/* iBuildMatriciesRecursive 0x427570: along the children in a loop, recursing for the siblings. */
static bool build(Build *b, uint32_t bone, const float *parent)
{
    if (++b->depth > (int)BODY_MAX_BONES) return false;
    for (;;) {
        if (bone >= b->bones) return false;
        const uint8_t *e = b->tree + 4 * bone;
        float *m = b->out[bone];
        memcpy(m, parent, sizeof(BodyMatrix));
        float t[3] = { rdf(b->trans + 12 * bone), rdf(b->trans + 12 * bone + 4), rdf(b->trans + 12 * bone + 8) };
        body_transform(parent, t, m + 9);
        if (b->quats) {
            float r[9], c[9];
            body_quat_to_matrix(b->quats + 4 * bone, r);
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 3; row++)
                    c[3 * col + row] = parent[row] * r[3 * col] + parent[3 + row] * r[3 * col + 1] + parent[6 + row] * r[3 * col + 2];
            memcpy(m, c, sizeof c);
        }
        if (e[2] && !build(b, e[2], parent)) return false;
        if (!e[1]) break;
        bone = e[1];
        parent = m;
    }
    b->depth--;
    return true;
}

bool body_matrices(const void *trcm, const BodyMatrix root, const float *quats, BodyMatrix *out)
{
    uint32_t nt, ntr, bones = body_bones(trcm);
    Build b = { var_array(trcm, TRCM_HIERARCHY, &nt), var_array(trcm, TRCM_TRANSLATIONS, &ntr), bones, quats, out, 0 };
    if (!b.tree || !b.trans || nt < bones || ntr < bones || bones > BODY_MAX_BONES) return false;
    return build(&b, 0, root);
}
