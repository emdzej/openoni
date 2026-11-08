/* Weapons: see weapon.h. */
#include "weapon.h"
#include "gametime.h"
#include "inventory.h"

#include "akev.h"
#include "akev_collide.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "combat.h"
#include "exe.h"
#include "impact.h"
#include "knowledge.h"
#include "m3gm.h"
#include "material.h"
#include "objc.h"
#include "object.h"
#include "body.h"
#include "particle.h"
#include "persist.h"
#include "powerup.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"
#include "turret.h"
#include "trac.h"
#include "ftol.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { float f; uint32_t v = rd32(p); memcpy(&f, &v, 4); return f; }

/* The class (ONWC, offsets from the instance data; docs/formats/onwc.md). */
enum {
    ONWC_GEOMETRY = 0x58,       /* M3GM */
    ONWC_RECOIL = 0x80,         /* the aim's kick a shot */
    ONWC_RECOIL_ANIM = 0xba,    /* the recoil overlay's animation type */
    ONWC_RELOAD_WAIT = 0xbe,
    ONWC_SHOTS = 0xc0,          /* full ammo */
    ONWC_SHOOTERS = 0xc2,
    ONWC_MODES = 0xc4,          /* firing modes: 0 can't fire */
    ONWC_PAUSE = 0xc6,
    ONWC_RELEASE_DELAY = 0xc8,
    ONWC_FLAGS = 0xcc,
    ONWC_SHOOTER = 0x1d0,       /* 0x50 each */
    ONWC_PICKUP = 0x704,        /* the handle's offset (x, y, z) */
};
/* A shooter: +0x30 the particle class's name, +0x44 the ammo a shot takes (0 none: it only follows the
   others), +0x46 the delay between shots, +0x48 the "roughjustice" one, +0x4a the ticks a shot fires
   at least, +0x4c its trigger (0, 1, -1 both), +0x4e its delay before it starts. */
enum { SH_NAME = 0x30, SH_AMMO = 0x44, SH_DELAY = 0x46, SH_DELAY_GATLING = 0x48, SH_ACTIVE = 0x4a, SH_TRIGGER = 0x4c, SH_START = 0x4e, SH_SIZE = 0x50 };

/* The particle events (P3's, the names at 0x5583a0 "update", "pulse", ...). */
enum { EV_PULSE = 1, EV_START = 2, EV_STOP = 3, EV_BG_START = 4, EV_BG_STOP = 5, EV_DELAY_START = 14, EV_DELAY_STOP = 15 };

static Weapon weapons[WEAPONS_MAX];
static uint32_t now;            /* the game time (GS+0x144): the weapons' updates */
static bool no_fade;            /* wp_disable_fade (0x627dcc) */
static bool kickable;           /* wp_kickable (0x620131): the player pushes the weapons on the ground */

static uint16_t shooters(const Weapon *w) { return rd16(w->cls + ONWC_SHOOTERS); }
static const uint8_t *shooter(const Weapon *w, unsigned k) { return w->cls + ONWC_SHOOTER + SH_SIZE * k; }
static uint32_t options(const Weapon *w) { return rd32(w->cls + ONWC_FLAGS); }

void weapons_reset(void)
{
    memset(weapons, 0, sizeof weapons);
    now = 0;
}

int weapon_slots(void) { return WEAPONS_MAX; }
Weapon *weapon_get(int i) { return i >= 0 && i < WEAPONS_MAX && (weapons[i].flags & WEAPON_IN_USE) ? &weapons[i] : NULL; }
const void *weapon_geometry(const Weapon *w) { return tm_link(w->cls + ONWC_GEOMETRY); }

/* ---- the shooters ---- */

static void pulse(Weapon *w, const uint8_t *sh);

/* WPiDoEvent 0x519f70: an event to a shooter's particle. A start marks it active, a stop needs it
   active. The particle gone (its handle no longer its), the slot is cleared; else its velocity (the
   owner's weapon hand's motion a tick, x60) and its damage's owner (0x1000000 | the owner's id) are set,
   the event goes to it (P3rSendEvent 0x45b2c0), and a start of a shooter that takes ammo pulses it. */
static void do_event(Weapon *w, int event, unsigned k)
{
    uint16_t bit = (uint16_t)(1u << k);
    if (event == EV_START) w->active |= bit;
    else if (event == EV_STOP) {
        if (!(w->active & bit)) return;
        w->active &= (uint16_t)~bit;
    }
    P3Class *pc;
    P3Particle *p = p3_from_handle(w->shooter_handle[k], &pc);
    if (!p) {
        w->shooter_handle[k] = 0;
        return;
    }
    float *vel = p3_block(pc, p, P3_X_VELOCITY);
    if (vel) {
        /* C+0x1c4, the weapon's move this tick */
        Character *c = w->owner;
        for (int i = 0; i < 3; i++) vel[i] = c ? c->weapon_move[i] * 60.0f : 0.0f;
    }
    uint8_t *own = p3_block(pc, p, P3_X_OWNER);
    if (own) {
        uint32_t o = w->owner ? (0x1000000u | w->owner->id) : 0;
        memcpy(own, &o, 4);
    }
    p3_send_event(pc, p, event, (float)now * (1.0f / 60.0f));
    if (event == EV_START && rd16(shooter(w, k) + SH_AMMO)) pulse(w, shooter(w, k));
}

static void every_shooter(Weapon *w, int event)
{
    for (unsigned k = 0; k < shooters(w); k++) do_event(w, event, k);
}

static bool on_trigger(const uint8_t *s, int16_t trigger)
{
    int16_t t = (int16_t)rd16(s + SH_TRIGGER);
    return trigger == -1 || t == -1 || t == trigger;
}

/* WPiPulseShooter 0x519c50: a shot. The shooters of its trigger pulse; the ammo goes down (unless the
   owner's C+0x04 flag 0x40); empty, the background events stop, else the delay events start once; the
   delays; the owner's recoil overlay (the class's type, from state 1) and the aim's kick. */
static void pulse(Weapon *w, const uint8_t *sh)
{
    Character *c = w->owner;
    if (!c) return;
    int16_t trigger = (int16_t)rd16(sh + SH_TRIGGER);
    for (unsigned k = 0; k < shooters(w); k++)
        if (on_trigger(shooter(w, k), trigger)) do_event(w, EV_PULSE, k);
    if (!(c->flags & 0x40)) w->ammo = (uint16_t)(w->ammo - rd16(sh + SH_AMMO));
    if (w->ammo == 0) {
        if (w->flags & WEAPON_BACKGROUND) {
            w->flags &= (uint16_t)~WEAPON_BACKGROUND;
            every_shooter(w, EV_BG_STOP);
        }
    } else if (!(w->flags & WEAPON_DELAYED)) {
        every_shooter(w, EV_DELAY_START);
        w->flags |= WEAPON_DELAYED;
    }
    /* the player's with the gatling guns cheat (0x620133): the "roughjustice" delay (+0x48), and the
       kick by it over the delay's */
    bool gatling = weapon_cheat_gatling && !c->ai;
    w->delay = w->delay2 = rd16(sh + (gatling ? SH_DELAY_GATLING : SH_DELAY));
    chr_make_active(c);     /* 0x4f11b0: the recoil overlay is the active record's */
    const void *t = chr_lookup_varient(c, rd16(w->cls + ONWC_RECOIL_ANIM), 1, c->varient);
    if (t) chr_overlay_set(c, CHR_OVERLAY_RECOIL, t, 0);
    /* the AI's kick is less by its shooting skill (ONCC +0x154): with the AI's weapons */
    float kick = rdf(w->cls + ONWC_RECOIL);
    if (gatling && rd16(sh + SH_DELAY)) kick = kick * (float)rd16(sh + SH_DELAY_GATLING) / (float)rd16(sh + SH_DELAY);
    c->aim_kick += kick;
    /* the shot's noise, at most every 30 ticks: a gunfire sound within the class's radius (+0x128 for
       trigger 0, +0x1a8 for the others), from the owner's pelvis, of the owner at its combat target (an
       AI's in combat, 0x48b5f0) */
    if (!w->sound_wait) {
        float radius = rdf(w->cls + (rd16(sh + SH_TRIGGER) ? 0x1a8 : 0x128));
        if (radius > 0.0f) {
            Chr *ch = (Chr *)c;
            float at[3] = { c->pos[0], c->pos[1] + c->pelvis_height, c->pos[2] };
            Chr *target = !ch->player && ch->ai.state == AI_STATE_COMBAT ? ch->ai.target : NULL;
            knowledge_sound(KNOW_SOUND_GUNFIRE, at, radius, ch, target);
        }
        w->sound_wait = 30;
    }
}

/* WPiFireShooter 0x51a220: the first shooter of the trigger that takes ammo; with enough ammo the
   weapon fires (once: the shooters of the trigger wait their start delays, or start), a sustained one
   pulses again while it fires. *empty: not enough ammo. */
static bool fire_shooter(Weapon *w, int16_t trigger, bool keep, bool *empty)
{
    unsigned n = shooters(w), k = 0;
    for (; k < n; k++)
        if (rd16(shooter(w, k) + SH_AMMO) && (int16_t)rd16(shooter(w, k) + SH_TRIGGER) == trigger) break;
    if (k >= n) return false;
    const uint8_t *sh = shooter(w, k);
    if (rd16(sh + SH_AMMO) > w->ammo) {
        if (empty) *empty = true;
        return false;
    }
    if (!(w->flags & WEAPON_FIRING)) {
        w->flags |= WEAPON_FIRING;
        if (!keep && rd16(sh + SH_ACTIVE) > w->fire_min) w->fire_min = rd16(sh + SH_ACTIVE);
        /* the firing spread the AIs dodge (AI2rKnowledge_NewDodgeFiringSpread 0x49c160), for a class
           with one (+0x144, its length) */
        if (rdf(w->cls + 0x144) > 0.0f) w->spread = knowledge_spread_new(w);
        for (unsigned j = 0; j < n; j++) {
            const uint8_t *s = shooter(w, j);
            int16_t t = (int16_t)rd16(s + SH_TRIGGER);
            if (trigger == -1 || t == -1 || t == trigger) {
                w->shooter_delay[j] = rd16(s + SH_START);
                if (!w->shooter_delay[j]) do_event(w, EV_START, j);
            }
        }
    } else if (options(w) & ONWC_SUSTAINED)
        pulse(w, sh);
    if (!keep) w->flags &= (uint16_t)~WEAPON_STOP_WANTED;
    w->pause = rd16(w->cls + ONWC_PAUSE);
    return true;
}

/* WPrDepressTrigger 0x51a1b0: not while a delay or the reload's wait runs, nor for a class without
   firing modes; the alternate trigger only for a class with it. */
static bool depress_trigger(Weapon *w, bool keep, bool alt, bool *empty)
{
    if (empty) *empty = false;
    if (w->delay || w->reload_wait || !rd16(w->cls + ONWC_MODES)) return false;
    if (!alt) return fire_shooter(w, 0, keep, empty);
    if (options(w) & ONWC_ALT_FIRE) return fire_shooter(w, 1, keep, empty);
    return false;
}

