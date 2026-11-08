/* Hits between characters: see chr_hit.h. */
#include "chr_hit.h"
#include "gametime.h"
#include "inventory.h"

#include "body.h"
#include "chr_anim.h"
#include "impact.h"
#include "m3gm.h"
#include "neutral.h"
#include "sound.h"
#include "tm.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void (*chr_hit_debug_log)(const char *msg);

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

void chr_hit_init(Character *c, int32_t extra)
{
    int32_t hp = (int32_t)rd32((const uint8_t *)c->oncc + 0xc50) + extra;
    c->hit_points = c->max_hit_points = (uint32_t)(hp < 1 ? 1 : hp);
}

/* ---- the reactions ---- */

/* The knockdowns a type falls back on when the character's collection has none of it (0x4e4e10). */
static uint16_t knockdown_fallback(uint16_t t)
{
    switch (t) {
    case 0x4c: case 0x4d: case 0x4e: case 0x52: case 0x54: case 0x7e: case 0x7f: return 0x4d;
    case 0x4f: case 0x50: case 0x51: case 0x53: return 0x50;
    case 0x55: case 0x56: case 0x57: case 0x5b: return 0x56;
    case 0x58: case 0x59: case 0x5a: case 0x5c: return 0x59;
    default: return t;
    }
}

/* ONrCharacter_Knockdown 0x4e4e10: fight mode, the combo and the hold dropped, then the reaction: in the
   air a hit's fall (0x4ecd70), lying the hit on the ground (0x54), crouching the reaction upgraded to a
   knockdown (0x4e5320). Looked for from the state, then standing (7), then the type asked and its
   fallback the same way, then for an airborne state (0x25..0x2e) from its falling half and from 0x26;
   blended in over 8 ticks, the stand next. */
static bool down_type(uint16_t t);
static uint16_t in_air(uint16_t t);
static uint16_t upgrade_to_knockdown(uint16_t t);
void (*chr_knockdown_hook)(Character *c);
bool (*chr_path_through_hook)(const Character *c, const Character *o);
void (*chr_movement_hit_hook)(Character *c, int kind, uintptr_t id, const float point[3], const float plane[4]);

void chr_knockdown(Character *c, uint16_t type)
{
    if (c->inactive && chr_activate_hook) chr_activate_hook(c, true);   /* 0x4db830 first */
    c->varient |= 0x200;
    c->fight_timer = chr_class_fight_time(c->oncc);
    c->combo_type = 0;
    c->combo_time = 0;
    c->hold = 0;
    uint16_t r = type, s = c->state;
    if (chr_is_victim_type(type) && c->air_ticks) r = in_air(type);
    else if (s == 0x12 || s == 0x32) r = 0x54;
    else if (s == 0x13) r = upgrade_to_knockdown(type);
    uint16_t f = knockdown_fallback(type);
    const void *t = chr_lookup(c, r, s);
    if (!t) t = chr_lookup(c, r, 7);
    if (!t) t = chr_lookup(c, type, s);
    if (!t) t = chr_lookup(c, f, s);
    if (!t) t = chr_lookup(c, type, 7);
    if (!t) t = chr_lookup(c, f, 7);
    if (!t && s >= 0x25 && s <= 0x2e) {
        /* the jumps' rising states (the odd ones) look in their falling half first; the original tests
           the fallback's low byte, never zero for a reaction */
        if (s & 1) t = chr_lookup(c, type, s + 1);
        if (!t && (uint8_t)f) t = chr_lookup(c, type, 0x26);
    }
    if (!t) return;     /* "failed to find knockdown animation" */
    /* a knockdown that leaves an AI's character down, from not lying: the AI told (0x48b530) */
    if (down_type(type) && s != 0x12 && s != 0x32 && c->ai && chr_knockdown_hook) chr_knockdown_hook(c);
    chr_set_animation_external(c, s, t, 8);     /* the remap hook only changes throws */
    c->queued = CHR_TYPE_STAND;
}

/* ONiCharacter_RunDeathAnimation 0x4eb970 (and the same test in TakeDamage): unless already lying
   (state 0x12 or 0x32, come from another state) in a reaction, knocked down by type if it is a reaction,
   else by 0x4f. */
static void death_knockdown(Character *c, uint16_t type)
{
    bool lying = c->state == 0x12 || c->state == 0x32;
    bool settled = lying && c->from_state == c->state;
    if (!settled && chr_is_victim_type(c->type) && lying) return;
    chr_knockdown(c, chr_is_victim_type(type) ? type : 0x4f);
}

/* The reactions that leave the character down, which a killing hit plays (TakeDamage's switch; the
   same table as ONrAnimType_IsKnockdown 0x4eccb0's). */
bool chr_is_down_type(uint16_t t) { return down_type(t); }
static bool down_type(uint16_t t)
{
    switch (t) {
    case 0x4f: case 0x50: case 0x51: case 0x53: case 0x58: case 0x59: case 0x5a: case 0x5c: case 0x8d:
    case 0xd6: case 0xd7:
        return true;
    default:
        return false;
    }
}

