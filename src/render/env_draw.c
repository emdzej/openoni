/* The environment's draw: see env_draw.h. */
#include "env_draw.h"

#include "akev.h"
#include "akev_vis.h"
#include "gl_dev.h"
#include "im.h"
#include "txmp.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const void *txmp;       /* its frame picked at the draw (gl_dev_texture_now) */
    uint32_t first, count;
    int mode;
} Range;

static unsigned mesh;
static const void *loaded_env;

const void *env_draw_environment(void) { return loaded_env; }
static uint32_t loaded_serial;
static Range *ranges;
static uint32_t n_ranges, n_solid;
/* For the visibility's draw: each range's quads in order (the quad, its first index, its count), the
   static indices, and the frame's. */
typedef struct { uint32_t quad, first, count; } Drawn;
static Drawn *drawn;
static uint32_t n_drawn, n_quads;
static uint32_t *static_idx, *frame_idx;
static uint32_t *range_drawn;       /* each range's first entry of drawn */
static Range *frame_ranges;
static uint8_t *allowed;
static bool replaced;           /* the mesh holds a frame's indices */

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
    free(drawn), free(static_idx), free(frame_idx), free(range_drawn), free(frame_ranges), free(allowed);
    drawn = NULL, static_idx = frame_idx = range_drawn = NULL, frame_ranges = NULL, allowed = NULL;
    n_drawn = n_quads = 0;
    replaced = false;
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
    drawn = malloc(sizeof *drawn * (n ? n : 1));
    range_drawn = malloc(sizeof *range_drawn * (n + 1));
    if (!v || !idx || !ranges || !drawn || !range_drawn) return free(order), free(q), free(v), free(idx), false;
    for (uint32_t k = 0; k < n; k++) {
        const AkevQuad *e = &q[order[k]];
        bool transparent = e->flags & AKEV_QUAD_TRANSPARENT;
        const void *txmp = e->texture < nt ? akev_texture(env, e->texture) : NULL;
        int mode = transparent ? GL_DEV_BLEND : solid_mode(txmp);
        if (!n_ranges || ranges[n_ranges - 1].txmp != txmp || ranges[n_ranges - 1].mode != mode) {
            range_drawn[n_ranges] = n_drawn;
            ranges[n_ranges++] = (Range){ txmp, ni, 0, mode };
            if (!transparent) n_solid = n_ranges;
        }
        drawn[n_drawn++] = (Drawn){ order[k], ni, e->corners == 4 ? 6u : 3u };
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
    range_drawn[n_ranges] = n_drawn;
    mesh = gl_dev_mesh(v, nv, idx, ni);
    n_quads = nq;
    static_idx = idx;
    frame_idx = malloc(sizeof *frame_idx * (ni ? ni : 1));
    frame_ranges = malloc(sizeof *frame_ranges * (n_ranges ? n_ranges : 1));
    allowed = calloc(1, nq ? nq : 1);
    free(order), free(q), free(v);
    return mesh != 0 && frame_idx && frame_ranges && allowed;
}

void env_draw(void)
{
    /* the quads' flags changed (env_show): the mesh again, once a frame at most */
    if (loaded_env && loaded_serial != akev_flags_serial()) env_draw_load(loaded_env);
    if (replaced) gl_dev_mesh_indices(mesh, static_idx, ranges && n_ranges ? ranges[n_ranges - 1].first + ranges[n_ranges - 1].count : 0), replaced = false;
    for (uint32_t r = 0; r < n_ranges; r++) gl_dev_draw_mesh(mesh, ranges[r].first, ranges[r].count, gl_dev_texture_now(ranges[r].txmp), ranges[r].mode);
}

/* AKiEnvironment_ComputeVis 0x444030: the quads the visibility rays have marked, the first 0x2000 of them
   by their index (the rest left out: "Exceeded max visible GQs"), in the ranges' order (by texture, the
   solid ones first). */
void env_draw_visible(void)
{
    if (loaded_env && loaded_serial != akev_flags_serial()) env_draw_load(loaded_env);
    vis_env_drawn();
    if (!mesh || !allowed) return;
    memset(allowed, 0, n_quads);
    for (uint32_t k = 0; k < n_drawn; k++) allowed[drawn[k].quad] = 1;
    for (uint32_t i = 0, kept = 0; i < n_quads; i++) {
        if (!allowed[i]) continue;
        if (vis_quad(i) && kept < 0x2000) kept++;
        else allowed[i] = 0;
    }
    uint32_t ni = 0;
    for (uint32_t r = 0; r < n_ranges; r++) {
        frame_ranges[r] = ranges[r];
        frame_ranges[r].first = ni;
        for (uint32_t k = range_drawn[r]; k < range_drawn[r + 1]; k++) {
            if (!allowed[drawn[k].quad]) continue;
            memcpy(frame_idx + ni, static_idx + drawn[k].first, drawn[k].count * sizeof *frame_idx);
            ni += drawn[k].count;
        }
        frame_ranges[r].count = ni - frame_ranges[r].first;
    }
    gl_dev_mesh_indices(mesh, frame_idx, ni);
    replaced = true;
    for (uint32_t r = 0; r < n_ranges; r++)
        if (frame_ranges[r].count) gl_dev_draw_mesh(mesh, frame_ranges[r].first, frame_ranges[r].count, gl_dev_texture_now(frame_ranges[r].txmp),
                             frame_ranges[r].mode);
}
