/* The AI: see ai.h. */
#include "ai.h"
#include "chars.h"
#include "combat.h"
#include "door.h"
#include "exe.h"
#include "alarm.h"
#include "graph.h"
#include "localpath.h"
#include "knowledge.h"
#include "neutral.h"
#include "panic.h"
#include "melee.h"
#include "movement.h"
#include "patrol.h"
#include "pursuit.h"
#include "chr_anim.h"
#include "trac.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

static float oncc_float(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    float f;
    memcpy(&f, &v, 4);
    return f;
}

bool ai_all_passive;

void ai_init(AiState *a)
{
    memset(a, 0, sizeof *a);
    if (ai_all_passive) a->flags = AI_FLAG_KNOWLEDGE | AI_FLAG_PASSIVE;     /* AI2rInitializeCharacter 0x48a925 */
    a->kind = AI_MOVE_NONE;
    a->facing_override = 1000.0f;
    a->patrol = -1;
    a->job_patrol = -1;
    a->neutral_id = -1;
    a->neutral_enemy = 100.0f;      /* AI2rNeutral_ChangeBehavior 0x4ac870 without one */
    a->local_seg = -1;
    a->avoid = 1000.0f;
    a->arrived = a->turned = true;
    movement_init(&a->move);
    a->aim.speed = 1.0f;
    a->aim.last_facing = 1000.0f;
    a->alarm_marker = a->alarm_last = -1;
    /* AI2rInitializeCharacter 0x48a6b0's, without a CHAR */
    a->pursue_alert = 2;
    a->pursue_strong_low = PURSUIT_MOVE, a->pursue_weak_low = PURSUIT_FORGET;
    a->pursue_strong_high = PURSUIT_HUNT, a->pursue_weak_high = PURSUIT_LOOK;
    a->pursue_lost = 0;
}

void ai_halt(AiState *a)
{
    a->path_type = 0;
    a->follow = NULL;
    a->moving = false;
    a->goal_facing = false;
    /* 0x4a8440 */
    a->nmod = 0;
    a->avoid = 1000.0f;
    a->move.counted = 0;
}

void ai_executor_move(AiState *a, float facing, int mode, int kind, bool full_turn)
{
    a->full_turn = full_turn;
    a->facing = facing;
    a->mode = mode;
    a->kind = kind;
}

bool ai_movement_modifier(AiState *a, float dir, float weight)
{
    if (a->nmod > 7) return false;      /* (0x4936b0: a report) */
    a->mod_dir[a->nmod] = dir;
    a->mod_weight[a->nmod] = a->mod_left[a->nmod] = weight;
    a->nmod++;
    /* 0x4ab570: counted; its weight worked out again with confirmed collisions or a direction to keep
       away from */
    a->move.counted++;
    if (a->move.nconfirmed || a->avoid != 1000.0f) a->move.reweigh = true;
    return true;
}

void ai_movement_avoid(AiState *a, float dir)
{
    a->avoid = dir;
    a->move.reweigh = true;
}

/* AI2rIsHurrying 0x48b020: an AI hurries in the states the table at 0x52a4d8 doesn't mark calm (combat,
   alarm, panic), and in the pursuit while it hurries to the contact's place. */
static bool hurrying(const AiState *a)
{
    if (a->state < 0 || a->state > 11) return true;
    if (exe_data(0x52a4d8 + (uint32_t)a->state, 1)[0]) return false;
    if (a->state == AI_STATE_PURSUIT) return pursuit_hurrying(a);
    return true;
}

/* The movement's default mode for the alert level (0x4a8d00): the tables at 0x52af18 (hurrying) and
   0x52af2c (not); run for a character the AI doesn't own (C+0x1e8 not 2: the player). */
int ai_default_mode(const AiState *a, bool player)
{
    if (player) return AI_MODE_RUN_AIMING;
    uint32_t table = hurrying(a) ? 0x52af18 : 0x52af2c;
    int level = a->alert >= 0 && a->alert < 5 ? a->alert : 0;   /* else "illegal alert status" */
    return (int)exe_u32(table + 4u * (uint32_t)level);
}

