/* M3GM drawing: see geom_draw.h. */
#include "geom_draw.h"

#include "body.h"
#include "gl_dev.h"
#include "m3gm.h"
#include "tm.h"

#include <stdlib.h>
#include <string.h>

static void draw(const void *g, const float m[12], const void *tex, uint8_t alpha, int mode)
{
    uint32_t np, nn, nuv;
    const uint8_t *pts = g ? m3gm_points(g, &np) : NULL, *vn = g ? m3gm_vertex_normals(g, &nn) : NULL;
    const uint8_t *uv = g ? m3gm_texcoords(g, &nuv) : NULL;
    int32_t nt = g ? m3gm_triangles(g, NULL, NULL, 0) : -1;
    if (!pts || nt <= 0 || np > 65535 || nn < np || nuv < np) return;
    static const float light[3] = { 0.3f, 0.85f, 0.42f };
    GlVertex *v = malloc(sizeof *v * np);
    uint32_t *tris = malloc(sizeof *tris * 3 * (size_t)nt);
    uint16_t *idx = malloc(sizeof *idx * 3 * (size_t)nt);
    if (v && tris && idx) {
        m3gm_triangles(g, tris, NULL, nt);
        for (uint32_t i = 0; i < np; i++) {
            float p[3], n[3], wn[3];
            memcpy(p, pts + 12 * i, 12);
            memcpy(n, vn + 12 * i, 12);
            body_transform(m, p, &v[i].x);
            for (int r = 0; r < 3; r++) wn[r] = m[r] * n[0] + m[3 + r] * n[1] + m[6 + r] * n[2];
            memcpy(&v[i].u, uv + 8 * i, 8);
            float d = wn[0] * light[0] + wn[1] * light[1] + wn[2] * light[2];
            uint8_t k = (uint8_t)(110 + 145 * (d > 0 ? d : 0));
            v[i].r = v[i].g = v[i].b = k, v[i].a = alpha;
        }
        for (int32_t t = 0; t < 3 * nt; t++) idx[t] = (uint16_t)tris[t];
        if (!tex) tex = tm_link((const uint8_t *)g + M3GM_TEXTURE);
        gl_dev_draw(v, np, idx, (uint32_t)(3 * nt), gl_dev_texture(tex), mode);
    }
    free(v), free(tris), free(idx);
}

void geom_draw(const void *g, const float m[12], const void *tex) { draw(g, m, tex, 255, GL_DEV_OPAQUE); }

void geom_draw_alpha(const void *g, const float m[12], const void *tex, uint8_t alpha)
{
    if (alpha) draw(g, m, tex, alpha, alpha == 255 ? GL_DEV_OPAQUE : GL_DEV_BLEND);
}