void (*chr_hurt_hook)(Character *c, Character *from, uint32_t damage, uint32_t kind);
void (*chr_melee_score_hook)(Character *attacker, Character *victim, uint32_t damage);
bool (*chr_block_hook)(Character *c);
void (*chr_attack_landed_hook)(Character *attacker, Character *defender);
void (*chr_die_hook)(Character *c, Character *killer);
void (*chr_script_hook)(Character *c, int script);

bool chr_cheat_unthrowable, chr_cheat_unstoppable, chr_cheat_invincible, chr_cheat_omnipotent, chr_cheat_fists;

void chr_cheats_clear(void)
{
    chr_cheat_unthrowable = chr_cheat_invincible = chr_cheat_omnipotent = chr_cheat_unstoppable = chr_cheat_fists = false;
}

bool chr_is_unstoppable(const Character *c) { return (chr_cheat_unstoppable && !c->ai) || (c->flags & 0x100); }

void chr_die(Character *c, Character *killer)
{
    c->flags |= 1;
    if (chr_die_hook) chr_die_hook(c, killer);
    death_knockdown(c, 0);
}

/* ---- the pain ---- */

/* The class's pain block (ONCC+0x74..0x8c, u16s but the last): the cry's chance at no damage and at the
   most (+0x74, +0x76, percent) and the damage for the most (+0x78); the ticks before the damage summed is
   forgotten (+0x7a) and between cries (+0x7c); the light and medium cries before a heavier one (+0x7e,
   +0x80); a death cry's chance (+0x82); the damage for full volume (+0x84) and for a medium and a heavy
   cry (+0x86, +0x88); the volume at none (+0x8c, a float). The cries' impulses are named at +0x90,
   +0xb0, +0xd0, +0xf0 (light, medium, heavy, death; resolved into +0x110..+0x11c). */
static uint16_t pain_u16(const Character *c, unsigned off) { const uint8_t *k = (const uint8_t *)c->oncc + off; return (uint16_t)(k[0] | k[1] << 8); }
static const void *pain_sound(const Character *c, uint32_t kind)
{
    char name[33];
    memcpy(name, (const uint8_t *)c->oncc + 0x90 + 0x20 * kind, 32);
    name[32] = 0;
    return name[0] ? sound_impulse_find(name) : NULL;
}

/* The head (A+0x247c, the part 10's place), the cries' place; inactive, the location 15 up (0x529d84). */
static void cry_place(const Character *c, float p[3])
{
    if (c->has_physics) memcpy(p, &c->bone_world[10][9], 3 * sizeof(float));
    else p[0] = c->pos[0], p[1] = c->pos[1] + 15.0f, p[2] = c->pos[2];
}

bool (*chr_calm_hurt_hook)(Character *c);

/* ONrCharacter_Pain 0x4f2250: damage of a kind (0 light, 1 knocked down, 2 blown up). A calm AI (0x48b060,
   chr_calm_hurt_hook) always cries; else the first hurt cries by a chance growing with the damage. The
   damage sums (forgotten after the class's ticks); between cries the class's wait; the kind no lighter than
   the last, light ones becoming medium past the class's count or damage, medium ones heavy. The cry is
   queued 7 ticks, its volume by the damage summed (the class's least at none, full at its damage). */
static void pain(Character *c, uint32_t damage, uint32_t kind)
{
    if (!damage) return;
    if (!(c->ai && chr_calm_hurt_hook && chr_calm_hurt_hook(c)) && c->pain_damage == 0) {
        uint32_t lo = pain_u16(c, 0x74), hi = pain_u16(c, 0x76), top = pain_u16(c, 0x78);
        uint32_t chance = damage < top ? ((hi - lo) * damage) / top + lo : hi;
        if ((chance * 0xffffu) / 100u <= (uint32_t)trac_random()) return;     /* UUrRandom 0x426e10 */
    }
    c->pain_damage = (uint16_t)(c->pain_damage + damage);
    c->pain_decay = pain_u16(c, 0x7a);
    if (game_time() < pain_u16(c, 0x7c) + c->pain_until) return;
    if (kind <= c->pain_last) kind = c->pain_last;
    if (kind == 0) {
        if (c->pain_light < pain_u16(c, 0x7e) && c->pain_damage < pain_u16(c, 0x86)) goto queue;
        kind = 1;
    } else if (kind != 1)
        goto queue;
    if (c->pain_medium >= pain_u16(c, 0x80) || c->pain_damage >= pain_u16(c, 0x88)) kind = 2;
queue:;
    float vol = 1.0f, least;
    memcpy(&least, (const uint8_t *)c->oncc + 0x8c, 4);
    if (c->pain_damage < pain_u16(c, 0x84)) vol = ((1.0f - least) * (float)c->pain_damage) / (float)pain_u16(c, 0x84) + least;
    if (c->pain_wait == 0) {
        c->pain_volume = vol;
        c->pain_wait = 7;
        c->pain_kind = kind;
    } else if (c->pain_kind < kind)
        c->pain_kind = kind;
}

