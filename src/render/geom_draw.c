/* M3GM drawing: see geom_draw.h. */
#include "geom_draw.h"

#include "body.h"
#include "gl_dev.h"
#include "m3gm.h"
#include "persist.h"
#include "tm.h"
#include "txmp.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The software geometry engine's lights (its private context, 0x560a28): the ambient (+0x60), the
   directional lights' count (+0x6c, at most 4) and the lights (+0x70, 0x18 bytes each: the direction,
   normalised, then the colour). */
static bool lit;
static float ambient[3];
static int nlights;
static float lights[4][6];
static float eye[3];

/* 0x4135c0 (the engine's method +0xc) */
void geom_set_ambient(float r, float g, float b) { ambient[0] = r, ambient[1] = g, ambient[2] = b; }

/* 0x4135e0 (the method +0x10): each direction's length is taken over x, y and x again (the direction's
   z is never read, and its x stands for it: the original's own slip, kept), the direction divided by it
   and the colour multiplied by it. */
void geom_set_lights(int n, const float (*l)[6])
{
    if (n > 4) n = 4;
    nlights = n;
    for (int i = 0; i < n; i++) {
        float x = l[i][0], y = l[i][1];
        float len = sqrtf(x * x + y * y + x * x), k = 1.0f / len;
        lights[i][0] = k * x, lights[i][1] = k * y, lights[i][2] = k * x;
        for (int c = 0; c < 3; c++) lights[i][3 + c] = len * l[i][3 + c];
    }
}

/* ONrGameState_SetupDefaultLighting 0x4f4a30: a white ambient and two lights, one from above, as many
   of them as the graphics quality allows (ONrMotoko_GraphicsQuality_NumDirectionalLights 0x508490: 1, 1,
   2, 2, 2), the ambient lower with more (0x52b37c, 0x52b390, 0x52b394). */
void geom_default_lighting(void)
{
    static const float l[2][6] = { { -0.5f, -1.0f, -0.5f, 0.7f, 0.7f, 0.7f }, { 0.5f, 0.5f, 1.0f, 0.3f, 0.3f, 0.3f } };
    int q = persist_quality(), n = q < 2 ? 1 : 2;
    float a = n == 0 ? 0.85f : n == 1 ? 0.65f : 0.55f;
    geom_set_ambient(a, a, a);
    geom_set_lights(n, l);
    lit = true;
}

void geom_set_eye(const float p[3]) { memcpy(eye, p, sizeof eye); }

/* MUrUnsignedSmallFloat_To_Uns_Round 0x4340a0, after the clamp to 255 */
static uint8_t channel(float f)
{
    f *= 255.0f;
    if (f > 255.0f) f = 255.0f;
    return (uint8_t)lrintf(f);
}

/* 0x417f00: a vertex's colour, the ambient plus each light facing the normal (n, in the world), and with
   uv the reflection map's coordinates: the view from the eye to the point mirrored about the normal, as
   a sphere map (the view isn't normalised: the +1 is in the world's units). */
static void vertex_light(const float p[3], const float n[3], float *uv, uint8_t rgb[3])
{
    if (uv) {
        float d[3] = { p[0] - eye[0], p[1] - eye[1], p[2] - eye[2] };
        float k = 2.0f * (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]);
        float rx = d[0] - k * n[0], ry = d[1] - k * n[1], rz = d[2] - k * n[2] + 1.0f;
        float m = 1.0f / sqrtf(rx * rx + ry * ry + rz * rz) * 0.5f;
        uv[0] = rx * m + 0.5f, uv[1] = ry * m + 0.5f;
    }
    float c[3] = { ambient[0], ambient[1], ambient[2] };
    for (int i = 0; i < nlights; i++) {
        float f = -lights[i][0] * n[0] + -lights[i][1] * n[1] + -lights[i][2] * n[2];
        if (f > 0.0f)
            for (int j = 0; j < 3; j++) c[j] = f * lights[i][3 + j] + c[j];
    }
    for (int j = 0; j < 3; j++) rgb[j] = channel(c[j]);
}

