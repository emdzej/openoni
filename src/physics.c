/* The characters' physics: see physics.h. */
#include "physics.h"

#include "akev.h"
#include "akev_collide.h"
#include "body.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "m3gm.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16u(const void *p, unsigned off) { const uint8_t *b = (const uint8_t *)p + off; return (uint16_t)(b[0] | b[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static float oncc_float(const Character *c, unsigned off) { return rdf((const uint8_t *)c->oncc + off); }

static const float EPS = 0.0009765625f;     /* 0x529cc8 */

enum { SPH_ROOT = 0, SPH_LEAF = 1, SPH_MID = 5 };   /* A+0x08; A+0x20 + 0x18 i; A+0x80, +0x98 */

/* ---- the body ---- */

void physics_body(Character *c)
{
    /* 0x4e5ff0: the parts' spheres through their matrices; the pelvis's box, grown by parts 1 to 10 */
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    float lo = c->pos[1], hi = c->pos[1] + 12.0f;
    if (bones && bones <= BODY_MAX_BONES && bones == CHR_BONES) {
        BodyMatrix root, m[BODY_MAX_BONES];
    chr_root_matrix(c, root);
        if (body_matrices(body, root, c->quats, m)) {
            for (uint32_t b = 0; b <= 10 && b < bones; b++) {
                float box[6], sph[4] = { 0, 0, 0, 0 }, w[3];
                m3gm_bounds(body_geometry(body, b), box, sph);
                body_transform(m[b], sph, w);
                if (!b || w[1] - sph[3] < lo) lo = b ? (w[1] - sph[3] < lo ? w[1] - sph[3] : lo) : w[1] - sph[3];
                if (!b || w[1] + sph[3] > hi) hi = b ? (w[1] + sph[3] > hi ? w[1] + sph[3] : hi) : w[1] + sph[3];
            }
        }
    }
    c->bottom = c->pos[1] < lo ? lo : c->pos[1];
    c->top = hi < lo + 16.0f ? lo + 16.0f : hi;
    /* 0x4dc160: the sphere tree, from the feet (or the bottom in the air) to the top */
    float r = c->ai ? 3.0f : 4.0f;      /* 0x529808 for an AI, 0x5296c8 for the player */
    float base = c->air_ticks ? c->bottom : c->pos[1];
    float h = c->top - base;
    if (h < 0.0f) h = 0.0f;
    else if (h > 72.0f) h = 72.0f;      /* 0x52b2cc */
    float half = h * 0.5f;
    float *s = c->spheres[SPH_ROOT];
    s[0] = c->pos[0], s[1] = half + base, s[2] = c->pos[2], s[3] = half;
    float step = (h - (r + r)) * 0.33333334f;   /* 0x52a5ac */
    for (int i = 0; i < 4; i++) {
        s = c->spheres[SPH_LEAF + i];
        s[0] = c->pos[0], s[1] = (float)i * step + r + base, s[2] = c->pos[2], s[3] = r;
    }
    for (int i = 0; i < 2; i++) {
        const float *a = c->spheres[SPH_LEAF + 2 * i], *b = c->spheres[SPH_LEAF + 2 * i + 1];
        float d = (b[1] - a[1]) * 0.5f;
        s = c->spheres[SPH_MID + i];
        s[0] = c->pos[0], s[1] = d + a[1], s[2] = c->pos[2], s[3] = d + a[3];
    }
}

void physics_start(Character *c)
{
    memcpy(c->phys_pos, c->pos, sizeof c->phys_pos);
    memset(c->phys_accel, 0, sizeof c->phys_accel);
    memset(c->move, 0, sizeof c->move);
    c->drop = 0.0f;
    c->ground_quad = -1;
    c->ground_height = c->pos[1];
    c->has_physics = true;
    physics_body(c);
}

void physics_place(Character *c, const float pos[3])
{
    memcpy(c->pos, pos, sizeof c->pos);
    memcpy(c->prev_pos, pos, sizeof c->prev_pos);
    memcpy(c->phys_pos, pos, sizeof c->phys_pos);
    c->ground_height = pos[1];
}