static void pain_forget(Character *c)
{
    c->pain_damage = c->pain_decay = 0;
    c->pain_last = c->pain_until = 0;
    c->pain_light = c->pain_medium = 0;
    c->pain_wait = 0;
    c->pain_kind = 0;
    c->pain_volume = 0.0f;
}

void chr_pain_frame(Character *c)
{
    /* DoCharacterFrame 0x4e07ef: the damage summed forgotten in time (everything with it); the queued cry
       played at the head as its wait runs out (the character's line cut short, 0x516a00), then counted:
       a light one, a medium one (the next not before half the wait), a heavy one (all forgotten, the next
       after the wait) */
    if (c->pain_decay > 0 && --c->pain_decay == 0) pain_forget(c);
    if (c->pain_wait == 0 || --c->pain_wait != 0) return;
    uint32_t kind = c->pain_kind;
    const void *imp = kind <= 3 ? pain_sound(c, kind) : NULL;
    if (imp) {
        speech_stop(c);
        float p[3];
        cry_place(c, p);
        sound_impulse_play_at(imp, p, &c->pain_volume);
    }
    uint32_t wait = pain_u16(c, 0x7c);
    if (kind == 0) {
        c->pain_light++;
        c->pain_last = 0;
        c->pain_until = game_time();
    } else if (kind == 1) {
        c->pain_medium++;
        c->pain_last = 1;
        c->pain_until = game_time() + (wait >> 1);
    } else if (kind == 2) {
        pain_forget(c);
        c->pain_last = 2;
        c->pain_until = game_time() + wait;
    }
    c->pain_kind = 0;
    c->pain_volume = 0.0f;
}

/* TakeDamage's death cry: after a medium cry the heavy one, else by the class's chance (+0x82) the
   death one, at the head at full volume. */
static void death_cry(Character *c)
{
    const void *imp;
    if (c->pain_medium > 0) imp = pain_sound(c, 2);
    else {
        uint32_t chance = pain_u16(c, 0x82);
        if ((uint32_t)(uint16_t)trac_random() >= (chance * 0xffffu) / 100u) return;
        imp = pain_sound(c, 3);
    }
    if (!imp) return;
    speech_stop(c);
    float p[3];
    cry_place(c, p);
    sound_impulse_play_at(imp, p, NULL);
}

/* 0x4f1210: the hit overlay (A+0x1c70) of type 0xe8 for an AI (C+0x1e8 2), 0x39 else, or 0x39, from
   state 1 in its variant. (The type is (C+0x1e8 == 2 ? 0 : 0x51) + 0xe8 with the 0x51 in a register's
   low byte over all ones: 0x39 once the add wraps.) An inactive character is made active first
   (0x4db830), an active one's C+0x1640 cleared. */
void chr_hit_overlay(Character *c)
{
    chr_make_active(c);
    const void *o = chr_lookup_varient(c, c->ai ? 0xe8 : 0x39, 1, c->varient);
    if (!o) o = chr_lookup_varient(c, 0x39, 1, c->varient);
    if (o) chr_overlay_set(c, CHR_OVERLAY_HIT, o, 0);
}

