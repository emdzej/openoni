/* The bosses' combat behaviours: see boss.h. */
#include "boss.h"

#include "chr_anim.h"
#include "combat.h"
#include "trac.h"
#include "weapon.h"

#include <string.h>

bool boss_barabbas_run = true;

/* 0x4e3b10: free to act: neither the current animation's states (A+0x1afc, A+0x1afe) is a fall or a
   lying one, and its type is a standing, walking or running one (or the character has no active
   record). */
static bool can_act(const Character *c)
{
    static const uint16_t down[] = { 4, 0xc, 0x12, 0x1e, 0x1f, 0x32 };
    static const uint16_t moving[] = { 2, 3, 6, 7, 8, 9, 10, 0xb, 0xd, 0xe, 0xf, 0x11, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
                                       0x3f, 0x40, 0x41, 0x5d, 0x8e, 0x8f, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
                                       0xd8, 0xd9, 0xda };
    for (unsigned i = 0; i < sizeof down / sizeof down[0]; i++)
        if (c->from_state == down[i] || c->state == down[i]) return false;
    for (unsigned i = 0; i < sizeof moving / sizeof moving[0]; i++)
        if (c->type == moving[i]) return true;
    return false;
}

static bool weapon_firing(const Character *c) { return c->weapon && (c->weapon->flags & WEAPON_FIRING); }  /* WPrIsFiring 0x51a3a0 */

/* 0x4e64a0 (not over the maximum): hit points added, not to the dead. */
static void heal(Character *c, uint32_t n)
{
    if (c->flags & 1) return;
    c->hit_points += n;
    if (c->hit_points > c->max_hit_points) c->hit_points = c->max_hit_points;
}

/* ---- Barabbas ---- */

/* Barabbas's modes (+0x2c): 1 shoot, 2 hold (the gun firing on, or the shot's wait), 3 advance, 4 advance
   firing the alternate, 5 hand to hand (or a gun), 6 heal (the animation 0xdb), 7 hand to hand, 8 back
   off. */
enum { BAR_SHOOT = 1, BAR_HOLD, BAR_ADVANCE, BAR_ADVANCE_ALT, BAR_MELEE_GUN, BAR_HEAL, BAR_MELEE, BAR_BACK };

/* his healing's wait from the hit points: 90 ticks, and 90 more at full health */
static uint32_t heal_wait(const Character *c) { return c->hit_points * 90 / c->max_hit_points + 90; }

/* AI2iBehavior_Barabbas 0x491540, under each of his behaviours (old: the behaviour entered from or left
   for; damage: a hit's). */