void physics_take(Character *c, const float w[3])
{
    /* DoCharacterFrame 0x4ddba0: an environment animation at its last frame ends (0x4ed080: the
       context's flags as before); while it plays the context is at its matrix's translation, down by the
       pelvis's height (C+0x18c). Its rotation (the quarter turn) is the body's, not the place's. */
    if (c->envanim.oban && (int)rd16u(c->envanim.oban, OBAN_FRAMES) - 1 <= c->envanim.frame) chr_envanim_start(c, NULL, false);
    if (c->envanim.oban && c->has_physics) {
        float m[12];
        if (oban_matrix(c->envanim.oban, c->envanim.frame, (c->envanim.state & 2) != 0, m)) {
            memcpy(c->phys_pos, m + 9, sizeof c->phys_pos);
            c->phys_pos[1] -= c->pelvis_height;
        }
    }
    memcpy(c->prev_pos, c->pos, sizeof c->pos);
    if (c->has_physics) memcpy(c->pos, c->phys_pos, sizeof c->pos);
    c->move[0] = w[0];
    c->move[2] = w[2];
    if (w[1] >= 0.0f) c->move[1] = w[1], c->drop = 0.0f;
    else c->move[1] = 0.0f, c->drop = w[1];
}

/* ---- the walls ---- */

typedef struct { uint32_t quad; float plane[4]; float point[3]; float d2; } Hit;
enum { HITS_MAX = 64 };

/* 0x43bfa0: a tree sphere moving by mv against the quad; down to the leaves. */
static bool tree_node(const float s[7][4], const void *env, uint32_t quad, int node, const float mv[3])
{
    if (!akev_quad_sphere_vector(env, quad, s[node], mv, NULL)) return false;
    if (node == SPH_ROOT) return tree_node(s, env, quad, SPH_MID + 1, mv) || tree_node(s, env, quad, SPH_MID, mv);
    if (node == SPH_MID) return tree_node(s, env, quad, SPH_LEAF + 1, mv) || tree_node(s, env, quad, SPH_LEAF, mv);
    if (node == SPH_MID + 1) return tree_node(s, env, quad, SPH_LEAF + 3, mv) || tree_node(s, env, quad, SPH_LEAF + 2, mv);
    return true;
}

bool physics_tree_hits(const float spheres[7][4], const void *env, uint32_t quad, const float mv[3])
{
    return tree_node(spheres, env, quad, SPH_ROOT, mv);
}

static bool tree_hits(const Character *c, const void *env, uint32_t quad, int node, const float mv[3])
{
    return tree_node(c->spheres, env, quad, node, mv);
}

static uint32_t quad_flags(const void *env, uint32_t q)
{
    AkevQuad aq;
    return akev_quad(env, q, &aq) ? aq.flags : 0;
}

/* 0x4ebd70: the character's collisions with the environment (the root sphere through the octree, each
   confirmed by the tree); walls (normals no more than 0.5 up) counted. */
static unsigned find_env(Character *c, const void *env, const float mv[3], Hit *out)
{
    static AkevHit hits[AKEV_HITS_MAX];
    unsigned n = akev_collide_sphere(env, c->spheres[SPH_ROOT], mv, 0x48800, 0xff, hits), k = 0;
    for (unsigned i = 0; i < n && k + 2 <= HITS_MAX; i++) {
        if (!tree_hits(c, env, hits[i].quad, SPH_ROOT, mv)) continue;
        Hit *h = &out[k++];
        h->quad = hits[i].quad;
        memcpy(h->plane, hits[i].plane, sizeof h->plane);
        memcpy(h->point, hits[i].point, sizeof h->point);
        if (h->plane[1] <= 0.5f) c->wall_hits++;     /* AI2rMovementState_NotifyWallCollision: not ported */
        if (quad_flags(env, h->quad) & 0x2a0) {
            Hit *g = &out[k++];
            *g = *h;
            for (int j = 0; j < 4; j++) g->plane[j] *= -1.0f;   /* 0x529804 */
        }
    }
    return k;
}

