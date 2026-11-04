/* Predicting an attack: see chr_intersect.h. */
#include "chr_intersect.h"

#include "body.h"
#include "chr_anim.h"
#include "exe.h"
#include "m3gm.h"
#include "tram.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static const float TWO_PI = 6.2831854820251465f;   /* 0x529548 */

/* The animation's fields for this (from the data, not worked out at run time). */
enum {
    TRAM_FRAME_BOUNDS = 0x28,   /* .raw: 8 bytes a frame; +4 the height (u16), +6 the bottom (s16), in 0.01 */
    TRAM_DIRECTION = 0x4c,      /* u16: the way the attack pushes (0, 1, 5 forward, 2 back, 3 left, 4 right) */
    TRAM_REACH = 0x50,          /* float */
    TRAM_REACH_BOTTOM = 0x54, TRAM_REACH_TOP = 0x58,
    TRAM_EXTENTS = 0x5c,        /* 36 floats: the reach every 10 degrees */
    TRAM_REACH_ANGLE = 0x128,   /* float: where the reach is longest */
    TRAM_DIRECTION2 = 0x12c,    /* u32: the direction when +0x4c is 0 */
    TRAM_POINT_COUNT = 0x130,
    TRAM_POINTS = 0x134,        /* .raw: 12 bytes each: the frame (u16), the angle (byte +3, of 256),
                                   the reach (u16 +4, in 0.01), the bottom and top (s16 +8, +0xa, in 0.01) */
};

/* ---- matrices (3x4, column-major, the translation last) ---- */

void chr_matrix3_apply(const float m[12], const float in[3], float out[3])
{
    float x = in[0], y = in[1], z = in[2];
    for (int i = 0; i < 3; i++) out[i] = m[i] * x + m[3 + i] * y + m[6 + i] * z;
}

/* MUrMatrix_Multiply 0x4341c0: a times b. */
static void matrix_multiply(const float a[12], const float b[12], float out[12])
{
    float r[12];
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 3; i++)
            r[3 * c + i] = a[i] * b[3 * c] + a[3 + i] * b[3 * c + 1] + a[6 + i] * b[3 * c + 2] + (c == 3 ? a[9 + i] : 0.0f);
    memcpy(out, r, sizeof r);
}

/* 0x434d70: m turned about y by a, from the left. */
static void matrix_rotate_y(float m[12], float a)
{
    float c = cosf(a), s = sinf(a);
    const float r[12] = { c, 0, -s, 0, 1, 0, s, 0, c, 0, 0, 0 };
    matrix_multiply(r, m, m);
}

/* ---- the context ---- */

/* 0x4eee20: a character's half. The original's characters without an active record (none here) keep
   no animation. */
static void get_state(const Character *c, ChrIntersectChar *h)
{
    memset(h, 0, sizeof *h);
    h->c = c;
    h->anim = c->anim;
    h->frame = (uint16_t)c->frame;
    memcpy(h->now.pos, c->pos, sizeof h->now.pos);
    h->now.facing = c->facing;
    h->radius = c->ai ? 3.0f : 4.0f;           /* 0x529808, 0x5296c8: an AI's or the player's */
    if (c->air_ticks) {
        h->now.air = true;
        h->now.jumped = (c->jumped & 1) != 0;
        h->now.jump_held = (c->held & CHR_KEY_JUMP) != 0;
        h->now.air_ticks = c->air_ticks;
        memcpy(h->now.air_velocity, c->air_velocity, sizeof h->now.air_velocity);
    }
    const uint8_t *o = c->oncc;
    h->gravity = rdf(o);
    h->fall_gravity = rdf(o + 4);
    h->oncc_08 = rdf(o + 8);
    h->oncc_0c = rdf(o + 0xc);
    h->lift = rdf(o + 0x10);
    h->fall_after = rd16(o + 0x14);
    h->lift_ticks = rd16(o + 0x16);
    h->saved = h->now;
}

