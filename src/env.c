/* Environment commands: see env.h. */
#include "env.h"

#include "akev.h"
#include "particle.h"
#include "sl.h"
#include "trac.h"

#include <math.h>
#include <string.h>

#include <stdint.h>

static void *level_env;
static int nbroken;   /* the glass broken since env_set */

void env_set(void *akev) { level_env = akev; nbroken = 0; }

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

/* env_show gq_ref:int on_off:int (0x4bcee0): every quad of the id is hidden (0x40000, which the draw test
   leaves out) or shown again. The original's walk goes one entry past the end of the id list; the port
   stops at the end. */
static bool cmd_show(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (!level_env || argc < 2) return true;
    uint32_t want = (uint32_t)num(&argv[0]), quad, id;
    bool on = num(&argv[1]) != 0;
    for (uint32_t i = 0; i < akev_script_count(level_env); i++)
        if (akev_script_entry(level_env, i, &quad, &id) && id == want)
            akev_quad_set_flags(level_env, quad, on ? 0 : 0x40000, on ? 0x40000 : 0);
    return true;
}

static float fnum(const SlValue *v) { return v->type == SL_FLOAT ? v->f : (float)v->i; }

/* A shade's channel: clamped to 0..1, times 255, rounded (0x4340a0). */
static uint32_t channel(const SlValue *v)
{
    float f = fnum(v);
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    return (uint32_t)lrintf(f * 255.0f) & 0xff;
}

uint32_t env_script_shade(const SlValue *rgb) { return 0xff000000u | channel(&rgb[0]) << 16 | channel(&rgb[1]) << 8 | channel(&rgb[2]); }

/* env_shade gq_ref:int gq_ref:int r g b (AIiScript_EnvShade 0x4bd070): the quads of the ids in the range
   get the shade (opaque) at their four corners. The original's walk reads one entry past the end of the
   id list; the port stops at the end. */
static bool cmd_shade(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (!level_env || argc < 5) return true;
    int32_t a = num(&argv[0]), b = num(&argv[1]);
    if (a > b) { int32_t x = a; a = b; b = x; }
    uint32_t shade = env_script_shade(&argv[2]), quad, id;
    for (uint32_t i = 0; i < akev_script_count(level_env); i++)
        if (akev_script_entry(level_env, i, &quad, &id) && id >= (uint32_t)a && id <= (uint32_t)b)
            akev_quad_set_shade(level_env, quad, shade);
    return true;
}

/* env_broken gq_ref:int [gq_endref:int] (AIiScript_EnvBroken 0x4bcf60): of the ids in the range (at most
   0x400), how many have no glass (quad flag 0x200000) left unbroken (0x40000): each id with unbroken
   glass counted once, taken from the range's size. The original's walk reads one entry past the end of
   the id list; the port stops at the end. */
static bool cmd_broken(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)u;
    r->type = SL_INT;
    r->i = 0;
    if (!level_env || argc < 1) return true;
    uint32_t from = (uint32_t)num(&argv[0]), to = argc > 1 ? (uint32_t)num(&argv[1]) : from, quad, id;
    if (to >= from + 0x400) return true;    /* "env_broken: trying to read over %d" */
    static uint8_t seen[0x400];
    memset(seen, 0, sizeof seen);
    uint32_t unbroken = 0;
    for (uint32_t i = 0; i < akev_script_count(level_env); i++) {
        AkevQuad q;
        if (!akev_script_entry(level_env, i, &quad, &id) || id < from || id > to || !akev_quad(level_env, quad, &q)) continue;
        if (!(q.flags & 0x200000) || (q.flags & 0x40000) || seen[id - from]) continue;
        seen[id - from] = 1;
        unbroken++;
    }
    r->i = (int32_t)(to - unbroken - from + 1);
    return true;
}

/* The glass's switches (ONrParticle3_Initialize's globals): p3_glass_breakable (0x5564b4: glass breaks
   at all), p3_everything_breakable (0x5e96b1: any quad), p3_furniture_breakable (0x5e96b2: the quads
   of flag 0x1000000 too). */
static bool shards_on = true, break_all, break_marked;
int env_glass_broken(void) { return nbroken; }

/* 0x462bf0 then 0x508f10: a particle's damage at the quad: only breakable glass (flag 0x200000), once
   (it is marked broken, 0x40000, which the draw and the collisions leave out); its normal faces the side
   hit. Then the shards (glass_shard, "ONrParticle3_LevelZeroLoaded"): a grid of cells of 15 units (the
   detail's size, 0x52b7c0) over the quad, 5 shards a cell (0x52b7cc) on the first row down to the far
   edge's share on the last, each at a random place in its cell, moving at random up to 10 and, for a
   hit at a point, pushed out from it (its force, at least 25, and its radius). */