static int barabbas(AiState *a, Character *c, int msg, bool *handled, int arg)
{
    CombatState *s = &a->combat;
    BarabbasState *b = &s->barabbas;
    *handled = true;
    switch (msg) {
    case MSG_ENTER:
        /* from another behaviour than his: his state afresh */
        if (arg < 5 || arg > 7) {
            b->prev = 0;
            b->shot_wait = b->regen_damage = b->regen_wait = b->retreat = 0;
        }
        return 0;
    case MSG_WEIGHTS: {
        b->shot_wait = b->shot_wait > 5 ? b->shot_wait - 5 : 0;
        b->regen_wait = b->regen_wait > 5 ? b->regen_wait - 5 : 0;
        if (can_act(c)) {
            /* backing off: when it ends, no healing for 10 seconds */
            if (b->retreat > 5) b->retreat -= 5;
            else if (b->retreat) b->retreat = 0, b->regen_wait = 600;
        }
        int mode = b->mode;
        float health = (float)c->hit_points * 100.0f / (float)c->max_hit_points;
        /* C+0x1024: what he has healed; unkillable (C+0x04 flag 0x20), he heals no more than his maximum */
        bool spent = (c->flags & 0x20) && a->healed >= c->max_hit_points * 100 / 100;
        bool checked = false;
        if (weapon_firing(c) && !s->alt) {
            mode = BAR_HOLD;
            goto chosen;
        }
        if (b->prev == BAR_HEAL) {
            if (spent) {
                b->regen_wait = 10000;
            } else if ((float)b->regen_damage * 100.0f / (float)c->max_hit_points >= 5.0f) {
                /* hurt too much while healing: stop */
                b->regen_damage = 0;
                b->regen_wait = heal_wait(c);
            } else if (health > 80.0f) {
                b->regen_wait = 240;
            } else if (s->dist >= 20.0f) {
                mode = BAR_HEAL;
                goto chosen;
            } else if (health < 15.0f) {
                mode = BAR_BACK;
                goto chosen;
            }
            checked = true;
        }
        if (!checked && !spent && health <= 40.0f && b->regen_wait == 0) {
            /* hurt: back off, then heal when free to */
            float far = b->mode == BAR_BACK ? 80.0f : 40.0f;
            mode = far <= s->dist && can_act(c) ? BAR_HEAL : BAR_BACK;
            goto chosen;
        }
        /* no gun, or one out of ammo with nothing to reload it: hand to hand */
        if (!c->weapon || weapon_has_empty(c)) mode = BAR_MELEE_GUN;
    chosen:
        b->mode = mode;
        /* AI2rMovement_ChangeMovementMode: running aiming while he may run with the gun, or when not
           shooting; else walking aiming */
        a->movement_mode = boss_barabbas_run || mode == BAR_MELEE_GUN || mode == BAR_HEAL || mode == BAR_BACK
                               ? AI_MODE_RUN_AIMING : AI_MODE_WALK_AIMING;
        if (mode != b->prev) {
            if (b->prev == BAR_HEAL) chr_do_animation(c, 4, 6);    /* the healing broken off */
            if (mode == BAR_HEAL) b->regen_damage = 0, b->retreat = 0;
            else if (mode == BAR_BACK) b->retreat = 240;
        }
        b->prev = mode;
        switch (mode) {
        case BAR_SHOOT: case BAR_HOLD:
            s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
            return 0;
        case BAR_ADVANCE: case BAR_ADVANCE_ALT:
            s->weight[COMBAT_MANEUVER_HOLD] = 0.3f;
            s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
            return 0;
        case BAR_MELEE_GUN:
            if (s->dist > 30.0f) {
                s->gun_near = 100.0f;
                s->gun_far = 350.0f;
                s->weight[COMBAT_MANEUVER_GUN] = 1.0f;
            }
            s->weight[COMBAT_MANEUVER_MELEE] = 0.8f;
            return 0;
        case BAR_HEAL:
            s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
            /* the loop of the healing animation (from its state 0x44: 0x4e3ab0): a 150th of his
               maximum each time */
            if (c->type == 0xdb && c->anim && tr_from(c->anim) == 0x44) {
                uint32_t n = c->max_hit_points * 8 / 1200;
                heal(c, n);
                a->healed += n;
            }
            chr_enter_fight_mode(c);
            chr_do_animation(c, 1, 0xdb);
            return 0;
        case BAR_MELEE:
            s->weight[COMBAT_MANEUVER_MELEE] = 1.0f;
            return 0;
        case BAR_BACK:
            s->weight[COMBAT_MANEUVER_HOLD] = 0.3f;
            s->weight[COMBAT_MANEUVER_RETREAT] = 1.0f;
            s->ret_near = 30.0f;
            s->ret_far = 100.0f;
            return 0;
        default:
            return 0;
        }
    }
    case MSG_EXIT:
        return 0;
    case MSG_MELEE:
        s->melee_ok = false;
        return 0;
    case MSG_FLAGS:
        s->targeting = true;
        if (b->mode == BAR_MELEE || b->mode == BAR_HEAL || b->mode == BAR_BACK) {
            s->check_env = s->check_friends = s->aim = s->fire = false;
            return 0;
        }
        s->check_env = s->check_friends = s->aim = true;
        switch (b->mode) {
        case BAR_SHOOT: s->fire = true, s->alt = false; return 0;
        case BAR_ADVANCE_ALT: s->fire = true, s->alt = true; return 0;
        case BAR_HOLD: case BAR_ADVANCE: case BAR_MELEE_GUN: s->fire = false; return 0;
        default: return 0;
        }
    case MSG_DAZE:
        b->regen_wait = heal_wait(c);
        *handled = false;
        return 0;
    case MSG_CLOSE:
        /* holding or hand to hand, he isn't moved off by the target's closeness */
        if (b->mode == BAR_HOLD || (b->mode > BAR_ADVANCE_ALT && b->mode <= BAR_BACK)) return 0;
        *handled = false;
        return 0;
    case MSG_KEEP_AIM:
        if (b->mode == BAR_SHOOT || b->mode == BAR_HOLD || b->mode == BAR_ADVANCE_ALT) return 1;
        *handled = false;
        return 0;
    case MSG_HURT:
        if (b->mode == BAR_HEAL) b->regen_damage += (uint32_t)arg;
        else b->regen_wait = heal_wait(c);
        return 0;
    default:
        *handled = false;
        return 0;
    }
}

