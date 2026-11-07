/* The AI's alarm: see alarm.h. */
#include "alarm.h"

#include "action.h"
#include "chars.h"
#include "chr_anim.h"
#include "console.h"
#include "graph.h"
#include "knowledge.h"

#include <math.h>
#include <string.h>

static float dist2(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

/* An alarm console that can be used: alarm, active, not used (0x48eb30's and 0x48eb50's tests). */
static bool usable(int console)
{
    uint16_t f = console_flags(console);
    return (f & CONSOLE_ALARM) && (f & CONSOLE_ACTIVE) && !(f & CONSOLE_USED);
}

/* The most important hostile contact (hostility and strength above 1), the nearest of the equally
   strong ones (the list is by importance: a weaker one ends the search). */
static KnowEntry *best_contact(const AiState *a, const Character *c)
{
    KnowEntry *best = NULL;
    float bd = 0.0f;
    for (KnowEntry *e = a->know; e; e = e->next) {
        if (e->hostility <= 1 || e->strength <= 1) continue;
        float d = dist2(c->pos, e->pos);
        if (!best) best = e, bd = d;
        else {
            if (e->strength < best->strength) break;
            if (d < bd) best = e, bd = d;
        }
    }
    return best;
}

/* The end of an alarm run in the alarm state (AI2iAlarm_Finish 0x48e950): after the best hostile
   contact, else back to the job (AI2rReturnToJob 0x48af80). */
static void finish(AiState *a, const Character *c)
{
    if (a->state == AI_STATE_ALARM) {
        KnowEntry *e = best_contact(a, c);
        if (e) ai_pursue(a, e);
        else ai_return_to_job(a);
    }
    a->alarm_marker = -1;
    a->alarm_fight_timer = 0;
}

int alarm_find_console(const AiState *a, const Character *c)
{
    /* OBJrObjectType_EnumerateObjects over the consoles, 0x48ebc0 for each */
    int best = -1;
    float best_d = 0.0f;
    float loc[3];
    int from = chr_room(c, loc);
    for (int i = 0; i < console_marker_count(); i++) {
        const ActionMarker *m = console_marker(i);
        if (!m || !usable(m->console)) continue;
        float d = a->combat_set.alarm_find;
        int to = graph_room_at(m->pos);
        if (from < 0 || to < 0 || !graph_find_path_within(from, loc, to, m->pos, true, -1, NULL, &d)) continue;   /* PHrPathDistance */
        if (best >= 0 && best_d <= d) continue;
        best = i, best_d = d;
    }
    return best;
}

/* AI2rAlarm_Enter 0x48e360: the calm states' knowledge handler; the damage of every contact
   forgotten; the goal 5 back from the marker (along its facing), running, no aim, glance or facing
   held; the path there, within 5, facing the marker's way. */
static void enter(AiState *a)
{
    const ActionMarker *m = console_marker(a->alarm_marker);
    for (KnowEntry *e = a->know; e; e = e->next) e->damage = 0;
    if (!m) return;
    a->alarm_goal[0] = m->pos[0] - sinf(m->facing) * 5.0f;      /* 0x52956c */
    a->alarm_goal[1] = m->pos[1];
    a->alarm_goal[2] = m->pos[2] - cosf(m->facing) * 5.0f;
    a->alarm_wait = 0;
    ai_set_movement_mode(a, AI_MODE_RUN);     /* AI2rMovement_ChangeMovementMode(6) */
    a->aim.look = 0;                    /* AI2rMovement_StopAiming (the glance and facing too) */
    ai_move_near(a, a->alarm_goal, 5.0f, true, m->facing);  /* AI2rPath_GoToPoint */
}

/* 0x48e7f0, the state's exit: the path halted, the default movement mode. */
static void leave(AiState *a)
{
    ai_halt(a);
    ai_set_movement_mode(a, 0);
}

bool alarm_setup(AiState *a, Character *c, int marker)
{
    if (marker < 0) {
        marker = a->alarm_marker;
        if (marker < 0) return false;
    } else if (a->alarm_marker != marker && a->alarm_marker >= 0) {
        finish(a, c);
    }
    a->alarm_marker = marker;
    a->alarm_fight_timer = 0;
    if (marker != a->alarm_last) {
        a->alarm_tries = 0;
        a->alarm_last = marker;
    }
    if (a->state == AI_STATE_ALARM) leave(a);
    ai_exit_state(a);
    a->state = AI_STATE_ALARM;
    enter(a);
    return true;
}

/* An enemy contact that makes the alarm run fight (strength above 3, hostility above 1, within the
   ignore distance, and within the chase distance or having done the damage threshold): its fight
   time, then the pursuit of it. */
static bool fight_on_the_way(AiState *a, const Character *c)
{
    const CombatSettings *s = &a->combat_set;
    KnowEntry *hit = NULL;
    for (KnowEntry *e = a->know; e; e = e->next) {
        if (e->hostility <= 1 || e->strength <= 3) continue;
        float d = dist2(c->pos, e->pos);
        if (d > s->alarm_ignore * s->alarm_ignore) continue;
        if (d > s->alarm_chase * s->alarm_chase && e->damage < s->alarm_damage) continue;
        hit = e;
    }
    if (!hit) return false;
    /* (the original enters the pursuit for each such contact in turn and goes on with the tick; the
       last one stands) */
    a->alarm_fight_timer = s->alarm_fight;
    leave(a);
    ai_pursue(a, hit);
    return true;
}

static bool lying(const Character *c) { return c->state == 0x12 || c->state == 0x32; }  /* 0x4e4070 */

/* 0x4ed280: standing still (state 7, in a standing type). */
static bool standing(const Character *c)
{
    if (c->state != CHR_STATE_STANDING) return false;
    switch (c->type) {
    case 6: case 7: case 8: case 0xce: case 0xcf: case 0xd8: return true;
    default: return false;
    }
}

void alarm_update(AiState *a, Character *c)
{
    if (c->console_marker >= 0) {
        ai_halt(a);      /* using the console */
        return;
    }
    if (a->alarm_marker < 0) {
        KnowEntry *e = best_contact(a, c);
        leave(a);
        if (e) ai_pursue(a, e);
        else ai_return_to_job(a);
        return;
    }
    if (fight_on_the_way(a, c)) return;
    if (a->moving) {
        /* on the way (C+0x112d not yet): lying, it gets up */
        if (lying(c)) {
            a->key_override = true;     /* AI2rExecutor_MoveOverride(forward) */
            a->override_keys = CHR_KEY_FORWARD;
        }
        return;
    }
    /* there */
    if (!standing(c)) {
        ai_halt(a);
        if (a->alarm_wait == 0) a->alarm_wait = 60;
        if (--a->alarm_wait != 0) return;
    } else {
        if (action_can_use(c, a->alarm_marker, true)) {     /* ONrGameState_TryActionMarker(wide) */
            action_start(c, a->alarm_marker);               /* 0x4ee1b0 */
            ai_halt(a);
            return;
        }
    }
    if (lying(c)) {
        a->key_override = true;     /* AI2rExecutor_MoveOverride(back) */
        a->override_keys = CHR_KEY_BACK;
        return;
    }
    if (a->alarm_tries > 2) {
        leave(a);
        finish(a, c);
        return;
    }
    const ActionMarker *m = console_marker(a->alarm_marker);
    if (m) ai_move_near(a, a->alarm_goal, 5.0f, true, m->facing);
    a->alarm_tries++;
}

void alarm_fight_tick(AiState *a, Character *c)
{
    if (a->alarm_marker < 0 || a->state == AI_STATE_ALARM || !a->combat_set.alarm_fight) return;
    if (a->alarm_fight_timer) a->alarm_fight_timer--;
    if (!a->alarm_fight_timer) alarm_setup(a, c, -1);
}

void alarm_notify(unsigned group, Chr *who)
{
    if (group >= 32) return;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch || ch->player || ch == who || (ch->ai.flags & AI_FLAG_PASSIVE)) continue;
        if (!(ch->ai.alarm_groups & (1u << group))) continue;
        if (who) {
            if ((who->c.flags & 1) || !(who->c.flags & 0x8000)) continue;   /* dead, or not in use */
            if (who->player) {
                if (ch->ai.flags & AI_FLAG_IGNORE_PLAYER) continue;
            } else if (!ai_is_hostile(ch, who, true) && !ai_is_hostile(who, ch, true)) {
                continue;
            }
        }
        float p[3] = { 0.0f, 0.0f, 0.0f };
        if (who) memcpy(p, who->c.pos, sizeof p);
        knowledge_post(ch, KNOW_ALARM, 3, p, who, group, false, false);     /* AI2iKnowledge_AddPending */
        ch->ai.flags |= AI_FLAG_ALARM_SCRIPT;
    }
}

void alarm_script(Chr *ch, uint16_t console)
{
    if (!ch || ch->player) return;          /* "ai2_doalarm" */
    if (ch->ai.flags & AI_FLAG_PASSIVE) return;     /* "AI %s is currently passive" */
    int marker = -1;
    if (!console) {
        marker = alarm_find_console(&ch->ai, &ch->c);
    } else {
        for (int i = 0; i < console_marker_count(); i++) {
            const ActionMarker *m = console_marker(i);
            if (m && console_id(m->console) == console) { marker = i; break; }
        }
        if (marker < 0) return;             /* "ai2_doalarm could not find console" */
        uint16_t f = console_flags(console_marker(marker)->console);
        if (!(f & CONSOLE_ACTIVE) || (f & CONSOLE_USED)) return;    /* 0x48eb30 */
    }
    if (marker >= 0) alarm_setup(&ch->ai, &ch->c, marker);
}
