/* The AI: see ai.h. */
#include "ai.h"
#include "chars.h"
#include "combat.h"
#include "alarm.h"
#include "graph.h"
#include "localpath.h"
#include "knowledge.h"
#include "melee.h"
#include "patrol.h"

#include <math.h>
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

void ai_init(AiState *a)
{
    memset(a, 0, sizeof *a);
    a->kind = AI_MOVE_NONE;
    a->facing_override = 1000.0f;
    a->patrol = -1;
    a->job_patrol = -1;
    a->local_seg = -1;
    a->aim.speed = 1.0f;
    a->aim.last_facing = 1000.0f;
    a->alarm_marker = a->alarm_last = -1;
}

void ai_executor_move(AiState *a, float facing, int mode, int kind, bool full_turn)
{
    a->full_turn = full_turn;
    a->facing = facing;
    a->mode = mode;
    a->kind = kind;
}

/* The movement's default mode for the alert level (0x4a8d00): the tables at 0x52af18 (hurrying,
   AI2rIsHurrying 0x48b020) and 0x52af2c (not). The port has no hurry yet. */
static int default_mode(const AiState *a)
{
    static const int calm[5] = { 4, 4, 5, 5, 5 };   /* 0x52af2c */
    return a->alert >= 0 && a->alert < 5 ? calm[a->alert] : calm[0];
}

/* AI2rExecutor_Update 0x498550, with the keys' override (A+0x1ab9) and the attack override (A+0x1acc);
   the aim is AI2rExecutor_UpdateAim 0x498f50 (combat.c). */
static void executor_update(AiState *a, Character *c, ChrInput *in)
{
    int mode = a->mode ? a->mode : a->movement_mode ? a->movement_mode : default_mode(a);
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

void ai_move_near(AiState *a, const float p[3], float radius, bool face, float angle)
{
    memcpy(a->goal, p, sizeof a->goal);
    a->moving = true;
    a->goal_facing = face;
    a->goal_angle = angle;
    a->goal_radius = radius;
}

void ai_move_to(AiState *a, const float p[3], bool face, float angle) { ai_move_near(a, p, 4.0f, face, angle); }

/* AI2iPath_MakePath 0x4aeb10: the path kept while it goes between the same rooms to within 20 of the
   same point (400 squared, 0x529dec) and is younger than 90 ticks; else the rooms' A* again (the
   character's room, C+0x188, found here from its position). */
static void make_path(AiState *a, const Character *c)
{
    int from = graph_room_at(c->pos), to = graph_room_at(a->goal);
    float dx = a->path_goal[0] - a->goal[0], dy = a->path_goal[1] - a->goal[1], dz = a->path_goal[2] - a->goal[2];
    if (a->path_made && a->ticks < a->path_time + 90 && dz * dz + dy * dy + dx * dx < 400.0f && from == a->path_from &&
        to == a->path_to)
        return;
    a->path_made = true;
    a->path_time = a->ticks;
    a->local_seg = -1;     /* a new path: its rooms' local paths again */
    a->path_from = from, a->path_to = to;
    memcpy(a->path_goal, a->goal, sizeof a->path_goal);
    a->path_n = a->path_k = 0;
    if (from < 0 || to < 0 || from == to) return;
    int n = graph_find_path(from, c->pos, to, a->goal, true, a->path);
    a->path_n = n > 1 ? n - 1 : 0;      /* no path: straight at the point (AI2iReport_Pathfinding_NoPath) */
}

/* AI2rMovementState_MakePath 0x4a9f20 for the global path's room `seg` (path_n: the destination's): from
   the character (in the first room) or the crossing of the link before (PHrWaypointFromGunk, near
   the character), to the crossing of the link out (AI2rPath_FindGhostWaypoint 0x4aea60, without its
   look ahead along the links after: a stand-in) or the destination, through the room's grid. */
static void make_local(AiState *a, const Character *c, const void *env, int seg)
{
    if (!a->local) a->local = malloc(sizeof(LocalPoint) * LOCAL_PATH_MAX);
    a->local_seg = seg;
    a->local_n = a->local_k = 0;
    if (!a->local) return;
    float start[3], end[3];
    int room;
    if (seg == 0 || seg > a->path_n) memcpy(start, c->pos, sizeof start);
    else graph_waypoint(graph_link(a->path[seg - 1])->quad, c->pos, start);
    if (seg >= a->path_n) {
        memcpy(end, a->goal, sizeof end);
        room = a->path_to;
    } else {
        graph_waypoint(graph_link(a->path[seg])->quad, start, end);
        room = graph_link(a->path[seg])->from;
    }
    /* +0x1148: the next room is a stair one */
    bool stairs = seg + 1 < a->path_n && (graph_room_flags(graph_link(a->path[seg + 1])->to) & 1);
    a->local_n = local_path(env, room, start, end, c, stairs, true, a->local);
}

/* The stand-in for the movement layer: forward, facing the local path's next waypoint (passed within
   2), the path's next door when there is no local path, then the goal, until within the goal's radius
   (then the facing asked for, if any). A link is passed in the room beyond, or within 2 of its middle. */
static void mover(AiState *a, const Character *c, const void *env)
{
    if (!a->moving) return;
    if (env && graph_for(env)) make_path(a, c);
    else a->path_n = 0;
    while (a->path_k < a->path_n) {
        const GraphLink *l = graph_link(a->path[a->path_k]);
        float lx = l->mid[0] - c->pos[0], lz = l->mid[2] - c->pos[2];
        if (graph_room_at(c->pos) == l->to || lx * lx + lz * lz < 4.0f) {
            a->path_k++;
            continue;
        }
        break;
    }
    int seg = a->path_k;
    if (env && graph_for(env) && a->local_seg != seg && a->path_from >= 0 && a->path_to >= 0) make_local(a, c, env, seg);
    const float *at = a->path_k < a->path_n ? graph_link(a->path[a->path_k])->mid : a->goal;
    while (a->local_k < a->local_n) {
        const float *w = a->local[a->local_k].p;
        float wx = w[0] - c->pos[0], wz = w[2] - c->pos[2];
        if (wx * wx + wz * wz < 4.0f && a->local_k + 1 < a->local_n) {
            a->local_k++;
            continue;
        }
        at = w;
        break;
    }
    bool last = a->path_k >= a->path_n;
    float dx = (last ? a->goal[0] : at[0]) - c->pos[0], dz = (last ? a->goal[2] : at[2]) - c->pos[2];
    if (last && dx * dx + dz * dz < a->goal_radius * a->goal_radius) {
        a->moving = false;
        a->path_made = false;
        a->local_seg = -1;
        ai_executor_move(a, a->goal_facing ? a->goal_angle : c->facing, AI_MODE_DEFAULT, AI_MOVE_NONE, false);
        return;
    }
    dx = at[0] - c->pos[0], dz = at[2] - c->pos[2];
    ai_executor_move(a, wrap_2pi(atan2f(dx, dz)), AI_MODE_DEFAULT, AI_MOVE_FORWARD, false);
}

static uint32_t oncc_u32(const Character *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->oncc + off;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The class's combat block (ONCC+0x138, the combat state's +0x28): +0x154 the wait for a reload, +0x158
   the wait before giving up a lost target, +0x15c the delay before pursuing a contact out of sight. */
enum { ONCC_COMBAT_WAIT_RELOAD = 0x138 + 0x154, ONCC_COMBAT_WAIT = 0x138 + 0x158, ONCC_COMBAT_PURSUE = 0x138 + 0x15c };

void ai_exit_state(AiState *a)
{
    if (a->state == AI_STATE_ALARM) a->movement_mode = 0;     /* 0x48e7f0: the default movement mode */
    /* the job's block (C+0x200) stays as it was: the port keeps its patrol */
    if (a->state == a->job && a->patrol >= 0) a->job_patrol = a->patrol, a->job_point = a->point;
    a->patrol = -1;
    a->moving = false;
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
    a->point_started = false;
    a->target = e->target;
    a->target_entry = e;
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
        /* seen no more: pursue after the class's delay (C+0x1ba of the target, which skips it, isn't
           kept by the port) */
        a->combat_pursue_delay = oncc_u32(c, ONCC_COMBAT_PURSUE);
        if (a->combat_pursue_delay) return;
        ai_pursue(a, e);
        return;
    }
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
            /* (the taunt over the body: +0x75, then the class's chance at ONCC+0x298 of type 0x5e and
               the vocalization 0x48ba60) */
        }
        if (!a->combat_wait) ai_return_to_job(a);
    }
    return false;
}

