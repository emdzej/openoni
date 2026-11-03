/* The environment's draw: see env_draw.h. */
#include "env_draw.h"

#include "akev.h"
#include "gl_dev.h"
#include "im.h"
#include "txmp.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned tex;
    uint32_t first, count;
    int mode;
} Range;

static unsigned mesh;
static const void *loaded_env;
static uint32_t loaded_serial;
static Range *ranges;
static uint32_t n_ranges, n_solid;

/* A solid quad's texture with an alpha channel is drawn with the alpha test. */
static int solid_mode(const void *txmp)
{
    if (!txmp) return GL_DEV_OPAQUE;
    TxmpInfo in;
    txmp_info(txmp, &in);
    return in.type == IM_ARGB4444 || in.type == IM_ARGB1555 ? GL_DEV_ALPHA_TEST : GL_DEV_OPAQUE;
}

void env_draw_unload(void)
{
    gl_dev_mesh_free(mesh);
    mesh = 0;
    free(ranges);
    ranges = NULL;
    n_ranges = n_solid = 0;
}

bool env_draw_load(const void *env)
{
    env_draw_unload();
    loaded_env = env;
    loaded_serial = akev_flags_serial();
    uint32_t nq = akev_quad_count(env), nt = akev_texture_count(env), np, nuv;
    const uint8_t *pts = akev_points(env, &np), *uv = akev_texcoords(env, &nuv);
    /* The quads AKiEnvironment_ComputeVis 0x444030 would draw, sorted by texture (its counting sort over
       the AGQR index), the solid ones first, then the transparent (0x80) ones, as M3rEnv_DrawGQList 0x41a930
       splits them. Nothing is culled by visibility yet: all of them are drawn. */
    uint32_t *order = malloc(sizeof *order * (nq ? nq : 1)), n = 0, nk = 2 * (nt + 1);
    uint32_t *start = calloc(nk + 1, sizeof *start);
    AkevQuad *q = malloc(sizeof *q * (nq ? nq : 1));
    if (!order || !q || !start) return free(order), free(q), free(start), false;
    /* the key: the pass (solid 0, transparent 1), then the texture index (past the array: nt) */
    for (uint32_t i = 0; i < nq; i++) {
        if (!akev_quad(env, i, &q[i]) || !akev_quad_drawn(q[i].flags)) { q[i].corners = 0; continue; }
        uint32_t key = (q[i].flags & AKEV_QUAD_TRANSPARENT ? nt + 1 : 0) + (q[i].texture < nt ? q[i].texture : nt);
        q[i].object = key;      /* reused: the sort key */
        start[key + 1]++;
        n++;
    }
    for (uint32_t k = 0; k < nk; k++) start[k + 1] += start[k];
    for (uint32_t i = 0; i < nq; i++)
        if (q[i].corners) order[start[q[i].object]++] = i;
    free(start);
    GlVertex *v = malloc(sizeof *v * 4 * (n ? n : 1));
    uint32_t *idx = malloc(sizeof *idx * 6 * (n ? n : 1)), nv = 0, ni = 0;
    ranges = malloc(sizeof *ranges * (n ? n : 1));
    if (!v || !idx || !ranges) return free(order), free(q), free(v), free(idx), false;
    for (uint32_t k = 0; k < n; k++) {
        const AkevQuad *e = &q[order[k]];
        bool transparent = e->flags & AKEV_QUAD_TRANSPARENT;
        const void *txmp = e->texture < nt ? akev_texture(env, e->texture) : NULL;
        unsigned tex = gl_dev_texture(txmp);
        int mode = transparent ? GL_DEV_BLEND : solid_mode(txmp);
        if (!n_ranges || ranges[n_ranges - 1].tex != tex || ranges[n_ranges - 1].mode != mode) {
            ranges[n_ranges++] = (Range){ tex, ni, 0, mode };
            if (!transparent) n_solid = n_ranges;
        }
        for (uint32_t c = 0; c < e->corners; c++) {
            const uint8_t *p = pts + 12 * e->point[c], *t = uv + 8 * e->texcoord[c];
            GlVertex *o = &v[nv + c];
            memcpy(&o->x, p, 12);
            memcpy(&o->u, t, 8);
            uint32_t s = e->shade[c];
            o->r = (uint8_t)(s >> 16), o->g = (uint8_t)(s >> 8), o->b = (uint8_t)s, o->a = 255;
        }
        static const uint32_t tri[6] = { 0, 1, 2, 0, 2, 3 };
        for (uint32_t j = 0; j < (e->corners == 4 ? 6u : 3u); j++) idx[ni++] = nv + tri[j];
        ranges[n_ranges - 1].count += e->corners == 4 ? 6 : 3;
        nv += e->corners;
    }
    mesh = gl_dev_mesh(v, nv, idx, ni);
    free(order), free(q), free(v), free(idx);
    return mesh != 0;
}

void env_draw(void)
{
    /* the quads' flags changed (env_show): the mesh again, once a frame at most */
    if (loaded_env && loaded_serial != akev_flags_serial()) env_draw_load(loaded_env);
    for (uint32_t r = 0; r < n_ranges; r++) gl_dev_draw_mesh(mesh, ranges[r].first, ranges[r].count, ranges[r].tex, ranges[r].mode);
}