void chr_take_damage(Character *c, uint32_t damage, float knockback, const float dir[3], uint16_t knockdown,
                     Character *from, uint32_t kind)
{
    c->block_pause = c->stun_pause = c->countdown_1c98 = c->countdown_1c9a = 0;
    if (c->flags & 1) {                 /* dead already: the "defeated" script, once */
        if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_DEFEATED);
        return;
    }
    /* A+0xd4: the hit's direction times its damage held to 5..15 (the kind's bits counted too), what a
       weapon thrown from the hand takes a part of (WPrRelease 0x51a550) */
    if (!dir) c->hit_velocity[0] = c->hit_velocity[1] = c->hit_velocity[2] = 0.0f;
    else {
        float k = (float)(damage < 5 ? 5 : damage > 15 ? 15 : damage);
        for (int i = 0; i < 3; i++) c->hit_velocity[i] = k * dir[i];
    }
    /* a conversation ends (AI2rNeutral_Stop 0x4acf70: not forced, aborted, back to the job) */
    if (c->talk_partner) neutral_stop((Chr *)c, false, true, false, true);
    if (kind == CHR_DAMAGE_MELEE && from && chr_melee_score_hook) chr_melee_score_hook(from, c, damage);
    if (chr_hit_debug_log && !c->ai) {      /* --param hitlog=1, a debug aid: the player's hits */
        char b[200];
        const TmInstance *fa = from && from->anim ? tm_handle_instance(tm_handle_of(from->anim)) : NULL;
        const TmInstance *ca = c->anim ? tm_handle_instance(tm_handle_of(c->anim)) : NULL;
        /* (the block's lookup draws from the generators, which a debug aid mustn't move) */
        uint32_t r1 = trac_random_state(), r2 = trac_local_state();
        bool hi = false, lo = false, could = from && chr_could_block(c, from, &hi, &lo);
        trac_random_seed(r1);
        trac_local_seed(r2);
        float ang = from ? wrap_2pi(wrap_2pi(atan2f(from->pos[0] - c->pos[0], from->pos[2] - c->pos[2])) -
                                    wrap_2pi(c->desired_facing - c->facing_offset)) : 0.0f;
        if (ang >= PI) ang -= TWO_PI;
        const TmInstance *fw = from && from->weapon ? tm_handle_instance(tm_handle_of(from->weapon->cls)) : NULL;
        snprintf(b, sizeof b, "OpenOni: hit %u: %u of %u hp, kind %x, from %s, in %s type %u from %u, at %.0f deg, block %d%d%d pause %u weapon %d hypos %u (%s)",
                 (unsigned)game_time(), (unsigned)damage, (unsigned)c->hit_points, (unsigned)kind,
                 fa && fa->name ? fa->name : "-", ca && ca->name ? ca->name : "-", (unsigned)c->type,
                 (unsigned)(c->anim ? tr_from(c->anim) : 0), ang / 0.017453292f, could, hi, lo, (unsigned)c->block_pause,
                 c->weapon != NULL, (unsigned)c->hypos, fw && fw->name ? fw->name : "-");
        chr_hit_debug_log(b);
    }
    if (!(chr_cheat_invincible && !c->ai) && !(c->flags & 0x2000000)) {      /* not invincible */
        if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_HURT);   /* once (C+0x44 bit 0x10) */
        if ((c->flags & 0x20) && c->hit_points <= damage && kind != CHR_DAMAGE_KILL) {
            /* unkillable (chr_unkillable): one hit point left and the defeated script, except for the
               kill command's damage (kind 0x6000000) */
            damage = c->hit_points - 1;
            if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_DEFEATED);
        }
        if (damage < c->hit_points) {
            c->hit_points -= damage;
            /* the pain: knocked down (0x4f..0x51, 0x58..0x5a, 0x8d) or blown up (0xd6, 0xd7) by the
               animation it is in; the hypo's healing still to come cut by 2.5 a point (0x52b308, rounded
               by 0x4340a0) */
            uint32_t kind = 0;
            switch (c->type) {
            case 0x4f: case 0x50: case 0x51: case 0x58: case 0x59: case 0x5a: case 0x8d: kind = 1; break;
            case 0xd6: case 0xd7: kind = 2; break;
            default: break;
            }
            pain(c, damage, kind);
            float hh = (float)damage * 2.5f + 8388608.0f;
            uint32_t hb;
            memcpy(&hb, &hh, 4);
            hb &= 0x7fffff;
            c->hypo_heal = c->hypo_heal < hb ? 0 : (uint16_t)(c->hypo_heal - hb);
            if (c->ai && chr_hurt_hook) chr_hurt_hook(c, from, damage, kind);
        } else {
            c->hit_points = 0;
            c->hypo_heal = 0;
            death_cry(c);
            /* the reaction that kills, if it leaves the character down (else the death's) */
            uint16_t k = knockdown ? knockdown : c->knockdown_type;
            if (knockdown || down_type(k)) death_knockdown(c, chr_is_victim_type(c->type) ? k : 0x4f);
            /* ONrCharacter_Die_1_Animating_Hook 0x4eb550: dead (no more animations asked), then
               ONiCharacter_RunDeathAnimation 0x4eb970. The scripts go through chr_die_hook (the
               weapons and powerups dropped there, chars.c). */
            chr_die(c, from);
        }
    }
    if (!chr_is_unstoppable(c) && knockback != 0.0f && dir)
        for (int k = 0; k < 3; k++) c->knockback[k] += dir[k] * knockback;
    chr_hit_direction(c, dir);
}

/* 0x4e6530: the HUD's hit arc on the side the hit came from: the direction turned by the facing wanted,
   less the run's turn (wrapped once into [0, 2 pi)), into the quarter around front, right, back or left. */
void chr_hit_direction(Character *c, const float dir[3])
{
    if (!dir) return;
    float a = wrap_2pi(c->desired_facing - c->facing_offset);
    float co = cosf(a), si = sinf(a);
    float x = co * dir[2] + si * dir[0], z = co * dir[0] - si * dir[2];
    int side = z <= x ? (-z <= x ? 1 : 2) : (-z <= x ? 0 : 3);
    c->hit_flash[side] = 0xff;
}

/* MUrFloat_Round_To_Int 0x4340a0 for the damage (under 2^23): to the nearest, halves to even. */
static uint32_t round_damage(float f)
{
    float h = f + 8388608.0f;
    uint32_t b;
    memcpy(&b, &h, 4);
    return b & 0x7fffff;
}

float chr_overpower(const Character *c)
{
    if (!c || !((const uint8_t *)c->oncc)[0xc8d] || c->hit_points <= c->max_hit_points) return 1.0f;
    float f = (float)(c->hit_points - c->max_hit_points) / (float)c->max_hit_points;
    if (f > 1.0f) f = 1.0f;
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");    /* 0x5ecbfc */
    if (!gs) return 1.0f;
    float lo, hi;
    memcpy(&lo, gs + 0xc, 4);
    memcpy(&hi, gs + 0x10, 4);
    return (hi - lo) * f + lo;
}

uint32_t chr_overpower_damage(const Character *c, uint32_t damage)
{
    return round_damage(chr_overpower(c) * (float)damage);
}