static int by_d2(const void *a, const void *b)
{
    const Hit *x = a, *y = b;
    return x->d2 < y->d2 ? -1 : x->d2 > y->d2;
}

/* 0x433da0: the part of v into the plane (when v·n < 0). */
static void into(const float v[3], const float n[3], float out[3])
{
    float d = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    if (d < 0.0f) out[0] = d * n[0], out[1] = d * n[1], out[2] = d * n[2];
    else out[0] = out[1] = out[2] = 0.0f;
}

static bool big(const float d[3]) { return EPS <= fabsf(d[0]) || EPS <= fabsf(d[1]) || EPS <= fabsf(d[2]); }

/* 0x4ecb20, the character's "on a hit": no move down after a hit. */
static void on_hit(float mv[3])
{
    if (mv[1] < 0.0f) mv[1] = 0.0f;
}

/* 0x43a710: the collisions, nearest first, slid along until the first that takes something off. */
static bool slide(Character *c, const void *env, float mv[3])
{
    Hit h[HITS_MAX];
    unsigned n = find_env(c, env, mv, h);
    for (unsigned i = 0; i < n; i++) {
        float x = c->phys_pos[0] - h[i].point[0], y = c->phys_pos[1] - h[i].point[1], z = c->phys_pos[2] - h[i].point[2];
        h[i].d2 = z * z + x * x + y * y;
    }
    qsort(h, n, sizeof *h, by_d2);
    for (unsigned i = 0; i < n; i++) {
        float d[3];
        into(mv, h[i].plane, d);
        for (int k = 0; k < 3; k++) mv[k] -= d[k];
        if (big(d)) {
            on_hit(mv);
            return true;
        }
    }
    return false;
}

/* 0x43aac0: still colliding after the slide: the move stops. */
static void respond(Character *c, const void *env, float mv[3])
{
    Hit h[HITS_MAX];
    unsigned n = find_env(c, env, mv, h);
    bool hit = false;
    for (unsigned i = 0; i < n; i++) {
        float d[3];
        into(mv, h[i].plane, d);
        if (big(d)) {
            hit = true;
            mv[0] = mv[1] = mv[2] = 0.0f;
        }
    }
    if (hit) on_hit(mv);
}

/* ---- the floor ---- */

/* 0x4ef020 and its kin: one edge (a to b) of the polygon clipped by "coordinate k below (or above) l". */
static int clip_edge(const float *a, const float *b, int k, float l, bool below, float *out)
{
    const float *lo, *hi;
    bool b_inside_end;
    if (a[k] < b[k]) lo = a, hi = b, b_inside_end = !below;
    else lo = b, hi = a, b_inside_end = below;
    if (below) {
        if (l <= lo[k]) return 0;
        if (hi[k] <= l) { memcpy(out, b, 12); return 1; }
    } else {
        if (hi[k] <= l) return 0;
        if (l <= lo[k]) { memcpy(out, b, 12); return 1; }
    }
    float t = (l - lo[k]) / (hi[k] - lo[k]);
    for (int j = 0; j < 3; j++) out[j] = j == k ? l : (hi[j] - lo[j]) * t + lo[j];
    if (b_inside_end) { memcpy(out + 3, b, 12); return 2; }
    return 1;
}

static int clip(const float *in, int n, int k, float l, bool below, float *out)
{
    int m = 0;
    for (int i = 0; i + 1 < n; i++) m += clip_edge(in + 3 * i, in + 3 * (i + 1), k, l, below, out + 3 * m);
    m += clip_edge(in + 3 * (n - 1), in, k, l, below, out + 3 * m);
    return m;
}

/* FindGroundCollision 0x4ec040: down from above the feet, the highest floor under the step's height
   that the feet's square (1.5 each way) stands on. Returns whether one was found. */
