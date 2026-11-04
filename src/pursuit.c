/* The AI's pursuit: see pursuit.h. */
#include "pursuit.h"

#include "akev.h"
#include "alarm.h"
#include "chars.h"
#include "exe.h"
#include "graph.h"
#include "knowledge.h"
#include "trac.h"
#include "ui.h"

#include <math.h>
#include <string.h>

static const float PI = 3.14159265f, TWO_PI = 6.28318531f;

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

static uint32_t oncc_u32(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The class's wait before giving up a lost target (ONCC+0x290, the combat block's +0x158). */
enum { ONCC_COMBAT_WAIT = 0x138 + 0x158 };

/* base plus a random part of n (UUrRandom: n * r >> 16), as the original's 16 bits */
static uint32_t random_ticks(uint32_t base, uint32_t n)
{
    uint16_t r = n ? (uint16_t)(((uint32_t)trac_random() * n) >> 16) : 0;
    return (uint16_t)(base + r);
}

static void stop_aiming(AiState *a) { a->aim.look = 0; }        /* AI2rMovement_StopAiming 0x4a8050 */


/* 0x4a8090: the movement looks along a direction. */
static void look_along(AiState *a, const float v[3])
{
    a->aim.look = 2;
    memcpy(a->aim.look_at, v, sizeof a->aim.look_at);
}

/* ---- the guard's scan ---- */

/* 0x499970: a new scan, its first look at angle. */
static void scan_set(AiScan *s, float angle)
{
    s->started = false;
    s->first = angle;
    s->hold = 0;
    s->sweep_timer = 0;
    s->sweep = 0.0f;
    s->on = false;
}

/* 0x499990: the next look, 30 along it. The first is the scan's first angle (1000: the facing); then a
   random one about the centre (all round when it is 1000) at least 40 degrees (0x52a8a0) from the last;
   the way along it must be clear of the environment (flags 0x42080) from the head. Six tries each. */
static void scan_pick(AiScan *s, const Character *c, float range, const void *env)
{
    float eye[3], last = 1000.0f;
    knowledge_head_point((const struct Chr *)c, eye);
    bool first = !s->started;
    if (s->started) last = s->angle;    /* (+0x15 set leaves the last one unset: nothing sets it) */
    for (unsigned tries = 0;;) {
        float a;
        if (first) {
            a = s->first == 1000.0f ? c->facing : s->first;
            first = false;
        } else if (s->center == 1000.0f) {
            a = (float)trac_random() * (1.0f / 65535.0f) * TWO_PI;
        } else {
            a = (range + range) * (float)trac_random() * (1.0f / 65535.0f) - range + s->center;
        }
        if (tries < 6) {
            float d = a - last;
            if (d < -PI) d += TWO_PI;
            else if (d > PI) d -= TWO_PI;
            if (fabsf(d) < 0.6981317f) {
                tries++;
                continue;
            }
        }
        a = a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a;
        s->v[0] = sinf(a) * 30.0f;
        s->v[1] = 0.0f;
        s->v[2] = cosf(a) * 30.0f;
        if (tries > 5 || !env || !akev_segment_hits(env, eye, s->v, 0x42080)) {
            s->started = true;
            s->again = false;
            s->angle = a;
            return;
        }
        tries++;
    }
}

/* 0x499d00: the look turned by the sweep about y, its height kept. */
static void scan_sweep(AiScan *s)
{
    float len = sqrtf(s->v[2] * s->v[2] + s->v[0] * s->v[0]);
    float a = atan2f(s->v[0], s->v[2]);
    if (a < 0.0f) a += TWO_PI;
    a = wrap_2pi(a + s->sweep);
    s->look[1] = s->v[1];
    s->look[0] = sinf(a) * len;
    s->look[2] = cosf(a) * len;
}

/* AI2rGuard_UpdateScan 0x499b50: a new look when the last one's hold is out (about the facing, or all
   round), and a new sweep offset (within its range) when its timer is out; the movement looks along
   the result. */
static void scan_update(AiState *a, const Character *c, const void *env)
{
    AiScan *s = &a->pursuit.scan;
    bool picked = false;
    if (!s->started || s->hold == 0) {
        s->center = !s->on ? 1000.0f : s->face == 1000.0f ? c->facing : s->face;
        scan_pick(s, c, s->on ? s->range : 1000.0f, env);
        picked = true;
        s->hold = random_ticks(s->hold_base, s->hold_rand);
        s->sweep_timer = random_ticks(s->sweep_base, s->sweep_rand);
    } else {
        s->hold--;
    }
    if (s->sweep_range > 0.0f) {
        if (s->sweep_timer) {
            s->sweep_timer--;
            if (!picked) goto aim;
        } else {
            float r = (float)trac_random();
            s->sweep = (r * (1.0f / 65535.0f) + r * (1.0f / 65535.0f) - 1.0f) * s->sweep_range;
            s->sweep_timer = random_ticks(s->sweep_base, s->sweep_rand);
        }
    } else if (!picked) {
        goto aim;
    }
    if (s->sweep_range == 0.0f) memcpy(s->look, s->v, sizeof s->look);
    else scan_sweep(s);
aim:
    look_along(a, s->look);
}