/* 0x4e64d0: the class has the super shield (ONCC+0xc8e) and it is up (C+0x1668 over 0.5). */
bool chr_super_shielded(const Character *c)
{
    return ((const uint8_t *)c->oncc)[0xc8e] && c->super_level > 0.5f;
}

/* 0x4ecd70: a hit in the air, which always knocks down: the falls of the hits that have one, else the
   knockdown. */
static uint16_t in_air(uint16_t t)
{
    switch (t) {
    case 0x4d: return 0x50;
    case 0x4e: return 0x51;
    case 0x52: return 0x53;
    case 0x55: return 0x58;
    case 0x56: return 0x59;
    case 0x57: return 0x5a;
    case 0x5b: return 0x5c;
    default: return 0x4f;
    }
}

/* ONrAnimType_UpgradeToKnockdown 0x4e5320: a crouching character's hits, the light ones to the crouch's
   hit 0x52, the knockdowns to its 0x53. */
static uint16_t upgrade_to_knockdown(uint16_t t)
{
    if (t >= 0x4c && t <= 0x4e) return 0x52;
    if (t >= 0x4f && t <= 0x51) return 0x53;
    return t;
}

/* The angle from d's facing to the direction of x, in (-pi, pi]. */
static float angle_to(const Character *d, const Character *x)
{
    float a = wrap_2pi(atan2f(x->pos[0] - d->pos[0], x->pos[2] - d->pos[2]));
    a = wrap_2pi(a - d->facing);
    if (a >= PI) a -= TWO_PI;
    return a;
}

/* The reaction a hit from behind plays instead. */
static uint16_t from_behind(uint16_t t)
{
    switch (t) {
    case 0x4c: return 0x55;
    case 0x4d: case 0x4e: case 0x7e: case 0x7f: return 0x56;
    case 0x4f: return 0x58;
    case 0x50: return 0x59;
    case 0x51: return 0x5a;
    case 0x52: return 0x5b;
    case 0x53: return 0x5c;
    case 0xc6: return 0xd5;
    case 0xd6: return 0xd7;
    default: return t;  /* 0x54, 0xd4; others "failed to adjust for behind" */
    }
}

static bool is_from_behind(uint16_t t)
{
    switch (t) {
    case 0x55: case 0x56: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0xd5: case 0xd7:
        return true;
    default:
        return false;
    }
}

/* 0x4e38b0: a state a block can start from: the animation from standing, crouching or 0x2f (tested
   twice; the original reads the animation's to state and doesn't use it) in a standing, turning,
   crouching or block type, or any backward move. */
static bool block_state(const Character *c)
{
    uint16_t from = tr_from(c->anim), t = c->type;
    switch (t) {
    case 9: case 0xf: case 0x3c: case 0x3d: case 0xd2: case 0xd3:
        return true;
    default:
        break;
    }
    bool f = from == 0x13 || from == 0x2f || from == 7;
    return f && (t == 0x8e || t == 0x8f || t == 7 || t == 8 || t == 0x17 || t == 6 || t == 0x11);
}

/* ONrCharacter_CouldBlock 0x4e62b0: in a state to block, not stunned (A+0x1c94), with a block (0x17)
   from the state in the fight variant, and the attacker within 20 degrees (0x54e168) of where the
   character aims (C+0x170 - C+0x174). high, low: the block animation's flags 3 and 2. An AI holding a
   weapon never blocks, the player not with a rifle (ONWC+0xcc 8). */
bool chr_could_block(const Character *d, const Character *x, bool *high, bool *low)
{
    if (d->weapon && (d->ai || (weapon_options(d->weapon) & 8))) return false;
    if (!block_state(d) || d->block_pause) return false;
    const void *b = chr_lookup_varient(d, 0x17, d->state, (uint16_t)(d->varient | 0x200));
    if (!b) return false;
    float aim = wrap_2pi(d->desired_facing - d->facing_offset);
    float a = wrap_2pi(wrap_2pi(atan2f(x->pos[0] - d->pos[0], x->pos[2] - d->pos[2])) - aim);
    if (a >= PI) a -= TWO_PI;
    if (fabsf(a) > 20.0f * 0.017453292f) return false;
    *high = tr_test_flag(b, 3);
    *low = tr_test_flag(b, 2);
    return true;
}

/* HandleAttackMask 0x4e7740, the hit that isn't blocked: once per animation and character; the reaction
   (+0x12), heavier when the stun (+0x14) is over 20; from behind; the defender turned to the attacker;
   the damage (+0xc) and knockback (+0x4, along the attacker's facing); then the stun. */
/* 0x4e8b80: a behemoth's reaction: the light hits (0x4c..0x4e, 0x52, 0x7e, 0x7f, 0xd4) a flinch (0x39),
   the heavy ones (0x4f..0x51, 0x53, 0xd6) a knockdown (0xc6), and a knockdown a hit (0x4d). */
static uint16_t behemoth_reaction(uint16_t r)
{
    switch (r) {
    case 0x4c: case 0x4d: case 0x4e: case 0x52: case 0x7e: case 0x7f: case 0xd4: return 0x39;
    case 0x4f: case 0x50: case 0x51: case 0x53: case 0xd6: return 0xc6;
    case 0xc6: return 0x4d;
    default: return r;
    }
}