static bool find_ground(Character *c, const void *env)
{
    bool landing_type = c->type == 0x18 || (c->type > 0x27 && c->type < 0x2c);
    bool from_air = c->air_ticks || landing_type;
    float feet, fh;
    if (from_air) {
        feet = c->bottom <= c->pos[1] ? c->pos[1] : c->bottom;
        fh = c->bottom - c->pos[1];
        c->ground_quad = -1;
    } else {
        feet = c->pos[1];
        fh = 0.0f;
    }
    float limit = (c->pelvis_height - fh) * 0.5f + c->pos[1] + fh;
    if (limit <= c->pos[1] + 6.0f) limit = c->pos[1] + 6.0f;
    float top = fh + c->pos[1] + 10.0f;     /* 0x52a190 */
    const float sph[4] = { c->pos[0], top, c->pos[2], 7.1f };
    const float mv[3] = { 0.0f, c->drop - (fh + 10.0f), 0.0f };
    static AkevHit hits[AKEV_HITS_MAX];
    unsigned n = akev_collide_sphere(env, sph, mv, 0x148800, 0xff, hits);
    c->phys_pos[1] += c->drop;
    feet += c->drop;
    memcpy(c->pos, c->phys_pos, sizeof c->pos);
    if (limit < c->ground_height + 6.0f) limit = c->ground_height + 6.0f;
    c->ground_height = feet;
    uint32_t np;
    const uint8_t *pts = akev_points(env, &np);
    bool found = false;
    int32_t best_quad = -1;
    float best = 0.0f;
    for (unsigned i = 0; i < n; i++) {
        float ny = hits[i].plane[1];
        AkevQuad q;
        if (!akev_quad(env, hits[i].quad, &q)) continue;
        /* a two-sided quad seen from behind by the sphere's start (10 above the feet), not the feet */
        if ((q.flags & 0x2a0) && !(q.flags & 0x20) &&
            hits[i].plane[0] * sph[0] + hits[i].plane[1] * sph[1] + hits[i].plane[2] * sph[2] + hits[i].plane[3] < 0.0f)
            ny = -ny;
        if (!(-0.5f <= ny && 0.5f < ny)) continue;
        float a[8 * 3], b[8 * 3];
        int m = (int)q.corners;
        for (int k = 0; k < m; k++) {
            uint32_t pi = q.point[k];
            const uint8_t *p = pi < np ? pts + 12u * pi : NULL;
            for (int j = 0; j < 3; j++) a[3 * k + j] = p ? rdf(p + 4 * j) : 0.0f;
        }
        float x0 = c->pos[0] - 1.5f, x1 = c->pos[0] + 1.5f, z0 = c->pos[2] - 1.5f, z1 = c->pos[2] + 1.5f;  /* 0x52a774 */
        m = clip(a, m, 0, x1, true, b);
        if (!m) continue;
        m = clip(b, m, 2, z0, false, a);
        if (!m) continue;
        m = clip(a, m, 0, x0, false, b);
        if (!m) continue;
        m = clip(b, m, 2, z1, true, a);
        if (m <= 2) continue;
        bool touch = false;
        float high = -3.4028235e+38f;
        for (int k = 0; k < m; k++) {
            float y0 = a[3 * k + 1], y1 = a[3 * ((k + 1) % m) + 1];
            float l = y0 < y1 ? y0 : y1, u = y0 < y1 ? y1 : y0;
            if (l < limit) {
                touch = true;
                if (high < u) high = u;
            }
        }
        if (limit < high) high = limit;
        if (!touch) continue;
        if (!found || best < high) best_quad = (int32_t)hits[i].quad, best = high;
        found = true;
    }
    if (!found || best < feet) return false;
    c->ground_height = best;
    c->phys_pos[1] += best - feet;
    memcpy(c->pos, c->phys_pos, sizeof c->pos);
    c->ground_quad = best_quad;
    c->ground_point[0] = c->phys_pos[0], c->ground_point[1] = best, c->ground_point[2] = c->phys_pos[2];
    if (!from_air) return true;
    if (landing_type) {
        if (c->air_ticks) c->air_ticks = 0;     /* "was landing, hit ground" */
        return true;
    }
    /* the landing; its damage by the fall's height between ONCC +0x18 and +0x1c, all of it beyond */
    float fall = c->fall_height - best, lo = oncc_float(c, 0x18), hi = oncc_float(c, 0x1c);
    uint32_t dmg = 0;
    if (fall > hi) {
        float x = (float)c->hit_points;
        if (x > 0.0f) dmg = (uint32_t)lrintf(x);
    } else if (fall >= lo) {
        float x = (fall - lo) / (hi - lo) * (float)c->max_hit_points;
        if (x > 0.0f) dmg = (uint32_t)lrintf(x);
    }
    if (dmg) chr_take_damage(c, dmg, 0.0f, NULL, 0, NULL);
    memset(c->air_velocity, 0, sizeof c->air_velocity);
    c->air_ticks = 0;
    c->landing = true;
    if (dmg) c->hard_landing = true;
    return true;
}