/* ---- the modes ---- */

/* AI2iPursuit_DesirePursuit 0x4b1620: whether to go after the contact. Not with an unhostile contact
   (but on entering), nor below the least alert that pursues; with a pursuit distance, the contact must
   be within it by way (why 0 and 1), or (2 and 3) the character still within it of its job's place. */
static bool desire(AiState *a, const Character *c, int why)
{
    const KnowEntry *e = a->target_entry;
    if (!e) return false;
    if (why != 0 && e->hostility == 0) return false;
    if (a->alert < a->pursue_alert) return false;
    float limit = a->combat_set.pursuit_distance;
    if (limit == 0.0f) return true;
    int from = graph_room_at(c->pos);
    if (why != 2 && why != 3) {
        /* PHrPathDistance 0x50edb0: a room unknown fails it */
        int to = graph_room_at(e->pos);
        return from >= 0 && to >= 0 && graph_find_path_within(from, c->pos, to, e->pos, false, NULL, &limit) != 0;
    }
    if (!a->job_pos_set) return false;
    float d = 0.0f;
    int to = graph_room_at(a->job_pos);
    if (from >= 0 && to >= 0 && graph_find_path_within(from, c->pos, to, a->job_pos, false, NULL, &d) != 0 && limit <= d)
        return false;
    return true;
}

/* AI2iPursuit_ChangeMode 0x4b1260: the mode for the contact's strength, unless the mode's timer runs.
   Strength 0 (its character dead) starts the class's wait; 1 (forgotten) forgets it, unless lost
   contacts are kept looking for; 2 the weak mode, 3 and 4 the strong (4 noted), by the alert. A
   pursuit going to the contact's place keeps going, but to forget. Returns whether it changed. */
static bool change_mode(AiState *a, const Character *c)
{
    PursuitState *p = &a->pursuit;
    const KnowEntry *e = a->target_entry;
    if (p->timer) return false;
    if (!e) return false;
    int cur = p->mode, mode = PURSUIT_FORGET;
    bool forced = false;
    if (cur == PURSUIT_FORGET) {
        forced = true;
    } else {
        switch (e->strength) {
        case 0:
            p->timer = oncc_u32(c, ONCC_COMBAT_WAIT);
            return true;
        case 1:
            if (a->pursue_lost == 1) return false;
            forced = true;
            break;
        case 2:
            mode = a->alert < 2 ? a->pursue_weak_low : a->pursue_weak_high;
            break;
        case 4:
            p->strong = true;
            /* fall through */
        default:
            mode = a->alert < 2 ? a->pursue_strong_low : a->pursue_strong_high;
            break;
        }
        if (!forced) {
            if (mode == PURSUIT_NONE) return false;
            if (mode != PURSUIT_FORGET && mode == cur) return false;
        }
    }
    if (cur == PURSUIT_GOTO && !forced) return false;
    p->mode = mode;
    return true;
}

static void apply_mode(AiState *a, Character *c);

/* 0x4b1570: the mode's end. Going to the place: a glance (80 ticks) at an unhostile contact seen, else
   a look around (600); after the look, on if it still wants to, else forget; after a glance, go if it
   wants to, else forget; any other: wait. */
static void next_mode(AiState *a, Character *c)
{
    PursuitState *p = &a->pursuit;
    const KnowEntry *e = a->target_entry;
    int was = p->mode;
    if (was == PURSUIT_GOTO) {
        if (e && e->hostility == 0 && e->strength == 4) {
            p->mode = PURSUIT_GLANCE;
            p->timer = 80;
        } else {
            p->mode = PURSUIT_LOOK;
            p->timer = 600;
        }
    } else if (was == PURSUIT_LOOK) {
        if (!desire(a, c, 2)) p->mode = PURSUIT_FORGET;
        p->timer = 0;
        change_mode(a, c);
    } else if (was == PURSUIT_GLANCE) {
        p->mode = desire(a, c, 1) ? PURSUIT_GOTO : PURSUIT_FORGET;
    } else {
        p->timer = 0;
        p->mode = PURSUIT_WAIT;
    }
    if (p->mode != was) apply_mode(a, c);
}

/* 0x4b1350: the mode begins (unless held still, but to forget): the path halted, no aim, then
   forgetting goes back to the job (or, by the lost setting, stays, or runs to the alarm for a contact
   that was seen); going runs a path to the contact's place (within 12); looking and glancing begin
   their scans. Move and hunt are "not yet implemented" in the original: nothing. */