/* The firing spread let go (AI2rKnowledge_DeleteDodgeFiringSpread 0x49c1e0). */
static void free_spread(Weapon *w)
{
    if (w->spread != -1) knowledge_spread_delete(w->spread);
    w->spread = -1;
}

/* ONiCharacter_StopFiring 0x51a3b0: unless forced, a shot's least time or the release delay still
   running only asks for the stop; else the shooters stop. */
static void stop_firing(Weapon *w, bool force)
{
    if (!force) {
        if (w->fire_min) { w->flags |= WEAPON_STOP_WANTED; return; }
        if ((options(w) & ONWC_HAS_RELEASE_DELAY) && rd16(w->cls + ONWC_RELEASE_DELAY) < w->delay) {
            w->flags |= WEAPON_STOP_WANTED;
            return;
        }
    }
    w->flags &= (uint16_t)~WEAPON_STOP_WANTED;
    w->fire_min = 0;
    if (w->flags & WEAPON_FIRING) {
        w->flags &= (uint16_t)~WEAPON_FIRING;
        free_spread(w);
        every_shooter(w, EV_STOP);
    }
}

/* WPrReloadDone 0x5181e0: the reload's end: the delay events stop, full of ammo, the background
   events start. */
static void reload_done(Weapon *w)
{
    if (w->flags & WEAPON_DELAYED) {
        every_shooter(w, EV_DELAY_STOP);
        w->flags &= (uint16_t)~WEAPON_DELAYED;
    }
    w->ammo = rd16(w->cls + ONWC_SHOTS);
    if (w->owner && (w->flags & WEAPON_OWNED) && !(w->flags & WEAPON_BACKGROUND)) {
        w->flags |= WEAPON_BACKGROUND;
        every_shooter(w, EV_BG_START);
    }
}

/* 0x51a990 (WPrHasAmmo): an owner with unlimited ammo (C+0x04 0x40) keeps it full; true if any. */
static bool has_ammo(Weapon *w)
{
    if (w->owner && (w->owner->flags & 0x40)) {
        w->ammo = rd16(w->cls + ONWC_SHOTS);
        if ((w->flags & WEAPON_OWNED) && !(w->flags & WEAPON_BACKGROUND)) {
            w->flags |= WEAPON_BACKGROUND;
            every_shooter(w, EV_BG_START);
        }
    }
    return w->ammo != 0;
}

/* 0x51cce0: what reloads the class: -1 nothing (+0xcc 0x800), 1 an energy cell (0x4), else 0 a clip. */
static int ammo_type(const Weapon *w)
{
    uint32_t f = options(w);
    return (f & 0x800) ? -1 : (int)(f >> 2 & 1);
}

/* 0x51c730: what c has of it. */
static uint16_t inventory(const Character *c, int type)
{
    return type == 0 ? c->ammo : type == 1 ? c->cells : type == 2 ? c->hypos : 0;
}

/* ONrCharacter_HasAmmoForReload 0x4edc90 */
static bool has_ammo_for_reload(const Character *c)
{
    return (c->flags & 0x40) || inventory(c, ammo_type(c->weapon)) != 0;
}

/* 0x4edcc0: the weapon's overlay is a reload (types 0xa4 to 0xab). */
static bool reloading(const Character *c)
{
    const void *t = c->overlay[CHR_OVERLAY_WEAPON].anim;
    uint16_t k = t ? tr_type(t) : 0;
    return k >= 0xa4 && k <= 0xab;
}

/* 0x4edc00: an empty weapon with nothing to reload it (no reload under way). */
static bool empty_unreloadable(Character *c)
{
    if (!c->weapon || has_ammo(c->weapon) || reloading(c)) return false;
    return inventory(c, ammo_type(c->weapon)) == 0;
}

bool weapon_try_reload(Character *c, Weapon *w, bool *full, bool *none)
{
    chr_make_active(c);     /* WPrTryReload 0x51c560 */
    if (full) *full = false;
    if (none) *none = false;
    if (!w || !c->weapon) return false;
    if (w->owner && (w->owner->flags & 0x40)) {
        w->ammo = rd16(w->cls + ONWC_SHOTS);
        if ((w->flags & WEAPON_OWNED) && !(w->flags & WEAPON_BACKGROUND)) {
            w->flags |= WEAPON_BACKGROUND;
            every_shooter(w, EV_BG_START);     /* 0x51a130 with trigger -1 */
        }
    }
    if (w->ammo == rd16(w->cls + ONWC_SHOTS)) {
        if (full) *full = true;
        return false;
    }
    if (!has_ammo_for_reload(c)) {
        if (none) *none = true;
        return false;
    }
    if (tr_is_attack(c->anim) || chr_is_victim_type(c->type) || c->overlay[CHR_OVERLAY_WEAPON].anim || w->pause || w->delay)
        return false;
    /* the class's reload animation (+0xbc) as an overlay (TRrCollection_Lookup with 7) */
    const void *t = chr_lookup_varient(c, rd16(w->cls + 0xbc), 7, c->varient);
    if (!t) return false;   /* "failed to find reload animation" */
    /* 0x51c770: a clip or cell used up */
    int k = ammo_type(w);
    if (k == 0 && c->ammo) c->ammo--;
    else if (k == 1 && c->cells) c->cells--;
    c->last_action = game_time();  /* 0x4edbb0: C+0x1e0 */
    chr_overlay_set(c, CHR_OVERLAY_WEAPON, t, 0);
    return true;
}

bool weapon_has_empty(Character *c)
{
    /* AI2iScript_HasEmptyWeapon 0x4b8cb0 */
    if (!c->weapon || has_ammo(c->weapon) || has_ammo_for_reload(c)) return false;
    return !reloading(c);
}

/* ---- the slots: the hand (C+0x194) and the two stowed (C+0x198, 0x19c) ---- */

static void release(Weapon *w, bool thrown);
static Weapon *slot_get(const Character *c, int i) { return i == 0 ? c->weapon : c->stowed[i - 1]; }
static void slot_put(Character *c, int i, Weapon *w) { if (i == 0) c->weapon = w; else c->stowed[i - 1] = w; }

/* WPrSlot_FindLastStowed 0x51bc70: the weapon of slot 1 (unless excluded), else of the last slot not
   excluded; with none, idx an empty slot other than 1 (from the last). */
static Weapon *find_last_stowed(const Character *c, int exclude, int *idx)
{
    if (exclude != 1 && slot_get(c, 1)) return *idx = 1, slot_get(c, 1);
    for (int i = 2; i >= 0; i--)
        if (i != exclude && slot_get(c, i)) return *idx = i, slot_get(c, i);
    for (int i = 2; i >= 0; i--)
        if (i != 1 && !slot_get(c, i)) return *idx = i, NULL;
    return NULL;
}

/* 0x51bc20: an empty slot, slot 1 first if asked, else the last other than 1. */
static bool find_empty(const Character *c, int *idx, bool first)
{
    if (first && !slot_get(c, 1)) return *idx = 1, true;
    for (int i = 2; i >= 0; i--)
        if (i != 1 && !slot_get(c, i)) return *idx = i, true;
    return false;
}

/* WPrSlot_Swap 0x51bd00: the weapons of two slots swapped; the one leaving the hand is let go of
   (0xc000: its firing and background events stop) and not owned, the one coming into it owned. */
static void slot_swap(Character *c, int a, int b)
{
    Weapon *wa = slot_get(c, a), *wb = slot_get(c, b);
    if (wa) {
        if (a == 0) wa->flags = (uint16_t)((wa->flags | 0xc000) & 0xf7ff);
        if (b == 0) wa->flags |= WEAPON_OWNED;
    }
    if (wb) {
        if (b == 0) wb->flags = (uint16_t)((wb->flags | 0xc000) & 0xf7ff);
        if (a == 0) wb->flags |= WEAPON_OWNED;
    }
    slot_put(c, a, wb);
    slot_put(c, b, wa);
}

/* The animation types during which no holster or draw starts (0x4edd20's table). */
static bool holster_blocked(uint16_t type)
{
    return (type >= 0x4c && type <= 0x5c) || (type >= 0x60 && type <= 0x65) || (type >= 0x72 && type <= 0x77) ||
           (type >= 0x7e && type <= 0x84) || type == 0x8d || type == 0xc6 || (type >= 0xd4 && type <= 0xd7);
}

/* ONrCharacter_Holster 0x4edd20: a weapon in the hand that can be holstered (class 2 not set) is put
   away by the Holster overlay (0xa0, in state 7; its flag 1, forced, to stow it even with slot 1
   taken), not during an attack or the blocked types. */
static bool holster(Character *c, bool force)
{
    bool attack = tr_is_attack(c->anim), blocked = holster_blocked(c->type);
    if (!c->weapon || (options(c->weapon) & ONWC_NO_HOLSTER)) return false;
    c->last_action = game_time();
    if (attack || blocked) return false;
    const void *t = chr_lookup_varient(c, 0xa0, 7, c->varient);
    if (!t) return false;
    chr_overlay_set(c, CHR_OVERLAY_WEAPON, t, force ? 1 : 0);
    return true;
}

/* 0x4edeb0 and 0x4effa0: a stowed weapon (or the one given) drawn by Draw_Pistol (0xa1) or Draw_Rifle
   (0xa2), not during an attack or the blocked types. */
static void draw_weapon(Character *c, Weapon *w)
{
    bool attack = tr_is_attack(c->anim), blocked = holster_blocked(c->type);
    int idx = 0;
    if (!w) w = find_last_stowed(c, 0, &idx);
    c->last_action = game_time();
    if (!w || attack || blocked) return;
    const void *t = chr_lookup_varient(c, (options(w) & ONWC_RIFLE) ? 0xa2 : 0xa1, 7, c->varient);
    if (t) chr_overlay_set(c, CHR_OVERLAY_WEAPON, t, 0);
}

/* 0x4eff60: the weapon put away (forced), for the cutscenes and chr_forceholster. */
void chr_force_holster(Character *c) { if (c->weapon) holster(c, true); }

/* 0x4effa0: a stowed weapon drawn, with the hand empty. */
void chr_force_draw(Character *c)
{
    if (c->stowed[0] && !c->weapon) draw_weapon(c, NULL);
}

Weapon *chr_slot(const Character *c, int i) { return i >= 0 && i < 3 ? slot_get(c, i) : NULL; }

/* The varient of the hand from the weapon, as ONrCharacter_SetWeapon makes it (the holster's again). */
void chr_hand_varient(Character *c)
{
    c->varient &= 0x87ff;
    if (!c->weapon || (c->flags & 0x2000)) return;
    bool rifle = (options(c->weapon) & ONWC_RIFLE) != 0, left = ((const uint8_t *)c->oncc)[0xc8c] != 0;
    c->varient |= rifle && left ? 0x4000 : rifle ? 0x2000 : left ? 0x1000 : 0x800;
}

void chr_hide_weapon(Character *c, bool hide)
{
    if (hide) c->flags |= 0x2000;
    else c->flags &= ~0x2000u;
    chr_hand_varient(c);
}

/* The hand's weapon dropped (ONrCharacter_DropWeapon 0x51b590, slot 0), thrown or not, then the hand's
   firing cleared and its variant (0x4ecec0). */
