/* The AI's combat: see combat.h. */
#include "combat.h"
#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "film.h"
#include "knowledge.h"
#include "melee.h"
#include "objc.h"
#include "trac.h"
#include "ui.h"
#include "weapon.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }
static float wrap_pi(float a) { return a < -PI ? a + TWO_PI : a > PI ? a - TWO_PI : a; }     /* 0x52a674, 0x529544 */
static float len3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

/* ---- the settings ---- */

/* The Combat object (OBJcCombat, version 39): the name (64), the id (u16 at +0x40), the flags (+0x42),
   the behaviours of the five ranges (+0x44), the medium range (+0x58), the unarmed's (+0x5c, +0x60),
   the short range (+0x64), the pursuit distance (+0x68), then the alarm's settings. */
enum { COMBAT_PROFILES_MAX = 64 };
static const uint8_t *profiles[COMBAT_PROFILES_MAX];
static int n_profiles;

static bool add_profile(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size >= 0x90 && n_profiles < COMBAT_PROFILES_MAX) profiles[n_profiles++] = o->data;
    return true;
}

void combat_load(void)
{
    n_profiles = 0;
    objc_each("Combat", add_profile, NULL);
}

void combat_settings(AiState *a, uint16_t id)
{
    CombatSettings *s = &a->combat_set;
    const uint8_t *p = NULL;
    for (int i = 0; i < n_profiles && !p; i++)     /* ONrLevel_Combat_ID_To_Combat 0x506b30 */
        if (rd16(profiles[i] + 0x40) == id) p = profiles[i];
    if (!p) {
        /* the defaults: medium 200, short 40, Hold And Fire at every range, unarmed: fight when the
           target is close (3) */
        s->id = 0;
        s->medium = 200.0f;
        for (int k = 0; k < 5; k++) s->behaviour[k] = 2;
        s->no_gun = 3;
        s->no_gun_else = 0;
        s->near = 40.0f;
        return;
    }
    s->id = id;
    s->medium = rdf(p + 0x58);
    for (int k = 0; k < 5; k++) s->behaviour[k] = (int)rd32(p + 0x44 + 4 * k);
    s->no_gun = (int)rd32(p + 0x5c);
    s->no_gun_else = (int)rd32(p + 0x60);
    s->near = rdf(p + 0x64);
}

/* ---- the targeting ---- */

/* The class's AI block (ONCC+0x138): +0 and +4 the first shots' error (by the distance, at most), +8
   the lead, +0xc its delay, +0x10 and +0x14 the motion's two windows, +0x18 the memory's size, +0x1c
   the 13 shooting skills (0x18 bytes: +4 the error close up, +0x14 and +0x16 the delay's range). */
enum { ONCC_AI = 0x138 };
/* The weapon's AI firing mode (ONWC+0xd0): +0 flags (1 no first error), +4 a matrix to the barrel's
   space, +0x34 the barrel, +0x40 the muzzle, +0x4c the shots' speed, +0x50 and +0x54 the tolerances,
   +0x5c the closest it fires at, +0x60 the farthest (0 none), +0x66 the skill, +0x68 the melee wait,
   +0x6c and +0x70 a ballistic shot's speed and gravity. */

/* The callbacks (0x53ebf8): +4 AI2rExecutor_SetAimingSpeed 0x498e80. */
static void set_aiming_speed(AiState *a, float speed) { a->aim.speed = speed; }

/* The callback +0x10 (0x490bd0): the shooter's place, its pelvis. */
static void shooter_place(const Character *c, float out[3])
{
    out[0] = c->pos[0];
    out[1] = c->pos[1] + c->pelvis_height;
    out[2] = c->pos[2];
}

static void pelvis(const Character *c, float out[3]) { shooter_place(c, out); }

/* 0x4bb700: the firing mode (NULL none); the ring of the target's places starts again. */
static void set_weapon(AiState *a, const uint8_t *params)
{
    AiTargeting *t = &a->combat.t;
    t->params = params;
    if (!params) {
        t->active = false;
        t->skill = NULL;
        set_aiming_speed(a, 1.0f);
        return;
    }
    t->active = true;
    t->skill = t->cls + 0x1c + 0x18u * rd16(params + 0x66);
    unsigned n = rd32(t->cls + 0x18);
    if (n > TARGETING_MEMORY) n = TARGETING_MEMORY;     /* (the port's memory holds 64: the classes have 60) */
    if (!n) n = 1;
    t->count = 0;
    t->head = n - 1;
    memset(t->vel_short, 0, sizeof t->vel_short);
    memset(t->vel_long, 0, sizeof t->vel_long);
}

static unsigned ring_size(const AiTargeting *t)
{
    unsigned n = rd32(t->cls + 0x18);
    return n > TARGETING_MEMORY ? TARGETING_MEMORY : n ? n : 1;
}

/* AI2rTargeting_SetupNewTarget 0x4babb0. */
static void setup_new_target(AiState *a, const Character *c, const Character *target, bool inaccurate)
{
    AiTargeting *t = &a->combat.t;
    t->target = target;
    t->aim_dist = t->dist = 0.0f;
    t->first = true;
    t->off_ticks = t->delay = t->close_ticks = 0;
    t->grouping = 0.0f;
    t->stopped = true;
    memset(t->offset, 0, sizeof t->offset);
    t->inaccurate = false;
    t->inaccuracy = 0.0f;
    if (target) {
        pelvis(target, t->point);
        shooter_place(c, t->from);
        float d[3] = { t->point[0] - t->from[0], t->point[1] - t->from[1], t->point[2] - t->from[2] };
        t->dist = t->aim_dist = len3(d);
        t->has_point = true;
        if (inaccurate && t->params && !(rd32(t->params) & 1)) {
            float e = t->dist * rdf(t->cls);
            t->inaccurate = true;
            t->inaccuracy = e > rdf(t->cls + 4) ? rdf(t->cls + 4) : e;
        }
    }
    set_aiming_speed(a, 0.5f);
}