/* AI2rExecutor_Update 0x498550, with the keys' override (A+0x1ab9) and the attack override (A+0x1acc);
   the aim is AI2rExecutor_UpdateAim 0x498f50 (combat.c). */
static void executor_update(AiState *a, Character *c, ChrInput *in)
{
    int mode = a->mode ? a->mode : a->movement_mode ? a->movement_mode : ai_default_mode(a, false);
    uint64_t held = 0;
    if (a->key_override) {
        /* the override's keys instead of the move's: running, or crouching with the crouch key (its
           flag 1, walking, has no user here; the aiming forms, A+0x1a88, aren't kept) */
        held = a->override_keys;
        mode = (held & CHR_KEY_CROUCH) ? AI_MODE_CROUCH : AI_MODE_RUN;
        a->key_override = false;
    } else
        switch (a->kind) {
        case AI_MOVE_FORWARD: held = CHR_KEY_FORWARD; break;
        case AI_MOVE_BACK: held = CHR_KEY_BACK; break;
        case AI_MOVE_LEFT: held = CHR_KEY_STEP_LEFT; break;
        case AI_MOVE_RIGHT: held = CHR_KEY_STEP_RIGHT; break;
        default: break;
        }
    /* the trigger (A+0x21e0, 0x21e1: the weapon's input reads them as the fire keys) */
    if (a->aim.fire) held |= CHR_KEY_FIRE;
    if (a->aim.fire_alt) held |= CHR_KEY_ALT_FIRE;
    if (mode == AI_MODE_CROUCH) held |= CHR_KEY_CROUCH;
    else if (mode == AI_MODE_WALK || mode == AI_MODE_WALK_AIMING) held |= CHR_KEY_WALK;
    /* the attack override: in melee its keys join the tick's (held, and pressed), and with no attack
       to follow, the combo's memory (A+0x1eb0) goes; out of it, the override is dropped */
    uint64_t pressed = 0;
    if (a->melee.override) {
        if (a->state == AI_STATE_COMBAT) {
            if (!a->melee.override_prev) c->combo_type = 0;
            held |= a->melee.override_held;
            pressed = a->melee.override_down;
        } else {
            a->melee.override = false;
        }
    }
    /* the turn: toward the facing wanted (the override, else the move's), less the diagonal's offset,
       at most the class's rate (ONCC+0x124, a hundredth of a turn, 0x52a878), half of it walking or
       crouching unless asked otherwise */
    c->turning_left = c->turning_right = false;
    if (!chr_holds_facing(c)) {
        float want = a->facing_override == 1000.0f ? a->facing : a->facing_override;   /* 0x52a87c */
        float d = wrap_2pi(wrap_2pi(want + c->facing_offset) - c->facing);
        float rate = oncc_float(c, 0x124) * 0.062831856f;
        if (!a->full_turn && (mode == 3 || mode == 4 || mode == 5)) rate *= 0.5f;
        if (a->turn_once != 0.0f) rate = a->turn_once, a->turn_once = 0.0f;
        if (d > PI) d -= TWO_PI;
        if (d < -rate) d = -rate;
        else if (d > rate) d = rate;
        if (d < -0.001f) c->turning_left = true;        /* A+0x21e5 (0x52a040) */
        else if (d > 0.001f) c->turning_right = true;   /* A+0x21e6 (0x529dfc) */
        c->facing = c->desired_facing = wrap_2pi(d + c->facing);   /* 0x4e3110 */
    }
    a->facing_override = 1000.0f;
    /* the input block: the keys held; jump, crouch and the action count as gone down while held; what
       was held last tick and isn't now went up */
    memset(in, 0, sizeof *in);
    in->held = held;
    in->not_held = ~held;
    in->down = pressed | (held & (CHR_KEY_JUMP | CHR_KEY_CROUCH | CHR_KEY_ACTION)) |
               (held & ~a->held & (CHR_KEY_FIRE | CHR_KEY_ALT_FIRE));
    in->up = a->held & ~held;
    a->held = held;
}