static void drop_hand(Character *c, bool thrown)
{
    Weapon *old = c->weapon;
    if (!old) return;
    old->flags = (uint16_t)((old->flags & 0xf7ff) | 0xc000);
    c->weapon = NULL;
    release(old, thrown);
    c->varient &= 0x87ff;
    c->fire_down = c->fire2_down = c->fire_again = c->fire_empty = false;
}

/* ONrCharacter_PickupWeapon 0x4ed770 without a weapon: forced, the hand's goes to an empty slot (slot 1
   first), dropped with none; else the stowed one trades places with the hand's. */
void chr_stow(Character *c, bool force)
{
    int idx = 0;
    if (force) {
        if (find_empty(c, &idx, true)) {
            slot_swap(c, idx, 0);
            chr_set_weapon(c, NULL);
        } else
            drop_hand(c, false);
        return;
    }
    Weapon *w = find_last_stowed(c, 0, &idx);
    slot_swap(c, idx, 0);
    chr_set_weapon(c, w);
}

/* DoCharacterFrame's weapon overlay at its action frame (slot 1, TRAM +0x174, 0x4dfb43): a reload
   (0xa4..0xab) ends. The holster (0xa0): forced, the weapon goes to an empty slot (slot 1 first), or is
   dropped with none; else it trades places with the stowed one (none: into an empty slot). With a weapon
   in the hand after, its varient, and its draw played from its action frame. The draw (0xa1, 0xa2):
   with the hand empty the stowed weapon comes into it. */
static void overlay_action(Character *c, const void *anim)
{
    uint16_t k = tr_type(anim);
    int idx = 0;
    if (k >= 0xa4 && k <= 0xab) {
        if (c->weapon) reload_done(c->weapon);
    } else if (k == 0xa0) {
        if (!c->weapon) return;
        if (c->overlay[CHR_OVERLAY_WEAPON].flags & 1) {
            if (find_empty(c, &idx, true)) {
                slot_swap(c, idx, 0);
                chr_set_weapon(c, NULL);
            } else
                drop_hand(c, false);
        } else {
            Weapon *w = find_last_stowed(c, 0, &idx);
            slot_swap(c, idx, 0);
            chr_set_weapon(c, w);
        }
        if (!c->weapon) return;
        chr_hand_varient(c);
        Weapon *w = c->weapon;
        bool attack = tr_is_attack(c->anim), blocked = holster_blocked(c->type);
        c->last_action = game_time();
        if (!attack && !blocked) {
            const void *t = chr_lookup_varient(c, (options(w) & ONWC_RIFLE) ? 0xa2 : 0xa1, 7, c->varient);
            if (t) chr_overlay_set(c, CHR_OVERLAY_WEAPON, t, 0);
        }
        const void *now_anim = c->overlay[CHR_OVERLAY_WEAPON].anim;
        int16_t action = now_anim ? (int16_t)rd16((const uint8_t *)now_anim + 0x174) : 0;
        if (action > 0) c->overlay[CHR_OVERLAY_WEAPON].frame = (uint16_t)action;
    } else if (k == 0xa1 || k == 0xa2) {
        if (c->weapon) return;
        Weapon *w = find_last_stowed(c, 0, &idx);
        slot_swap(c, idx, 0);
        chr_set_weapon(c, w);
    }
    /* (any other type: a line on the developers' console, 0x550d74) */
}

/* 0x518180: full of ammo; owned, the background events start. */
static void fill_ammo(Weapon *w)
{
    w->ammo = rd16(w->cls + ONWC_SHOTS);
    if (w->owner && (w->flags & WEAPON_OWNED) && !(w->flags & WEAPON_BACKGROUND)) {
        w->flags |= WEAPON_BACKGROUND;
        every_shooter(w, EV_BG_START);
    }
}

/* WPiCreateParticles 0x519680 (WPiNewParticles 0x5182c0): each shooter's particle, once (flag 0x1000):
   at the shooter's place on the weapon, its orientation the shooter's, hanging from the weapon's
   matrix, still, unowned, its create event; then each shooter's start event (the delayed ones wait,
   their delays set, when the background events are on). */
static void create_particles(Weapon *w)
{
    if (w->flags & WEAPON_PARTICLES) return;
    float t = (float)now * (1.0f / 60.0f);
    for (unsigned k = 0; k < shooters(w) && k < WEAPON_SHOOTERS; k++) {
        const uint8_t *sh = shooter(w, k);
        char name[17];
        memcpy(name, sh + SH_NAME, 16);
        name[16] = 0;
        w->shooter_handle[k] = 0;
        P3Class *pc = p3_class(name);
        P3Particle *p = pc ? p3_create(pc, now) : NULL;
        if (!p) continue;
        float *pos = p3_position(pc, p);
        if (!pos) continue;
        for (int i = 0; i < 3; i++) pos[i] = rdf(sh + 0x24 + 4 * i);
        float *o = p3_block(pc, p, P3_X_ORIENTATION);
        if (o) for (int i = 0; i < 9; i++) o[i] = rdf(sh + 4 * i);
        float *v = p3_block(pc, p, P3_X_VELOCITY);
        if (v) v[0] = v[1] = v[2] = 0.0f;
        float *off = p3_block(pc, p, P3_X_OFFSET);
        if (off) off[0] = off[1] = off[2] = 0.0f;
        uint8_t *at = p3_block(pc, p, P3_X_ATTACH);
        if (at) {
            uint32_t id = p3_attach_id(w->m);
            memcpy(at, &id, 4);
            p3_set_flags(p, p3_flags(p) | 0x2000);
        }   /* else "particle class %s attached to weapon has no matrix" */
        uint8_t *b;
        if ((b = p3_block(pc, p, P3_X_OWNER))) memset(b, 0, 4);
        if ((b = p3_block(pc, p, P3_X_TEX_INDEX))) { uint32_t r = trac_local_random(); memcpy(b, &r, 4); }
        if ((b = p3_block(pc, p, P3_X_TEX_TIME))) memcpy(b, &now, 4);
        if ((b = p3_block(pc, p, P3_X_PREV_POS))) memset(b, 0, 4);
        if ((b = p3_block(pc, p, P3_X_LENSFLARE))) memset(b, 0, 4);
        if ((b = p3_block(pc, p, P3_X_CACHE))) memset(b, 0, 20);
        p3_set_life(p, 0.0f);
        p3_send_event(pc, p, P3_EV_CREATE, t);
        w->shooter_handle[k] = p3_handle(p);
    }
    w->flags |= WEAPON_PARTICLES;
    /* the background's state to every shooter: start (4) with the background events on, else stop (5);
       the original's test for a start (2), to wait the shooter's delay, never holds */
    int ev = (w->flags & WEAPON_BACKGROUND) ? EV_BG_START : EV_BG_STOP;
    for (unsigned k = 0; k < shooters(w) && k < WEAPON_SHOOTERS; k++) do_event(w, ev, k);
}

/* 0x5196a0: the particles go (a weapon without an owner). */
static void delete_particles(Weapon *w)
{
    if (!(w->flags & WEAPON_PARTICLES)) return;
    for (unsigned k = 0; k < WEAPON_SHOOTERS; k++) {
        P3Class *pc;
        P3Particle *p = p3_from_handle(w->shooter_handle[k], &pc);
        if (p) p3_kill(pc, p);
        w->shooter_handle[k] = 0;
    }
    w->flags &= (uint16_t)~WEAPON_PARTICLES;
}

/* ---- in the world ---- */

void weapon_fall_impact(int type, const PhysHit *h, const void *env)
{
    if (type < 0) return;
    P3EffectLoc loc;
    memset(&loc, 0, sizeof loc);
    loc.tick = p3_tick();
    memcpy(loc.pos, h->point, sizeof loc.pos);
    memcpy(loc.normal, h->plane, sizeof loc.normal);
    loc.kind = P3_HIT_WALL, loc.quad = h->quad;
    int material = 0;
    AkevQuad q;
    if (env && akev_quad(env, h->quad, &q) && (q.texture & 0xffff) != 0xffff) {
        int mi = impact_material_index(material_of_texture(akev_texture(env, q.texture & 0xffff)));
        if (mi >= 0) material = mi;     /* the texture's material (TXMP+0x98) */
    }
    impact_effect(type, material, 0, &loc, 1.0f, NULL, NULL);
}

static const void *update_env;

/* WPiPostCollision 0x518930: a wall met faster than 0.35 into it plays the fall's impact; any hit but a
   character's marks the weapon (flag 2). */
static void post_collision(PhysObject *o, const PhysHit *hits, unsigned n)
{
    Weapon *w = o->owner;
    for (unsigned i = 0; i < n; i++) {
        if (hits[i].kind == 1 && hits[i].other.kind == PHYS_KIND_CHARACTER) continue;
        if (hits[i].kind != 0) {
            w->flags |= 2;
            continue;
        }
        const float *nm = hits[i].plane;
        float d = nm[1] * o->vel[1] + nm[0] * o->vel[0] + o->vel[2] * nm[2];
        static int type = -2;   /* the "Weapon" impact type (0x622194, WPrInitialize 0x51b0f0) */
        if (type == -2) type = impact_type_by_name(exe_str(0x54c520));
        if (fabsf(d) > 0.35f) weapon_fall_impact(type, &hits[i], update_env);     /* 0x52adb8 */
        w->flags |= 2;
    }
}

/* WPiWeaponPhysics_SkipPhyCollisions_Callback 0x518a80: another weapon, when either has flag 0x2000. */
static bool skip_other(const PhysCtx *self, const PhysCtx *o)
{
    if (o->kind != PHYS_KIND_WEAPON) return false;
    const Weapon *a = self->owner, *b = o->owner;
    return (a->flags & 0x2000u) || (b->flags & 0x2000u);
}

/* WPiReceiveForce 0x518b80: pushed by a context's move, but by a character only once on the ground
   (flag 2) and unowned: the player's only with wp_kickable, and not an AI's going for this one (the
   combat state's gun maneuver). */
static void receive_force(const PhysCtx *self, const PhysCtx *from, const float force[3])
{
    Weapon *w = self->owner;
    if (from->kind == PHYS_KIND_CHARACTER && !w->owner) {
        if (!(w->flags & 2)) return;
        const Chr *ch = from->owner;     /* (a Chr's first member) */
        if (!ch->c.ai) {
            if (!kickable) return;
        } else if (ch->ai.state == AI_STATE_COMBAT && ch->ai.combat.maneuver == COMBAT_MANEUVER_GUN && ch->ai.combat.gun == w)
            return;
    }
    physobj_push(&w->phys, force);
}

static int ctx_count(void) { return WEAPONS_MAX; }
static bool ctx_get(int i, PhysCtx *out)
{
    Weapon *w = &weapons[i];
    if (!(w->flags & WEAPON_IN_USE) || !w->has_phys) return false;
    physobj_ctx(&w->phys, out);
    return true;
}

/* WPiWeapon_CreatePhysics 0x518e10: a context of type 2 (flags 0x5400: the walls of flag 0x4000 skipped,
   friction only touching; friction 0.0025 and 0.75), its sphere a quarter of the geometry's largest
   coordinate about the place (+0x30). */