static void attack_lands(Character *x, Character *d, const uint8_t *e, uint32_t amask, uint32_t dmask)
{
    int slot = 0;
    for (; slot < 8 && x->hit_list[slot]; slot++)
        if (x->hit_list[slot] == d->id) return;
    if (slot == 8) return;
    x->hit_list[slot] = d->id;
    bool unstoppable = chr_is_unstoppable(d);
    /* an overpowered attacker's blow (0x4e7bec): its damage and knockback scaled */
    float boost = chr_overpower(x);
    uint32_t damage = round_damage(boost * (float)rd16(e + 0xc));
    if (chr_cheat_omnipotent && !x->ai && damage <= d->hit_points) damage = d->hit_points;
    float knock = boost * rdf(e + 4);
    uint8_t aflags = e[8];
    float dir[3] = { sinf(x->facing), 0.0f, cosf(x->facing) };
    /* the block: not for unblockable attacks (flag 1); a low one (flag 2) needs a block with flag 2, a
       high one (4) a block with flag 3. Then the character's block function (C+0x15ec, the AI's
       AI2rBlockFunction; none for the player) has the last word. */
    bool blocked = false;
    /* a class with the super shield (ONCC+0xc8e) shielded (C+0x1668 above 0.5) takes it as blocked and
       unstoppable (the impact on the shield's material, impact_melee) */
    if (((const uint8_t *)d->oncc)[0xc8e] && d->super_level > 0.5f) {
        blocked = true;
        unstoppable = true;
    } else if (!(aflags & 1)) {
        bool high, low;
        blocked = chr_could_block(d, x, &high, &low);
        if (blocked && (aflags & 2) && !low) blocked = false;
        if (blocked && (aflags & 4) && !high) blocked = false;
        if (blocked && d->ai && chr_block_hook) blocked = chr_block_hook(d);
    }
    if (d->ai && chr_attack_landed_hook) chr_attack_landed_hook(x, d);
    /* the attacker seen again if invisible (its ticks from 0), and both shown to at least 0.6 */
    inventory_reveal(x, true);
    inventory_reveal(d, false);
    if (blocked) {
        /* the impact of a blocked blow: a shield's (2) and a blocked one's (1) the same, one that hurts
           through the block (flag 8) a hit's */
        bool shielded = ((const uint8_t *)d->oncc)[0xc8e] && d->super_level > 0.5f;
        int outcome = shielded ? 2 : (aflags & 8) ? 0 : 1;
        float a = angle_to(d, x);
        d->facing = wrap_2pi(d->facing + a);
        if (aflags & 8) chr_take_damage(d, (damage & ~1u) < 2 ? 1 : damage >> 1, knock * 0.5f, dir, 0, x, CHR_DAMAGE_MELEE);
        else chr_take_damage(d, 0, 1.0f, dir, 0, x, CHR_DAMAGE_MELEE);
        if (unstoppable) d->flags |= 0x80;
        else if (rd16(e + 0x18)) {
            d->knockdown_type = 0xc6;
            d->countdown_1c9a = rd16(e + 0x18);
        } else {
            d->flags |= 0x80;
            d->stun_pause = rd16(e + 0x16);
        }
        impact_melee(x, d, e, outcome, amask, dmask);
        return;
    }
    bool staggered = d->countdown_1c98 || d->countdown_1c9a;
    uint16_t stun = rd16(e + 0x14), react = rd16(e + 0x12);
    bool heavy = stun > 0x14;
    if (chr_cheat_fists && !x->ai) {
        react = 0xd6;       /* fists of legend: the player's blows, whatever they are */
    } else if (staggered) {
        if (heavy) {
            heavy = false;
            switch (react) {
            case 0x4d: react = 0x50; break;
            case 0x4e: react = 0x51; break;
            case 0x52: react = 0x53; break;
            case 0x55: react = 0x58; break;
            case 0x56: react = 0x59; break;
            case 0x57: react = 0x5a; break;
            case 0x5b: react = 0x5c; break;
            default: react = 0x4f; break;
            }
        }
    } else if (heavy)
        react = 0xc6;
    if (!unstoppable && react) {
        uint16_t r = react;
        /* a behemoth (a class with ONCC +0xc8f, or the player with the cheat, 0x5ece90) */
        bool behemoth = ((const uint8_t *)d->oncc)[0xc8f] || (!d->ai && chr_cheat_unthrowable);
        if (chr_is_victim_type(react) && d->air_ticks) r = in_air(r);
        else {
            /* lying: the hit on the ground; crouching: the reaction upgraded to a knockdown */
            if (d->state == 0x12 || d->state == 0x32) r = 0x54;
            else if (d->state == 0x13) r = upgrade_to_knockdown(r);
            if (behemoth) r = behemoth_reaction(r);
        }
        if (r == 0x39) {
            /* a behemoth's flinch: no reaction but the hit overlay (the same code as 0x4f1210's) */
            chr_hit_overlay(d);
            goto hurt;
        }
        if (behemoth) {
            /* knocked down by what didn't knock it down, heavily, for at least 30 */
            if (r == 0xc6) {
                if (react != 0xc6) {
                    heavy = true;
                    if (stun < 0x1f) stun = 30;
                }
            } else {
                heavy = false;
            }
        }
        float a = angle_to(d, x);
        if (fabsf(a) > 1.5707964f) r = from_behind(r);
        d->knockdown_type = r;
        if (r != 0x54 && r != 0xd4) {
            d->facing = wrap_2pi(d->facing + a);
            if (is_from_behind(r)) d->facing = wrap_2pi(d->facing + PI);
        }
    }
hurt:;
    bool killed = d->hit_points <= damage;
    if (killed) d->flags |= 0x20000000;   /* killed by a blow */
    chr_take_damage(d, damage, knock, dir, 0, x, CHR_DAMAGE_MELEE);
    if (heavy) d->countdown_1c9a = stun;
    else d->block_pause = stun;
    impact_melee(x, d, e, killed ? 3 : 0, amask, dmask);
}