static void apply_mode(AiState *a, Character *c)
{
    PursuitState *p = &a->pursuit;
    AiScan *s = &p->scan;
    const KnowEntry *e = a->target_entry;
    if (p->halt != 0 && p->mode != PURSUIT_FORGET) return;
    ai_halt(a);
    stop_aiming(a);
    switch (p->mode) {
    case PURSUIT_FORGET:
        if (a->pursue_lost < 0) return;
        if (a->pursue_lost == 2 && p->strong) {
            int m = a->alarm_marker >= 0 ? -1 : alarm_find_console(a, c);
            if (a->alarm_marker >= 0 || m >= 0) {
                alarm_setup(a, c, m);
                return;
            }
        }
        ai_return_to_job(a);
        return;
    case PURSUIT_GOTO:
        p->start = 0;
        p->close = 0;
        p->run = p->alerted;
        p->looked = false;
        if (e) ai_move_near(a, e->pos, 12.0f, false, 0.0f);    /* AI2rPath_GoToPoint */
        return;
    case PURSUIT_LOOK:
        if (!p->looked && e) {
            scan_set(s, atan2f(e->pos[0] - c->pos[0], e->pos[2] - c->pos[2]));
            p->looked = true;
        }
        s->on = false;
        s->hold_base = 100, s->hold_rand = 100;
        s->sweep_range = 0.7853982f;
        s->sweep_base = 40, s->sweep_rand = 40;
        return;
    case PURSUIT_GLANCE: {
        if (!e) return;
        float d[3] = { e->pos[0] - c->pos[0], e->pos[1] - c->pos[1], e->pos[2] - c->pos[2] };
        p->looked = false;
        float angle = atan2f(d[0], d[2]);
        scan_set(s, angle);
        s->angle = angle;
        memcpy(s->v, d, sizeof s->v);
        memcpy(s->look, d, sizeof s->look);
        s->sweep = 0.0f;
        s->sweep_range = 0.7853982f;
        s->hold_base = 0, s->hold_rand = 0;
        s->sweep_base = 40, s->sweep_rand = 40;
        s->hold = 10000;
        s->sweep_timer = 80;
        s->started = true;
        s->again = false;
        look_along(a, s->look);
        return;
    }
    default:
        return;
    }
}

/* ---- the state ---- */

/* AI2rPursuit_Enter 0x4b0ea0 */
void pursuit_enter(AiState *a, Character *c)
{
    PursuitState *p = &a->pursuit;
    const KnowEntry *e = a->target_entry;
    p->strong = e && e->highest > 3;
    p->halt = 0;
    p->timer = 0;
    p->start = 0;
    p->reached = false;
    p->alerted = a->alert > 1;
    a->aim.speed = 0.5f;        /* AI2rExecutor_SetAimingSpeed */
    if (p->alerted && desire(a, c, 0)) {
        p->mode = PURSUIT_GOTO;
    } else {
        p->mode = PURSUIT_GLANCE;
        p->timer = e && e->type >= 0 && e->type <= 9 ? exe_u32(0x52a64c + 4u * (uint32_t)e->type) : 0;     /* 0x48ecb0 */
        if (!p->alerted) a->glance = p->timer;
    }
    p->vocal = 120;
    change_mode(a, c);
    apply_mode(a, c);
}

/* AI2rPursuit_Exit 0x4b0f50 */
void pursuit_exit(AiState *a)
{
    PursuitState *p = &a->pursuit;
    a->aim.speed = 1.0f;
    p->halt = 0;
    p->mode = PURSUIT_NONE;
    p->timer = 0;
    ai_halt(a);                    /* the glance and the facing held aren't kept by the port */
    stop_aiming(a);
}

/* 0x4b0f90 */
bool pursuit_hurrying(const AiState *a) { return a->pursuit.mode == PURSUIT_GOTO && a->pursuit.run; }

static bool lying(const Character *c) { return c->state == 0x12 || c->state == 0x32; }  /* 0x4e4070 */

