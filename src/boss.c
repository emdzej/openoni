/* The bosses' combat behaviours: see boss.h. */
#include "boss.h"

#include "chars.h"
#include "chr_anim.h"
#include "combat.h"
#include "localpath.h"
#include "objc.h"
#include "powerup.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
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
        ai_set_movement_mode(a, boss_barabbas_run || mode == BAR_MELEE_GUN || mode == BAR_HEAL || mode == BAR_BACK
                               ? AI_MODE_RUN_AIMING : AI_MODE_WALK_AIMING);
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
        if (!boss_barabbas_run) ai_set_movement_mode(a, AI_MODE_WALK_AIMING);
        break;
    case MSG_WEIGHTS:
        b->mode = b->shot_wait == 0 ? BAR_SHOOT : BAR_HOLD;
        break;
    case MSG_EXIT:
        ai_set_movement_mode(a, 0);
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
        ai_set_movement_mode(a, 0);
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


/* ---- Mutant Muro ---- */

/* 0x4ed3c0: standing still (the type 6 in the standing state). */
static bool standing_still(const Character *c) { return c->type == 6 && c->state == 7 && c->from_state == 7; }

/* 0x492cd0: the shield drops over length ticks and stays down for off (a drop under way is rescaled to
   the new length, the longer stay kept). From a rise under way, the drop starts from the rise's part
   done (the original's: the part left would be the shield's level). */
static void muro_drop(MuroState *m, uint32_t length, uint32_t off)
{
    if (m->off == 0 && m->drop == 0) {
        m->up = 0;
        m->drop_length = length;
        m->drop = m->rise ? m->rise * length / m->rise_length : length;
        m->off = off;
        return;
    }
    if (m->off < off) m->off = off;
    if (m->drop) {
        uint32_t old = m->drop_length;
        m->drop_length = length;
        m->drop = m->drop * length / old;
    }
    m->up = 0;
}

static uint32_t down5(uint32_t t) { return t > 5 ? t - 5 : 0; }

/* AI2iBehavior_MutantMuro 0x492d50, before each of his behaviours: his state afresh on entering from
   another's; every weights message his shield (C+0x1668: falling, off, rising, or full and counting its
   time), the damage taken lately and the timers; hits and his animations' (TRAM flag 22: the shield drops
   for 30, off 360). True when the message is taken. */
static bool muro(AiState *a, Character *c, int msg, bool *handled, int p5, const void *tram, int *r)
{
    MuroState *m = &a->combat.muro;
    *r = 0;
    switch (msg) {
    case MSG_ENTER:
        if (p5 < 12 || p5 > 13) memset(m, 0, sizeof *m);
        return false;
    case MSG_WEIGHTS:
        if (m->damage) {
            if (m->damage_timer < 6) m->damage_timer = m->damage = 0;
            else m->damage_timer -= 5;
        }
        if (m->drop) {
            c->super_level = (float)m->drop / (float)m->drop_length;
            m->up = 0;
            m->drop = down5(m->drop);
        } else if (m->off) {
            c->super_level = 0.0f;
            m->up = 0;
            if (m->off < 6) {
                m->off = 0;
                m->rise = m->rise_length = 240;
            } else {
                m->off -= 5;
            }
        } else if (m->rise) {
            c->super_level = 1.0f - (float)m->rise / (float)m->rise_length;
            m->up = 0;
            m->rise = down5(m->rise);
        } else {
            c->super_level = 1.0f;
            m->up += 5;
        }
        m->engaged = down5(m->engaged);
        m->charge = down5(m->charge);
        m->cooldown = down5(m->cooldown);
        return false;
    case MSG_MELEE:
        a->combat.melee_ok = true;
        return false;
    case MSG_HURT:
        m->damage_timer = 75;
        m->damage += (uint32_t)p5;
        return false;
    case MSG_ANIM_END:
        if (tram && tr_test_flag(tram, 22)) muro_drop(m, 30, 360);
        return false;
    case 16:
        *handled = true;
        *r = m->up < 360;
        return true;
    default:
        return false;
    }
}

/* Behaviour 12, Mutant Muro Melee (0x492f90): hand to hand while shielded; unshielded, hit hard (12 or
   more lately) with the target within 30, he stands and, once still, blasts (the animation 0x91). */
