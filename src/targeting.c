/* The AI's targeting: see targeting.h. */
#include "targeting.h"
#include "trac.h"

#include <math.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f;     /* 0x529548 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static float len3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

/* A character's place for the aim: its pelvis (ONrCharacter_GetPelvisPosition, the feet and the
   class's pelvis height). */
static void pelvis(const Character *c, float out[3])
{
    out[0] = c->pos[0];
    out[1] = c->pos[1] + c->pelvis_height;
    out[2] = c->pos[2];
}

/* The callback +4, the aiming speed (none for some owners). */
static void aiming_speed(AiTargeting *t, float speed)
{
    if (t->ops->aiming_speed) t->ops->aiming_speed(t->owner, speed);
}

void targeting_init(AiTargeting *t, int axis, const TargetingOps *ops, void *owner, const uint8_t *params,
                    const uint8_t *cls, const uint8_t *skills)
{
    t->owner = owner;
    t->axis = axis;
    t->ops = ops;
    t->cls = cls;
    t->skills = skills;
    t->mode = 0;
    t->count = 0;
    t->has_point = false;
    t->aim_dist = t->dist = 0.0f;
    targeting_set_weapon(t, params);
}

void targeting_stop(AiTargeting *t)
{
    aiming_speed(t, 1.0f);
    if (t->mode && t->ops->stop_aiming) t->ops->stop_aiming(t->owner);     /* the callback +0xc */
    t->count = 0;       /* the ring freed */
}


/* 0x4bb700: the firing mode (NULL none); the ring of the target's places starts again. */
void targeting_set_weapon(AiTargeting *t, const uint8_t *params)
{
    t->params = params;
    if (!params) {
        t->active = false;
        t->skill = NULL;
        aiming_speed(t, 1.0f);
        return;
    }
    t->active = true;
    t->skill = t->skills + 0x18u * rd16(params + 0x66);
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
void targeting_new_target(AiTargeting *t, const Character *target, bool inaccurate)
{
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
        t->ops->place(t->owner, t->from);
        float d[3] = { t->point[0] - t->from[0], t->point[1] - t->from[1], t->point[2] - t->from[2] };
        t->dist = t->aim_dist = len3(d);
        t->has_point = true;
        if (inaccurate && t->params && !(rd32(t->params) & 1)) {
            float e = t->dist * rdf(t->cls);
            t->inaccurate = true;
            t->inaccuracy = e > rdf(t->cls + 4) ? rdf(t->cls + 4) : e;
        }
    }
    aiming_speed(t, 0.5f);
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
static void update_targeting(AiTargeting *t, bool aim)
{
    t->ballistic = true;
    bool on = aim && t->params;
    int mode = on ? 1 : 2;
    if (t->mode != mode) t->mode = mode;    /* the callback +8 (0x490ba0, the movement's weapon aim): not kept */
    if (!on) {
        t->aim_dist = t->dist;
        if (!t->target) {
            if (t->ops->look_point) t->ops->look_point(t->owner, t->aim);       /* the callback +0x18 */
        } else {
            if (t->ops->look_char) t->ops->look_char(t->owner, t->target);      /* +0x1c */
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
    if (t->ops->aim_vector) t->ops->aim_vector(t->owner, t->vector);   /* the callback +0x14 */
}

/* AI2rTargeting_Update 0x4ba720. *close: how close the target is within the weapon's closest (0 none). */
void targeting_update(AiTargeting *t, bool has_target, bool aim, bool fire, float *close)
{
    t->on_target = false;
    t->active = has_target && t->params;
    if (!t->target) {
        if (!t->has_point) aim = false;
        else memcpy(t->aim, t->point, sizeof t->aim);
    } else {
        t->ops->place(t->owner, t->from);
        update_prediction(t);
        for (int j = 0; j < 3; j++) t->aim[j] = t->point[j] + (fire ? t->offset[j] : 0.0f);
    }
    update_targeting(t, has_target);
    if (close) {
        *close = 0.0f;
        if (t->params && aim && t->dist < rdf(t->params + 0x5c)) *close = 1.0f - t->dist / rdf(t->params + 0x5c);
    }
}


/* 0x4ba830: whether to fire: the aim against the barrel (in the barrel's space, the miss across it),
   within the tolerance (the firing mode's, grown by the grouping, the first shots' error, the time off
   target, and close up the skill's), after the delay; far off, faster aiming; point blank, fire. */
void targeting_fire_decision(AiTargeting *t, bool aim, bool want, bool lof)
{
    const uint8_t *p = t->params;
    if (!p || !aim || !want || !lof) {
        t->stopped = true;
        t->fire = false;
        t->ops->fire(t->owner, t->fire);
        return;
    }
    if (t->stopped) {
        t->first = true;
        t->close_ticks = 0;
    }
    t->stopped = false;
    if (t->delay && --t->delay) {
        t->fire = false;
        t->ops->fire(t->owner, t->fire);
        return;
    }
    if (t->aim_dist >= 10.0f) t->close_ticks = 0;  /* 0x52a190 */
    else if (++t->close_ticks > 9) {
        t->first = false;
        t->fire = true;
        t->ops->fire(t->owner, t->fire);
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
        aiming_speed(t, 1.0f);
        t->fire = false;
        t->ops->fire(t->owner, t->fire);
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
        if (tol * 5.0f <= across) aiming_speed(t, 1.0f);   /* 0x52956c */
        t->first = !t->inaccurate;
        if (wide) {
            unsigned lo = rd16(t->skill + 0x14), hi = rd16(t->skill + 0x16);
            t->delay = (lo + (hi > lo ? (unsigned)(((uint32_t)trac_random() * (hi - lo)) >> 16) : 0)) & 0xffff;
        }
        t->off_ticks++;
        t->fire = false;
    }
    t->ops->fire(t->owner, t->fire);
}

