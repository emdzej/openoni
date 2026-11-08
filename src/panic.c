/* The panic state: see panic.h. */
#include "panic.h"

#include "chr_anim.h"
#include "knowledge.h"
#include "neutral.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
#include <string.h>

static const float PI = 3.1415927f, TWO_PI = 6.2831855f;

static bool lying(const Character *c) { return c->state == 0x12 || c->state == 0x32; }   /* 0x4ecdf0 */

/* ONrAnimState_IsCrouch 0x4ece10. */
static bool crouching(uint16_t s)
{
    switch (s) {
    case 0x13: case 0x23: case 0x2f: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x40: return true;
    default: return false;
    }
}

/* 0x4ed030, 0x4ecf80 and 0x4ecf30: out of fight mode (C+0x04 0x10000), the weapon hidden (0x2000), and
   the panicked variant (0x8000); or none of them. */
static void panic_flags(Character *c, bool on)
{
    if (on) {
        c->flags |= 0x10000;
        c->varient &= (uint16_t)~0x200;
    } else {
        c->flags &= ~0x10000u;
    }
    chr_hide_weapon(c, on);
    if (on) c->varient |= 0x8000;
    else c->varient &= 0x7fff;
}

static void look_at(AiState *a, const Character *c)
{
    a->aim.look = 3;    /* AI2rMovement_LookAtCharacter 0x4a80c0 */
    a->aim.look_char = c;
}

/* AI2rPanic_Enter 0x4adbe0 (after the state's block cleared). */
static void enter(Chr *c)
{
    AiState *a = &c->ai;
    panic_flags(&c->c, true);
    a->panic_cower = false;
    ai_halt(a);
    a->movement_mode = AI_MODE_RUN;
    /* (AI2rMovement_FreeFacing, StopGlancing) */
    a->aim.look = 0;
    a->panic_entry = NULL;
    a->panic_until = 0;
    a->panic_dazed = a->panic_known = false;
    a->panic_vocal = 45;
}

void panic_exit(Chr *c)
{
    c->ai.movement_mode = 0;
    c->ai.aim.look = 0;
    panic_flags(&c->c, false);
}

void panic_start(Chr *c, KnowEntry *e, uint32_t ticks)
{
    AiState *a = &c->ai;
    if (e) {
        switch (e->type) {
        case KNOW_SOUND_MELEE: ticks = a->combat_set.panic_melee; break;
        case KNOW_SOUND_GUNFIRE: ticks = a->combat_set.panic_gunfire; break;
        case KNOW_SEEN_CENTRAL: ticks = a->combat_set.panic_sight; break;
        case KNOW_HURT: case KNOW_HURT_MELEE: ticks = a->combat_set.panic_hurt; break;
        default: return;    /* nothing else panics it */
        }
    }
    if (a->state != AI_STATE_PANIC) {
        ai_exit_state(a);
        a->state = AI_STATE_PANIC;
        ai_entered_state(a);
        enter(c);
    }
    if (e && e->hostility > 0) a->panic_known = true;
    if (e && (e->type == KNOW_HURT || e->type == KNOW_HURT_MELEE)) {
        /* a hurt brings its cry nearer */
        uint32_t d = e->value * 5;
        a->panic_vocal = a->panic_vocal < d ? 0 : a->panic_vocal - d;
    }
    uint32_t r = 0;
    if (ticks & ~1u) r = (uint32_t)trac_random() * (ticks >> 1) >> 16;   /* UUrRandom 0x426e10 */
    uint32_t until = knowledge_now() + ((r + ticks) & 0xffff);
    if (until < a->panic_until) until = a->panic_until;
    a->panic_until = until;
    /* the strongest contact (of no less hostility) is what it fears, and looks at unless cowering */
    KnowEntry *cur = a->panic_entry;
    if (e && e->target && (!cur || (cur->strength < e->strength && cur->hostility <= e->hostility))) {
        a->panic_entry = e;
        if (!a->panic_cower) look_at(a, &e->target->c);
    }
}