/* 0x4eca00, after the move: the floor while going down; on the ground with none, the fall counts up. */
static void after_move(Character *c, const void *env)
{
    if (c->drop == 0.0f) return;
    bool found = find_ground(c, env);
    if (!c->air_ticks && !found) {
        if (!c->no_ground_ticks) c->fall_height = c->phys_pos[1];
        c->no_ground_ticks++;
    } else {
        c->no_ground_ticks = 0;
    }
    if (c->no_ground_ticks > 11) c->start_falling = true;
}

void (*physics_debug_log)(const char *msg);

void physics_update(Character *c, const void *env)
{
    if (!c->has_physics || !env) return;
    if (c->envanim.oban) {
        /* animated (flags 0x44): PHrPhysics_Update_Animation 0x43a620 plays it; no move, walls or floor */
        oban_update(&c->envanim, ui_time());
        memset(c->phys_accel, 0, sizeof c->phys_accel);
        return;
    }
    /* UpdateCharacters' step 10: the move into the acceleration */
    for (int k = 0; k < 3; k++) c->phys_accel[k] += c->move[k];
    /* OBiPhysics_Update: no gravity (the character's own), the acceleration into the move */
    float mv[3] = { c->phys_accel[0], c->phys_accel[1], c->phys_accel[2] };
    memset(c->phys_accel, 0, sizeof c->phys_accel);
    /* chr_nocollision (C+4 0x20000) sets the context's flags 0x44 (0x4ed080): PHrPhysics_Update 0x43af50
       skips the walls (0x43a710) for flag 4, and 0x4eca00 the floor for flag 0x40 */
    bool through = (c->flags & 0x20000u) != 0;
    float mv0[3] = { mv[0], mv[1], mv[2] };
    if (!through && slide(c, env, mv)) respond(c, env, mv);
    if (physics_debug_log && !c->ai && (fabsf(mv0[0]) + fabsf(mv0[2]) > 0.05f) && fabsf(mv[0]) + fabsf(mv[2]) < 0.01f) {
        Hit h[HITS_MAX];
        uint16_t wh = c->wall_hits;
        unsigned n = find_env(c, env, mv0, h);
        c->wall_hits = wh;
        char b[1600];
        int o = snprintf(b, sizeof b, "physlog pos %.2f %.2f %.2f mv %.2f %.2f %.2f sph %.2f %.2f %.2f r %.2f hits %u:", c->phys_pos[0], c->phys_pos[1], c->phys_pos[2],
                         mv0[0], mv0[1], mv0[2], c->spheres[SPH_ROOT][0], c->spheres[SPH_ROOT][1], c->spheres[SPH_ROOT][2], c->spheres[SPH_ROOT][3], n);
        for (unsigned i = 0; i < n && o < 1450; i++)
            o += snprintf(b + o, sizeof b - o, " q%u f%x n(%.2f %.2f %.2f %.1f) vn %.2f", h[i].quad, quad_flags(env, h[i].quad), h[i].plane[0], h[i].plane[1], h[i].plane[2], h[i].plane[3],
                          mv0[0] * h[i].plane[0] + mv0[1] * h[i].plane[1] + mv0[2] * h[i].plane[2]);
        physics_debug_log(b);
    }
    for (int k = 0; k < 3; k++) c->phys_pos[k] += mv[k];
    if (!through) after_move(c, env);
}
