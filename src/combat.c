/* The AI's combat: see combat.h. */
#include "combat.h"
#include "boss.h"
#include "alarm.h"
#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "film.h"
#include "graph.h"
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
   the short range (+0x64), the pursuit's (+0x68..+0x78), then the alarm's (+0x7c..+0x8c: the find,
   ignore and chase distances, the damage threshold, the fight time). */
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

void combat_default_settings(AiState *a)
{
    /* the defaults: medium 200, short 40, Hold And Fire at every range, unarmed: fight when the
       target is close (3) */
    CombatSettings *s = &a->combat_set;
    s->id = 0;
    s->medium = 200.0f;
    for (int k = 0; k < 5; k++) s->behaviour[k] = 2;
    s->no_gun = 3;
    s->no_gun_else = 0;
    s->near = 40.0f;
    s->alarm_find = 300.0f;
    s->alarm_ignore = 80.0f;
    s->alarm_chase = 0.0f;
    s->alarm_damage = 30;
    s->alarm_fight = 360;
    s->pursuit_distance = 200.0f;
    s->panic_melee = 600;
    s->panic_gunfire = 900;
    s->panic_sight = 600;
    s->panic_hurt = 1200;
}

void combat_settings(AiState *a, uint16_t id)
{
    CombatSettings *s = &a->combat_set;
    const uint8_t *p = NULL;
    for (int i = 0; i < n_profiles && !p; i++)     /* ONrLevel_Combat_ID_To_Combat 0x506b30 */
        if (rd16(profiles[i] + 0x40) == id) p = profiles[i];
    if (!p) {
        combat_default_settings(a);
        return;
    }
    s->id = id;
    s->medium = rdf(p + 0x58);
    for (int k = 0; k < 5; k++) s->behaviour[k] = (int)rd32(p + 0x44 + 4 * k);
    s->no_gun = (int)rd32(p + 0x5c);
    s->no_gun_else = (int)rd32(p + 0x60);
    s->near = rdf(p + 0x64);
    s->alarm_find = rdf(p + 0x7c);
    s->alarm_ignore = rdf(p + 0x80);
    s->alarm_chase = rdf(p + 0x84);
    s->alarm_damage = rd32(p + 0x88);
    s->alarm_fight = rd32(p + 0x8c);
    s->pursuit_distance = rdf(p + 0x68);
    s->panic_melee = rd32(p + 0x6c);
    s->panic_gunfire = rd32(p + 0x70);
    s->panic_sight = rd32(p + 0x74);
    s->panic_hurt = rd32(p + 0x78);
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
static int send_args(AiState *a, const Character *c, int msg, int p5, int p6, const void *ptr, float *arg);

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
    } else {
        s->firing = false;
        a->aim.fire = a->aim.fire_alt = false;
    }
    /* message 8: the trigger, and whether it is the alternate's */
    send_args(a, c, MSG_FIRE, on, s->alt, NULL, NULL);
}


/* The AI's callbacks (0x53ebf8): +0 the trigger (0x490b70), +4 AI2rExecutor_SetAimingSpeed 0x498e80,
   +0x10 the shooter's place, its pelvis (0x490bd0), +0x14 the aim along the vector (0x4a8090), +0x18
   and +0x1c the look at the point or the character (AI2rMovement_LookAtPoint, LookAtCharacter). The
   owner is the character (its Chr). */
static void op_fire(void *o, bool on) { fire(&((Chr *)o)->ai, &((Chr *)o)->c, on); }
static void op_speed(void *o, float speed) { set_aiming_speed(&((Chr *)o)->ai, speed); }
static void op_place(void *o, float out[3]) { shooter_place(&((Chr *)o)->c, out); }

static void op_vector(void *o, const float v[3])
{
    AiState *a = &((Chr *)o)->ai;
    a->aim.look = 2;
    memcpy(a->aim.look_at, v, sizeof a->aim.look_at);
}

static void op_point(void *o, const float p[3])
{
    AiState *a = &((Chr *)o)->ai;
    a->aim.look = 1;
    memcpy(a->aim.look_at, p, sizeof a->aim.look_at);
}

static void op_char(void *o, const Character *c)
{
    AiState *a = &((Chr *)o)->ai;
    a->aim.look = 3;
    a->aim.look_char = c;
}