static void create_physics(Weapon *w)
{
    PhysObject *o = &w->phys;
    physobj_init(o);
    o->flags |= 0x5400u;
    o->owner = w;
    o->kind = PHYS_KIND_WEAPON;     /* the callbacks 0x6200d0 */
    o->post_collision = post_collision;
    o->skip = skip_other;
    o->receive_force = receive_force;
    o->friction_sub = 0.0025f;  /* 0x3b23d70a */
    o->friction_mul = 0.75f;
    uint32_t n = 0;
    const uint8_t *p = m3gm_points(weapon_geometry(w), &n);
    float big = -1e9f;      /* 0x52963c */
    for (uint32_t i = 0; p && i < 3 * n; i++) {
        float a = fabsf(rdf(p + 4 * i));
        if (a > big) big = a;
    }
    o->sphere[0] = o->sphere[1] = o->sphere[2] = 0.0f;
    o->sphere[3] = big * 0.25f;     /* 0x5296c4 */
    memcpy(o->pos, w->m + 9, sizeof o->pos);
    physobj_matrix(o);              /* PHrPhysics_MaintainMatrix 0x43b410 */
    w->has_phys = true;
}

static void identity(float m[12])
{
    static const float id[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };   /* 0x529d10 */
    memcpy(m, id, sizeof id);
}

Weapon *weapon_new(const void *onwc, const float pos[3], float facing)
{
    Weapon *w = NULL;
    for (int i = 0; i < WEAPONS_MAX && !w; i++)
        if (!(weapons[i].flags & WEAPON_IN_USE)) w = &weapons[i];
    if (!w) {
        /* every slot used: the oldest not flagged 4 (the original first looks for one with neither 1
           nor 4, which a used slot can't be) */
        uint32_t best = 0xffffffffu;
        for (int i = 0; i < WEAPONS_MAX; i++)
            if (!(weapons[i].flags & 4) && weapons[i].time < best) w = &weapons[i], best = weapons[i].time;
        if (!w) return NULL;
    }
    memset(w, 0, sizeof *w);
    w->cls = onwc;
    w->flags = WEAPON_IN_USE;
    w->spread = -1;
    fill_ammo(w);
    w->time = now;
    /* the context made, then put 2 above the place (its matrix and the weapon's follow on the next
       physics and update); without one the weapon's matrix there */
    create_physics(w);
    float at[3] = { pos[0], pos[1] + 2.0f, pos[2] };    /* 0x529818 */
    if (w->has_phys) memcpy(w->phys.pos, at, sizeof at);
    else {
        identity(w->m);
        memcpy(w->m + 9, at, sizeof at);
    }
    w->facing = facing;
    w->flags |= 0x80;
    return w;
}

static bool place_object(const ObjcObject *o, void *user)
{
    (void)user;
    char name[33] = { 0 };
    memcpy(name, o->data, o->size < 32 ? o->size : 32);
    const void *cls = tm_instance_get(TM_TAG("ONWC"), name);
    if (cls) weapon_new(cls, o->pos, o->rot[0] * 0.017453293f);    /* 0x529550 */
    return true;
}

void weapons_level_begin(void) { objc_each("Weapon", place_object, NULL); }

void weapon_delete(Weapon *w)
{
    if (!(w->flags & WEAPON_IN_USE)) return;
    w->flags &= (uint16_t)~WEAPON_STOP_WANTED;
    w->fire_min = 0;
    if (w->flags & WEAPON_FIRING) {
        w->flags &= (uint16_t)~WEAPON_FIRING;
        free_spread(w);
        every_shooter(w, EV_STOP);
    }
    if (w->flags & WEAPON_DELAYED) {
        every_shooter(w, EV_DELAY_STOP);
        w->flags &= (uint16_t)~WEAPON_DELAYED;
    }
    if (w->flags & WEAPON_BACKGROUND) {
        w->flags &= (uint16_t)~WEAPON_BACKGROUND;
        every_shooter(w, EV_BG_STOP);
    }
    if (w->owner && w->owner->weapon == w) w->owner->weapon = NULL;
    w->has_phys = false;            /* (0x43aef0) */
    w->flags &= (uint16_t)~WEAPON_IN_USE;
}

/* 0x51a4d0: the owner takes it: no physics, no fading, owned; with ammo the background events start. */
static void set_owner(Weapon *w, Character *c)
{
    w->owner = c;
    w->reload_wait = 0;
    w->flags &= 0xfff1;
    w->has_phys = false;            /* the context removed (0x43aef0) */
    w->fade_delay = w->fade = 0;
    uint16_t old = w->flags;
    w->flags = (uint16_t)((old & 0x77ff) | WEAPON_OWNED);
    if (w->ammo && !(old & WEAPON_BACKGROUND)) {
        w->flags |= WEAPON_BACKGROUND;
        every_shooter(w, EV_BG_START);
    }
}

/* WPrNotThroughWall 0x51b690: the place p moved back to 6 short of a wall between it and 7.5 above the
   character's feet (the line from there lengthened by 6). */