void ai_set_movement_mode(AiState *a, int mode)
{
    a->movement_mode = mode;
    movement_new_mode((Chr *)((char *)a - offsetof(Chr, ai)));
}

/* the path given (0x4a8480: the arrival's facing over; the arrival tested and the path made at the
   path's next update, movement.c) */
static void path_given(AiState *a)
{
    if (a->facing_mode == 3) a->facing_mode = 0;
    a->move.arrived_seen = a->move.waiting = false;
    a->arrived = a->turned = false;
    a->moving = true;
    a->path_pending = true;
}

/* AI2rPath_GoToPoint 0x4ae480: the radius at least 3 (0x529808) */
void ai_go_to(AiState *a, const float p[3], float radius, bool stop, bool face, float angle)
{
    if (radius <= 3.0f) radius = 3.0f;
    memcpy(a->goal, p, sizeof a->goal);
    a->path_type = 1;
    a->follow = NULL;
    a->goal_radius = radius;
    a->path_stop = stop;
    a->goal_facing = face;
    a->goal_angle = angle;
    path_given(a);
}

void ai_move_near(AiState *a, const float p[3], float radius, bool face, float angle) { ai_go_to(a, p, radius, true, face, angle); }
void ai_move_to(AiState *a, const float p[3], bool face, float angle) { ai_go_to(a, p, 0.0f, true, face, angle); }

/* AI2rPath_FollowCharacter 0x4ae580: to the character's place (taken again as the path is made) */
void ai_follow(AiState *a, const Character *who)
{
    if (!who) {
        ai_halt(a);
        return;
    }
    memcpy(a->goal, who->pos, sizeof a->goal);
    a->path_type = 2;
    a->follow = who;
    a->goal_facing = false;
    path_given(a);
}