static const TargetingOps ai_ops = { op_fire, op_speed, NULL, op_place, op_vector, op_point, op_char };

/* AI2rInitializeCharacter's part (0x4ba6d0): the character's targeting, with the class's AI block. */
static AiTargeting *targeting(AiState *a, Character *c)
{
    AiTargeting *t = &a->combat.t;
    if (!t->ops) {
        const uint8_t *cls = (const uint8_t *)c->oncc + ONCC_AI;
        targeting_init(t, 0, &ai_ops, c, NULL, cls, cls + 0x1c);
    }
    return t;
}

/* ---- the behaviours ---- */


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
        /* find a gun (within 30 all of its weight, none at 200), then the fallback */
        s->gun_near = 30.0f;
        s->gun_far = 200.0f;
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
   4 Melee 0x4912d0; the bosses' (5 to 13) in boss.c. */
static int behaviour(AiState *a, Character *c, int id, int msg, bool *handled, int p5, int p6, const void *ptr)
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
        return boss_behaviour(a, c, id, msg, handled, p5, p6, ptr);
    }
}

/* 0x4ecdf0: lying (the states 0x12 and 0x32). */
static bool lying(const Character *c) { return c->state == 0x12 || c->state == 0x32; }

/* AI2rStartDazeTimer 0x48b9f0: the class's daze (ONCC+0x128 to +0x12a, a random part of the span). */
static void start_daze_timer(AiState *a, const Character *c)
{
    const uint8_t *k = c->oncc;
    uint16_t lo = rd16(k + 0x128), hi = rd16(k + 0x12a);
    a->daze_timer = lo;
    if (hi > lo) a->daze_timer = (uint16_t)(lo + (int)((trac_random() * (uint32_t)(hi - lo)) >> 16));   /* UUrRandom */
}

/* AI2rBehavior_Default 0x493120 (messages 0, 2 and 3, the vocalizations and the report, aren't kept). */
static int behaviour_default(AiState *a, const Character *c, int msg, float *arg, const void *ptr)
{
    CombatState *s = &a->combat;
    switch (msg) {
    case MSG_MELEE:
        s->melee_ok = true;
        return 0;
    case MSG_DAZE:
        /* lying: the daze first (once a fall: a new knockdown starts another), lying still with the
           targeting off; after it, the getting up */
        if (!s->dazed) {
            if (!a->daze_started) {
                s->dazed = a->daze_started = true;
                start_daze_timer(a, c);
                return 1;
            }
            s->getup = 0;
            return 0;
        }
        if (!a->daze_timer) {
            s->dazed = false;
            return 0;
        }
        s->targeting = s->check_env = s->check_friends = s->aim = false;
        return 1;
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
    case MSG_HURT: {
        /* a hit by the target: when (+0x84), and when hit hand to hand (+0x88) */
        const KnowEntry *e = ptr;
        if (e && a->target && e->target == a->target) {
            s->hurt_any_time = e->posted;
            if (e->type == KNOW_HURT_MELEE) s->hurt_time = e->posted;
        }
        return 0;
    }
    default:
        return 0;
    }
}

/* A message to the behaviour, then (unhandled) the default's. p5 and p6: the message's arguments (the
   other behaviour for enter and exit, the trigger and the alternate for the fire, the damage and the
   contact for a hit, the new range for a range's change). */
static int send_args(AiState *a, const Character *c, int msg, int p5, int p6, const void *ptr, float *arg)
{
    bool handled = false;
    int r = behaviour(a, (Character *)c, a->combat.behaviour, msg, &handled, p5, p6, ptr);
    if (!handled) r = behaviour_default(a, c, msg, arg, ptr);
    return r;
}

static int send(AiState *a, const Character *c, int msg, float *arg) { return send_args(a, c, msg, 0, 0, NULL, arg); }

void combat_animation(AiState *a, const Character *c)
{
    if (a->state == AI_STATE_COMBAT && a->combat.behaviour) send_args(a, c, MSG_ANIM_END, c->type, 0, c->anim, NULL);
}

void combat_action(AiState *a, const Character *c)
{
    if (a->state == AI_STATE_COMBAT && a->combat.behaviour) send_args(a, c, MSG_ACTION, c->type, 0, NULL, NULL);
}