/* ---- the contacts ---- */

typedef struct { float s[BODY_MAX_BONES][4]; uint32_t n; } Spheres;

/* The parts' bounding spheres in the world: each geometry's (PNTA +0x24) under the part's matrix. */
static void part_spheres(const Character *c, Spheres *out)
{
    out->n = 0;
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES) return;
    BodyMatrix m[BODY_MAX_BONES];
    if (!chr_pose_matrices(c, m)) return;
    for (uint32_t b = 0; b < bones; b++) {
        float box[6], sph[4] = { 0, 0, 0, 0 };
        m3gm_bounds(body_geometry(body, b), box, sph);
        body_transform(m[b], sph, out->s[b]);
        out->s[b][3] = sph[3];
    }
    out->n = bones;
}

/* 0x4ef8e0: the parts whose bounding spheres (the class's lowest body, ONCC+0xc34's first; PNTA +0x24
   under the part's matrix, A+0x2278) are within r of the point past their radius: their bits. */
uint32_t chr_parts_near(const Character *c, const float p[3], float r)
{
    const void *body = body_of_class(c->oncc, 0);
    uint32_t bones = body ? body_bones(body) : 0, mask = 0;
    if (bones > 19) bones = 19;
    for (uint32_t b = 0; b < bones; b++) {
        float box[6], sph[4] = { 0, 0, 0, 0 }, w[3];
        if (!m3gm_bounds(body_geometry(body, b), box, sph)) continue;
        body_transform(c->bone_world[b], sph, w);
        float dx = w[0] - p[0], dy = w[1] - p[1], dz = w[2] - p[2], rr = sph[3] + r;
        if (dx * dx + dy * dy + dz * dz < rr * rr) mask |= 1u << b;
    }
    return mask;
}

bool chr_part_point(const Character *c, unsigned part, float out[3])
{
    if (c->inactive) return false;      /* the parts are the active record's */
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES || part >= bones) return false;
    BodyMatrix m[BODY_MAX_BONES];
    if (!chr_pose_matrices(c, m)) return false;
    const float zero[3] = { 0, 0, 0 };
    body_transform(m[part], zero, out);
    return true;
}

bool chr_weapon_matrix(const Character *c, float out[12])
{
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES) return false;
    BodyMatrix m[BODY_MAX_BONES];
    if (!chr_pose_matrices(c, m)) return false;
    /* ONrCharacter_SetCharacterClass 0x4d7c30: the weapon node hangs from the right fist (18), or the
       left (14) for a class with +0xc8c; the left hand's half turn about x (ONrCharacter_SetHand 0x4332b0) */
    bool left = ((const uint8_t *)c->oncc)[0xc8c] != 0;
    const float *f = m[left ? 14 : 18];
    memcpy(out, f, 12 * sizeof(float));
    if (left)
        for (int k = 3; k < 9; k++) out[k] = -f[k];
    return true;
}

/* 0x4e8e70: of a's attacks at its frame, those whose parts touch one of b's (touch_b: per part of b,
   the parts of a it touches). Returns the landed attacks' bits (over the attacks' order), the entries,
   and the highest priority (+0x1a); amask: the landed attacks' parts, dmask: b's part each touched first. */
static uint32_t landed(const Character *a, const Spheres *sb, const uint32_t *touch_b, const uint8_t **entries, uint16_t *prio,
                       uint32_t *amask, uint32_t *dmask)
{
    uint32_t bits = 0;
    *prio = 0;
    *amask = *dmask = 0;
    const uint8_t *t = a->anim;
    for (unsigned i = 0; i < t[TRAM_ATTACK_COUNT] && i < 32; i++) {
        const uint8_t *e = tr_attack(t, i);
        entries[i] = NULL;
        if (!e || rd16(e + 0xe) > a->frame || a->frame > rd16(e + 0x10)) continue;
        entries[i] = e;
        for (uint32_t j = 0; j < sb->n; j++)
            if (touch_b[j] & rd32(e)) {
                bits |= 1u << i;
                if (rd16(e + 0x1a) >= *prio) *prio = rd16(e + 0x1a);
                *amask |= rd32(e);
                *dmask |= 1u << j;
                break;
            }
    }
    return bits;
}