static void not_through_wall(const Character *c, float p[3])
{
    float from[3] = { c->pos[0], c->pos[1] + 7.5f, c->pos[2] };     /* 0x52a5bc */
    float d[3] = { p[0] - from[0], p[1] - from[1], p[2] - from[2] };
    float l = sqrtf(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
    float k = (l + 6.0f) / l;       /* 0x5297f8, MUrVector_SetLength 0x433d60 */
    for (int i = 0; i < 3; i++) d[i] *= k;
    static AkevHit hit[AKEV_HITS_MAX];
    if (!update_env || !akev_collide_point(update_env, from, d, 0x40800, 1, hit)) return;
    l = sqrtf(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
    k = 6.0f / l;
    for (int i = 0; i < 3; i++) p[i] = hit[0].point[i] - d[i] * k;
}

/* WPrRelease 0x51a550: let go, into the world. Its physics made where it is (kept out of the walls from
   the owner), facing the way the weapon node's x axis points (a random way without an owner), still or,
   thrown, rising 1.5 with a part of the owner's last hit (A+0xd4: 0.07 for a pistol (class +0xcc & 3),
   0.1 for a rifle (8), else 0.12). It fades after 4800 ticks, unless the class has 0x80000. */
static void release(Weapon *w, bool thrown)
{
    if (!(w->flags & WEAPON_IN_USE)) return;
    free_spread(w);
    if (!w->has_phys) create_physics(w);
    PhysObject *o = &w->phys;
    Character *c = w->owner;
    float m19[12];
    if (c) {
        not_through_wall(c, o->m + 9);
        memcpy(o->pos, o->m + 9, sizeof o->pos);
        if (chr_weapon_matrix(c, m19)) {
            float a = atan2f(m19[0], m19[2]) - 1.5707964f;     /* 0x529cd0 */
            if (a < 0.0f) a += 6.2831855f;                  /* 0x529548 */
            w->facing = a;
        } else
            c = NULL;
    }
    if (!c) w->facing = (float)trac_random() * 6.2831855f * 1.5259022e-05f;     /* UUrRandom 0x426e10 */
    o->vel[0] = o->vel[1] = o->vel[2] = 0.0f;
    if (thrown) {
        o->vel[1] = 1.5f;
        if (w->owner) {
            uint32_t k = options(w);
            float f = (k & 3) ? 0.07f : (k & ONWC_RIFLE) ? 0.1f : 0.12f;  /* 0x52b2e0, 0x529cc4, 0x52b1a4 */
            for (int i = 0; i < 3; i++) o->vel[i] += f * w->owner->hit_velocity[i];
        }
    }
    w->flags &= (uint16_t)~WEAPON_OWNED;
    w->owner = NULL;
    w->fire_min = 0;
    w->reload_wait = 0;
    w->sound_wait = 0;
    w->delay = 0;
    w->pause = 0;
    for (unsigned k = 0; k < shooters(w) && k < WEAPON_SHOOTERS; k++) w->shooter_delay[k] = 0;
    w->fade = 0;
    w->flags |= WEAPON_DROPPED_STOP | WEAPON_DROPPED;
    w->fade_delay = (options(w) & 0x80000) ? 0 : 4800;
}

/* WPiUpdate 0x5192b0. */
void weapons_update(const void *env)
{
    update_env = env;
    now++;
    for (int i = 0; i < WEAPONS_MAX; i++) {
        Weapon *w = &weapons[i];
        if (!(w->flags & WEAPON_IN_USE)) continue;
        if (w->flags & WEAPON_DROPPED_STOP) {
            stop_firing(w, false);
            w->flags &= (uint16_t)~WEAPON_DROPPED_STOP;
        }
        if (w->flags & WEAPON_DROPPED) {
            if (w->flags & WEAPON_DELAYED) {
                every_shooter(w, EV_DELAY_STOP);   /* 0x51a130 with trigger -1 */
                w->flags &= (uint16_t)~WEAPON_DELAYED;
            }
            if (w->flags & WEAPON_BACKGROUND) {
                w->flags &= (uint16_t)~WEAPON_BACKGROUND;
                every_shooter(w, EV_BG_STOP);
            }
            w->flags &= (uint16_t)~WEAPON_DROPPED;
        }
        if (!w->owner) {
            delete_particles(w);
            /* the sphere at the place, the rise no faster than 2.25 (0x52b8d4) */
            if (w->has_phys) {
                memcpy(w->phys.sphere, w->phys.pos, sizeof w->phys.pos);
                if (w->phys.vel[1] > 2.25f) w->phys.vel[1] = 2.25f;
            }
            /* fading: +0x46 (4800 from the release) counts down to the 600 ticks of +0x44 */
            if (!no_fade) {
                if (w->fade_delay && !--w->fade_delay) w->fade = 600;
                if (w->fade && !--w->fade) { weapon_delete(w); continue; }
            }
            if (w->has_phys) memcpy(w->m, w->phys.m, sizeof w->m);
            continue;
        }
        if (w->owner->weapon == w) {
            if (!chr_weapon_matrix(w->owner, w->m)) {
                memcpy(w->m + 9, w->owner->pos, 3 * sizeof(float));
                w->m[10] += w->owner->pelvis_height;
            }
        }
        create_particles(w);
        if (w->fire_min && --w->fire_min && (options(w) & ONWC_SUSTAINED)) depress_trigger(w, false, false, NULL);
        if (w->flags & WEAPON_STOP_WANTED) stop_firing(w, false);
        if (w->reload_wait) w->reload_wait--;
        if (w->sound_wait) w->sound_wait--;
        if (w->delay && !--w->delay) {
            if ((w->flags & WEAPON_FIRING) && (options(w) & ONWC_AUTO_RELEASE)) stop_firing(w, false);
            /* the ammo drain of a continuous weapon (class 0x100000, w4_psm): with that weapon */
            if (w->flags & WEAPON_DELAYED) {
                every_shooter(w, EV_DELAY_STOP);
                w->flags &= (uint16_t)~WEAPON_DELAYED;
            }
        }
        if (w->pause) w->pause--;
        if (w->flags & WEAPON_FIRING)
            for (unsigned k = 0; k < shooters(w) && k < WEAPON_SHOOTERS; k++)
                if (w->shooter_delay[k] && !--w->shooter_delay[k]) do_event(w, EV_START, k);
    }
}

/* OBiPhysics_Update 0x43af50 for the weapons' contexts (after WPiUpdate and the powerups'). */
void weapons_physics(const void *env)
{
    update_env = env;
    for (int i = 0; i < WEAPONS_MAX; i++)
        if ((weapons[i].flags & WEAPON_IN_USE) && weapons[i].has_phys) physobj_update(&weapons[i].phys, env);
}

/* WPrDisplay 0x519710's matrix and alpha for a weapon in the world: its context's matrix (else its own)
   turned about y by its facing (MUrMatrix3x3_RotateY 0x434e70) and raised by the class's +0x710;
   fading, the alpha the part of 600 ticks left. */
uint8_t weapon_world_matrix(const Weapon *w, float out[12])
{
    const float *m = w->has_phys ? w->phys.m : w->m;
    float c = cosf(w->facing), s = sinf(w->facing);
    for (int j = 0; j < 3; j++) {
        const float *col = m + 3 * j;
        out[3 * j] = c * col[0] + s * col[2];
        out[3 * j + 1] = col[1];
        out[3 * j + 2] = c * col[2] - s * col[0];
    }
    memcpy(out + 9, m + 9, 3 * sizeof(float));
    out[10] += rdf(w->cls + 0x710);
    if (!w->fade) return 0xff;
    float a = (float)w->fade * 0.0016666667f;   /* 0x52b8d8 */
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return (uint8_t)ftol(a * 255.0f);          /* 0x4264e0 */
}

bool weapon_glow(const Weapon *w, const float m[12], uint8_t alpha, const void *tex[2], float pos[3], float size[2],
                 uint8_t *glow_alpha)
{
    tex[0] = tm_link(w->cls + 0x6f4);
    tex[1] = w->ammo ? tm_link(w->cls + 0x6f8) : NULL;
    float box[6], sphere[4];
    if (!tex[0] || !m3gm_bounds(weapon_geometry(w), box, sphere)) return false;
    size[0] = (box[3] - box[0]) * rdf(w->cls + 0x6fc) * 1.1f;     /* 0x5295a0 */
    size[1] = (box[5] - box[2]) * rdf(w->cls + 0x700) * 1.1f;
    /* the bottom's middle, 0.1 up (0x529cc4), through the matrix (MUrMatrix_MultiplyPoint 0x4339e0) */
    float p[3] = { (box[3] + box[0]) * 0.5f, box[1] + 0.1f, (box[5] + box[2]) * 0.5f };
    for (int k = 0; k < 3; k++) pos[k] = m[k] * p[0] + m[3 + k] * p[1] + m[6 + k] * p[2] + m[9 + k];
    uint32_t a = (uint32_t)alpha * 255u;
    *glow_alpha = (uint8_t)(((uint32_t)(((uint64_t)a * 0xaaaaaaabu) >> 32) >> 1) >> 8);    /* a third */
    return true;
}

/* ---- the character's side ---- */

static float pickup_y(const Weapon *w) { return w->m[10] + rdf(w->cls + ONWC_PICKUP + 4); }

/* 0x51b180: the unowned weapon (or only that one) whose handle is between the heights and nearest
   (across) within the squared distance. */
static Weapon *find_pickup(const float pos[3], const Weapon *only, float ymin, float ymax, float within2)
{
    Weapon *best = NULL;
    for (int i = 0; i < WEAPONS_MAX; i++) {
        Weapon *w = &weapons[i];
        if (!(w->flags & WEAPON_IN_USE) || w->owner || (w->flags & 4)) continue;
        if (only && only != w) continue;
        float y = pickup_y(w);
        if (y < ymin || y > ymax) continue;
        float dx = pos[0] - (w->m[9] + rdf(w->cls + ONWC_PICKUP));
        float dz = pos[2] - (w->m[11] + rdf(w->cls + ONWC_PICKUP + 8));
        float d2 = dz * dz + dx * dx;
        if (d2 < within2) best = w, within2 = d2;
    }
    return best;
}

bool weapon_free(const Weapon *w) { return (w->flags & WEAPON_IN_USE) && !w->owner && !(w->flags & 4); }

int weapons_nearby(const float p[3], float radius, Weapon **out)
{
    /* the list kept by distance, a later one after those as near (WEAPON flag 0x40 marks the listed) */
    float d2[WEAPONS_MAX];
    int n = 0;
    for (int i = 0; i < WEAPONS_MAX; i++) {
        Weapon *w = &weapons[i];
        if (!(w->flags & WEAPON_IN_USE)) continue;
        w->flags &= (uint16_t)~0x40;
        if (!weapon_free(w)) continue;
        float h[3];
        weapon_pickup_position(w, h);
        float dx = h[0] - p[0], dy = h[1] - p[1], dz = h[2] - p[2];
        float d = dx * dx + dy * dy + dz * dz;      /* MUrPoint_Distance_Squared, to the handle */
        if (d > radius * radius) continue;
        w->flags |= 0x40;
        int k = n;
        while (k > 0 && d2[k - 1] > d) out[k] = out[k - 1], d2[k] = d2[k - 1], k--;
        out[k] = w, d2[k] = d, n++;
    }
    return n;
}

void weapon_pickup_position(const Weapon *w, float out[3])
{
    for (int k = 0; k < 3; k++) out[k] = w->m[9 + k] + rdf(w->cls + ONWC_PICKUP + 4 * k);
}

uint32_t weapon_options(const Weapon *w) { return options(w); }
bool weapon_loaded(Weapon *w) { return has_ammo(w); }
bool chr_has_ammo_for(const Character *c, const Weapon *w) { return inventory(c, ammo_type(w)) != 0; }
void chr_drop_weapon(Character *c) { drop_hand(c, false); }
void chr_throw_weapon(Character *c) { drop_hand(c, true); }

void (*weapon_new_hook)(const void *onwc);

void chr_set_weapon(Character *c, Weapon *w)
{
    if (!w) {
        if (c->weapon) {
            Weapon *old = c->weapon;
            old->flags = (uint16_t)((old->flags & 0xf7ff) | 0xc000);
            c->weapon = NULL;
        }
        c->varient &= 0x87ff;
        c->fire_down = c->fire2_down = c->fire_again = c->fire_empty = false;
        return;
    }
    if (c->flags & 1) return;
    c->weapon = w;
    c->varient &= 0x87ff;
    /* the variant of the hand (C+0x04 flag 0x2000: none): rifle in the left hand 0x4000, a rifle 0x2000,
       the left hand 0x1000, else 0x800 */
    if (!(c->flags & 0x2000)) {
        bool rifle = (options(w) & ONWC_RIFLE) != 0, left = ((const uint8_t *)c->oncc)[0xc8c] != 0;
        c->varient |= rifle && left ? 0x4000 : rifle ? 0x2000 : left ? 0x1000 : 0x800;
    }
    set_owner(w, c);
    /* the player's first of its class: the pause screen's new weapon (ONrInGameUI_NewWeapon 0x505050),
       recorded as had (0x50f500) */
    const Chr *p = chars_player();
    uint16_t index = (uint16_t)(w->cls[0x136] | w->cls[0x137] << 8);
    if (p && c == &p->c && !persist_weapon_seen(index)) {
        if (weapon_new_hook) weapon_new_hook(w->cls);
        persist_set_weapon_seen(index);
    }
}

/* ONrCharacter_PickupWeapon 0x4ed770, a weapon picked up: the one in the hand drops first. */
static void pickup_weapon(Character *c, Weapon *w)
{
    if (c->weapon) {
        Weapon *old = c->weapon;
        old->flags = (uint16_t)((old->flags & 0xf7ff) | 0xc000);
        c->weapon = NULL;
        release(old, false);
        c->varient &= 0x87ff;
    }
    chr_set_weapon(c, w);
}

void chr_pickup_weapon(Character *c, Weapon *w) { pickup_weapon(c, w); }

/* HandleStartPickup (in HandleHeartbeatInput 0x4f6ad0), the swap key gone down and no weapon overlay:
   the powerup and the weapon at hand (0x4fa4b0: a handle within 3 below and 18 above the feet, 8
   across), a weapon only with none stowed. With neither, the weapon in the hand is holstered, else the
   stowed one drawn. An animation that allows it (TRAM flag 20) picks up at once (the weapon first);
   else the pickup animation: Pickup_Object (0x9d) or Pickup_Pistol (0x9e) or Pickup_Rifle (0x9f), the
   _Mid ones (0xe3, 0xe4, 0xe5) for one over 9 above the feet. A powerup the player can't carry more of
   plays the event sound 6. */
static bool start_pickup(Character *c, const ChrInput *in)
{
    if (!(in->down & CHR_KEY_SWAP) || c->overlay[CHR_OVERLAY_WEAPON].anim) return false;
    bool full = false;
    Powerup *pu = powerup_find(c, c->pos[1] - 3.0f, c->pos[1] + 18.0f, NULL, 64.0f, &full);
    Weapon *w = find_pickup(c->pos, NULL, c->pos[1] - 3.0f, c->pos[1] + 18.0f, 64.0f);
    int idx = 0;
    Weapon *stowed = find_last_stowed(c, 0, &idx);
    bool want_weapon = w && !stowed, started = false;
    if (!pu && !want_weapon) {
        if (c->weapon) holster(c, false);
        else if (stowed) draw_weapon(c, NULL);
    } else if (tr_test_flag(c->anim, 20)) {
        /* an animation that picks up as it goes: at once */
        if (want_weapon) pickup_weapon(c, w);
        else if (powerup_give(c, pu->type, pu->amount, false, true)) powerup_delete(pu);     /* 0x4eda10 */
    } else {
        uint16_t type;
        if (pu) {
            type = pu->pos[1] <= c->pos[1] + 9.0f ? 0x9d : 0xe3;
            c->pickup_powerup = pu, c->pickup = NULL;
        } else {
            bool mid = c->pos[1] + 9.0f < pickup_y(w);
            type = (options(w) & ONWC_RIFLE) ? 0x9f : 0x9e;
            if (mid) type = (uint16_t)(type + 0x46);
            c->pickup = w, c->pickup_powerup = NULL;
        }
        started = chr_do_animation(c, CHR_ANIM_WHEN_CAN, type) != NULL;
    }
    const Chr *p = chars_player();
    if (full && p && c == &p->c) sound_event_play(6);     /* ONrGameState_EventSound_Play 0x4fdac0 */
    return started;
}

/* The weapon's keys of HandleHeartbeatInput: without a weapon no reload; the reload key asks for one
   (0x4edc00, run at the weapon's frame step); with no reload asked and no weapon overlay, fire gone
   down (0x4e8f80), else held (0x4e8fa0: sustained fire again, else the empty click), else released
   (0x4e9000); the alternate fire only when the first is neither down nor held. HandlePickupInput
   0x4eda40: drop, hypo (weapon_input). */
static void fire_input(Character *c, const ChrInput *in)
{
    if (!c->weapon) {
        c->reload_wanted = false;
        return;
    }
    /* fire gone down with an empty weapon and nothing to reload: C+0x08 bit 0, no firing */
    if (in->down & CHR_KEY_FIRE) c->reload_wanted = empty_unreloadable(c);
    if (in->down & CHR_KEY_RELOAD) c->reload_key = true;
    if (c->reload_wanted || c->overlay[CHR_OVERLAY_WEAPON].anim) {
        c->stop_fire = c->stop_fire2 = true;
        return;
    }
    if (in->down & CHR_KEY_FIRE) c->fire_down = true;
    else if (in->held & CHR_KEY_FIRE) {
        if (options(c->weapon) & ONWC_SUSTAINED) c->fire_again = true;
        else c->fire_empty = true;
    } else {
        if (in->up & CHR_KEY_FIRE) c->stop_fire = true;
        if (in->down & CHR_KEY_ALT_FIRE) {
            if (options(c->weapon) & ONWC_ALT_FIRE) c->fire2_down = true;
        } else if (in->up & CHR_KEY_ALT_FIRE)
            c->stop_fire2 = true;
    }
}

/* ONrCharacter_HandlePickupInput 0x4eda40: with no weapon overlay, the drop key lets the weapon go
   (0x51b590 then ONrCharacter_SetWeapon's clearing); else the hypo key uses a hypo (0x51c770), with
   the event sound 4. */
static void weapon_input(Character *c, const ChrInput *in)
{
    fire_input(c, in);
    if (c->overlay[CHR_OVERLAY_WEAPON].anim) return;
    if ((in->down & CHR_KEY_DROP) && c->weapon) {
        Weapon *w = c->weapon;
        w->flags = (uint16_t)((w->flags & 0xf7ff) | 0xc000);
        c->weapon = NULL;
        release(w, false);
        c->varient &= 0x87ff;
        c->fire_down = c->fire2_down = c->fire_again = c->fire_empty = false;
    } else if ((in->down & CHR_KEY_HYPO) && inventory_use_hypo(c)) {
        sound_event_play(4);
    }
}

static void xform(const float m[12], const float p[3], float out[3])
{
    for (int k = 0; k < 3; k++) out[k] = m[k] * p[0] + m[3 + k] * p[1] + m[6 + k] * p[2] + m[9 + k];
}

/* 0x4f0260: something between p and p + d: the level's quads (the first hit, skipping 0x44800), or an
   object nearer than d's length (AMrRayToObject 0x4d52c0 along d made a unit, MUrVector_Normalize_ZeroTest
   0x4340f0: a zero d counts as blocked). */
static bool gun_ray_blocked(const float p[3], const float d[3])
{
    const void *env = p3_environment();
    static AkevHit h[AKEV_HITS_MAX];
    if (env && akev_collide_point(env, p, d, 0x44800, 1, h)) return true;
    float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (fabsf(l2) < 9.5367432e-07f) return true;     /* 0x529d00: 2^-20 */
    float l = sqrtf(l2), n[3] = { d[0] / l, d[1] / l, d[2] / l }, hit[3];
    if (!object_ray(p, n, hit)) return false;
    float e[3] = { hit[0] - p[0], hit[1] - p[1], hit[2] - p[2] };
    return l > sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
}

/* ONrCharacter_IsGunThroughWall 0x4f0150: from the pelvis's middle (its geometry's sphere's centre under
   the part's matrix, A+0x2278) to each shooter that takes ammo (its matrix's place under the weapon
   node's, A+0x2608), then to the weapon node itself: a wall or an object in the way. */
static bool gun_through_wall(const Character *c, const Weapon *w)
{
    const void *body = body_of_class(c->oncc, 4);
    const void *g = body ? body_geometry(body, 0) : NULL;
    float box[6], sph[4] = { 0, 0, 0, 0 }, wm[12], p[3];
    if (g) m3gm_bounds(g, box, sph);
    xform(c->bone_world[0], sph, p);
    if (!chr_weapon_matrix(c, wm)) return false;
    for (unsigned k = 0; k < shooters(w); k++) {
        const uint8_t *s = shooter(w, k);
        if (!rd16(s + SH_AMMO)) continue;
        float at[3] = { rdf(s + 0x24), rdf(s + 0x28), rdf(s + 0x2c) }, q[3];
        xform(wm, at, q);
        float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
        if (gun_ray_blocked(p, d)) return true;
    }
    float d[3] = { wm[9] - p[0], wm[10] - p[1], wm[11] - p[2] };
    return gun_ray_blocked(p, d);
}

/* AMrRayToCharacter 0x4d5150: the nearest of the active characters but the one of index skip the ray
   from p along d (unbounded, 0x4f1630) meets; its part and the point. */
static Chr *ray_to_character(uint16_t skip, const float p[3], const float d[3], float point[3])
{
    Chr *best = NULL;
    float bd = 3.4028235e38f;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *o = chars_get(i);
        if (!o || o->c.inactive || o->c.id == skip) continue;
        float q[3];
        if (!p3_character_ray_part(&o->c, p, d, false, q, NULL)) continue;
        float e[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] }, dd = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (dd < bd) bd = dd, best = o, memcpy(point, q, 12);
    }
    return best;
}