static int muro_melee(AiState *a, Character *c, int msg, bool *handled, int p5, const void *tram)
{
    CombatState *s = &a->combat;
    MuroState *m = &s->muro;
    int r;
    *handled = false;
    if (muro(a, c, msg, handled, p5, tram, &r) || *handled) return r;
    *handled = true;
    switch (msg) {
    case MSG_ENTER: case MSG_EXIT:
        return 0;
    case MSG_WEIGHTS:
        m->engaged = 720;
        m->charge = 0;
        if (c->super_level > 0.5f || m->damage < 12 || s->dist >= 30.0f) {
            if (c->super_level > 0.5f) m->damage = m->damage_timer = 0;
            s->weight[COMBAT_MANEUVER_MELEE] = 1.0f;
            return 0;
        }
        if (c->state == 0x12 || c->state == 0x32) {     /* lying (0x4e4070) */
            s->weight[COMBAT_MANEUVER_GETUP] = 1.0f;
            return 0;
        }
        s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        if (standing_still(c)) {
            c->block_pause = c->stun_pause = 0;
            c->countdown_1c9a = 0;
            c->hard_pause = c->soft_pause = 0;
            chr_enter_fight_mode(c);
            chr_do_animation(c, 1, 0x91);
            m->damage = m->damage_timer = 0;
        }
        return 0;
    case MSG_FLAGS:
        s->check_env = s->check_friends = false;
        s->targeting = true;
        s->aim = s->fire = false;
        return 0;
    case MSG_KEEP_AIM:
        return 1;
    default:
        *handled = false;
        return 0;
    }
}

/* Behaviour 13, Mutant Muro Thunderbolt (0x492ac0): closing in; with the target beyond 60 or in his
   line of fire for a while (120 ticks), unless hit hard (more than 35) or within 40 (then 300 ticks
   without), he stands and casts the thunderbolt (the animation 0xe7), his shield dropping (90, off
   600). */
static int muro_thunderbolt(AiState *a, Character *c, int msg, bool *handled, int p5, const void *tram)
{
    CombatState *s = &a->combat;
    MuroState *m = &s->muro;
    int r;
    *handled = false;
    if (muro(a, c, msg, handled, p5, tram, &r) || *handled) return r;
    *handled = true;
    switch (msg) {
    case MSG_ENTER: case MSG_EXIT:
        return 0;
    case MSG_WEIGHTS: {
        if (c->state == 0x12 || c->state == 0x32) {
            s->weight[COMBAT_MANEUVER_GETUP] = 1.0f;
            return 0;
        }
        bool open = s->dist > 60.0f || s->lof;
        if (m->damage > 35 || s->dist < 40.0f) m->cooldown = 300;
        else if (open) m->charge = 120;
        if (m->engaged && m->charge && !m->cooldown) {
            s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
            if (c->queued == 0xe7) return 0;
            if (!standing_still(c) && c->type != 0xe7) return 0;
            chr_enter_fight_mode(c);
            if (!chr_do_animation(c, 1, 0xe7)) {
                m->cooldown = 300;
                return 0;
            }
            muro_drop(m, 90, 600);
            return 0;
        }
        if (!m->engaged && m->charge && !m->cooldown) m->cooldown = 300;
        m->engaged = 720;
        m->charge = 0;
        m->damage = m->damage_timer = 0;
        s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
        return 0;
    }
    case MSG_FLAGS:
        s->check_env = s->check_friends = true;
        s->targeting = true;
        s->aim = false;
        return 0;
    case MSG_KEEP_AIM:
        return 1;
    case MSG_RANGE:
        /* charging, he keeps his range */
        return m->charge && !m->cooldown;
    default:
        *handled = false;
        return 0;
    }
}

/* ---- the super ninja ---- */