static uint32_t oncc_u32(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 0x4ed280: standing still (state 7, in a standing type). */
static bool standing_still(const Character *c)
{
    if (c->state != 7) return false;
    switch (c->type) {
    case 6: case 7: case 8: case 0xce: case 0xcf: case 0xd8: return true;
    default: return false;
    }
}

/* The class's combat block (ONCC+0x138, the combat state's +0x28): +0x154 the wait for a reload, +0x158
   the wait before giving up a lost target, +0x15c the delay before pursuing a contact out of sight. */
enum { ONCC_COMBAT_WAIT_RELOAD = 0x138 + 0x154, ONCC_COMBAT_WAIT = 0x138 + 0x158, ONCC_COMBAT_PURSUE = 0x138 + 0x15c };

void ai_exit_state(AiState *a)
{
    if (a->state == AI_STATE_NEUTRAL) neutral_exit((Chr *)((char *)a - offsetof(Chr, ai)));
    if (a->state == AI_STATE_PANIC) panic_exit((Chr *)((char *)a - offsetof(Chr, ai)));
    if (a->state == AI_STATE_PURSUIT) pursuit_exit(a);
    if (a->state == AI_STATE_ALARM) {
        a->movement_mode = 0;     /* 0x48e7f0: the default movement mode */
        movement_new_mode((Chr *)((char *)a - offsetof(Chr, ai)));
    }
    if (a->state == AI_STATE_COMBAT) {
        /* 0x490170: slowing again, the facing free, no modifiers */
        a->no_slow = false;
        a->facing_mode = 0;
        a->nmod = 0;
        a->move.counted = 0;
    }
    /* the job's block (C+0x200) stays as it was: the port keeps its patrol */
    if (a->state == a->job && a->patrol >= 0) a->job_patrol = a->patrol, a->job_point = a->point;
    patrol_exit(a);     /* AI2rPatrol_Exit 0x4afba0 */
    a->patrol = -1;
    ai_halt(a);
    a->aim.look = 0;
    a->aim.fire = a->aim.fire_alt = false;
}

void ai_combat_set_target(AiState *a, KnowEntry *e)
{
    a->target = e ? e->target : NULL;
    a->target_entry = e;
    a->combat_lost = false;
    a->combat_last_strength = e ? e->strength : 0;
    a->combat_taunt_ready = a->combat_taunted = false;
    a->combat_wait_reload = a->combat_wait = a->combat_pursue_delay = 0;
    /* the targeting, the range and its behaviour (combat.c) */
    if (e && e->owner) combat_new_target(a, &e->owner->c, a->target ? &a->target->c : NULL);
}

void ai_return_to_job(AiState *a)
{
    ai_exit_state(a);
    if (a->alert < a->job_alert) {
        a->state = AI_STATE_IDLE;
        return;
    }
    /* AI2rEnterState 0x48ad50 on the job's block: a patrol picks up where it was */
    a->state = a->job;
    if (a->job == AI_STATE_PATROL && a->job_patrol >= 0) {
        patrol_start(a, a->job_patrol);
        a->point = a->job_point;
        patrol_nearest(a);
    }
}

/* AI2iCombat_LostTarget 0x4909a0: the target is lost; the waits start from the class's. (The best other
   hostile contact it looks for isn't used.) */
static void combat_lost(AiState *a, const Character *c)
{
    a->combat_lost = true;
    /* +0x78: with a weapon to reload (0x51a990), not in behaviour 5: with the weapons */
    a->combat_wait = oncc_u32(c, ONCC_COMBAT_WAIT);
    if (a->target && !chars_in_use(a->target)) {
        a->target = NULL;
        a->target_entry = NULL;
        a->combat_last_strength = 0;
    }
}

void ai_pursue(AiState *a, KnowEntry *e)
{
    ai_exit_state(a);
    a->state = AI_STATE_PURSUIT;
    a->target = e->target;
    a->target_entry = e;
    pursuit_enter(a, &e->owner->c);
}

void ai_combat_notify(AiState *a, const Character *c, KnowEntry *e, bool degraded)
{
    if (a->state != AI_STATE_COMBAT) return;
    if (degraded) {
        if (!a->target || e->target != a->target || e != a->target_entry) return;
        if (e->strength == 0) {
            combat_lost(a, c);
            return;
        }
        if (e->strength > 3) {
            a->combat_pursue_delay = 0;
            return;
        }
        if (a->combat_last_strength != 4) return;
        /* seen no more: pursue after the class's delay, at once if the target is invisible (C+0x1ba) */
        a->combat_pursue_delay = a->target->c.invisibility ? 0 : oncc_u32(c, ONCC_COMBAT_PURSUE);
        if (a->combat_pursue_delay) return;
        ai_pursue(a, e);
        return;
    }
    /* a hit: message 10 to the behaviour */
    if (e->type == KNOW_HURT || e->type == KNOW_HURT_MELEE) combat_hurt(a, c, e);
    if (a->state != AI_STATE_COMBAT) return;
    if (e == a->target_entry) {
        a->combat_last_strength = e->strength;
        return;
    }
    /* a new contact: taking it as the target (0x49ba10's comparison, the hurts' timing) comes with
       the combat state's port */
}

/* The start of AI2rCombat_Update 0x48f3e0: a dead or gone target is lost; a lost one is waited for
   (+0x78, then +0x7c, counted only once its body is gone), then the job resumes; a contact out of sight
   is pursued after its delay. Returns whether the combat state goes on. */
static bool combat_update(AiState *a, Character *c)
{
    if (!a->combat_lost) {
        if (!a->target || !chars_in_use(a->target) || (a->target->c.flags & 1)) {
            combat_lost(a, c);      /* "target is gone" */
        } else {
            /* the contact's place follows the target's pelvis (ONrCharacter_GetPelvisPosition 0x4e34f0;
               the port's: the location raised by the pelvis height) */
            if (a->target_entry) {
                const Character *t = &a->target->c;
                a->target_entry->pos[0] = t->pos[0];
                a->target_entry->pos[1] = t->pos[1] + t->pelvis_height;
                a->target_entry->pos[2] = t->pos[2];
            }
            if (!a->combat_pursue_delay) return true;
            /* (as the exe does: any delay left pursues at once, counting it down by one) */
            a->combat_pursue_delay--;
            KnowEntry *e = knowledge_find((Chr *)c, a->target);
            if (e) ai_pursue(a, e);
            else ai_return_to_job(a);   /* "lost contact" */
            return false;
        }
    }
    if (a->target && !chars_in_use(a->target)) {
        a->target = NULL;
        a->target_entry = NULL;
        a->combat_last_strength = 0;
    }
    if (a->combat_wait_reload) a->combat_wait_reload--;
    if (!a->combat_wait_reload) {
        if (a->combat_wait && (!a->target || !((Chr *)a->target)->corpse_time)) {
            a->combat_wait--;
            /* the taunt over the body of one it felled in melee (+0x75, 0x48b370): once, standing still
               (0x4ed280), past half the wait; the taunt animation (0x5e) by the class's chance
               (ONCC+0x298, percent) and the cry (type 3) */
            if (a->combat_taunt_ready && !a->combat_taunted && standing_still(c) &&
                a->combat_wait < oncc_u32(c, ONCC_COMBAT_WAIT) / 2) {
                a->combat_taunted = true;
                a->combat_taunt_ready = false;
                uint32_t r = trac_random(), pct = oncc_u32(c, 0x298);
                if (r < (uint32_t)(((uint64_t)((pct << 16) - pct) * 0x51eb851fu) >> 37))
                    chr_do_animation(c, CHR_ANIM_WHEN_CAN, 0x5e);
                ai_vocalize((Chr *)c, 3, false);
            }
        }
        if (!a->combat_wait) ai_return_to_job(a);
    }
    return false;
}

/* The states' ticks: combat (AI2rCombat_Update 0x48f3e0: the lost target here, the rest in combat.c),
   alarm and pursuit. */
static void state_update(AiState *a, Character *c, const void *env)
{
    if (a->state == AI_STATE_COMBAT) {
        /* AI2rCombat_Update 0x48ff14: the modifiers cleared (0x4a8370), no slowing in melee (0x49000c) */
        a->nmod = 0;
        a->move.counted = 0;
        a->no_slow = a->combat.maneuver == 5;
        bool has_target = combat_update(a, c);
        if (a->state == AI_STATE_COMBAT) combat_tick(a, c, env, has_target);
    } else if (a->state == AI_STATE_ALARM) {
        alarm_update(a, c);
    } else if (a->state == AI_STATE_PURSUIT) {
        /* (a contact gone without the forget's notice leaves the original a stale pointer: the port
           goes back to the job) */
        if (!a->target_entry) ai_return_to_job(a);
        else pursuit_update(a, c, env);
    } else if (a->state == AI_STATE_NEUTRAL) {
        neutral_update((Chr *)c);
    } else if (a->state == AI_STATE_PANIC) {
        panic_update((Chr *)c);
    }
}

/* AI2rPath_Update 0x4ae670, then AI2rMovement_Update 0x4a8550's movement state (0x4a97b0) and the
   executor's aim (AI2rExecutor_UpdateAim 0x498f50, combat.c) */
static void movement_tick(AiState *a, Character *c, const void *env)
{
    Chr *ch = (Chr *)c;
    a->ticks++;
    movement_path_update(ch, env);
    movement_update(ch);
    combat_aim(a, c);
}

void ai_control_update(AiState *a, Character *c, const void *env)
{
    movement_tick(a, c, env);
    ChrInput in;
    executor_update(a, c, &in);
    character_update_input(c, &in, env);
}

void ai_take_control(AiState *a, Character *c)
{
    character_set_facing_offset(c, 0.0f);   /* 0x4f5be0 */
    c->flags |= CHR_FLAG_AI_MOVEMENT;
    /* 0x4ae360: no path, arrived, the repaths and the collisions forgotten, the rooms and the local
       path cleared */
    ai_halt(a);
    a->arrived = a->turned = true;
    a->repaths = a->check_delay = a->repath_timer = 0;
    a->fail_set = false;
    a->path_n = a->path_k = 0;
    a->path_made = false;
    a->local_n = a->local_k = 0;
    a->local_seg = -1;
    MoveState *m = &a->move;
    m->ncoll = m->nconfirmed = m->narc = 0;
    m->near_all = m->has_point = m->stuck = m->slowed = m->stop_arrive = m->stop_turn = false;
    m->arrive_ticks = m->kind_ticks = 0;
    m->room = m->prev_room = -1;
    /* 0x4a7ff0: the facing free, no modifiers, look or glance, the default movement mode */
    a->facing_mode = 0;
    a->aim.look = 0;
    a->glance_ticks = a->glance_delay = 0;
    a->no_slow = false;
    a->movement_mode = 0;
    movement_new_mode((Chr *)c);
}

void ai_update(AiState *a, Character *c, const void *env)
{
    /* AI2rUpdateAI 0x489d40 runs only the living (C+0x04 bit 1 clear): a dead AI's path, executor and
       scripts stop; its frame still runs (ONrGameState_UpdateCharacters) */
    if (c->flags & 1) {
        character_update_dead(c, env);
        return;
    }
    /* AI2rUpdateAI 0x489d40: the daze runs out while lying */
    if (a->daze_timer && (c->state == 0x12 || c->state == 0x32)) a->daze_timer--;
    /* the job's place is kept while at it (in its patrol; the original's test of the patrol's own
       flags, +0x53a and +0x53b, isn't kept) */
    if (a->state == AI_STATE_PATROL && a->job == AI_STATE_PATROL && !(c->flags & 1)) {
        a->job_pos_set = true;
        memcpy(a->job_pos, c->pos, sizeof a->job_pos);
    }
    chars_ai_scripts((Chr *)c);
    /* the startle (C+0xfa8): while its animation plays (0x4e3ff0: types 0xc0..0xc3, now or queued, the
       frame 0 for a queued one) the character turns toward its contact with the frames, stops aiming
       and does nothing else */
    if (a->startle_ticks) {
        bool now = c->type >= 0xc0 && c->type <= 0xc3, queued = c->queued >= 0xc0 && c->queued <= 0xc3;
        if (now || queued) {
            uint16_t frame = now ? (uint16_t)c->frame : 0;
            float f = a->startle_turn / (float)a->startle_ticks * (float)frame + a->startle_from;
            if (f < 0.0f) f += TWO_PI;
            else if (f >= TWO_PI) f -= TWO_PI;
            if (a->aim.on) a->aim.stop = true;      /* AI2rExecutor_StopAiming 0x498da0 */
            ai_executor_move(a, f, 0, 4, true);
            ChrInput in;
            executor_update(a, c, &in);
            character_update_input(c, &in, env);
            return;
        }
        a->startle_ticks = 0;
    }
    /* the cry put off (C+0xf8c): at its end the startled one (type 1) */
    if (a->vocal_delay && --a->vocal_delay == 0) {
        ai_vocalize((Chr *)c, 1, false);
        a->flags |= AI_FLAG_STARTLED;
    }
    /* AI2rMovement_Update 0x4a8550's glance: the delay first, then the ticks, a tick counted once the aim is done (A+0x1ab8,
       0x498e60) */
    if (a->glance_delay) a->glance_delay--;
    else if (a->glance_ticks && a->aim.done) a->glance_ticks--;
    neutral_end_script((Chr *)c);   /* the conversation's end script, due from the last tick */
    /* before the states (not passive): a neutral may start a conversation by itself */
    if (!(a->flags & AI_FLAG_PASSIVE) && !(c->flags & 1)) neutral_check((Chr *)c);
    if (a->flags & AI_FLAG_PASSIVE) ai_halt(a);     /* AI2rPassive 0x48b160: the state left, the path halted */
    else if (a->patrol >= 0 && !(c->flags & 1)) patrol_update(a, c, env, a->name);
    else if (!(c->flags & 1)) {
        state_update(a, c, env);
        alarm_fight_tick(a, c);
    }
    movement_tick(a, c, env);
    ChrInput in;
    executor_update(a, c, &in);
    character_update_input(c, &in, env);
}