/* AI2iTargeting_UpdatePrediction 0x4bacc0: the target's place now (or as it was the lead's delay ago),
   its motion over the two windows (per second), and the point led by the shots' travel. */
static void update_prediction(AiTargeting *t)
{
    const Character *tc = t->target;
    float now[3];
    pelvis(tc, now);
    unsigned n = ring_size(t);
    if (!t->active || !t->params) {
        memcpy(t->point, now, sizeof now);
        float d[3] = { now[0] - t->from[0], now[1] - t->from[1], now[2] - t->from[2] };
        t->dist = len3(d);
        t->has_point = true;
        return;
    }
    unsigned c = t->count, w_long = rd32(t->cls + 0x18), w_short = rd32(t->cls + 0x14), w_start = rd32(t->cls + 0x10);
    unsigned a_long = (w_long < c ? w_long : c), a_short = (w_short < c ? w_short : c), a_start = (w_start < c ? w_start : c);
    unsigned i_long = (a_long + t->head) % n, i_short = (a_short + t->head) % n, i_start = (t->head + a_start) % n;
    unsigned delay = rd32(t->cls + 0xc);
    if (delay == 0 || c == 0) memcpy(t->point, now, sizeof now);
    else memcpy(t->point, t->ring[((delay < c ? delay : c) + t->head) % n], sizeof now);
    float d[3] = { t->point[0] - t->from[0], t->point[1] - t->from[1], t->point[2] - t->from[2] };
    t->dist = len3(d);
    bool moving;
    float v1 = 0.0f, v2 = 0.0f, vs[3] = { 0 };
    if (i_short == i_start) {
        moving = false;
        memset(t->vel_short, 0, sizeof t->vel_short);
    } else {
        float k = 60.0f / (float)(w_short - w_start);     /* 0x52a188: per second */
        for (int j = 0; j < 3; j++) vs[j] = t->vel_short[j] = k * (t->ring[i_start][j] - t->ring[i_short][j]);
        v1 = len3(vs);
        moving = v1 >= 1.0f;
        t->speed = v1;
    }
    if (i_long == i_start) {
        moving = false;
        memset(t->vel_long, 0, sizeof t->vel_long);
    } else {
        float k = 60.0f / (float)(w_long - w_start);
        for (int j = 0; j < 3; j++) t->vel_long[j] = k * (t->ring[i_start][j] - t->ring[i_long][j]);
        v2 = len3(t->vel_long);
        if (v1 < 1.0f) moving = false;
    }
    memcpy(t->ring[t->head], now, sizeof now);
    t->count++;
    t->head = t->head ? t->head - 1 : n - 1;
    t->has_point = true;
    if (!moving) return;
    /* how steady the motion is: the two windows' agreement (their cosine, at most their speeds'
       ratio), none under 0.5, all over 0.9 (0x529560, 0x5295a4, 0x52b014) */
    float dot = vs[0] * t->vel_long[0] + vs[1] * t->vel_long[1] + vs[2] * t->vel_long[2];
    float f = dot / (v2 * v1), r = v2 / v1;
    if (r < f) f = r;
    if (f <= 0.5f) {
        t->predict = 0.0f;
        return;
    }
    f = f <= 0.9f ? (f - 0.5f) * 2.5f : 1.0f;
    t->predict = f;
    float lead = (float)rd32(t->cls + 0xc) * (1.0f / 60.0f) + rdf(t->cls + 8) * t->dist * f / rdf(t->params + 0x4c);
    for (int j = 0; j < 3; j++) t->point[j] += lead * vs[j];
}

/* MUrMatrix_MultiplyPoint, 3x3 by columns then the place. */
static void mul_point(const float m[12], const float p[3], float out[3])
{
    float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[3] * y + m[6] * z + m[9];
    out[1] = m[1] * x + m[4] * y + m[7] * z + m[10];
    out[2] = m[2] * x + m[5] * y + m[8] * z + m[11];
}

static void mul_vector(const float m[12], const float p[3], float out[3])
{
    float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[3] * y + m[6] * z;
    out[1] = m[1] * x + m[4] * y + m[7] * z;
    out[2] = m[2] * x + m[5] * y + m[8] * z;
}

static void mul_transpose(const float m[12], const float p[3], float out[3])
{
    float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[1] * y + m[2] * z;
    out[1] = m[3] * x + m[4] * y + m[5] * z;
    out[2] = m[6] * x + m[7] * y + m[8] * z;
}

static void params_matrix(const uint8_t *params, float m[12])
{
    for (int k = 0; k < 12; k++) m[k] = rdf(params + 4 + 4 * k);
}

/* AI2iTargeting_UpdateTargeting 0x4bb370: with a weapon to aim, the aim from the muzzle (a ballistic
   shot's arc solved for); else the look at the target, or at the point. */