/* Behaviour 5, Barabbas Shoot (0x491470): shooting, holding for 3 seconds after each shot. */
static int barabbas_shoot(AiState *a, Character *c, int msg, bool *handled, int p5, int p6)
{
    BarabbasState *b = &a->combat.barabbas;
    *handled = false;
    switch (msg) {
    case MSG_ENTER:
        b->mode = BAR_SHOOT;
        if (!boss_barabbas_run) a->movement_mode = AI_MODE_WALK_AIMING;
        break;
    case MSG_WEIGHTS:
        b->mode = b->shot_wait == 0 ? BAR_SHOOT : BAR_HOLD;
        break;
    case MSG_EXIT:
        a->movement_mode = 0;
        break;
    case MSG_FIRE:
        if (p5 && !p6) b->shot_wait = 180;
        *handled = true;
        return 0;
    default:
        break;
    }
    return barabbas(a, c, msg, handled, p5);
}

/* Behaviour 6, Barabbas Advance (0x491b10): closing in firing the alternate for 6 to
   7.5 seconds after each shot, else shooting. */
static int barabbas_advance(AiState *a, Character *c, int msg, bool *handled, int p5, int p6)
{
    BarabbasState *b = &a->combat.barabbas;
    *handled = false;
    switch (msg) {
    case MSG_WEIGHTS:
        if (b->shot_wait) {
            b->mode = BAR_ADVANCE_ALT;
            break;
        }
        /* fall through */
    case MSG_ENTER:
        b->mode = BAR_SHOOT;
        break;
    case MSG_EXIT:
        a->movement_mode = 0;
        break;
    case MSG_FIRE:
        if (p5 && !p6) b->shot_wait = (uint16_t)(((uint32_t)trac_random() * 90) >> 16) + 360u;   /* UUrRandom */
        *handled = true;
        return 0;
    default:
        break;
    }
    return barabbas(a, c, msg, handled, p5);
}

/* Behaviour 7, Barabbas Melee (0x491be0): hand to hand, holding while his gun fires. */
static int barabbas_melee(AiState *a, Character *c, int msg, bool *handled, int p5)
{
    BarabbasState *b = &a->combat.barabbas;
    *handled = false;
    if (msg == MSG_ENTER) b->mode = BAR_MELEE;
    else if (msg == MSG_WEIGHTS) b->mode = weapon_firing(c) ? BAR_HOLD : BAR_MELEE;
    return barabbas(a, c, msg, handled, p5);
}

int boss_behaviour(AiState *a, Character *c, int id, int msg, bool *handled, int p5, int p6, const void *ptr)
{
    (void)ptr;
    switch (id) {
    case 5: return barabbas_shoot(a, c, msg, handled, p5, p6);
    case 6: return barabbas_advance(a, c, msg, handled, p5, p6);
    case 7: return barabbas_melee(a, c, msg, handled, p5);
    default:
        /* the ninja's (8 to 10), Run For Alarm (11) and Muro's (12, 13): not yet, the default's */
        *handled = false;
        return 0;
    }
}