/* 0x4d4e50: what the ray from p along dir (a unit) meets first: a character (but skip's; 1), the level's
   quads (1000 along it, skipping 0x44800; 2) or an object (AMrRayToObject; 3), the nearest of those
   found (a missing one 1e9 away); 0 none, its end 1000 along. The point in at. */
static int ray_pick(uint16_t skip, const float p[3], const float dir[3], Chr **who, float at[3])
{
    const float far = 1e9f;     /* 0x4e6e6b28 */
    float pc[3] = { far, far, far }, pe[3] = { far, far, far }, po[3] = { far, far, far };
    const void *env = p3_environment();
    *who = NULL;
    if (!env) return 0;
    Chr *ch = skip != 0xfffe ? ray_to_character(skip, p, dir, pc) : NULL;
    bool obj = object_ray(p, dir, po);
    const float v[3] = { dir[0] * 1000.0f, dir[1] * 1000.0f, dir[2] * 1000.0f };    /* 0x52a87c */
    static AkevHit h[AKEV_HITS_MAX];
    bool wall = akev_collide_point(env, p, v, 0x44800, 1, h) != 0;
    if (wall) memcpy(pe, h[0].point, 12);
    int kind;
    if (!obj) kind = wall ? (ch ? -1 : 2) : (ch ? 1 : 0);
    else kind = !wall && !ch ? 3 : -1;
    if (kind < 0) {
        float de = sqrtf((pe[0] - p[0]) * (pe[0] - p[0]) + (pe[1] - p[1]) * (pe[1] - p[1]) + (pe[2] - p[2]) * (pe[2] - p[2]));
        float dc = sqrtf((pc[0] - p[0]) * (pc[0] - p[0]) + (pc[1] - p[1]) * (pc[1] - p[1]) + (pc[2] - p[2]) * (pc[2] - p[2]));
        float dob = sqrtf((po[0] - p[0]) * (po[0] - p[0]) + (po[1] - p[1]) * (po[1] - p[1]) + (po[2] - p[2]) * (po[2] - p[2]));
        kind = dc < de && dc < dob ? 1 : dob < de ? 3 : 2;
    }
    switch (kind) {
    case 1: *who = ch, memcpy(at, pc, 12); break;
    case 2: memcpy(at, pe, 12); break;
    case 3: memcpy(at, po, 12); break;
    default: for (int k = 0; k < 3; k++) at[k] = p[k] + v[k];
    }
    return kind;
}

bool weapon_sight(const Character *c, WeaponSight *s)
{
    /* 0x4e1900: the player's (C+0x1e8 0) gun, not through a wall, with ammo, and a laser (ONWC +0x0c) */
    Weapon *w = c ? c->weapon : NULL;
    if (!w || c->ai || c->inactive || gun_through_wall(c, w) || !has_ammo(w)) return false;
    const uint8_t *k = w->cls;
    float lp[3] = { rdf(k), rdf(k + 4), rdf(k + 8) }, ld[3] = { rdf(k + 0xc), rdf(k + 0x10), rdf(k + 0x14) };
    if (!(fabsf(ld[0]) >= 0.0009765625f || fabsf(ld[1]) >= 0.0009765625f || fabsf(ld[2]) >= 0.0009765625f)) return false;
    /* WPrGetMarker 0x51a8a0: the marker and its point along under the weapon's matrix, the way made a unit */
    float tip[3] = { lp[0] + ld[0], lp[1] + ld[1], lp[2] + ld[2] }, from[3], to[3], dir[3];
    xform(w->m, lp, from);
    xform(w->m, tip, to);
    for (int i = 0; i < 3; i++) dir[i] = to[i] - from[i];
    float l = 1.0f / sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (int i = 0; i < 3; i++) dir[i] *= l;
    float range = rdf(k + 0x18), at[3], end[3];
    Chr *who;
    int kind = ray_pick((uint16_t)(c->id & 0xff), from, dir, &who, at);
    if (kind == 0) {
        for (int i = 0; i < 3; i++) end[i] = dir[i] * range + from[i];
    } else {
        float e[3] = { at[0] - from[0], at[1] - from[1], at[2] - from[2] };
        float d = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        memcpy(end, at, 12);
        if (d <= range) {
            /* the end drawn back 2 from what it meets (0x529818), toward the gun (0x432df0) */
            if (d > 0.0009765625f) {
                float f = 2.0f / d;
                if (f < 0.0f) f = 0.0f;
                else if (f > 1.0f) f = 1.0f;
                for (int i = 0; i < 3; i++) end[i] = (at[i] - from[i]) * (1.0f - f) + from[i];
            }
        } else {
            /* beyond the range: as far as it goes (MUrVector_SetLength 0x433d60), meeting nothing */
            float m = range / d;
            for (int i = 0; i < 3; i++) end[i] = from[i] + e[i] * m;
            kind = 0;
        }
    }
    memcpy(s->from, from, 12);     /* 0x5ecb70 */
    memcpy(s->to, end, 12);        /* 0x5ecb58 */
    s->kind = kind == 1 && who && (who->c.flags & 1) ? 0 : kind;   /* 0x5ecb68: a dead one isn't a target */
    s->cls = k;
    return true;
}

/* 0x4e9040, a trigger of DoCharacterFrame (mode 0 fire, 1 again, 2 alternate, 3 held): the flags
   cleared; a dead character doesn't; a gun through a wall stops firing; the trigger; out of ammo the
   firing stops (then the reload and the empty sound). */
static void trigger(Character *c, int mode)
{
    if (mode == 2) c->fire2_down = false;
    else c->fire_down = c->fire_again = c->fire_empty = false;
    if (!c->weapon) return;
    if (gun_through_wall(c, c->weapon)) {
        c->stop_fire = c->stop_fire2 = false;       /* A+0x21e3, A+0x21e4 */
        stop_firing(c->weapon, false);
        return;
    }
    if (c->flags & 1) return;
    c->last_action = game_time();
    bool empty = false;
    depress_trigger(c->weapon, false, mode == 2, &empty);
    if (empty) {
        stop_firing(c->weapon, false);
        /* a press (not a held trigger) tries a reload, the AI's always; with nothing to reload with,
           the empty click (WPrOutOfAmmoSound 0x51d080, with the weapon sounds) and the AI's note
           (C+0x1f0 0x40: with the AI's weapons) */
        if ((mode != 1 && mode != 3) || c->ai) {
            bool none = false;
            weapon_try_reload(c, c->weapon, NULL, &none);
            if (none && c->ai) ((Chr *)c)->ai.flags |= AI_FLAG_OUT_OF_AMMO;   /* its script, AI2rUpdateAI */
        }
    }
}