/* The combat state (AI2rCombat_Update 0x48f3e0: the lost target here, the rest in combat.c), and a
   stand-in for the pursuit (AI2rPursuit_Update) until its port: it goes to where the contact was. */
static void state_stand_in(AiState *a, Character *c, const void *env)
{
    if (a->state == AI_STATE_COMBAT) {
        bool has_target = combat_update(a, c);
        if (a->state == AI_STATE_COMBAT) combat_tick(a, c, env, has_target);
    } else if (a->state == AI_STATE_ALARM) {
        alarm_update(a, c);
    } else if (a->state == AI_STATE_PURSUIT && a->target_entry && !a->point_started) {
        const float *p = a->target_entry->pos;
        ai_move_near(a, p, 8.0f, true, wrap_2pi(atan2f(p[0] - c->pos[0], p[2] - c->pos[2])));
        a->point_started = true;    /* once: there, it looks the contact's way */
    }
}

void ai_control_update(AiState *a, Character *c, const void *env)
{
    a->ticks++;
    mover(a, c, env);
    ChrInput in;
    executor_update(a, c, &in);
    character_update_input(c, &in, env);
}

void ai_take_control(AiState *a, Character *c)
{
    character_set_facing_offset(c, 0.0f);   /* 0x4f5be0 */
    c->flags |= CHR_FLAG_AI_MOVEMENT;
    /* 0x4ae360 and 0x4a7ff0: the movement's state and path cleared, the default movement mode */
    a->moving = false;
    a->movement_mode = 0;
    a->aim.look = 0;
}

void ai_update(AiState *a, Character *c, const void *env)
{
    /* AI2rUpdateAI 0x489d40: the daze runs out while lying */
    if (a->daze_timer && (c->state == 0x12 || c->state == 0x32)) a->daze_timer--;
    if (a->flags & AI_FLAG_PASSIVE) a->moving = false;     /* AI2rPassive 0x48b160: the state left, the path halted */
    else if (a->patrol >= 0 && !(c->flags & 1)) patrol_update(a, c->pos, c->facing, a->name);
    else if (!(c->flags & 1)) {
        state_stand_in(a, c, env);
        alarm_fight_tick(a, c);
    }
    a->ticks++;
    mover(a, c, env);
    combat_aim(a, c);
    ChrInput in;
    executor_update(a, c, &in);
    character_update_input(c, &in, env);
}