static void update_targeting(AiState *a, bool aim)
{
    AiTargeting *t = &a->combat.t;
    t->ballistic = true;
    bool on = aim && t->params;
    int mode = on ? 1 : 2;
    if (t->mode != mode) t->mode = mode;    /* the callback +8 (0x490ba0, the movement's weapon aim): not kept */
    if (!on) {
        t->aim_dist = t->dist;
        if (!t->target) {
            a->aim.look = 1;                /* AI2rMovement_LookAtPoint */
            memcpy(a->aim.look_at, t->aim, sizeof t->aim);
        } else {
            a->aim.look = 3;                /* AI2rMovement_LookAtCharacter */
            a->aim.look_char = t->target;
        }
        return;
    }
    const uint8_t *p = t->params;
    float v[3] = { t->aim[0] - t->from[0], t->aim[1] - t->from[1], t->aim[2] - t->from[2] };
    float len = len3(v), speed = rdf(p + 0x6c);
    if (speed > 0.0f) {
        /* the arc: the time for the horizontal distance and the rise under gravity (0x52a054 98) */
        float hh = v[0] * v[0] + v[2] * v[2], h = sqrtf(hh), g = 98.0f * rdf(p + 0x70), gg = g * g * 0.25f;
        float b = g * v[1] - speed * speed, disc = b * b - (v[1] * v[1] + hh) * gg * 4.0f;
        t->ballistic = false;
        float tt;
        if (disc >= 0.0f && (tt = (-b - sqrtf(disc)) / (gg + gg)) > 0.0f) {
            tt = sqrtf(tt);
            t->bal_h = h / tt;
            t->bal_v = v[1] / tt + tt * g * 0.5f;
            t->bal_angle = atan2f(v[0], v[2]);
            if (t->bal_angle < 0.0f) t->bal_angle += TWO_PI;
            t->ballistic = true;
        }
        if (t->ballistic) {
            float k = t->bal_h / speed;
            v[0] = (v[0] / h) * k * len;
            v[2] = (v[2] / h) * k * len;
            v[1] = (t->bal_v / speed) * len;
        }
    }
    /* close up, the muzzle drawn back along the barrel (0x52a1d0 25, 0x52b01c -1/15, 0x52a740 8) */
    float close = (len - 25.0f) * -0.06666667f;
    close = close < 0.0f ? 0.0f : close > 1.0f ? 1.0f : close;
    t->close = close;
    float m[3] = { rdf(p + 0x40) - close * 8.0f, rdf(p + 0x44), rdf(p + 0x48) };
    mul_point(t->matrix, m, t->muzzle);
    for (int j = 0; j < 3; j++) t->vector[j] = (t->from[j] - t->muzzle[j]) + v[j];
    t->aim_dist = len3(t->vector);
    float b[3] = { rdf(p + 0x34), rdf(p + 0x38), rdf(p + 0x3c) };
    mul_vector(t->matrix, b, t->barrel);
    a->aim.look = 2;                        /* the callback +0x14 (0x4a8090): aim along the vector */
    memcpy(a->aim.look_at, t->vector, sizeof t->vector);
}

/* AI2rTargeting_Update 0x4ba720. *close: how close the target is within the weapon's closest (0 none). */
static void targeting_update(AiState *a, const Character *c, bool has_target, bool aim, bool fire, float *close)
{
    AiTargeting *t = &a->combat.t;
    t->on_target = false;
    t->active = has_target && t->params;
    if (!t->target) {
        if (!t->has_point) aim = false;
        else memcpy(t->aim, t->point, sizeof t->aim);
    } else {
        shooter_place(c, t->from);
        update_prediction(t);
        for (int j = 0; j < 3; j++) t->aim[j] = t->point[j] + (fire ? t->offset[j] : 0.0f);
    }
    update_targeting(a, has_target);
    if (close) {
        *close = 0.0f;
        if (t->params && aim && t->dist < rdf(t->params + 0x5c)) *close = 1.0f - t->dist / rdf(t->params + 0x5c);
    }
}

/* 0x4e37b0: knocked down, getting up, a victim or stunned (DoCharacterFrame's step 9 list). */
static bool knocked(uint16_t t)
{
    return (t >= 0x4c && t <= 0x5c) || (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) ||
           (t >= 0x7e && t <= 0x84) || t == 0x8d || t == 0xc6 || (t >= 0xd4 && t <= 0xd7);
}

/* ONrCharacter_IsPickingUp 0x4e3f30: the pickup animations. */
static bool picking_up(uint16_t t) { return t == 0x9d || t == 0x9e || t == 0x9f || (t >= 0xe3 && t <= 0xe5); }

/* FUN_00490a50: the trigger held (or the alternate's), unless picking up or the weapon can't; let go,
   the trigger's release. */
static void fire(AiState *a, const Character *c, bool on)
{
    CombatState *s = &a->combat;
    if (knocked(c->type) || picking_up(c->type)) on = false;
    /* (with the class's flag 0x200, 0x51a380 can stop it too) */
    if (!c->weapon) on = false;
    if (on) {
        s->firing = s->firing_alt = false;
        if (!s->alt) a->aim.fire = true, s->firing = true;
        else a->aim.fire_alt = true, s->firing_alt = true;
        return;
    }
    s->firing = false;
    a->aim.fire = a->aim.fire_alt = false;
    /* (the behaviour's message 8 has no handler) */
}

/* 0x4ba830: whether to fire: the aim against the barrel (in the barrel's space, the miss across it),
   within the tolerance (the firing mode's, grown by the grouping, the first shots' error, the time off
   target, and close up the skill's), after the delay; far off, faster aiming; point blank, fire. */