void panic_notify(Chr *c, KnowEntry *e, bool degraded)
{
    AiState *a = &c->ai;
    if (!degraded) {
        if (e->hostility == 2 || (e->type >= KNOW_SOUND_DANGER && e->type <= KNOW_SOUND_GUNFIRE)) {   /* 0x49ad70 */
            bool known = a->state == AI_STATE_PANIC && a->panic_known;
            if (ai_dangerous_contact(c, NULL, known, e, c->c.pos)) {
                if (a->state == AI_STATE_NEUTRAL) neutral_stop(c, false, true, true, true);
                panic_start(c, e, 0);
            }
        }
        /* (not panicking nor talking: a glance at it, 0x4a8150, which the port's movement doesn't keep) */
        return;
    }
    if (a->state != AI_STATE_PANIC || e != a->panic_entry || (e->strength >= KNOW_WEAK && e->hostile)) return;
    /* what it feared fades: another danger, else calm 180 ticks after the last news of it */
    for (KnowEntry *k = a->know; k; k = k->next)
        if (ai_dangerous_contact(c, NULL, a->panic_known, k, c->c.pos)) {
            a->panic_entry = k;
            return;
        }
    a->panic_entry = NULL;
    a->panic_until = e->posted + 180;
}

/* 0x4e95c0: the angle from c's heading to o, in [-pi, pi). */
static float angle_to(const Character *c, const Character *o)
{
    float h = c->desired_facing - c->facing_offset;
    if (h < 0.0f) h += TWO_PI;
    else if (h >= TWO_PI) h -= TWO_PI;
    float a = atan2f(o->pos[0] - c->pos[0], o->pos[2] - c->pos[2]);
    if (a < 0.0f) a += TWO_PI;
    a -= h;
    if (a < 0.0f) a += TWO_PI;
    return a >= PI ? a - TWO_PI : a;
}

void panic_update(Chr *c)
{
    AiState *a = &c->ai;
    if (knowledge_now() > a->panic_until) {
        ai_return_to_job(a);
        return;
    }
    if (a->panic_vocal) a->panic_vocal--;
    if (!a->panic_vocal) {
        ai_vocalize(c, 5, false);   /* the panic's cry, AI2rVocalize 0x48ba60 */
        a->panic_vocal = ((trac_random() * 500u) >> 16) + 1500;
    }
    if (!a->panic_cower) {
        if (!crouching(c->c.state)) {
            /* cowering once what it fears is within 20 (400 squared, 0x529dec) */
            const KnowEntry *e = a->panic_entry;
            if (!e) return;
            float dx = c->c.pos[0] - e->pos[0], dy = c->c.pos[1] - e->pos[1], dz = c->c.pos[2] - e->pos[2];
            if (dx * dx + dy * dy + dz * dz >= 400.0f) return;
        }
        a->panic_cower = true;
        a->movement_mode = AI_MODE_CROUCH;
        a->aim.look = 0;
        return;
    }
    if (!lying(&c->c)) return;
    if (a->panic_dazed) {
        if (!a->daze_timer) a->panic_dazed = false;
    } else if (!a->daze_started) {
        /* knocked down: its class's daze (AI2rStartDazeTimer 0x48b9f0) */
        a->panic_dazed = a->daze_started = true;
        const uint8_t *k = c->c.oncc;
        uint16_t lo = (uint16_t)(k[0x128] | k[0x129] << 8), hi = (uint16_t)(k[0x12a] | k[0x12b] << 8);
        a->daze_timer = lo;
        if (hi > lo) a->daze_timer = (uint16_t)(lo + (int)((trac_random() * (uint32_t)(hi - lo)) >> 16));
        a->panic_cower = true;
        a->movement_mode = AI_MODE_CROUCH;
        a->aim.look = 0;
    } else {
        /* the daze over: up, away from what it fears when facing it (backward), else forward */
        uint64_t key = CHR_KEY_BACK;
        const KnowEntry *e = a->panic_entry;
        if (e && e->target) {
            if (!a->panic_cower) look_at(a, &e->target->c);
            if (fabsf(angle_to(&c->c, &e->target->c)) > 1.5707964f) key = CHR_KEY_FORWARD;   /* 0x529cd0 */
        }
        if (a->panic_cower) key |= CHR_KEY_CROUCH;
        a->key_override = true;     /* AI2rExecutor_MoveOverride 0x498b60 */
        a->override_keys = key;
    }
    if (a->panic_dazed) {
        ai_halt(a);
        a->aim.look = 0;
    }
}