void combat_hurt(AiState *a, const Character *c, const KnowEntry *e)
{
    if (a->combat.behaviour) send_args(a, c, MSG_HURT, (int)e->value, 0, e, NULL);
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
    int r = s->range;
    int id = r >= 1 && r <= 5 ? a->combat_set.behaviour[r - 1] : 0;
    if (id < 1 || id > 13) id = 1;      /* "illegal behaviour" (the default: Stare) */
    int old = s->behaviour;
    send_args(a, c, MSG_EXIT, id, 0, NULL, NULL);
    s->behaviour = id;
    send_args(a, c, MSG_ENTER, old, 0, NULL, NULL);
}

void combat_enter(AiState *a)
{
    CombatState *s = &a->combat;
    s->maneuver = -1;
    s->gun = NULL;
    s->gun_thwarted = s->gun_below = s->gun_ignored_n = 0;
    memset(s->gun_ignored, 0, sizeof s->gun_ignored);
    s->gun_rolled = s->gun_go = s->run_rolled = s->run_go = false;
    a->flags |= AI_FLAG_COMBAT_SCRIPT;      /* the character's combat script, run by AI2rUpdateAI */
}

void combat_new_target(AiState *a, Character *c, const Character *target)
{
    CombatState *s = &a->combat;
    s->melee_wait = s->backed = s->backed2 = 0;
    s->hurt_time = 0;
    const KnowEntry *e = a->target_entry;
    if (e && e->type == KNOW_HURT_MELEE) s->hurt_time = e->posted;
    targeting_new_target(targeting(a, c), target, true);
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

/* AI2iManeuver_TryPathToGun 0x49e810: a way through the rooms to the gun no longer than *best
   (PHrPathDistance 0x50edb0), then the path there (AI2rPath_GoToPoint, within 8). Its weight falls
   with the way's length, from all of it at the gun's near distance to none at the far one; under the
   next maneuver's, the path halts and it fails. *best: the way's length. */
static bool try_path_to_gun(AiState *a, const Character *c, const void *env, float second, const float to[3], float *best)
{
    CombatState *s = &a->combat;
    if (!env || !graph_for(env)) return false;
    int from = graph_room_at(c->pos), dest = graph_room_at(to);
    if (from < 0 || dest < 0 || !graph_find_path_within(from, c->pos, dest, to, false, NULL, best)) return false;
    ai_move_near(a, to, 8.0f, false, 0.0f);     /* (its failures, through 0x49e370, aren't the stand-in's) */
    float f = (*best - s->gun_near) / (s->gun_far - s->gun_near);
    if (f <= 0.0f) f = 0.0f;
    s->weight[COMBAT_MANEUVER_GUN] *= 1.0f - f;
    if (s->weight[COMBAT_MANEUVER_GUN] < second) {
        ai_halt(a);
        return false;
    }
    return true;
}

static bool gun_ignored(const CombatState *s, const Weapon *w)
{
    for (uint32_t k = 0; k < s->gun_ignored_n; k++)
        if (s->gun_ignored[k] == w) return true;
    return false;
}

/* AI2iManeuver_CheckGun 0x49e390 (second: the next maneuver's weight; arrived: the path's C+0x112c,
   from before the maneuvers' halt). Without a gun to go for: once a target (until the range changes,
   AI2rCombat_Update), the class's chance (ONCC+0x138 +0x164, a percentage) to look for one at all;
   then the weapons within the gun's far distance, nearest first: not one the AI can't holster (ONWC
   +0xcc 2), with ammo or a clip for it (with unlimited ammo, C+0x04 0x40, only those needing none,
   +0xcc 0x800), not one found out of reach; the one with the shortest way to it. With one: lying, it
   gets up (the forward key); running to it with a pickup that runs (B+0x1d4, an animation with TRAM
   flag 20 but not 23), it stops and looks at it; within 3 of its handle, the pickup animation
   (Pickup_Pistol 0x9e, Pickup_Rifle 0x9f), the hand's weapon dropped. Else it goes on to it. Arrived
   (or with A+0x9f0, which nothing writes directly: the port's false), or next to it (under 25 across)
   but not level with it (more than 0.8 of the distance up or down), for 40 maneuvers, or next to it
   and level for 180 without getting within 3, it is out of reach: it
   goes first in the list of four ignored, which copies its first entry over the others as it shifts
   (the original's), and counts at least four from then on. */
static bool try_gun_pickup(AiState *a, Character *c, const void *env, float second, bool arrived)
{
    CombatState *s = &a->combat;
    if (!s->gun || !weapon_free(s->gun)) {
        if (!s->gun_rolled) {
            uint32_t chance = rd32((const uint8_t *)c->oncc + ONCC_AI + 0x164);
            s->gun_rolled = true;
            s->gun_go = trac_random() < (uint16_t)((chance * 0xffffu) / 100u);     /* UUrRandom */
        }
        if (!s->gun_go) {
            s->weight[COMBAT_MANEUVER_GUN] = 0.0f;
            return false;
        }
        Weapon *near[64];
        int n = weapons_nearby(c->pos, s->gun_far, near);
        s->gun = NULL;
        float best = s->gun_far;
        for (int i = 0; i < n; i++) {
            Weapon *w = near[i];
            if (!(c->flags & 0x40)) {
                if (weapon_options(w) & ONWC_NO_HOLSTER) continue;
                if (!weapon_loaded(w) && !chr_has_ammo_for(c, w)) continue;
            } else if (!(weapon_options(w) & ONWC_NO_AMMO))
                continue;
            if (gun_ignored(s, w)) continue;
            float h[3];
            weapon_pickup_position(w, h);
            if (try_path_to_gun(a, c, env, second, h, &best)) s->gun = w;
        }
        if (!s->gun) s->weight[COMBAT_MANEUVER_GUN] = 0.0f;
        if (second <= s->weight[COMBAT_MANEUVER_GUN]) return true;
        ai_halt(a);
        return false;
    }
    if (lying(c)) {
        a->key_override = true;     /* AI2rExecutor_MoveOverride(forward, 0) */
        a->override_keys = CHR_KEY_FORWARD;
        return true;
    }
    float h[3];
    weapon_pickup_position(s->gun, h);
    float d = dist_to(c, h);
    if (s->gun_running && c->anim && tr_test_flag(c->anim, 20) && !tr_test_flag(c->anim, 23)) {
        ai_halt(a);
        a->aim.look = 1;            /* AI2rMovement_LookAtPoint */
        memcpy(a->aim.look_at, h, sizeof h);
        return true;
    }
    if (d < 3.0f) {                 /* 0x52a9a0 */
        if (c->pickup == s->gun) return true;
        ai_halt(a);
        if (!chr_do_animation(c, CHR_ANIM_WHEN_CAN, (weapon_options(s->gun) & ONWC_RIFLE) ? 0x9f : 0x9e)) return true;
        c->pickup = s->gun, c->pickup_powerup = NULL;
        if (c->weapon) chr_drop_weapon(c);
        return true;
    }
    if (!s->gun_running) c->pickup = NULL;
    bool stopped = arrived;         /* (|| A+0x9f0) */
    float dx = h[0] - c->pos[0], dz = h[2] - c->pos[2];
    bool beside = dx * dx + dz * dz < 625.0f;       /* MUrPoint_Distance_SquaredXZ, 0x52a99c */
    if (stopped || (beside && fabsf(h[1] - c->pos[1]) > d * 0.8f))     /* 0x5296d8 */
        s->gun_thwarted++;
    else {
        s->gun_thwarted = 0;
        s->gun_below = beside ? s->gun_below + 1 : 0;
    }
    if (s->gun_thwarted < 40 && s->gun_below < 180) {
        float best = s->gun_far;
        if (try_path_to_gun(a, c, env, second, h, &best)) return true;
        s->weight[COMBAT_MANEUVER_GUN] = 0.0f;
        s->gun = NULL;
        return false;
    }
    /* thwarted: out of reach from now on */
    if (!gun_ignored(s, s->gun)) {
        for (uint32_t k = 0; k < s->gun_ignored_n; k++)
            if (k < 3) s->gun_ignored[k + 1] = s->gun_ignored[k];
        s->gun_ignored[0] = s->gun;
        s->gun_ignored_n = s->gun_ignored_n + 1 < 5 ? 4 : s->gun_ignored_n + 1;
    }
    s->weight[COMBAT_MANEUVER_GUN] = 0.0f;
    s->gun = NULL;
    return false;
}

/* 0x49dbf0: the heaviest maneuver that can be done (a failing one's weight goes, and the next is
   tried). The local paths' tests (AI2rTryLocalMovement, AI2rFindLocalPath, the blockage) are the
   port's stand-ins: the advance runs to the target's point, the retreat backs straight away 15. */
static void select_maneuver(AiState *a, Character *c, const void *env)
{
    CombatState *s = &a->combat;
    bool arrived = !a->moving;      /* the path's C+0x112c, from its last update */
    ai_halt(a);      /* AI2rPath_Halt */
    for (;;) {
        /* the heaviest, and w the next heaviest (what the tried one's weight must still reach) */
        int best = -1;
        float top = 0.0f, w = 0.0f;
        for (int k = 0; k < COMBAT_MANEUVERS; k++) {
            if (s->weight[k] > top) w = top, top = s->weight[k], best = k;
            else if (s->weight[k] > w) w = s->weight[k];
        }
        if (best < 0) {
            best = COMBAT_MANEUVER_HOLD;
            s->weight[best] = 1.0f;
        }
        bool ok = false;
        switch (best) {
        case COMBAT_MANEUVER_HOLD:
            ai_halt(a);
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
        case COMBAT_MANEUVER_GUN:
            ok = try_gun_pickup(a, c, env, w, arrived);
            break;
        case COMBAT_MANEUVER_ALARM:
            /* (a console set beforehand, B+0x1fc, is never set) the nearest alarm console by way */
            s->alarm_marker = alarm_find_console(a, c);
            ok = s->alarm_marker >= 0;
            break;
        case COMBAT_MANEUVER_MELEE:         /* 0x49e910 */
            if (!a->melee.profile || !s->t.target) break;
            {
                /* every fifth tick a countdown (+0x17c) that nothing sets; at 0, within 80 (0x52a9a4) a
                   clear way to the target is a melee */
                MeleeState *m = &a->melee;
                m->env_wait = m->env_wait > 5 ? m->env_wait - 5 : 0;
                if (m->env_wait == 0 && dist_to(c, s->t.target->pos) * dist_to(c, s->t.target->pos) < 6400.0f) {
                    melee_test_local_environment(m, c, s->t.target);
                    if (!m->env_blocked) {
                        ok = true;
                        break;
                    }
                }
                m->env_hold = 60;
            }
            /* too far, or no way: advance to within 15 to 20 of it */
            ai_halt(a);
            s->targeting = true;
            s->aim = false;
            float h = s->weight[COMBAT_MANEUVER_HOLD];
            s->weight[COMBAT_MANEUVER_MELEE] = 0.0f;
            s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
            s->adv_far = 20.0f;
            s->adv_near = 15.0f;
            s->weight[COMBAT_MANEUVER_HOLD] = h >= 0.2f ? h : 0.2f;
            continue;
        case COMBAT_MANEUVER_GETUP:
            /* lying: forward, which gets it up */
            if (lying(c)) {
                a->key_override = true;     /* AI2rExecutor_MoveOverride(forward, 0) */
                a->override_keys = CHR_KEY_FORWARD;
                ok = true;
                break;
            }
            s->weight[best] = 0.0f;
            continue;
        default:
            break;
        }
        if (ok) {
            s->maneuver = best;
            if (best == COMBAT_MANEUVER_ALARM) alarm_setup(a, c, s->alarm_marker);     /* AI2rAlarm_Setup */
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
    targeting(a, c);
    if (s->t.target != target && !target) s->t.target = NULL;
    /* the weapon (or the firing mode) changed: the targeting's (its matrix: the body's node 19) */
    const Weapon *w = c->weapon;
    if (s->weapon != (const void *)w || s->weapon_alt != s->alt) {
        targeting_set_weapon(&s->t, w ? w->cls + (s->alt ? 0x150 : 0xd0) : NULL);
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
        targeting_update(&s->t, has_target, s->aim, s->fire, &close);
    }
    /* below the height a behaviour watches (+0x5d8, +0x5dc: the super ninja's), message 13: taken, the
       tick ends there */
    if (s->fall_check && c->pos[1] < s->fall_y && s->behaviour && send(a, c, MSG_FELL, NULL)) return;
    s->getup = 4;
    /* lying: message 6 to a behaviour; dazed, the character stays down, still and not firing */
    if (lying(c) && s->behaviour && send(a, c, MSG_DAZE, NULL)) {
        ai_halt(a);                  /* AI2rPath_Halt */
        a->aim.look = 0;                    /* AI2rMovement_StopAiming */
        fire(a, c, false);                  /* AI2iCombat_Fire 0x490a50 */
        return;
    }
    bool waiting = a->combat_wait != 0 && a->combat_wait_reload == 0;
    /* every fifth tick, by the character's index: the range, the line of fire and the maneuver */
    if ((uint32_t)(c->id - 1) % 5 == ui_time() % 5) {
        int r = get_range(a, c, s->range);
        /* (message 15: a behaviour may keep the range it has) */
        if (r != s->range && !send_args(a, c, MSG_RANGE, r, 0, NULL, NULL)) {
            /* from none or short, or farther from long or medium, closer from the retreats: the gun's
               chance is drawn again */
            int old = s->range;
            if (old == 0 || old == 3 || (old < 3 ? r < old : old < r)) s->gun_rolled = s->gun_go = false;
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
        select_maneuver(a, c, env);
        if (a->state != AI_STATE_COMBAT) return;
        /* going for a gun: once, the class's chance (ONCC+0x138 +0x168) to pick it up running
           (AI2rManeuver_TryRunningGunPickup 0x49f020, not ported: it fails) */
        s->gun_running = false;
        if (s->maneuver == COMBAT_MANEUVER_GUN && s->gun) {
            if (!s->run_rolled) {
                uint32_t chance = rd32((const uint8_t *)c->oncc + ONCC_AI + 0x168);
                s->run_rolled = true;
                s->run_go = trac_random() < (uint16_t)((chance * 0xffffu) / 100u);     /* UUrRandom */
            }
        } else {
            s->run_rolled = s->run_go = false;
            s->gun_thwarted = 0;
        }
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
    }
    /* an alarm run left for the fight: a target within the chase distance keeps it fighting for the
       fight time; one beyond the ignore distance sends it back to the alarm (AI2rAlarm_Setup) */
    if (a->alarm_marker >= 0) {
        if (s->dist < a->combat_set.alarm_chase) a->alarm_fight_timer = a->combat_set.alarm_fight;
        else if (s->dist > a->combat_set.alarm_ignore) {
            alarm_setup(a, c, -1);
            return;
        }
    }
    /* the trigger */
    bool may = !fought && !special_anim(c) && (close <= 0.2f || s->maneuver != COMBAT_MANEUVER_RETREAT) &&
               !(c->anim && tr_is_attack(c->anim)) && !a->melee.override &&
               (!target || !((const Chr *)target)->player || ui_input_enabled());   /* AI2rAllowedToAttack 0x48bc30 */
    if (!may) s->fire = false;
    if (may || !fought) {
        if (s->targeting) {
            targeting_fire_decision(&s->t, s->aim, targeted && s->fire, s->lof);
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
    /* a glance under way (no delay left) takes the look's place (0x4aa250) */
    bool glancing = a->glance_ticks && !a->glance_delay;
    int look = glancing ? a->glance_kind : m->look;
    const float *look_at = glancing ? a->glance_at : m->look_at;
    switch (look) {
    case 0:
        if (m->on) m->stop = true;          /* 0x498da0 */
        break;
    case 1:
        face = wrap_2pi(atan2f(look_at[0] - c->pos[0], look_at[2] - c->pos[2]));
        m->on = true, m->at_point = true, m->weapon_speed = weapon_speed;
        memcpy(m->at, look_at, sizeof m->at);
        break;
    case 2:
        face = wrap_2pi(atan2f(look_at[0], look_at[2]));
        m->on = true, m->at_point = false, m->weapon_speed = weapon_speed;
        memcpy(m->at, look_at, sizeof m->at);
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
    if (face != 1000.0f && !a->moving) a->facing = face;

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