static void fire_decision(AiState *a, const Character *c, bool aim, bool want, bool lof)
{
    AiTargeting *t = &a->combat.t;
    const uint8_t *p = t->params;
    if (!p || !aim || !want || !lof) {
        t->stopped = true;
        t->fire = false;
        fire(a, c, t->fire);
        return;
    }
    if (t->stopped) {
        t->first = true;
        t->close_ticks = 0;
    }
    t->stopped = false;
    if (t->delay && --t->delay) {
        t->fire = false;
        fire(a, c, t->fire);
        return;
    }
    if (t->aim_dist >= 10.0f) t->close_ticks = 0;  /* 0x52a190 */
    else if (++t->close_ticks > 9) {
        t->first = false;
        t->fire = true;
        fire(a, c, t->fire);
        return;
    }
    float m[12], w[3], l[3];
    params_matrix(p, m);
    mul_transpose(t->matrix, t->vector, w);
    mul_vector(m, w, l);
    float across, ahead;
    if (t->axis == 0) across = sqrtf(l[1] * l[1] + l[2] * l[2]), ahead = l[0];
    else across = sqrtf(l[1] * l[1] + l[0] * l[0]), ahead = l[2];
    if (t->close * 1.5f + 0.5f > ahead / across) {     /* 0x52a774, 0x529560: not even toward it */
        set_aiming_speed(a, 1.0f);
        t->fire = false;
        fire(a, c, t->fire);
        return;
    }
    float tol = rdf(p + 0x54);
    bool wide = false;
    if (rdf(p + 0x50) > tol && !t->first) {
        tol = rdf(p + 0x50);
        wide = true;
    }
    tol = t->grouping * 1.5f + tol;
    if (t->inaccurate && tol <= t->inaccuracy) tol = t->inaccuracy;
    if (t->off_ticks > 30) tol = (float)(t->off_ticks - 30) * 0.1f + tol;   /* 0x529cc4 */
    if (t->close >= 1.0f) {
        float near = ahead * rdf(t->skill + 4);
        if (tol < near) tol = near;
    } else
        tol = ((1.0f - t->close) * 3.0f + 1.0f) * tol;   /* 0x529808 */
    t->on_target = true;
    t->error = across;
    t->tolerance = tol;
    if (across <= tol) {
        if (t->inaccurate) {
            if (t->inaccuracy > across) t->inaccuracy = across;
            if (t->inaccuracy < rdf(p + 0x54)) t->inaccurate = false;
        }
        t->first = false;
        t->off_ticks = 0;
        t->fire = true;
    } else {
        if (tol * 5.0f <= across) set_aiming_speed(a, 1.0f);   /* 0x52956c */
        t->first = !t->inaccurate;
        if (wide) {
            unsigned lo = rd16(t->skill + 0x14), hi = rd16(t->skill + 0x16);
            t->delay = (lo + (hi > lo ? (unsigned)(((uint32_t)trac_random() * (hi - lo)) >> 16) : 0)) & 0xffff;
        }
        t->off_ticks++;
        t->fire = false;
    }
    fire(a, c, t->fire);
}

/* ---- the behaviours ---- */

enum { MSG_ENTER = 0, MSG_WEIGHTS = 1, MSG_EXIT = 2, MSG_MELEE = 4, MSG_FLAGS = 5, MSG_DAZE = 6, MSG_CLOSE = 7,
       MSG_KEEP_AIM = 9, MSG_RANGE = 15 };

/* The unarmed's choice (0x491060's test, and the default's message 7): 5 always fight, 1 when hurt in
   the last 4 seconds, 3 and up when the range is as close as the table's (0x52a688). */
static bool unarmed_fights(const AiState *a)
{
    static const int closest[6] = { 0, 0, 0, 3, 2, 1 };    /* 0x52a688 */
    int n = a->combat_set.no_gun;
    if (n == 5) return true;
    if (n == 0) return false;
    if (n > 2 && n < 6) {
        int r = a->combat.range == 5 ? 1 : a->combat.range == 4 ? 2 : a->combat.range;
        if (closest[n] <= r) return true;
    }
    return n == 1 && ui_time() < a->combat.hurt_time + 240;
}

/* 0x491060: a behaviour that needs a gun, without one. */
static int no_gun(AiState *a, int msg, bool *handled)
{
    CombatState *s = &a->combat;
    *handled = true;
    if (msg == MSG_WEIGHTS) {
        if (unarmed_fights(a)) {
            s->weight[COMBAT_MANEUVER_MELEE] = 1.0f;
            return 0;
        }
        /* find a gun (the gun's distances 30 and 200: +0x1cc, +0x1d0), then the fallback */
        s->weight[COMBAT_MANEUVER_GUN] = 1.0f;
        int e = a->combat_set.no_gun_else;
        if (e == 1) {
            s->weight[COMBAT_MANEUVER_RETREAT] = 0.8f;
            s->ret_near = 100.0f;
            s->ret_far = 1000.0f;
            return 0;
        }
        if (e == 2) s->weight[COMBAT_MANEUVER_ALARM] = 1.0f;
        else if (e != 0) return 0;
        s->weight[COMBAT_MANEUVER_MELEE] = 0.8f;
    } else if (msg == MSG_FLAGS) {
        s->check_env = s->check_friends = s->aim = false;
        s->targeting = s->maneuver != COMBAT_MANEUVER_GUN || s->lof2;
    } else if (msg == MSG_MELEE) {
        if (s->maneuver != COMBAT_MANEUVER_GUN) s->melee_ok = true;
    } else
        *handled = false;
    return 0;
}

static bool needs_gun(const Character *c) { return !c->weapon || weapon_has_empty((Character *)c); }

/* The behaviours (0x52a6c4): 1 Stare 0x490f20, 2 Hold And Fire 0x490f90, 3 Firing Charge 0x4911e0,
   4 Melee 0x4912d0. The bosses' (5 to 13: Barabbas, the ninja, Muro) and Run For Alarm aren't ported:
   the default's messages only. */
static int behaviour(AiState *a, const Character *c, int id, int msg, bool *handled)
{
    CombatState *s = &a->combat;
    *handled = true;
    switch (id) {
    case 1:
        if (msg == MSG_WEIGHTS) s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        else if (msg == MSG_MELEE) s->melee_ok = true;
        else if (msg == MSG_FLAGS) {
            s->check_env = true;
            s->check_friends = s->aim = false;
            s->targeting = true;
        } else if (msg != MSG_ENTER && msg != MSG_EXIT)
            *handled = false;
        return 0;
    case 2:
    case 3:
        if (msg != MSG_ENTER && msg != MSG_EXIT && needs_gun(c)) return no_gun(a, msg, handled);
        if (msg == MSG_WEIGHTS) {
            if (id == 2) {
                s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
            } else {
                s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
                s->adv_far = 80.0f;
                s->adv_near = 20.0f;
                s->weight[COMBAT_MANEUVER_HOLD] = 0.2f;
            }
        } else if (msg == MSG_MELEE)
            s->melee_ok = true;
        else if (msg == MSG_FLAGS)
            s->check_env = s->check_friends = s->targeting = s->aim = s->fire = true;
        else if (msg != MSG_ENTER && msg != MSG_EXIT)
            *handled = false;
        return 0;
    case 4:
        if (msg == MSG_WEIGHTS) s->weight[COMBAT_MANEUVER_MELEE] = 1.0f;
        else if (msg == MSG_MELEE) s->melee_ok = s->maneuver != COMBAT_MANEUVER_MELEE;
        else if (msg == MSG_FLAGS) {
            s->targeting = true;
            s->check_env = s->check_friends = s->aim = s->fire = false;
        } else if (msg != MSG_ENTER && msg != MSG_EXIT)
            *handled = false;
        return 0;
    default:
        *handled = false;
        return 0;
    }
}