static void glass_hit(uint32_t quad, const float point[3], const float dir[3], float force, float radius)
{
    AkevQuad q;
    if (!level_env || !akev_quad(level_env, quad, &q)) return;
    if (!break_all && !(q.flags & 0x200000) && !(break_marked && (q.flags & 0x1000000))) return;
    float pl[4], n[3];
    if (!akev_quad_plane(level_env, quad, pl)) return;
    n[0] = pl[0], n[1] = pl[1], n[2] = pl[2];
    float side = point ? n[0] * point[0] + n[1] * point[1] + n[2] * point[2] + pl[3] : 0.0f;
    if (side < -4.0f) { n[0] = -n[0], n[1] = -n[1], n[2] = -n[2]; dir = NULL; }
    else if (side > 4.0f) dir = NULL;
    else if (dir && n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2] > 0.0f) n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
    P3Class *shard = p3_class("glass_shard");      /* 0x5ed310 */
    if (!shards_on || !shard || (q.flags & 0x40000)) return;
    akev_quad_set_flags(level_env, quad, 0x40000, 0);
    nbroken++;
    if (force <= 25.0f) force = 25.0f;
    float away[3] = { -n[0], -n[1], -n[2] }, c[4][3];
    const float *push = dir ? dir : away;
    for (unsigned k = 0; k < 4; k++)
        if (!akev_quad_corner(level_env, quad, k < q.corners ? k : q.corners - 1, c[k])) return;
    float e01[3], e32[3], e03[3];
    for (int i = 0; i < 3; i++) e01[i] = c[1][i] - c[0][i], e32[i] = c[2][i] - c[3][i], e03[i] = c[3][i] - c[0][i];
    float l01 = sqrtf(e01[0] * e01[0] + e01[1] * e01[1] + e01[2] * e01[2]);
    float l32 = sqrtf(e32[0] * e32[0] + e32[1] * e32[1] + e32[2] * e32[2]);
    float l03 = sqrtf(e03[0] * e03[0] + e03[1] * e03[1] + e03[2] * e03[2]);
    if (l01 < 1e-6f) return;
    uint32_t cols = (uint32_t)lrintf(l01 / 15.0f) + 1, rows = (uint32_t)lrintf(l03 / 15.0f) + 1;
    if (cols > 50) cols = 50;
    if (rows > 50) rows = 50;
    uint32_t tick = p3_tick();
    for (uint32_t row = 0; row < rows; row++) {
        float per = 5.0f;
        if (rows != 1) {
            float s = (float)row / (float)(rows - 1);
            per = ((1.0f - s) + (l32 / l01) * s) * 5.0f;
        }
        for (uint32_t col = 0; col < cols; col++) {
            uint32_t m = (uint32_t)lrintf(per);
            if ((float)trac_local_random() * (1.0f / 65535.0f) < per - (float)m) m++;
            for (uint32_t s = 0; s < m; s++) {
                float uu = (float)trac_local_random() * (1.0f / 65535.0f) + (float)col;
                float rr = (float)trac_local_random() * (1.0f / 65535.0f) + (float)row;
                float w0 = (1.0f - rr / (float)rows) * uu, w1 = (rr / (float)rows) * uu, pos[3], vel[3];
                for (int i = 0; i < 3; i++)
                    pos[i] = w1 / (float)cols * e32[i] + w0 / (float)cols * e01[i] + rr / (float)rows * e03[i] + c[0][i];
                float sp = (float)trac_local_random() * (1.0f / 65535.0f) * 10.0f, l;
                do {    /* a random direction (0x455990) */
                    for (int i = 0; i < 3; i++) vel[i] = (float)trac_local_random() * (1.0f / 32767.5f) - 1.0f;
                    l = vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2];
                } while (l > 1.0f || l < 1e-6f);
                l = sp / sqrtf(l);
                for (int i = 0; i < 3; i++) vel[i] *= l;
                if (point) {    /* pushed out from the point hit (none for a hit without one) */
                    float d[3] = { pos[0] - point[0], pos[1] - point[1], pos[2] - point[2] }, e[3];
                    for (int i = 0; i < 3; i++) e[i] = 2.0f * radius * push[i] + d[i];
                    float le = 1.0f / sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
                    float f = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / (radius * 1.5f);
                    if (f > 1.0f) f = 1.0f;
                    float g = 1.0f - f, v = f * 12.0f + force * 0.04f * g * 90.0f;
                    for (int i = 0; i < 3; i++) vel[i] += (-f * n[i] + le * e[i] * g) * v;
                }
                p3_effect(shard, pos, vel, NULL, tick);
            }
        }
    }
}

void env_register_commands(void)
{
    p3_glass_hook = glass_hit;
    sl_register("env_show", cmd_show, NULL);
    sl_register("env_shade", cmd_shade, NULL);
    sl_register("env_broken", cmd_broken, NULL);
    sl_register_bool("p3_glass_breakable", &shards_on);
    sl_register_bool("p3_everything_breakable", &break_all);
    sl_register_bool("p3_furniture_breakable", &break_marked);
}