/* AI2rPursuit_Update 0x4b0fb0 */
void pursuit_update(AiState *a, Character *c, const void *env)
{
    PursuitState *p = &a->pursuit;
    KnowEntry *e = a->target_entry;
    if (lying(c)) {
        a->key_override = true;     /* AI2rExecutor_MoveOverride(forward): up */
        a->override_keys = CHR_KEY_FORWARD;
    }
    if (p->halt) {
        if (--p->halt) {
            ai_halt(a);
            return;
        }
        apply_mode(a, c);
        if (a->state != AI_STATE_PURSUIT) return;
    }
    if (p->timer && --p->timer == 0) next_mode(a, c);
    if (a->state != AI_STATE_PURSUIT) return;
    if (p->mode >= PURSUIT_WAIT && p->mode <= PURSUIT_HUNT && p->vocal && --p->vocal == 0) {
        /* AI2rVocalize 0x48ba60 (4): with the vocalizations */
        p->vocal = random_ticks(720, 180);
    }
    if (p->mode == PURSUIT_LOOK || p->mode == PURSUIT_GLANCE) {
        scan_update(a, c, env);
        return;
    }
    if (p->mode != PURSUIT_GOTO || !e) return;
    if (e->degrade_at) e->degrade_at++;     /* the contact doesn't fade while it is gone after */
    /* the path's C+0x113c (0x4af030): the squared distance to its goal, the height ignored within 22.5 */
    float dx = a->goal[0] - c->pos[0], dy = a->goal[1] - c->pos[1], dz = a->goal[2] - c->pos[2];
    if (fabsf(dy) < 22.5f) dy = 0.0f;
    float d2 = dx * dx + dy * dy + dz * dz;
    bool look = false;
    if (a->moving && d2 >= 144.0f) {
        if (d2 > 40000.0f) {
            /* far: on its way, hurrying if alerted */
            p->run = p->alerted;
            p->looked = false;
            stop_aiming(a);
            return;
        }
        if (ui_time() < p->start + 15) {
            p->close = 0;
            p->looked = false;
            stop_aiming(a);
            return;
        }
        float eye[3];
        knowledge_head_point((const struct Chr *)c, eye);
        float v[3] = { e->pos[0] - eye[0], e->pos[1] - eye[1], e->pos[2] - eye[2] };
        if (env && akev_segment_hits(env, eye, v, 0x42080)) {
            p->close = 0;
            p->looked = false;
            stop_aiming(a);
            return;
        }
        look = true;
        if (d2 >= 3600.0f) {
            /* in sight: it looks at the place, hurrying while it hasn't reached it once */
            p->run = !p->reached && p->alerted;
        } else if (++p->close >= 6) {
            look = false;
        }
        if (look) goto scan;
    }
    /* there (or within 60 and in sight for 6 ticks): the mode's end */
    p->reached = true;
    p->run = false;
    p->looked = false;
    next_mode(a, c);
    if (a->state != AI_STATE_PURSUIT) return;
    /* (the original tests a flag here that only the in-sight way sets: arriving, it is the stack's
       leftover; the port takes it as clear) */
    if (!look) {
        p->looked = false;
        stop_aiming(a);
        return;
    }
scan:
    if (!p->looked) {
        scan_set(&p->scan, atan2f(e->pos[0] - c->pos[0], e->pos[2] - c->pos[2]));
        p->looked = true;
        p->scan.on = true;
        p->scan.face = c->facing;
        p->scan.range = 1.2217305f;
        p->scan.hold = 0;
        p->scan.hold_base = 40, p->scan.hold_rand = 40;
        p->scan.sweep_range = 0.0f;
        p->scan.sweep_timer = 0;
    }
    scan_update(a, c, env);
}

/* ---- the knowledge ---- */

/* 0x4b1750 */
void pursuit_notify(Chr *ch, KnowEntry *e, bool degraded)
{
    AiState *a = &ch->ai;
    Character *c = &ch->c;
    knowledge_alert_notify(ch, e, degraded);    /* AI2rAlert_NotifyKnowledge: it may go to combat */
    if (a->state != AI_STATE_PURSUIT) return;
    PursuitState *p = &a->pursuit;
    p->alerted = a->alert > 1;
    if (degraded && e == a->target_entry) {
        if (a->alarm_marker >= 0 && e->strength < 3) {
            alarm_setup(a, c, -1);
            return;
        }
        if (change_mode(a, c)) {
            apply_mode(a, c);
            if (a->state != AI_STATE_PURSUIT) return;
        }
    }
    KnowEntry *top = a->know;
    if (top) {
        if (top == a->target_entry) {
            if (degraded) return;
        } else {
            if (top->hostility < 1) return;
            a->target_entry = top;
            a->target = top->target;
        }
    }
    if (desire(a, c, 3)) {
        p->mode = PURSUIT_GOTO;
        apply_mode(a, c);
    }
}

/* 0x4b1830 */
void pursuit_forgotten(Chr *ch, KnowEntry *e)
{
    AiState *a = &ch->ai;
    Character *c = &ch->c;
    if (a->target_entry != e) return;
    if (a->alarm_marker >= 0 && e->strength < 3) {
        alarm_setup(a, c, -1);
        return;
    }
    for (KnowEntry *k = a->know; k; k = k->next) {
        if (k == e) continue;
        a->target_entry = k;
        a->target = k->target;
        if (desire(a, c, 3)) {
            a->pursuit.mode = PURSUIT_GOTO;
            apply_mode(a, c);
            return;
        }
    }
    a->pursuit.mode = PURSUIT_FORGET;
    apply_mode(a, c);
}