/* AI2rBehavior_Default 0x493120 (messages 0, 2 and 3, the vocalizations and the report, aren't kept;
   6, the daze, waits for the getting up). */
static int behaviour_default(AiState *a, const Character *c, int msg, float *arg)
{
    CombatState *s = &a->combat;
    switch (msg) {
    case MSG_MELEE:
        s->melee_ok = true;
        return 0;
    case MSG_FLAGS:
        s->targeting = true;
        s->check_env = s->check_friends = s->aim = false;
        return 0;
    case MSG_CLOSE: {
        /* too close for the gun: fight, step in, or back off, by how close (*arg) */
        float k = *arg * 1.5f;
        if (k > 1.0f) k = 1.0f;
        bool fight = unarmed_fights(a);
        s->weight[COMBAT_MANEUVER_ADVANCE] *= 1.0f - k;
        s->weight[COMBAT_MANEUVER_HOLD] *= 1.0f - k;
        float d = s->t.params ? rdf(s->t.params + 0x5c) : 0.0f;
        if (fight) {
            s->weight[COMBAT_MANEUVER_MELEE] = k;
            s->adv_far = d;
            s->adv_near = d * 0.75f;    /* 0x52a770 */
            if (s->melee_wait < 0x79) s->melee_wait = 0x78;
            s->backed = s->backed2 = 0;
            return 0;
        }
        if (k > 0.5f && (s->backed2 > 0xb3 || (s->backed && s->backed < 300))) {
            s->weight[COMBAT_MANEUVER_MELEE] = k;
            s->backed2 = 0;
            s->adv_far = d;
            s->adv_near = d * 0.75f;
            s->backed += 5;
            return 0;
        }
        s->weight[COMBAT_MANEUVER_RETREAT] = k;
        s->backed = 0;
        s->ret_near = d;
        s->ret_far = d * 1.5f;
        if (k > 0.9f) s->backed2 += 5;
        return 0;
    }
    case MSG_KEEP_AIM: {
        /* keep aiming while firing or near enough for the maneuver (0x52a6a0; else 500) */
        static const float near[8] = { 80, 120, 140, 80, 60, 140, 160, 0 };
        if (!s->firing && !s->firing_alt && !s->fire && !(c->weapon && (c->weapon->flags & WEAPON_FIRING))) {
            float d = s->maneuver >= 0 && s->maneuver < 8 ? near[s->maneuver] : 500.0f;
            if (d < s->dist) return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int send(AiState *a, const Character *c, int msg, float *arg)
{
    bool handled = false;
    int r = behaviour(a, c, a->combat.behaviour, msg, &handled);
    if (!handled) r = behaviour_default(a, c, msg, arg);
    return r;
}

/* AI2rCombat_GetRange 0x4901f0. */
static int get_range(AiState *a, const Character *c, int prev)
{
    CombatState *s = &a->combat;
    if (!s->t.has_point) return COMBAT_RANGE_LONG;
    float d[3] = { c->pos[0] - s->t.point[0], c->pos[1] - s->t.point[1], c->pos[2] - s->t.point[2] };
    s->dist = len3(d);
    if (s->dist < a->combat_set.near) return COMBAT_RANGE_SHORT;
    if (s->dist < a->combat_set.medium) return prev < 3 ? COMBAT_RANGE_MEDIUM : COMBAT_RANGE_MEDIUM_RETREAT;
    return prev < 2 ? COMBAT_RANGE_LONG : COMBAT_RANGE_LONG_RETREAT;
}

/* 0x490290: the range's behaviour, the old one left, the new one entered. */
static void switch_behaviour(AiState *a, const Character *c)
{
    CombatState *s = &a->combat;
    send(a, c, MSG_EXIT, NULL);
    int r = s->range;
    int id = r >= 1 && r <= 5 ? a->combat_set.behaviour[r - 1] : 0;
    s->behaviour = id >= 0 && id < 14 ? id : 1;
    send(a, c, MSG_ENTER, NULL);
}

void combat_new_target(AiState *a, Character *c, const Character *target)
{
    CombatState *s = &a->combat;
    s->melee_wait = s->backed = s->backed2 = 0;
    s->hurt_time = 0;
    const KnowEntry *e = a->target_entry;
    if (e && e->type == KNOW_HURT_MELEE) s->hurt_time = e->posted;
    s->t.cls = (const uint8_t *)c->oncc + ONCC_AI;
    setup_new_target(a, c, target, true);
    s->range = get_range(a, c, 0);
    s->behaviour = 0;
    switch_behaviour(a, c);
}

/* ---- the maneuvers ---- */

static float dist_to(const Character *c, const float p[3])
{
    float d[3] = { p[0] - c->pos[0], p[1] - c->pos[1], p[2] - c->pos[2] };
    return len3(d);
}

/* 0x49dbf0: the heaviest maneuver that can be done (a failing one's weight goes, and the next is
   tried). The local paths' tests (AI2rTryLocalMovement, AI2rFindLocalPath, the blockage) are the
   port's stand-ins: the advance runs to the target's point, the retreat backs straight away 15. The
   gun and the alarm aren't ported (they fail). */
static void select_maneuver(AiState *a, Character *c)
{
    CombatState *s = &a->combat;
    a->moving = false;      /* AI2rPath_Halt */
    for (;;) {
        int best = -1;
        float w = 0.0f;
        for (int k = 0; k < COMBAT_MANEUVERS; k++)
            if (s->weight[k] > 0.0f && s->weight[k] > w) w = s->weight[k], best = k;
        if (best < 0) {
            best = COMBAT_MANEUVER_HOLD;
            s->weight[best] = 1.0f;
        }
        bool ok = false;
        switch (best) {
        case COMBAT_MANEUVER_HOLD:
            a->moving = false;
            ok = true;
            break;
        case COMBAT_MANEUVER_ADVANCE: {     /* AI2iManeuver_CheckAdvance 0x49df80 */
            if (!s->t.has_point) break;
            float d = dist_to(c, s->t.point), f;
            if (d > s->adv_far) f = 1.0f;
            else if (d >= s->adv_near) f = (d - s->adv_near) / (s->adv_far - s->adv_near);
            else f = 0.0f;
            if (f > 0.0f) {
                s->weight[best] *= f > 0.5f ? f : 0.5f;
                if (s->weight[best] >= w) {
                    ai_move_near(a, s->t.point, 4.0f, false, 0.0f);    /* AI2rPath_GoToPoint (4) */
                    ok = true;
                }
            }
            break;
        }
        case COMBAT_MANEUVER_RETREAT: {
            const Character *t = s->t.target;
            if (!t) break;
            float dx = c->pos[0] - t->pos[0], dz = c->pos[2] - t->pos[2], dd = dx * dx + dz * dz;
            float d2 = dd + (c->pos[1] - t->pos[1]) * (c->pos[1] - t->pos[1]), f = 1.0f;
            if (s->ret_near * s->ret_near > d2) f = 1.0f;
            else if (s->ret_far * s->ret_far < d2) break;
            else f = (sqrtf(d2) - s->ret_far) / (s->ret_near - s->ret_far);
            s->weight[best] = f * s->weight[best];      /* (the path's quality, 0x52a944, taken as clear) */
            if (s->weight[best] < w || dd <= 0.0f) break;
            float r = sqrtf(dd), to[3] = { c->pos[0] + dx / r * 15.0f, c->pos[1], c->pos[2] + dz / r * 15.0f };
            ai_move_near(a, to, 4.0f, false, 0.0f);
            ok = true;
            break;
        }
        case COMBAT_MANEUVER_MELEE:         /* 0x49e910 */
            if (!a->melee.profile || !s->t.target) break;
            if (dist_to(c, s->t.target->pos) * dist_to(c, s->t.target->pos) < 6400.0f) {   /* 0x52a9a4 */
                ok = true;  /* (AI2rMelee_TestLocalEnvironment: taken as clear) */
                break;
            }
            /* too far: advance to within 15 to 20 of it */
            a->moving = false;
            s->targeting = true;
            s->aim = false;
            float h = s->weight[COMBAT_MANEUVER_HOLD];
            s->weight[COMBAT_MANEUVER_MELEE] = 0.0f;
            s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
            s->adv_far = 20.0f;
            s->adv_near = 15.0f;
            s->weight[COMBAT_MANEUVER_HOLD] = h >= 0.2f ? h : 0.2f;
            continue;
        default:
            break;
        }
        if (ok) {
            s->maneuver = best;
            return;
        }
        s->weight[best] = 0.0f;
    }
}

/* FUN_00490c50: the line from p along v clear of the environment (AKrCollision_Point, flags 0x242000)
   and of everyone not an enemy (a cylinder of radius 4 up to 8 over the pelvis). The objects
   (AMrRayToObject) aren't tested. */
static bool line_clear(AiState *a, const Character *c, const void *env, const float p[3], const float v[3])
{
    CombatState *s = &a->combat;
    if (s->check_env && env && akev_segment_hits(env, p, v, 0x242000)) return false;
    if (!s->check_friends) return true;
    const Chr *self = (const Chr *)c;
    for (int i = 0; i < chars_slots(); i++) {
        const Chr *o = chars_get(i);
        if (!o || o == self || &o->c == s->t.target) continue;
        if (ai_team_relation(self->team, o->team) == 2) continue;
        float ox = p[0] - o->c.pos[0], oz = p[2] - o->c.pos[2];
        float aa = v[2] * v[2] + v[0] * v[0], b = 2.0f * (ox * v[0] + oz * v[2]);
        float disc = b * b - ((ox * ox + oz * oz) - 16.0f) * aa * 4.0f;   /* 0x529e40 */
        if (disc < 0.0f || aa == 0.0f) continue;
        float k = (-b - sqrtf(disc)) / (aa + aa);
        if (k < 0.0f || k > 1.0f) continue;
        float y = k * v[1] + p[1] - o->c.pos[1];
        if (y >= 0.0f && y <= o->c.pelvis_height + 8.0f) return false;
    }
    return true;
}

/* 0x4e3ff0: in an animation of the types 0xc0..0xc3 (or about to be). */
static bool special_anim(const Character *c)
{
    return (c->type >= 0xc0 && c->type < 0xc4) || (c->queued >= 0xc0 && c->queued < 0xc4);
}

void (*combat_log)(const char *line);

static void debug_line(const AiState *a, const Character *c)
{
    const CombatState *s = &a->combat;
    char b[256];
    snprintf(b, sizeof b, "%s: range %d beh %d man %d dist %.0f lof %d aim %d fire %d look %d tgt %d err %.1f tol %.1f "
             "trig %d ammo %d self %u hp %u lr %.2f ud %.2f", a->name, s->range, s->behaviour, s->maneuver, s->dist, s->lof,
             s->aim, s->fire, a->aim.look, s->targeting, s->t.error, s->t.tolerance, a->aim.fire,
             c->weapon ? c->weapon->ammo : -1, c->hit_points, chars_player() ? chars_player()->c.hit_points : 0, c->aim_lr, c->aim_ud);
    combat_log(b);
}

void combat_tick(AiState *a, Character *c, const void *env, bool has_target)
{
    if (combat_log && ui_time() % 60 == 0) debug_line(a, c);
    CombatState *s = &a->combat;
    const Character *target = a->target ? &a->target->c : NULL;
    float close = 0.0f;
    if (!s->t.cls) s->t.cls = (const uint8_t *)c->oncc + ONCC_AI;
    if (s->t.target != target && !target) s->t.target = NULL;
    /* the weapon (or the firing mode) changed: the targeting's (its matrix: the body's node 19) */
    const Weapon *w = c->weapon;
    if (s->weapon != (const void *)w || s->weapon_alt != s->alt) {
        set_weapon(a, w ? w->cls + (s->alt ? 0x150 : 0xd0) : NULL);
        s->weapon = w;
        s->weapon_alt = s->alt;
    }
    if (w && !chr_weapon_matrix(c, s->t.matrix)) memcpy(s->t.matrix, w->m, sizeof s->t.matrix);
    bool targeted;
    if (!s->targeting) {
        targeted = false;
        if (target) {
            pelvis(target, s->t.point);
            s->t.has_point = true;
        }
        a->aim.look = 0;                    /* AI2rMovement_StopAiming */
    } else {
        targeted = true;
        targeting_update(a, c, has_target, s->aim, s->fire, &close);
    }
    /* (messages 13 and 6: the daze and the getting up, with the knockdowns' port) */
    s->getup = 4;
    bool waiting = a->combat_wait != 0 && a->combat_wait_reload == 0;
    /* every fifth tick, by the character's index: the range, the line of fire and the maneuver */
    if ((uint32_t)(c->id - 1) % 5 == ui_time() % 5) {
        int r = get_range(a, c, s->range);
        if (r != s->range) {
            s->range = r;
            switch_behaviour(a, c);
        }
        if (!w) {
            s->lof = true;
        } else {
            float far = rdf(s->t.params + 0x60);
            if (far <= 0.0f || s->dist < far) {
                s->lof = true;
                s->lof2 = false;
                if (s->t.has_point) {
                    float v[3] = { s->t.point[0] - s->t.muzzle[0], s->t.point[1] - s->t.muzzle[1], s->t.point[2] - s->t.muzzle[2] };
                    if (rdf(s->t.params + 0x6c) <= 0.0f) s->lof = line_clear(a, c, env, s->t.muzzle, v);
                    else if (!s->t.ballistic) s->lof = false;
                    /* (else the arc tested in steps (0x4264e0): taken as clear) */
                    if (!s->lof) s->lof2 = false;
                }
            } else {
                s->lof = s->lof2 = false;
            }
        }
        memset(s->weight, 0, sizeof s->weight);     /* 0x49dbc0 */
        send(a, c, MSG_WEIGHTS, NULL);
        if (s->getup != 4) s->weight[COMBAT_MANEUVER_GETUP] = 1.5f;
        /* (the melee wait of an empty weapon, +0x64 from the firing mode's +0x68: with the clips) */
        bool fight = false;
        if (s->melee_wait == 0) {
            if (waiting || close < 0.5f) s->backed2 = s->backed = 0;
            else send(a, c, MSG_CLOSE, &close);
        } else {
            s->weight[COMBAT_MANEUVER_MELEE] = 1.2f;
            s->melee_wait--;
            s->weight[COMBAT_MANEUVER_HOLD] = 0.2f;
            s->lof = true;
            fight = true;
            close = 0.0f;
        }
        if (waiting) {
            /* the target lost: go and look, or stand over the body */
            const Chr *tc = a->target;
            if (!tc || ((tc->c.flags & 1) && !tc->corpse_time)) {
                s->adv_far = 30.0f;
                s->adv_near = 12.0f;
                s->weight[COMBAT_MANEUVER_ADVANCE] = 1.2f;
                s->weight[COMBAT_MANEUVER_HOLD] = 0.8f;
                s->weight[COMBAT_MANEUVER_MELEE] = 0.0f;
            } else if (!(tc->c.flags & 1))
                a->combat_wait = 0;
            else
                s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        }
        if (!s->lof && !fight && !s->lof2) {
            /* no line of fire: close in */
            s->adv_far = 30.0f;
            s->adv_near = 12.0f;
            s->weight[COMBAT_MANEUVER_ADVANCE] = 1.2f;
            s->weight[COMBAT_MANEUVER_HOLD] = 0.4f;
            if (s->dist < 18.0f) s->weight[COMBAT_MANEUVER_MELEE] = 0.7f;   /* 0x52a730 */
        }
        select_maneuver(a, c);
        if (a->state != AI_STATE_COMBAT) return;
        /* (the gun pickup, the class's chance at +0x168: with the maneuver) */
        send(a, c, MSG_MELEE, NULL);
        /* (an attack of opportunity, 0x49f2e0, with ONCC+0x120 bit 2: not yet) */
        send(a, c, MSG_FLAGS, NULL);
        if (fight || s->maneuver == COMBAT_MANEUVER_MELEE) s->aim = s->fire = false;
        else if (waiting) s->fire = false;
    }
    /* the melee maneuver */
    bool fought = false;
    if (s->maneuver == COMBAT_MANEUVER_MELEE && target) {
        fought = melee_update(a, c, target);
        if (!fought) {
            /* (the port's: closing in when there is no technique to do) */
            float dx = target->pos[0] - c->pos[0], dz = target->pos[2] - c->pos[2];
            ai_move_near(a, target->pos, 12.0f, true, wrap_2pi(atan2f(dx, dz)));
        }
    }
    /* the trigger */
    bool may = !fought && !special_anim(c) && (close <= 0.2f || s->maneuver != COMBAT_MANEUVER_RETREAT) &&
               !(c->anim && tr_is_attack(c->anim)) && !a->melee.override &&
               (!target || !((const Chr *)target)->player || ui_input_enabled());   /* AI2rAllowedToAttack 0x48bc30 */
    if (!may) s->fire = false;
    if (may || !fought) {
        if (s->targeting) {
            fire_decision(a, c, s->aim, targeted && s->fire, s->lof);
            if (close > 0.0f) return;
            if (send(a, c, MSG_KEEP_AIM, NULL)) return;
        }
        a->aim.look = 0;                    /* AI2rMovement_StopAiming */
    }
    fire(a, c, false);
}

/* ---- the aim ---- */

void combat_aim(AiState *a, Character *c)
{
    AiAim *m = &a->aim;
    /* the movement's look (AI2iMovementState_Calculate_Movement 0x4aa250): the executor's aim, and
       standing, the facing toward it; in combat the weapon's aiming speed counts (0x52a4d8) */
    bool weapon_speed = a->state == AI_STATE_COMBAT || a->state == AI_STATE_PURSUIT || a->state == AI_STATE_ALARM ||
                        a->state == AI_STATE_PANIC || a->state == 10 || a->state == 11;
    float face = 1000.0f;
    switch (m->look) {
    case 0:
        if (m->on) m->stop = true;          /* 0x498da0 */
        break;
    case 1:
        face = wrap_2pi(atan2f(m->look_at[0] - c->pos[0], m->look_at[2] - c->pos[2]));
        m->on = true, m->at_point = true, m->weapon_speed = weapon_speed;
        memcpy(m->at, m->look_at, sizeof m->at);
        break;
    case 2:
        face = wrap_2pi(atan2f(m->look_at[0], m->look_at[2]));
        m->on = true, m->at_point = false, m->weapon_speed = weapon_speed;
        memcpy(m->at, m->look_at, sizeof m->at);
        break;
    case 3:
        if (!m->look_char || !chars_in_use((const Chr *)m->look_char)) {
            if (m->on) m->stop = true;
            break;
        }
        face = wrap_2pi(atan2f(m->look_char->pos[0] - c->pos[0], m->look_char->pos[2] - c->pos[2]));
        m->on = true, m->at_point = true, m->weapon_speed = weapon_speed;
        pelvis(m->look_char, m->at);
        break;
    }
    /* (melee turns the character itself: the original through the executor's overrides, the port's
       melee through its moves) */
    bool melee = a->state == AI_STATE_COMBAT && a->combat.maneuver == COMBAT_MANEUVER_MELEE;
    if (face != 1000.0f && !a->moving && !melee) a->facing = face;

    /* AI2rExecutor_UpdateAim 0x498f50: from 15 over the feet, toward the point (or along the
       direction) at the aiming speed (6 degrees a tick, 0x52a880, by the weapon's +0x7c); more than
       130 degrees off the facing (0x52a888), ahead */
    float from[3] = { c->pos[0], c->pos[1] + 15.0f, c->pos[2] };
    if (!m->on) {
        m->aiming = false;
        m->done = true;
        m->last_facing = c->facing;
    } else {
        float cy, cp;
        if (!m->aiming) cy = c->facing, cp = 0.0f;
        else {
            cy = m->yaw;
            if (m->last_facing != 1000.0f) cy = wrap_2pi(c->facing - m->last_facing + cy);
            cp = wrap_pi(m->pitch);
        }
        float d[3], yaw, pitch, len;
        if (!m->stop) {
            if (m->at_point) for (int j = 0; j < 3; j++) d[j] = m->at[j] - from[j];
            else memcpy(d, m->at, sizeof d);
            yaw = wrap_2pi(atan2f(d[0], d[2]));
            float hh = d[0] * d[0] + d[2] * d[2];
            pitch = atan2f(d[1], sqrtf(hh));
            len = sqrtf(d[1] * d[1] + hh);
            if (fabsf(wrap_pi(yaw - c->facing)) > 2.2689281f) {
                yaw = c->facing;
                d[0] = sinf(c->facing) * 100.0f, d[1] = 0.0f, d[2] = cosf(c->facing) * 100.0f;   /* C+0x178 x 100 */
                pitch = 0.0f;
                len = 100.0f;
            }
        } else {
            yaw = c->facing;
            pitch = 0.0f;
            len = 100.0f;
            d[0] = d[1] = d[2] = 0.0f;
        }
        float dy = wrap_pi(yaw - cy), dp = pitch - cp, mag = sqrtf(dy * dy + dp * dp);
        float rate = m->speed * 0.10471976f;
        if (c->weapon && m->weapon_speed) rate *= rdf(c->weapon->cls + 0x7c);
        if (rate < mag) {
            float k = rate / mag, ny = wrap_2pi(k * dy + cy), np = wrap_2pi(k * dp + cp);
            m->done = false;
            m->aiming = true;
            m->point[0] = sinf(ny) * cosf(np) * len + from[0];
            m->point[1] = sinf(np) * len + from[1];
            m->point[2] = cosf(ny) * cosf(np) * len + from[2];
        } else {
            m->done = true;
            if (!m->stop) {
                m->aiming = true;
                for (int j = 0; j < 3; j++) m->point[j] = d[j] + from[j];
            } else {
                m->stop = m->on = m->aiming = false;
            }
        }
        m->last_facing = c->facing;
    }

    /* DoCharacterFrame 0x4dccf0, a character the AI moves: the aim point against the body (not under a
       film, A+0x21f4 bit 0: the film's input aims) */
    if (film_playing(c)) return;
    float v[3] = { from[0] - m->point[0], from[1] - m->point[1], from[2] - m->point[2] };
    if (fabsf(v[0]) < 1.0f / 1024 && fabsf(v[1]) < 1.0f / 1024 && fabsf(v[2]) < 1.0f / 1024) m->aiming = false;   /* 0x529cc8 */
    else {
        float l = 1.0f / len3(v);
        for (int j = 0; j < 3; j++) v[j] *= l;
    }
    if (!m->aiming) {
        c->aim_lr = c->aim_ud = 0.0f;
        m->yaw = c->facing;
        m->pitch = 0.0f;
        return;
    }
    float yaw = v[0] * v[0] + v[2] * v[2] >= 0.001f ? -(atan2f(v[2], v[0]) + 1.5707964f) : c->facing;
    c->aim_ud = -asinf(v[1]);
    c->aim_lr = wrap_pi(yaw - c->facing);
    m->yaw = wrap_2pi(c->facing + c->aim_lr);
    m->pitch = c->aim_ud;
}
