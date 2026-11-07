/* The sky: see sky_draw.h. */
#include "sky_draw.h"

#include "gl_dev.h"
#include "tm.h"

#include <math.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

enum { PLANETS_MAX = 8 };

/* The sky's state (the game state's +0x167714): the ONSK, the box (+0x68, made by the geometry engine's
   method +0x4c, 0x412e60) and the planets' places (+4, 3 floats each). */
static struct {
    const uint8_t *onsk;
    const void *face[5];
    uint32_t planets;
    float planet[PLANETS_MAX][3];
} S;

/* 0x412e60: a cube 5 each way of the eye, its corners 0 to 3 the top (y 5: -x -z, +x -z, +x +z, -x +z),
   4 to 7 the bottom; the faces, each with its texture: the top, -x, +x, -z, +z (no bottom); every face's
   corners take the coordinates (0, 0), (1, 0), (1, 1), (0, 1) in turn. */
static const float corner[8][3] = { { -5, 5, -5 }, { 5, 5, -5 }, { 5, 5, 5 }, { -5, 5, 5 },
                                    { -5, -5, -5 }, { 5, -5, -5 }, { 5, -5, 5 }, { -5, -5, 5 } };
static const uint8_t face_corner[5][4] = { { 3, 2, 1, 0 }, { 3, 0, 4, 7 }, { 1, 2, 6, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 } };
static const float face_uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

void sky_level_begin(const void *onsk, float far)
{
    memset(&S, 0, sizeof S);
    S.onsk = onsk;
    if (!onsk) return;
    for (int i = 0; i < 5; i++) S.face[i] = tm_link(S.onsk + 4 * i);
    /* each planet at its elevation (+0x98) and azimuth (+0xb8) on a sphere of half the far plane's
       distance (M3rCamera_GetStaticData 0x41b460) */
    S.planets = rd32(S.onsk + 0x50);
    if (S.planets > PLANETS_MAX) S.planets = PLANETS_MAX;
    float r = far * 0.5f;
    for (uint32_t i = 0; i < S.planets; i++) {
        float el = rdf(S.onsk + 0x98 + 4 * i), az = rdf(S.onsk + 0xb8 + 4 * i);
        S.planet[i][1] = sinf(el) * r;
        S.planet[i][0] = sinf(az) * cosf(el) * r;
        S.planet[i][2] = cosf(az) * cosf(el) * r;
    }
}

void sky_draw(const float eye[3], const float right[3], const float up[3])
{
    if (!S.onsk) return;
    /* the skybox (0x412fc0, the engine's method +0x48): every face with a texture, round the eye */
    for (int f = 0; f < 5; f++) {
        if (!S.face[f]) continue;
        GlVertex v[4];
        for (int k = 0; k < 4; k++) {
            const float *c = corner[face_corner[f][k]];
            v[k] = (GlVertex){ eye[0] + c[0], eye[1] + c[1], eye[2] + c[2], face_uv[k][0], face_uv[k][1], 255, 255, 255, 255 };
        }
        static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
        gl_dev_draw(v, 4, idx, 6, gl_dev_texture(S.face[f]), GL_DEV_SKY);
    }
    /* the planets: each one's texture (+0x18) as a sprite facing the camera (the engine's method +0x34,
       0x4118b0), as wide as +0x58 and as tall as +0x78 */
    for (uint32_t i = 0; i < S.planets; i++) {
        const void *tex = tm_link(S.onsk + 0x18 + 4 * i);
        if (!tex) continue;
        float sx = rdf(S.onsk + 0x58 + 4 * i), sy = rdf(S.onsk + 0x78 + 4 * i), p[3];
        for (int k = 0; k < 3; k++) p[k] = eye[k] + S.planet[i][k];
        static const float fs[4] = { -1, 1, -1, 1 }, ws[4] = { -1, -1, 1, 1 };
        GlVertex v[4];
        for (int k = 0; k < 4; k++) {
            float q[3];
            for (int a = 0; a < 3; a++) q[a] = p[a] + fs[k] * sx * right[a] + ws[k] * sy * up[a];
            v[k] = (GlVertex){ q[0], q[1], q[2], fs[k] < 0 ? 0.0f : 1.0f, ws[k] < 0 ? 0.0f : 1.0f, 255, 255, 255, 255 };
        }
        static const uint16_t idx[6] = { 0, 1, 3, 0, 3, 2 };
        const uint8_t *tb = tex;
        gl_dev_draw(v, 4, idx, 6, gl_dev_texture(tex), (tb[0x81] & 0x04) ? GL_DEV_ADD : GL_DEV_BLEND);
    }
}