static void frame(Character *c, int step)
{
    if (step == CHR_FRAME_PICKUP) {
        /* DoCharacterFrame: a pickup target is forgotten once neither the animation nor the queued one
           is a pickup; at the action frame (TRAM +0x174, at most the last frame) the weapon still there
           (6 below, 24 above, 16 across) is picked up */
        uint16_t t = c->type, q = c->queued;
        if ((c->pickup || c->pickup_powerup) && t != 0x9d && t != 0x9e && t != 0x9f && q != 0x9d && q != 0x9e && q != 0x9f)
            c->pickup = NULL, c->pickup_powerup = NULL;
        int act = (int16_t)rd16((const uint8_t *)c->anim + 0x174), last = (int)rd16((const uint8_t *)c->anim + 0x164) - 1;
        if (act > last) act = last;
        if ((int)c->frame == act) {
            if (t == 0x9d || t == 0xe3) {
                /* the powerup still there (6 below, 24 above, 16 across) goes into the inventory; with the
                   target forgotten (as for 0xe3, which the test above doesn't keep), any in reach */
                Powerup *p = powerup_find(c, c->pos[1] - 6.0f, c->pos[1] + 24.0f, c->pickup_powerup, 256.0f, NULL);
                if (p && powerup_give(c, p->type, p->amount, false, true)) powerup_delete(p);
                c->pickup = NULL, c->pickup_powerup = NULL;
            } else if (t == 0x9e || t == 0x9f || t == 0xe4 || t == 0xe5) {
                Weapon *w = find_pickup(c->pos, c->pickup, c->pos[1] - 6.0f, c->pos[1] + 24.0f, 256.0f);
                if (w) pickup_weapon(c, w);
                c->pickup = NULL, c->pickup_powerup = NULL;
            }
        }
        if (c->reload_key) {
            /* the reload key gone down (A+0x217c bit 3): a try, the player's click when it can't
               (full, or nothing to reload with: ONrGameState_EventSound_Play, with the event sounds) */
            c->reload_key = false;
            bool is_full = false, none = false;
            if (c->weapon) weapon_try_reload(c, c->weapon, &is_full, &none);
        }
        return;
    }
    if (c->fire_down) trigger(c, 0);
    else if (c->fire_again) trigger(c, 1);
    else if (c->fire2_down) trigger(c, 2);
    else if (c->fire_empty) trigger(c, 3);
    if (c->stop_fire || c->stop_fire2) {
        c->stop_fire = c->stop_fire2 = false;
        if (c->weapon) stop_firing(c->weapon, false);
    }
}

/* ---- a particle's damage (0x5178c0, then 0x5179f0) ---- */

static Character *owner_character(uint32_t owner)
{
    uint32_t k = owner & 0xff000000u;
    if (k != 0x1000000u && k != 0x2000000u) return NULL;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && chars_in_use(c) && c->c.id == (owner & 0xffffff)) return &c->c;
    }
    return NULL;
}

/* WPrParticleInaccuracy 0x51ced0: a character owner's (0x1000000 | id) inaccuracy, if it is alive and
   AI-controlled (C+0x1e8 2): in combat (state 5) its shooting skill's (the targeting's, the combat
   block +0x164, +0x10); for an enemy of the player (AI2rTeam_FriendOrFoe 2) times the difficulty's
   factor (game settings +0xad4: 1.2 easy, 1 normal, 0.6 hard). A turret owner's (0x3000000 | id): its
   class's skill (+0x5f8), always times the factor. Patrolling (state 3), a slip of the original's (see
   below). */
bool weapon_cheat_gatling;

static float particle_inaccuracy(uint32_t owner)
{
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    float aim = gs ? rdf(gs + 0xad4 + 4 * persist_difficulty()) : 1.0f;
    if ((owner & 0xff000000u) == 0x3000000u) {
        float v;
        return turret_inaccuracy((uint16_t)owner, &v) ? v * aim : 0.0f;
    }
    if ((owner & 0xff000000u) != 0x1000000u) return 0.0f;
    Chr *c = NULL;
    for (int i = 0; i < chars_slots() && !c; i++) {
        Chr *x = chars_get(i);
        if (x && x->c.id == (owner & 0xffffff)) c = x;
    }
    if (!c || (c->c.flags & 1) || !c->c.ai) return 0.0f;
    float v = 0.0f;
    if (c->ai.state == AI_STATE_COMBAT && c->ai.combat.t.skill) v = rdf(c->ai.combat.t.skill + 0x10);
    /* patrolling (its shooting at a flag): the original reads the patrol targeting's +0xdc and the block's
       +0x5bc, next to the skill (+0xd4) and the point's float (+0x5b4) it seems to have meant: the hand's
       matrix (its fifth float) plus the weapon's address taken as a float (a denormal, as good as 0) */
    else if (c->ai.state == AI_STATE_PATROL && c->ai.shoot_init) v = c->ai.shoot.matrix[4];
    const Chr *pl = chars_player();
    if (pl && ai_team_relation(c->team, pl->team) == 2) v *= aim;
    return v;
}

/* 0x4e9530: the character turned to the direction; -1 (and turned the other way) when it was more than
   a right angle (0x529cd0) off, else 1. */
static int face_along(Character *c, const float d[3])
{
    float a = atan2f(d[0], d[2]);
    if (a < 0.0f) a += 6.2831855f;
    float rel = a - c->facing;
    if (rel < -3.1415927f) rel += 6.2831855f;
    else if (rel > 3.1415927f) rel -= 6.2831855f;
    int r = 1;
    if (fabsf(rel) > 1.5707964f) {
        a += 3.1415927f;
        if (a >= 6.2831855f) a -= 6.2831855f;
        r = -1;
    }
    c->facing = a;
    return r;
}

/* 0x5178c0: the hit's damage to the character: none to its own owner when the class says so; the
   direction the particle's move (else from the hit to the pelvis, else the character's back). Then
   0x5179f0: a particle hurts once (A+0x18c); the class's resistance to the damage's type (ONCC+0xc60)
   lessens the stun and the knockback; ONrCharacter_TakeDamage; the type's reaction: 1 a light stun (0xd4),
   2 a heavy one (0xc6, 0xd5 from behind), 3 a knockdown when the knockback is strong, 4 a knockdown
   (0x4f, 0x58), 5 the blow-up (0xd6, 0xd7). A force shield (C+0x1b8) or the super shield takes the hit
   first, lit where it is hit. The hit puts the character in fight mode
   (0x4eb0b0); type 0 plays the hit overlay (0x4f1210). Type 6 is the tractor beam's hold (A+0x112, A+0x120,
   A+0x124; its pull at 0x4de759); the HUD's direction is TakeDamage's (0x4e6530). True when the particle already hurt the character. */
static bool inflict_damage(Character *c, const float at[3], float reach, const float dir[3], int type, uint32_t dmg,
                           uint32_t st, float knockback, uint32_t handle, uint32_t owner);

static bool particle_damage(Character *c, const float point[3], const float move[3], int type, float damage,
                            float stun, float knockback, uint32_t owner, uint32_t handle, bool self_immune)
{
    if (owner == 0) owner = 0x4000000u;
    else if (self_immune && ((owner & 0xff000000u) == 0x1000000u || (owner & 0xff000000u) == 0x2000000u) &&
             (owner & 0xffffff) == c->id)
        return false;
    float dir[3] = { move[0], move[1], move[2] };
    float l = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    if (l < 1e-6f) {
        float pel[3];
        if (chr_part_point(c, 0, pel)) dir[0] = pel[0] - point[0], dir[1] = pel[1] - point[1], dir[2] = pel[2] - point[2];
        l = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
        if (l < 1e-6f) dir[0] = -sinf(c->facing), dir[1] = 0.0f, dir[2] = -cosf(c->facing), l = 1.0f;
    }
    l = 1.0f / sqrtf(l);
    dir[0] *= l, dir[1] *= l, dir[2] *= l;
    return inflict_damage(c, point, 4.0f, dir, type, (uint32_t)lrintf(damage), (uint32_t)lrintf(stun), knockback, handle,
                          owner);
}

/* InflictDamage 0x5179f0 */
static bool inflict_damage(Character *c, const float at[3], float reach, const float dir[3], int type, uint32_t dmg,
                           uint32_t st, float knockback, uint32_t handle, uint32_t owner)
{
    if (handle) {
        if (c->last_particle == handle) return true;
        c->last_particle = handle;
    }
    /* a force shield left (C+0x1b8), the Daodan's (C+0x04 bit 0x40000000) or the super shield up: the
       shield's glow lit on the parts near the hit (within the reach: 4 for a particle's, the blast's
       radius, inferred) (0x4ef8e0, 0x4ef850); the super shield takes the hit */
    bool super = chr_super_shielded(c);
    if (c->shield || (c->flags & 0x40000000u) || super) inventory_shield_hit(c, chr_parts_near(c, at, reach));
    if (super) return false;
    if (c->flags & 0x80000000u) return false;
    /* the force shield takes the whole hit: what it has goes, the rest of the damage with it (a hit
       bigger than the shield is lost); the HUD's arc, and nothing more */
    if (c->shield) {
        c->shield = c->shield >= dmg ? (uint16_t)(c->shield - dmg) : 0;
        chr_hit_direction(c, dir);
        return false;
    }
    if (type >= 0 && type < 7) {
        float r = rdf((const uint8_t *)c->oncc + 0xc60 + 4 * type);
        if (r > 0.0f) {
            r = 1.0f - r;
            if (r < 0.0f) r = 0.0f;
            st = (uint32_t)lrintf((float)st * r);
            knockback *= r;
        }
    }
    /* the boss's shield (C+0x04 0x40000000, chr_boss_shield): the damage, the stun and the knockback cut by
       the class's share (ONCC+0xc7c); the damage's and the stun's fractions each kept as a point with
       their chance (UUrRandom 0x426e10 against 65535 times the fraction) */
    if (c->flags & 0x40000000u) {
        float k = 1.0f - rdf((const uint8_t *)c->oncc + 0xc7c);
        knockback *= k;
        float fd = (float)dmg * k, fs = (float)st * k;
        uint32_t d = (uint32_t)ftol(fd), s = (uint32_t)ftol(fs);     /* 0x4264e0 */
        if ((double)(fd - (float)d) * 65535.0 > (double)trac_random()) d++;
        if ((double)(fs - (float)s) * 65535.0 > (double)trac_random()) s++;
        dmg = d, st = s;
    }
    chr_enter_fight_mode(c);    /* 0x4eb0b0: a hit character goes to fight mode */
    /* type 6's damage is held for its hold, none taken */
    chr_take_damage(c, type == 6 ? 0 : dmg, knockback, dir, 0, owner_character(owner), owner & 0xff000000u);
    /* an unstoppable character (0x4e6500: the cheat for the player, C+0x04 bit 0x100) takes no
       knockdown or stun of the damage's kind */
    if (chr_is_unstoppable(c) || (c->flags & 1)) return false;
    switch (type) {
    case 0:
        chr_hit_overlay(c);
        break;
    case 1:
        if (st) {
            chr_knockdown(c, 0xd4);
            if (c->countdown_1c98 < st) c->countdown_1c98 = (uint16_t)st;
        }
        break;
    case 2:
        if (st) {
            if (c->knockdown_type != 0xc6 && (c->knockdown_type < 0xd4 || c->knockdown_type > 0xd5))
                chr_knockdown(c, face_along(c, dir) == 1 ? 0xd5 : 0xc6);
            c->countdown_1c9a = 30;
            if (c->countdown_1c98 < st) c->countdown_1c98 = (uint16_t)st;
        }
        break;
    case 3: {
        /* by the knockback's strength (A+0xb8, squared): over 16.81 (0x52b8cc) a knockdown unless down
           already (0x4eccb0); over 4.84 (0x52b8c8) a heavy stun unless in a reaction, and its stagger */
        float k = c->knockback[0] * c->knockback[0] + c->knockback[1] * c->knockback[1] + c->knockback[2] * c->knockback[2];
        if (k > 16.81f) {
            if (!chr_is_down_type(c->type)) chr_knockdown(c, face_along(c, dir) == 1 ? 0x58 : 0x4f);
        } else if (k > 4.84f) {
            if (!chr_is_victim_type(c->type)) chr_knockdown(c, face_along(c, dir) == 1 ? 0xd5 : 0xc6);
            c->countdown_1c9a = 22;
        }
        break;
    }
    case 4:
        chr_knockdown(c, face_along(c, dir) == 1 ? 0x58 : 0x4f);
        break;
    case 5:
        chr_knockdown(c, face_along(c, dir) == 1 ? 0xd7 : 0xd6);
        break;
    case 6: {
        /* the tractor beam: the damage gathers (A+0x124) to 100 for a hold to start; held, each hit
           lengthens it by the stun, to 60 at most, and the shooter, a character still there, pulls */
        if (!c->hold_ticks) {
            c->hold_damage += dmg;
            if (c->hold_damage < 100) break;
        }
        Character *by = owner_character(owner);
        if (!by) break;
        uint32_t h = c->hold_ticks + (st & 0xffff);
        c->hold_ticks = (uint16_t)((h & 0xffff) > 60 ? 60 : h);
        c->held_by = by;
        break;
    }
    default:
        break;
    }
    return false;
}