void chr_intersect_setup(const Character *a, const Character *t, bool melee, ChrIntersect *x)
{
    memset(x, 0, sizeof *x);
    x->melee = melee;
    get_state(a, &x->a);
    get_state(t, &x->t);
    /* from the target's (x, forward, height) to the attacker's: swap y and z (0x52b298), turn by the
       target's facing, place it, turn back by the attacker's, swap again */
    const float swap[12] = { 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0 };
    float m[12];
    memcpy(m, swap, sizeof m);
    matrix_rotate_y(m, x->t.now.facing);
    for (int i = 0; i < 3; i++) m[9 + i] = x->t.now.pos[i] - x->a.now.pos[i];
    matrix_rotate_y(m, TWO_PI - x->a.now.facing);
    matrix_multiply(swap, m, m);
    memcpy(x->m0, m, sizeof m);
    memcpy(x->m, m, sizeof m);

    /* the target's motion: the animation's estimate (stand-in: the original takes the move made when the
       character collided this tick, A+0xb0..0xb4, which aren't counted yet), and the climb */
    float v[3];
    chr_velocity_estimate(t, v);
    x->t_velocity[0] = v[0];
    x->t_velocity[1] = v[2];
    x->t_velocity[2] = t->pos[1] - t->prev_pos[1];

    /* the target's parts (0x54e1e0: the part, "in the melee's set", "not for the radius"); the melee's
       set ends at the first part out of it */
    bool lying = t->state == 0x12 || t->state == 0x32;
    float bottom = 1e9f, top = -1e9f, radius = 0.0f;
    const void *body = body_of_class(t->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    BodyMatrix pm[BODY_MAX_BONES];
    bool have = false;
    if (bones && bones <= BODY_MAX_BONES && bones == CHR_BONES) {
        BodyMatrix root;
        chr_root_matrix(t, root);
        have = body_matrices(body, root, t->quats, pm);
    }
    for (uint32_t va = 0x54e1e0;; va += 8) {
        const uint8_t *e = have ? exe_data(va, 8) : NULL;
        if (!e || rd32(e) == 0xffffffffu) break;
        if (melee && !e[4]) break;
        uint32_t part = rd32(e);
        if (part >= bones) break;
        bool skip = !lying && melee && e[5];
        float box[6], sph[4] = { 0, 0, 0, 0 };
        m3gm_bounds(body_geometry(body, part), box, sph);
        float dx = pm[part][9] + sph[0] - t->pos[0];
        float dy = pm[part][10] + sph[1] - t->pos[1];
        float dz = pm[part][11] + sph[2] - t->pos[2];
        if (!skip) {
            float h = sqrtf(dx * dx + dz * dz) + sph[3];
            if (h > radius) radius = h;
        }
        if (dy + sph[3] >= top) top = dy + sph[3];
        if (dy - sph[3] < bottom) bottom = dy - sph[3];
    }
    if (!have) {        /* no body: the original's inactive character */
        x->t_top = 15.0f;
        x->t_radius = t->ai ? 3.0f : 4.0f;
        return;
    }
    if (melee) radius *= 0.75f;     /* the double at 0x52b330 */
    x->t_radius = radius;
    x->t_bottom = bottom;
    x->t_top = top;
}

void chr_intersect_restore(ChrIntersect *x)
{
    if (x->m_modified) memcpy(x->m, x->m0, sizeof x->m);
    if (x->a.modified) x->a.now = x->a.saved;
    if (x->t.modified) x->t.now = x->t.saved;
}

/* ---- the tests ---- */

/* The direction tables of TRrInitialize 0x427290: (sin, cos) every 10 degrees (0x571a20), and at the
   middle of each 256th of the circle (0x571220). */
static void dir36(unsigned k, float *s, float *c)
{
    double a = (double)k * (double)0.17453293f;     /* 0x5297f4 */
    *s = (float)sin(a);
    *c = (float)cos(a);
}

static void dir256(unsigned k, float *s, float *c)
{
    double a = (double)(0x80u + 0x100u * k) * (double)9.5875264e-05f;    /* 0x5297f0, 2 pi / 65535 */
    *s = (float)sin(a);
    *c = (float)cos(a);
}

static uint32_t direction(const void *tram)
{
    const uint8_t *t = tram;
    uint32_t d = rd16(t + TRAM_DIRECTION);
    return d ? d : rd32(t + TRAM_DIRECTION2);
}

/* One end of the run against one direction's reach (ext): whether the target's circle (radius r) at
   along/perp to the line is touched. */
static bool touches(float along, float perp, float ext, float r)
{
    if (!(fabsf(perp) < r && along <= ext + r)) return false;
    if (along <= ext) return 0.0f < along || (-r < along && 0.0f < sqrtf(r * r - perp * perp) + along);
    return along - sqrtf(r * r - perp * perp) < ext;
}

bool chr_intersect_bounds(ChrIntersect *x)
{
    const uint8_t *t = x->a.anim;
    uint32_t n = rd32(t + TRAM_POINT_COUNT);
    if (!n) return false;
    const uint8_t *last = tr_raw_block(t, TRAM_POINTS, 12u * (n - 1), 12);
    uint16_t lf = last ? rd16(last) : 0;
    if (lf < x->a.frame) return false;
    uint16_t frames = (uint16_t)(lf - x->a.frame);
    if (x->a.now.air && frames <= x->a.now.air_frames) frames = x->a.now.air_frames;
    float px = x->m[9], py = x->m[10], ph = x->m[11];
    float v[3];
    chr_matrix3_apply(x->m, x->t_velocity, v);
    float r = x->t_radius, reach = rdf(t + TRAM_REACH);
    if (!x->a.now.air) {
        float ex = (float)frames * v[0] + px, ey = (float)frames * v[1] + py;
        float d0 = px * px + py * py, d1 = ex * ex + ey * ey, rr = (r + reach) * (r + reach);
        if (rr < d0 && rr < d1) {
            /* both ends beyond the reach: the nearest point between */
            float a0 = px * v[0] + py * v[1], a1 = ex * v[0] + ey * v[1];
            if (0.0f < a1 * a0 || fabsf(a1 - a0) < 0.001f) return false;
            float k = -a0 / (a1 - a0);
            float cx = (ex - px) * k + px, cy = (ey - py) * k + py;
            if (rr < cx * cx + cy * cy) return false;
        }
        if (ph + x->t_top < rdf(t + TRAM_REACH_BOTTOM)) return false;
        if (rdf(t + TRAM_REACH_TOP) < ph + x->t_bottom) return false;
        /* the directions, from the longest reach's round the circle */
        long ad = lrintf(rdf(t + TRAM_REACH_ANGLE) * 5.729578f);    /* 0x52980c; rounded by 0x4340a0 */
        unsigned start = (unsigned)ad % 36u, k = start;
        bool first = true;
        for (;;) {
            float ext, s, c;
            do {
                if (k == start && !first) return false;
                first = false;
                ext = rdf(t + TRAM_EXTENTS + 4 * k);
                if (x->melee) ext -= 3.0f;
                dir36(k, &s, &c);
                k = (k + 1) % 36u;
            } while (ext < 0.01f);
            float a0 = px * s + py * c, q0 = py * s - px * c;
            if (touches(a0, q0, ext, r)) return true;
            float a1 = s * ex + c * ey, q1 = s * ey - c * ex;
            if (touches(a1, q1, ext, r)) return true;
            /* the run crossing the line between its ends */
            if (0.0f <= q1 * q0) continue;
            if (a1 < -r && a0 < -r) continue;
            float far = ext + r;
            if (far < a1 && far < a0) continue;
            float cross = (a1 - a0) * (-q0 / (q1 - q0)) + a0;
            if (cross <= -r || far <= cross) continue;
            return true;
        }
    }
    /* in the air: where the two circles meet, the target's motion less the attacker's */
    float rr = r + reach;
    float vx = v[0] - x->a.now.air_velocity[0], vy = v[1] - x->a.now.air_velocity[2];
    float aa = vx * vx + vy * vy, b = px * vx + py * vy;
    b += b;
    float cc = (px * px + py * py) - rr * rr, disc = b * b - cc * aa * 4.0f;
    if (cc < 0.0f) return true;
    if (disc < 0.0f) return false;
    float k = aa <= 0.0f ? sqrtf(disc) - b : -b - sqrtf(disc);
    k = k / (aa + aa);
    if ((float)frames < k) return false;
    if (0.01f <= k) return true;
    return !(cc / (k * aa) < 0.0f);
}

/* One frame of a half's air (the jump held: the lift while it lasts; then the fall's gravity). lift_on
   is the attacker's jump flag for both halves: the original reads the attacker's for the target too. */
static void air_step(ChrIntersectChar *h, bool lift_on)
{
    h->s_air_ticks++;
    if (lift_on && h->s_air_ticks < h->lift_ticks) h->s_velocity[2] += h->lift;
    if (h->fall_after < h->s_air_ticks) h->s_falling = true;
    h->s_velocity[2] -= h->s_falling ? h->fall_gravity : h->gravity;
}

bool chr_intersect(ChrIntersect *x)
{
    ChrIntersectChar *a = &x->a, *b = &x->t;
    const uint8_t *ta = a->anim, *tb = b->anim;
    memset(a->s_pos, 0, sizeof a->s_pos);
    a->s_frame = a->frame;
    if (a->now.air) {
        a->s_air_ticks = a->now.air_ticks;
        a->s_velocity[0] = a->now.air_velocity[0];
        a->s_velocity[1] = a->now.air_velocity[2];
        a->s_velocity[2] = a->now.air_velocity[1];
        a->s_falling = a->now.jumped;
    }
    b->s_pos[0] = x->m[9];
    b->s_pos[1] = x->m[10];
    b->s_pos[2] = x->m[11];
    b->s_frame = b->frame;
    if (b->now.air) {
        b->s_air_ticks = b->now.air_ticks;
        float v[3] = { b->now.air_velocity[0], b->now.air_velocity[2], b->now.air_velocity[1] };
        chr_matrix3_apply(x->m, v, b->s_velocity);
        b->s_falling = b->now.jumped;
    }
    float step[3];
    chr_matrix3_apply(x->m, x->t_velocity, step);

    uint32_t n = rd32(ta + TRAM_POINT_COUNT), i = 0;
    const uint8_t *pt = NULL;
    for (; i < n; i++) {
        pt = tr_raw_block(ta, TRAM_POINTS, 12u * i, 12);
        if (!pt || a->s_frame <= rd16(pt)) break;
    }
    if (i >= n || !pt) {
        x->result = CHR_ISECT_NO_POINTS;
        return false;
    }
    float rr = a->radius + x->t_radius;
    rr *= rr;
    uint32_t dir = direction(ta);
    uint16_t a_frames = tr_duration(ta), b_frames = tr_duration(tb);
    uint32_t too_far = 0, missed = 0, away = 0, too_high = 0, too_low = 0;
    for (;;) {
        const uint8_t *bf = tr_raw_block(tb, TRAM_FRAME_BOUNDS, 8u * b->s_frame, 8);
        bool tested = false;
        if (pt && a->s_frame == rd16(pt)) {
            tested = true;
            float dh = a->s_pos[2] - b->s_pos[2];
            float pbot = (float)rds16(pt + 8) * 0.01f, ptop = (float)rds16(pt + 10) * 0.01f;
            float bbot = bf ? (float)rds16(bf + 6) * 0.01f : 0.0f, bh = bf ? (float)rd16(bf + 4) * 0.01f : 0.0f;
            if (!(bbot + 1.0f <= ptop + dh)) {
                tested = false;
                too_low++;
            } else if (!(pbot + dh <= (bh + bbot) - 1.0f)) {
                tested = false;
                too_high++;
            } else {
                float dx = a->s_pos[0] - b->s_pos[0], dy = a->s_pos[1] - b->s_pos[1];
                float c = (dx * dx + dy * dy) - x->t_radius * x->t_radius;
                if (c < 0.0f) {
                    x->frames = (uint32_t)a->s_frame - a->frame;
                    return true;
                }
                float s, co;
                dir256(pt[3], &s, &co);
                float bb = dx * s + dy * co;
                bb += bb;
                if (bb <= 0.0f) {
                    float disc = bb * bb - c * 4.0f;
                    if (0.0f <= disc) {
                        float k = (-bb - sqrtf(disc)) * 0.5f;
                        if (!((float)rd16(pt + 4) * 0.01f < k)) {
                            x->frames = (uint32_t)a->s_frame - a->frame;
                            return true;
                        }
                        too_far++;
                    } else {
                        missed++;
                    }
                } else {
                    away++;
                }
            }
            /* the next point; past the last, the most frequent miss */
            i++;
            pt = i < n ? tr_raw_block(ta, TRAM_POINTS, 12u * i, 12) : NULL;
            if (i >= n) {
                uint32_t m = too_high;
                if (too_low > m) m = too_low;
                if (missed > m) m = missed;
                if (too_far > m) m = too_far;
                if (away > m) m = away;
                x->result = m == too_high ? CHR_ISECT_TOO_HIGH : m == too_low ? CHR_ISECT_TOO_LOW
                          : m == missed ? CHR_ISECT_MISSED : m == too_far ? CHR_ISECT_TOO_FAR : CHR_ISECT_AWAY;
                return false;
            }
        }
        /* the target's frame: its air, else its estimated motion */
        if (b->now.air) {
            air_step(b, a->now.jump_held);
            chr_matrix3_apply(x->m, b->s_velocity, step);   /* turned again: the original's */
        }
        for (int k = 0; k < 3; k++) b->s_pos[k] += step[k];
        /* the attacker's: its air, else the animation's root motion */
        if (a->now.air) {
            air_step(a, a->now.jump_held);
            for (int k = 0; k < 3; k++) a->s_pos[k] += a->s_velocity[k];
        } else {
            const uint8_t *mv = tr_raw_block(ta, TRAM_VELOCITIES, 8u * a->s_frame, 8);
            if (mv) {
                a->s_pos[0] += rdf(mv);
                a->s_pos[1] += rdf(mv + 4);
            }
        }
        if ((uint32_t)a_frames - 1u <= a->s_frame) return false;
        a->s_frame++;
        if ((uint32_t)b->s_frame < (uint32_t)b_frames - 1u) b->s_frame++;
        if (tested) continue;
        /* overlapping: the attacker kept out of the target's circle, back along the attack */
        float dx = a->s_pos[0] - b->s_pos[0], dy = a->s_pos[1] - b->s_pos[1];
        float dx2 = dx * dx, dy2 = dy * dy;
        if (dx2 + dy2 > rr) continue;
        const uint8_t *nb = tr_raw_block(tb, TRAM_FRAME_BOUNDS, 8u * b->s_frame, 8);
        const uint8_t *na = tr_raw_block(ta, TRAM_FRAME_BOUNDS, 8u * a->s_frame, 8);
        if (!na || !nb) continue;
        /* both tops add their height twice: the original's */
        float bbot = (float)rds16(nb + 6) * 0.01f + b->s_pos[2];
        float abot = (float)rds16(na + 6) * 0.01f + a->s_pos[2];
        if (!(abot <= (float)rd16(nb + 4) * 0.01f + bbot + b->s_pos[2])) continue;
        if (!(bbot <= (float)rd16(na + 4) * 0.01f + abot + a->s_pos[2])) continue;
        switch (dir) {
        case 0: case 1: case 5: a->s_pos[1] = b->s_pos[1] - sqrtf(rr - dx2); break;
        case 2: a->s_pos[1] = sqrtf(rr - dx2) + b->s_pos[1]; break;
        case 3: a->s_pos[0] = b->s_pos[0] - sqrtf(rr - dy2); break;
        case 4: a->s_pos[0] = sqrtf(rr - dy2) + b->s_pos[0]; break;
        default: break;
        }
    }
}