enum { NIN_MELEE = 1, NIN_FIREBALL, NIN_ADVANCE, NIN_ALT, NIN_FLAG, NIN_BEHIND };

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static float wrap(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

/* 0x4e37b0: knocked down, getting up, a victim or stunned (as combat.c's). */
static bool knocked(uint16_t t)
{
    return (t >= 0x4c && t <= 0x5c) || (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) ||
           (t >= 0x7e && t <= 0x84) || t == 0x8d || t == 0xc6 || (t >= 0xd4 && t <= 0xd7);
}

static bool lying(const Character *c) { return c->state == 0x12 || c->state == 0x32; }  /* 0x4e4070 */

/* 0x4e33a0: the way the character faces, its run's turn taken off. */
static float heading(const Character *c) { return wrap(c->desired_facing - c->facing_offset); }

static uint32_t down5u(uint32_t t) { return t < 6 ? 0 : t - 5; }

static bool even_chance(NinjaState *n)
{
    if (!n->rolled) {
        n->rolled = true;
        n->chance = trac_random() < 0x7fff;     /* UUrRandom */
    }
    return n->chance;
}

/* The teleport to a flag (mode 5; AI2iBehavior_FindRetreatFlag, inlined): of the flags from 9500 on, the one best placed: about 200 from the
   target (all of it at 200, falling to a fifth at 100 off and beyond) and in front of it (within 15
   degrees, falling to a fifth at the edge; a fifth beyond); he faces the target from there. */
static bool teleport_to_flag(Character *c, const Character *t)
{
    float tf = heading(t), best = 0.0f;
    bool found = false;
    ObjcObject f;
    for (uint16_t id = 9500; objc_flag_find(id, &f); id++) {
        float dx = f.pos[0] - t->pos[0], dy = f.pos[1] - t->pos[1], dz = f.pos[2] - t->pos[2];
        float r = sqrtf(dx * dx + dy * dy + dz * dz), a = atan2f(dx, dz) - tf;
        if (a < -PI) a += TWO_PI;
        else if (a > PI) a -= TWO_PI;
        float off = fabsf(r - 200.0f), w = off <= 100.0f ? 1.0f - off * 0.00800000037997961f : 0.2f;
        float score = fabsf(a) <= 0.2617993950843811f ? (1.0f - fabsf(a) * 3.0557749271392822f) * w : w * 0.2f;
        if (!found || best < score) {
            best = score;
            memcpy(c->teleport_to, f.pos, sizeof c->teleport_to);
            found = true;
        }
    }
    c->teleport = found;
    if (found) c->teleport_facing = wrap(atan2f(t->pos[0] - c->teleport_to[0], t->pos[2] - c->teleport_to[2]));
    return found;
}

/* The teleport behind the target (mode 6): a point 35 behind it (15 at least, up to 6 steps of 15
   degrees to either side), facing it. */
static bool teleport_behind(Character *c, const Character *t)
{
    float p[3];
    if (!local_find_nearby_point(t->pos, 35.0f, 15.0f, wrap(heading(t) + PI), 0.2617993950843811f, 6, 4, 3, p)) return false;
    c->teleport = true;
    memcpy(c->teleport_to, p, sizeof p);
    c->teleport_facing = wrap(atan2f(t->pos[0] - p[0], t->pos[2] - p[2]));
    return true;
}

/* Message 13, below the level (550): hidden there for 150 ticks; then back at the nearest point of the
   loop the flags from 9000 on make, 40 off it (a random turn of up to 30 degrees from square to it, up
   to 4 steps of 11.25 degrees to either side; else 80 straight out), facing out, through the teleport's
   second half (0xdf from state 7), seen and on the ground (AI2iBehavior_FindEdgeFlag, inlined). True
   when taken. */
static bool fell(AiState *a, Character *c)
{
    NinjaState *n = &a->combat.ninja;
    if (++n->fallen < 150) {
        c->pos[1] = c->prev_pos[1] = c->phys_pos[1] = 550.0f;
        return true;
    }
    ObjcObject f;
    float first[3], prev[3], p[3], best[3] = { 0 }, best_d = 0.0f, facing = 0.0f;
    bool found = false, done = false;
    for (uint16_t id = 9000; !done; id++) {
        if (!objc_flag_find(id, &f)) {
            if (id == 9000) return false;
            done = true;
            memcpy(p, first, sizeof p);      /* the loop closed: the last flag back to the first */
        } else {
            memcpy(p, f.pos, sizeof p);
            if (id == 9000) {
                memcpy(first, p, sizeof first);
                memcpy(prev, p, sizeof prev);
                continue;
            }
        }
        float dx = p[0] - prev[0], dz = p[2] - prev[2], q[3];
        float u = ((c->pos[2] - prev[2]) * dz + (c->pos[0] - prev[0]) * dx) / (dx * dx + dz * dz);
        if (u <= 0.0f) memcpy(q, prev, sizeof q);
        else if (u >= 1.0f) memcpy(q, p, sizeof q);
        else for (int i = 0; i < 3; i++) q[i] = u * p[i] + (1.0f - u) * prev[i];
        float ex = q[0] - c->pos[0], ez = q[2] - c->pos[2], d = ex * ex + ez * ez;
        if (!found || d < best_d) {
            memcpy(best, q, sizeof best);
            facing = wrap(atan2f(dz, -dx));
            best_d = d;
            found = true;
        }
        memcpy(prev, p, sizeof prev);
    }
    if (!found) return false;
    float turn = wrap((float)trac_random() * 1.5259021893143654e-05f * 1.0471975803375244f - 0.5235987901687622f + facing);
    float to[3];
    if (local_find_nearby_point(best, 40.0f, 0.0f, turn, 0.19634954631328583f, 4, 4, 3, to)) {
        memcpy(c->teleport_to, to, sizeof to);
    } else {
        memcpy(c->teleport_to, best, sizeof best);
        c->teleport_to[0] += sinf(facing) * 80.0f;
        c->teleport_to[2] += cosf(facing) * 80.0f;
    }
    c->teleport_facing = facing;
    c->teleport = true;
    const void *t = chr_lookup_varient(c, 0xdf, 7, c->varient | 0x200);
    if (!t) return false;
    chr_set_animation_external(c, 7, t, 0);
    c->invisibility = 0;
    c->air_ticks = 0;
    c->air_velocity[0] = c->air_velocity[1] = c->air_velocity[2] = 0.0f;
    n->fallen = 0;
    return true;
}

/* The animation of a mode (type), once he stands still: started, true; lying or a victim, its cooldown
   is 300. */
static bool start_mode_animation(Character *c, uint16_t type, bool *started, uint32_t *cool)
{
    if (*started) return false;
    if (standing_still(c)) {
        chr_enter_fight_mode(c);
        if (chr_do_animation(c, 1, type)) *started = true;
        return false;
    }
    if (lying(c) || chr_is_victim_type(c->type)) {
        *cool = 300;
        return true;
    }
    return false;
}

/* AI2iBehavior_SuperNinja 0x491cc0, under each of his behaviours (8 fireball, 9 advance, 10 melee: the
   mode they start each weights message from): his state afresh on entering from another's; the
   cooldowns, the damage taken lately (it decays by 3 every 6 weights messages, 240 ticks after a hit);
   hit hard (35 lately) within 50 he teleports to a flag (if he can act and isn't invisible); advancing
   80 or more away, an even chance to teleport behind the target, else (40 or more away, the cooldown
   over) the alternate fireball (0xe1: invisible for 600 at its action frame). Every mode's animation
   ends it (the teleport's 0xdf back to none, the damage forgotten). Below the level, message 13. */
static int ninja(AiState *a, Character *c, int msg, bool *handled, int p5)
{
    CombatState *s = &a->combat;
    NinjaState *n = &s->ninja;
    const Character *t = a->target ? &a->target->c : NULL;
    *handled = true;
    switch (msg) {
    case MSG_ENTER:
        s->fall_check = true;
        s->fall_y = 550.0f;
        if (p5 < 8 || p5 > 10) memset(n, 0, sizeof *n);
        return 0;
    case MSG_EXIT:
        return 0;
    case MSG_WEIGHTS:
        break;
    case MSG_MELEE:
        s->melee_ok = true;
        return 0;
    case MSG_FLAGS:
        s->check_env = s->check_friends = true;
        s->targeting = true;
        s->aim = false;
        return 0;
    case MSG_KEEP_AIM:
        if (n->mode == NIN_FIREBALL) return 1;
        *handled = false;
        return 0;
    case MSG_HURT:
        n->damage_hold = 240;
        n->damage += (uint32_t)p5;
        return 0;
    case MSG_ACTION:
        if (p5 == 0xe0) {
            n->fire_cool = 1200;
            n->fire_started = false;
        } else if (p5 == 0xe1) {
            powerup_give(c, POWERUP_INVISIBILITY, 600, true, false);
            n->alt_cool = 1200;
            n->alt_started = false;
        }
        return 0;
    case MSG_ANIM_END:
        /* a new animation (type p5): another than the mode's ends it */
        if (n->fire_started) {
            if (p5 != 0xe0) n->fire_cool = 300, n->fire_started = false;
        } else if (n->alt_started) {
            if (p5 != 0xe1) n->alt_cool = 300, n->alt_started = false;
        } else if (n->teleport_started || p5 == 0xdf) {
            n->mode = n->prev = 0;
            n->teleport_started = false;
            if (p5 == 0xdf) n->count = n->damage = 0, n->rolled = false;
            else n->teleport_cool = 300;
        }
        return 0;
    case MSG_FELL:
        if (fell(a, c)) return 1;
        *handled = false;
        return 0;
    default:
        *handled = false;
        return 0;
    }
    n->fire_cool = down5u(n->fire_cool);
    n->alt_cool = down5u(n->alt_cool);
    n->teleport_cool = down5u(n->teleport_cool);
    if (n->count == 0) {
        n->count = 6;
        if (n->damage_hold < 31) n->damage = n->damage < 4 ? 0 : n->damage - 3;
        else n->damage_hold -= 30;
    }
    n->count--;
    if (n->mode < NIN_FLAG || n->mode > NIN_BEHIND || lying(c) || knocked(c->type)) {
        if (c->invisibility == 0) {
            if (n->damage >= 35 && s->dist < 50.0f && can_act(c)) {
                n->mode = NIN_FLAG;
            } else if (n->mode == NIN_ADVANCE && s->dist >= 80.0f && can_act(c) && even_chance(n)) {
                n->mode = NIN_BEHIND;
            } else if (n->alt_cool == 0 && can_act(c) && n->mode != NIN_FIREBALL && s->dist >= 40.0f && !even_chance(n)) {
                n->mode = NIN_ALT;
            }
        }
    } else {
        n->mode = n->prev;      /* a teleport under way */
    }
    if (n->mode != n->prev) {
        if (n->prev == NIN_FIREBALL) n->fire_started = false;
        else if (n->prev == NIN_ALT) n->alt_started = false;
        if (n->mode == NIN_FLAG) {
            if (!t || !teleport_to_flag(c, t)) n->mode = NIN_MELEE;
        } else if (n->mode == NIN_BEHIND) {
            if (!t || !teleport_behind(c, t)) n->mode = NIN_ADVANCE;
        }
    }
    n->prev = n->mode;
    switch (n->mode) {
    case NIN_MELEE:
        n->rolled = false;
        s->weight[COMBAT_MANEUVER_MELEE] = 1.0f;
        return 0;
    case NIN_FIREBALL:
        s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        start_mode_animation(c, 0xe0, &n->fire_started, &n->fire_cool);
        return 0;
    case NIN_ADVANCE:
        s->weight[COMBAT_MANEUVER_ADVANCE] = 1.0f;
        return 0;
    case NIN_ALT:
        s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        start_mode_animation(c, 0xe1, &n->alt_started, &n->alt_cool);
        return 0;
    case NIN_FLAG: case NIN_BEHIND:
        s->weight[COMBAT_MANEUVER_HOLD] = 1.0f;
        if (start_mode_animation(c, 0xde, &n->teleport_started, &n->teleport_cool)) {
            n->mode = NIN_MELEE;
            n->prev = 0;
            s->weight[COMBAT_MANEUVER_MELEE] = 1.2f;
        }
        return 0;
    default:
        return 0;
    }
}

/* Behaviours 8, AI2iBehavior_SuperNinjaFireball 0x491c50 (the fireball, with its cooldown over and free
   to act, else advancing), 9, AI2iBehavior_SuperNinjaAdvance 0x492a20, 10, AI2iBehavior_SuperNinjaMelee
   0x492a70: the mode each sets on entering and each weights message (over a teleport under way, too),
   then the common part. */
static int ninja_behaviour(AiState *a, Character *c, int id, int msg, bool *handled, int p5)
{
    NinjaState *n = &a->combat.ninja;
    if (msg == MSG_ENTER || msg == MSG_WEIGHTS) {
        if (id == 8) n->mode = msg == MSG_ENTER || (n->fire_cool == 0 && can_act(c)) ? NIN_FIREBALL : NIN_ADVANCE;
        else n->mode = id == 9 ? NIN_ADVANCE : NIN_MELEE;
    }
    return ninja(a, c, msg, handled, p5);
}

int boss_behaviour(AiState *a, Character *c, int id, int msg, bool *handled, int p5, int p6, const void *ptr)
{
    switch (id) {
    case 5: return barabbas_shoot(a, c, msg, handled, p5, p6);
    case 6: return barabbas_advance(a, c, msg, handled, p5, p6);
    case 7: return barabbas_melee(a, c, msg, handled, p5);
    case 8: case 9: case 10: return ninja_behaviour(a, c, id, msg, handled, p5);
    case 12: return muro_melee(a, c, msg, handled, p5, ptr);
    case 13: return muro_thunderbolt(a, c, msg, handled, p5, ptr);
    default:
        /* Run For Alarm (11): not yet, the default's */
        *handled = false;
        return 0;
    }
}