/* HandleGenericMask 0x4e8c50: an active character touching another loses the part of its move (A+0xc4)
   that heads into it, along the line between them on the ground (MUrVector_PushBack 0x433da0: only a
   move toward it, the move's dot with the line below 0); not closer than 1 apart. A+0xb4 counts the
   pushes; for one the AI moves, a character its path goes through sets A+0xb6, and the AI's movement
   hears of any other (0x4ab920): the point half a unit from its pelvis toward the other, the plane
   through it facing it. */
static void push_off(Character *c, const Character *o)
{
    if (!c->has_physics) return;
    float dx = c->pos[0] - o->pos[0], dz = c->pos[2] - o->pos[2];
    float d = dx * dx + dz * dz;
    if (d < 1.0f) return;                                       /* 0x529508 */
    d = 1.0f / sqrtf(d);
    float u[3] = { dx * d, 0.0f, dz * d };
    float dot = c->move[0] * u[0] + c->move[1] * u[1] + c->move[2] * u[2];
    if (dot < 0.0f)
        for (int k = 0; k < 3; k++) c->move[k] -= dot * u[k];
    c->pushes++;
    if (!(c->flags & 0x400000u)) return;
    if (chr_path_through_hook && chr_path_through_hook(c, o)) {
        c->blocked_by_char = true;
    } else if (chr_movement_hit_hook) {
        float pt[3] = { c->pos[0] - u[0] * 0.5f, c->pos[1] + c->pelvis_height - u[1] * 0.5f, c->pos[2] - u[2] * 0.5f };
        float plane[4] = { u[0], u[1], u[2], -(u[0] * pt[0] + u[1] * pt[1] + u[2] * pt[2]) };
        chr_movement_hit_hook(c, CHR_HIT_CHARACTER, (uintptr_t)o, pt, plane);
    }
}

/* iCollideTwoLikelyCharacters 0x4e73e0. A thrower and the one it throws (A+0x1ed8) don't touch. */
static void collide_two(Character *a, Character *b)
{
    if (a->threw == b || b->threw == a) return;
    Spheres sa, sb;
    part_spheres(a, &sa);
    part_spheres(b, &sb);
    if (!sa.n || !sb.n) return;
    uint32_t ta[BODY_MAX_BONES] = { 0 }, tb[BODY_MAX_BONES] = { 0 };
    bool any = false;
    for (uint32_t i = 0; i < sa.n; i++)
        for (uint32_t j = 0; j < sb.n; j++) {
            float dx = sa.s[i][0] - sb.s[j][0], dy = sa.s[i][1] - sb.s[j][1], dz = sa.s[i][2] - sb.s[j][2];
            float r = sa.s[i][3] + sb.s[j][3];
            if (dx * dx + dy * dy + dz * dz < r * r) {      /* 0x4331d0 */
                any = true;
                ta[i] |= 1u << j;
                tb[j] |= 1u << i;
            }
        }
    if (!any) return;
    const uint8_t *ea[32], *eb[32];
    uint16_t pa, pb;
    uint32_t ama, dma, amb, dmb;
    uint32_t la = landed(a, &sb, tb, ea, &pa, &ama, &dma), lb = landed(b, &sa, ta, eb, &pb, &amb, &dmb);
    push_off(a, b);
    push_off(b, a);
    if (pb <= pa) {
        for (unsigned i = 0; i < 32; i++)
            if (la >> i & 1) attack_lands(a, b, ea[i], ama, dma);
        if (pb < pa) return;
    }
    for (unsigned i = 0; i < 32; i++)
        if (lb >> i & 1) attack_lands(b, a, eb[i], amb, dmb);
}

void chr_collide_all(Character **chars, int n)
{
    for (int i = 0; i < n; i++) {
        Character *a = chars[i];
        if (a->flags & 0x40004) continue;
        for (int j = i + 1; j < n; j++) {
            Character *b = chars[j];
            if ((b->flags & 0x40004) || (a->flags & 2) || (b->flags & 2)) continue;
            float dx = a->pos[0] - b->pos[0], dy = a->pos[1] - b->pos[1], dz = a->pos[2] - b->pos[2];
            if (dx * dx + dy * dy + dz * dz < 625.0f) collide_two(a, b);    /* 0x52a99c */
        }
    }
    /* the reactions asked: a knockdown, or a block held (flag 0x80) */
    for (int i = 0; i < n; i++) {
        Character *c = chars[i];
        if (c->knockdown_type) {
            if (!(c->flags & 1)) chr_knockdown(c, c->knockdown_type);
            c->knockdown_type = 0;
        } else if (c->flags & 0x80) {
            c->varient |= 0x200;
            c->fight_timer = chr_class_fight_time(c->oncc);
            c->hold = 0;
            if (c->type != 0x17 && !chr_do_animation(c, CHR_ANIM_NOW, 0x17)) chr_do_animation(c, CHR_ANIM_NOW, 0x17);
        }
        c->flags &= ~0x80u;
    }
}