/* WPrBlast 0x517f40: the blast's damage to each living character but the one the particle hit (and,
   when the class says so, its owner) whose pelvis is within the radius with no wall between (0x242000):
   at the distance's share d of the radius, the falloff 2 gives 1/(1+d)^2, 1 gives 1-d, else all of it. */
static void blast_damage(const void *env, const float pos[3], int type, float damage, float stun, float knockback,
                         float radius, int falloff, uint32_t owner, Character *exclude, uint32_t handle,
                         bool self_immune)
{
    if (owner == 0) owner = 0x4000000u;
    static AkevHit hit[AKEV_HITS_MAX];
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch || !chars_in_use(ch)) continue;
        Character *c = &ch->c;
        if ((c->flags & 1) || c == exclude) continue;
        if (self_immune && ((owner & 0xff000000u) == 0x1000000u || (owner & 0xff000000u) == 0x2000000u) &&
            (owner & 0xffffff) == c->id)
            continue;
        float d[3];
        if (!chr_part_point(c, 0, d)) continue;
        d[0] -= pos[0], d[1] -= pos[1], d[2] -= pos[2];
        float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), t = l / radius;
        if (!(t <= 1.0f) || (env && akev_collide_point(env, pos, d, 0x242000, 0, hit))) continue;
        float f = falloff == 2 ? 1.0f / ((1.0f + t) * (1.0f + t)) : falloff == 1 ? 1.0f - t : 1.0f;
        if (l > 0.0f) d[0] /= l, d[1] /= l, d[2] /= l;
        inflict_damage(c, pos, radius, d, type, (uint32_t)lrintf(f * damage), (uint32_t)lrintf(f * stun), knockback * f,
                       handle, owner);
    }
}

void weapons_install(void)
{
    p3_damage_hook = particle_damage;
    p3_inaccuracy_hook = particle_inaccuracy;
    p3_blast_hook = blast_damage;
    character_pickup_hook = start_pickup;
    character_weapon_input_hook = weapon_input;
    character_frame_hook = frame;
    chr_overlay_hook = overlay_action;
    chr_drop_hook = chr_throw_weapon;
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

/* weapon_spawn class flag (AI2iScript_WeaponSpawn 0x4b8550) */
static bool cmd_spawn(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[0].type != SL_STRING) return true;
    const void *cls = tm_instance_get(TM_TAG("ONWC"), v[0].s);
    ObjcObject flag;
    if (!cls || !objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;   /* "unknown weapon", "could not find flag" */
    weapon_new(cls, flag.pos, 0.0f);
    return true;
}

/* chr_inv_reset char (AI2iScript_Reset_Inventory 0x4ba280): the ammo, cells, hypos and keys gone, the
   weapon in the hand deleted (ONrCharacter_UseNewWeapon 0x4d7bb0: then ONrCharacter_SetWeapon), then
   every slot's (WPrDelete 0x518f90), the stowed ones too. */
static Chr *char_arg(const SlValue *v)
{
    if (v->type == SL_STRING) {
        char *end;
        long id = strtol(v->s, &end, 10);
        if (*v->s && !*end) v = &(SlValue){ .type = SL_INT, .i = (int32_t)id };
        else return chars_find(v->s);
    }
    int32_t id = num(v);
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (ch && ch->script_id == id) return ch;
    }
    return NULL;
}

static bool cmd_inv_reset(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? char_arg(&v[0]) : NULL;
    if (!ch) return true;
    /* the inventory (0x4ba280: C+0x1a2..0x1ba, the hypo's healing left, +0x1ae, too) */
    ch->c.ammo = ch->c.hypos = ch->c.cells = 0;
    ch->c.hypo_heal = 0;
    ch->c.lsi = false;
    ch->c.shield = ch->c.invisibility = 0;
    if (ch->c.weapon) {
        Weapon *w = ch->c.weapon;
        weapon_delete(w);
        chr_set_weapon(&ch->c, NULL);
    }
    for (int k = 0; k < 2; k++) {
        if (ch->c.stowed[k]) weapon_delete(ch->c.stowed[k]);
        ch->c.stowed[k] = NULL;
    }
    return true;
}

/* chr_has_empty_weapon char (AI2iScript_HasEmptyWeapon 0x4b8cb0): a bool. */
static bool cmd_has_empty(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)u;
    Chr *ch = c >= 1 ? char_arg(&v[0]) : NULL;
    r->type = SL_BOOL;
    r->i = ch && weapon_has_empty(&ch->c);     /* none: "chr_has_empty_weapon: no such character" */
    return true;
}

/* WPrFindBarabbasWeapon 0x51b260: the first weapon in use of a class with nothing to reload it
   (ONWC +0xcc 0x800). */
static Weapon *barabbas_weapon(void)
{
    for (int i = 0; i < WEAPONS_MAX; i++)
        if ((weapons[i].flags & WEAPON_IN_USE) && (options(&weapons[i]) & ONWC_NO_AMMO)) return &weapons[i];
    return NULL;
}

/* ai2_barabbas_retrievegun char (AI2iScript_BarabbasRetrieveGun 0x4b50d0): an AI (C+0x1e8 2) with
   nothing in its hand takes that gun into the hand at once, wherever it lies, unless someone holds it
   or it is above 700 (0x52afd4). */
static bool cmd_barabbas_retrievegun(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? char_arg(&v[0]) : NULL;
    if (!ch || !ch->c.ai) return true;      /* "ai2_barabbas_retrievegun": no such AI */
    if (ch->c.weapon) return true;
    Weapon *w = barabbas_weapon();
    if (!w) return true;                    /* "ai2_barabbas_retrievegun: can't find..." */
    if (w->owner || w->m[10] > 700.0f) return true;
    pickup_weapon(&ch->c, w);               /* ONrCharacter_PickupWeapon */
    return true;
}

/* chr_has_lsi char (AI2iScript_HasLSI 0x4b8c10): a bool, whether the character carries the LSI
   (C+0x1b4). */
static bool cmd_has_lsi(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)u;
    Chr *ch = c >= 1 ? char_arg(&v[0]) : NULL;
    r->type = SL_BOOL;
    r->i = ch && ch->c.lsi;     /* none: "chr_has_lsi: no such character" */
    return true;
}

/* 0x519b30: the ammo set to a percentage of the class's shots (at most all of them); owned, the
   background events stop at none and start at some. */
void weapon_set_ammo_percent(Weapon *w, unsigned percent)
{
    unsigned full = rd16(w->cls + ONWC_SHOTS), n = (percent & 0xffff) * full / 100;
    w->ammo = (uint16_t)(n <= full ? n : full);
    if (!w->owner || !(w->flags & WEAPON_OWNED)) return;
    if (w->ammo == 0) {
        if (w->flags & WEAPON_BACKGROUND) {
            w->flags &= (uint16_t)~WEAPON_BACKGROUND;
            every_shooter(w, EV_BG_STOP);
        }
    } else if (!(w->flags & WEAPON_BACKGROUND)) {
        w->flags |= WEAPON_BACKGROUND;
        every_shooter(w, EV_BG_START);
    }
}

bool weapon_give_class(Character *c, const char *name, unsigned percent)
{
    const void *cls = name && *name ? tm_instance_get(TM_TAG("ONWC"), name) : NULL;
    if (!cls) return false;     /* "can't find weapon class" (but for "none" and the empty name) */
    Weapon *w = weapon_new(cls, c->pos, 0.0f);   /* 0x518690 */
    if (!w) return false;
    chr_set_weapon(c, w);
    weapon_set_ammo_percent(w, percent);
    return true;
}

/* chr_giveweapon char class (AI2iScript_GiveWeapon 0x4b56c0): a new weapon of the class in the hand. */
static bool cmd_give(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? char_arg(&v[0]) : NULL;
    const void *cls = c >= 2 && v[1].type == SL_STRING ? tm_instance_get(TM_TAG("ONWC"), v[1].s) : NULL;
    if (!ch || !cls) return true;
    Weapon *w = weapon_new(cls, ch->c.pos, 0.0f);
    if (w) pickup_weapon(&ch->c, w);
    return true;
}

void weapons_register_commands(void)
{
    sl_register("weapon_spawn", cmd_spawn, NULL);
    sl_register("chr_inv_reset", cmd_inv_reset, NULL);
    sl_register("chr_giveweapon", cmd_give, NULL);
    sl_register("chr_has_empty_weapon", cmd_has_empty, NULL);
    sl_register("chr_has_lsi", cmd_has_lsi, NULL);
    sl_register("ai2_barabbas_retrievegun", cmd_barabbas_retrievegun, NULL);
    sl_register_bool("wp_disable_fade", &no_fade);
    sl_register_bool("wp_kickable", &kickable);
    physctx_add_source((PhysSource){ ctx_count, ctx_get });
}
