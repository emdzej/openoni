/* The decals: see decal_draw.h. */
#include "decal_draw.h"

#include "akev.h"
#include "decal.h"
#include "akev_vis.h"
#include "gl_dev.h"

#include <stdint.h>

enum { VERTS_MAX = 0x200 };

typedef struct { const void *env; } Ctx;

/* P3iDisplayDecals 0x453760's decal (M3rDecal_Draw, its method in the Mac build's
   MSrGeomContext_Method_Decal_Draw): the quad's alpha (a see-through quad's, AGQR's second half; else
   0xff) times the decal's, twice over (at most 0xff); each vertex its shade times the tint. Blended,
   without writing the depth (0x41e720 state 4 off around both lists). A decal on a quad the visibility
   hasn't marked (AKrEnvironment_GetVisVector 0x444010) is left out. */
static void one(const DecalDraw *d, void *user)
{
    const Ctx *x = user;
    if (d->quad >= 0 && !vis_quad((uint32_t)d->quad)) return;
    uint32_t qa = 0xff;
    if (d->quad >= 0) {
        uint32_t nq, nr;
        const uint8_t *g = akev_quad_records(x->env, &nq), *r = akev_render_records(x->env, &nr);
        if (g && r && (uint32_t)d->quad < nq && (uint32_t)d->quad < nr) {
            const uint8_t *f = g + 0x38u * (uint32_t)d->quad + 0x30;
            if (f[0] & AKEV_QUAD_JELLO) qa = (uint32_t)r[4u * (uint32_t)d->quad + 2] | (uint32_t)r[4u * (uint32_t)d->quad + 3] << 8;
        }
    }
    uint32_t a = (uint32_t)d->alpha * qa * 2 >> 8;
    if (a > 0xff) a = 0xff;
    unsigned tex = gl_dev_texture(d->texture);
    if (!tex) tex = gl_dev_white();
    static GlVertex v[VERTS_MAX];
    uint32_t n = d->nverts > VERTS_MAX ? VERTS_MAX : d->nverts;
    uint32_t tr = (d->tint >> 16) & 0xff, tg = (d->tint >> 8) & 0xff, tb = d->tint & 0xff;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t s = d->shade[i];
        v[i] = (GlVertex){ d->pos[3 * i], d->pos[3 * i + 1], d->pos[3 * i + 2], d->uv[2 * i], d->uv[2 * i + 1],
                           (uint8_t)(((s >> 16) & 0xff) * tr / 255), (uint8_t)(((s >> 8) & 0xff) * tg / 255),
                           (uint8_t)((s & 0xff) * tb / 255), (uint8_t)a };
    }
    uint32_t ni = 3u * d->ntris;
    for (uint32_t i = 0; i < ni; i++)
        if (d->tris[i] >= n) return;
    gl_dev_draw(v, n, d->tris, ni, tex, GL_DEV_BLEND);
}

void decal_draw(const void *env)
{
    if (!decal_count()) return;
    Ctx x = { env };
    gl_dev_cull(0);
    decal_each(one, &x);
}