static void draw(const void *g, const float m[12], const void *tex, uint8_t alpha, int mode)
{
    uint32_t np, nn, nuv;
    const uint8_t *pts = g ? m3gm_points(g, &np) : NULL, *vn = g ? m3gm_vertex_normals(g, &nn) : NULL;
    const uint8_t *uv = g ? m3gm_texcoords(g, &nuv) : NULL;
    int32_t nt = g ? m3gm_triangles(g, NULL, NULL, 0) : -1;
    if (!pts || nt <= 0 || np > 65535 || nn < np || nuv < np) return;
    if (!lit) geom_default_lighting();
    if (!tex) tex = tm_link((const uint8_t *)g + M3GM_TEXTURE);
    const uint8_t *t = tex;
    uint32_t tflags = t ? rd32(t + TXMP_FLAGS) : 0;
    /* M3rGeometry_Draw 0x41a750: a texture with a reflection map, from the graphics quality 1 (0x508540),
       is drawn twice (the draw engine's modes 7 and 8, 0x405b90): the reflection map, lit, over the
       reflection coordinates (computed with the texture's flag 0x200, 0x410930), then the texture lit
       and added to it as much as its alpha (GL_ONE, GL_SRC_ALPHA) */
    const void *refl = t ? tm_link(t + TXMP_ENVMAP) : NULL;
    if (refl && persist_quality() < 1) refl = NULL;
    /* 0x417690: the vertices unlit (white) with the geometry's flag 0x10 or the texture's 0x800 */
    bool unlit = (rd32((const uint8_t *)g + M3GM_FLAGS) & 0x10) || (tflags & 0x800);
    bool want_uv = (tflags & 0x200) != 0;
    GlVertex *v = malloc(sizeof *v * np);
    GlVertex *rv = refl ? malloc(sizeof *rv * np) : NULL;
    uint32_t *tris = malloc(sizeof *tris * 3 * (size_t)nt);
    uint16_t *idx = malloc(sizeof *idx * 3 * (size_t)nt);
    if (v && tris && idx && (!refl || rv)) {
        m3gm_triangles(g, tris, NULL, nt);
        for (uint32_t i = 0; i < np; i++) {
            float p[3], n[3], wn[3], ruv[2] = { 0, 0 };
            uint8_t rgb[3] = { 255, 255, 255 };
            memcpy(p, pts + 12 * i, 12);
            memcpy(n, vn + 12 * i, 12);
            body_transform(m, p, &v[i].x);
            for (int r = 0; r < 3; r++) wn[r] = m[r] * n[0] + m[3 + r] * n[1] + m[6 + r] * n[2];
            memcpy(&v[i].u, uv + 8 * i, 8);
            if (!unlit) vertex_light(&v[i].x, wn, want_uv ? ruv : NULL, rgb);
            v[i].r = rgb[0], v[i].g = rgb[1], v[i].b = rgb[2], v[i].a = alpha;
            if (rv) rv[i] = v[i], rv[i].u = ruv[0], rv[i].v = ruv[1];
        }
        for (int32_t k = 0; k < 3 * nt; k++) idx[k] = (uint16_t)tris[k];
        if (rv) {
            gl_dev_draw(rv, np, idx, (uint32_t)(3 * nt), gl_dev_texture(refl), GL_DEV_OPAQUE);
            gl_dev_draw(v, np, idx, (uint32_t)(3 * nt), gl_dev_texture(tex), GL_DEV_REFLECT);
        } else
            gl_dev_draw(v, np, idx, (uint32_t)(3 * nt), gl_dev_texture(tex), mode);
    }
    free(v), free(rv), free(tris), free(idx);
}

void geom_draw(const void *g, const float m[12], const void *tex) { draw(g, m, tex, 255, GL_DEV_OPAQUE); }

void geom_draw_alpha(const void *g, const float m[12], const void *tex, uint8_t alpha)
{
    if (alpha) draw(g, m, tex, alpha, alpha == 255 ? GL_DEV_OPAQUE : GL_DEV_BLEND);
}

/* 0x4fb290 (OBiDraw 0x438fb0, for an object with its own shade: its flag 0x8000, obj_shade): the shade
   as the ambient, no directional lights; OBiDraw sets the default lights again after the object */
void geom_set_shade(uint32_t shade)
{
    geom_set_ambient((float)(shade >> 16 & 0xff) * (1.0f / 255.0f), (float)(shade >> 8 & 0xff) * (1.0f / 255.0f),
                     (float)(shade & 0xff) * (1.0f / 255.0f));
    geom_set_lights(0, NULL);
    lit = true;
}
