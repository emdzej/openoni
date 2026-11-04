/* Weapons (WP, ONI_Weapon.c): the 64 weapons of the level (0x170 bytes each at 0x6221c0), lying in the
   world or in a character's hand; picking them up, the trigger, the shooters and their pulses (ammo,
   the delays, the recoil overlay). Format of the class (ONWC): docs/formats/onwc.md.

   A weapon's shooters are particles (the class's 16 slots at +0x1d0 name a BINA 3RAP particle class):
   the events of WPiDoEvent 0x519f70 go to them and they emit the projectiles. The port has no particle
   system yet: a shooter's particle is a stand-in that exists when its class does, takes the events and
   emits nothing. Not ported either: the physics of a weapon in the world (the port rests it on the
   floor below), the AI's use, the noise of a shot (AI2rKnowledge_Sound). */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

enum { WEAPONS_MAX = 64, WEAPON_SHOOTERS = 16 };

enum {
    WEAPON_IN_USE = 0x0001,
    WEAPON_FIRING = 0x0010,
    WEAPON_BACKGROUND = 0x0020,     /* the shooters' background events started: there is ammo */
    WEAPON_NEARBY = 0x0040,         /* WPrLocateNearbyWeapons */
    WEAPON_STOP_WANTED = 0x0200,
    WEAPON_DELAYED = 0x0400,        /* the shooters' delay events started (out of ammo) */
    WEAPON_OWNED = 0x0800,
    WEAPON_PARTICLES = 0x1000,
    WEAPON_DROPPED_STOP = 0x4000,   /* dropped: stop firing on the next update */
    WEAPON_DROPPED = 0x8000,
};

typedef struct Weapon {
    uint16_t flags;                 /* +0x02: WEAPON_* */
    const uint8_t *cls;             /* +0x04: the ONWC */
    Character *owner;               /* +0x08 */
    float m[12];                    /* +0x0c: 3x3 by columns, then the place (+0x30) */
    bool resting;                   /* +0x3c: its physics (the port's: on the floor) */
    float facing;                   /* +0x40 */
    uint16_t fade;                  /* +0x44: ticks until it goes, once fading */
    uint16_t fade_delay;            /* +0x46: ticks until it fades */
    uint32_t time;                  /* +0x48: when it was made */
    uint16_t delay, delay2;         /* +0x4c (no trigger until 0), +0x4e */
    uint16_t pause;                 /* +0x50: class +0xc6 at each trigger */
    uint16_t reload_wait;           /* +0x52 */
    uint16_t fire_min;              /* +0x54: ticks a shot keeps firing at least */
    uint16_t ammo;                  /* +0x56 */
    uint16_t active;                /* +0x58: the shooters started */
    uint16_t shooter_delay[WEAPON_SHOOTERS];    /* +0x5a: ticks until a shooter starts */
    uint32_t shooter_handle[WEAPON_SHOOTERS];   /* +0xbc: the shooters' particles' handles (+0x7c their pointers) */
    uint32_t sound_wait;            /* +0xfc */
} Weapon;

void weapons_reset(void);
/* WPrTryReload 0x51c560: the reload overlay starts if the weapon isn't full, c has a clip (or cell) for
   it and nothing stops it (an attack, a hit, an overlay, the weapon's pause or delay): one is used up,
   and the overlay's action frame fills the weapon (WPrReloadDone 0x5181e0). *full: already full;
   *none: nothing to reload with. True if it started. */
bool weapon_try_reload(Character *c, Weapon *w, bool *full, bool *none);
/* chr_has_empty_weapon's test (AI2iScript_HasEmptyWeapon 0x4b8cb0): a weapon out of ammo, nothing to
   reload it with, and no reload under way. */
bool weapon_has_empty(Character *c);
/* WPiInit 0x518c00: a new weapon of the class at pos (raised 2), full of ammo; NULL if none can be had. */
Weapon *weapon_new(const void *onwc, const float pos[3], float facing);
/* WPrDelete 0x518f90. */
void weapon_delete(Weapon *w);
int weapon_slots(void);
Weapon *weapon_get(int i);          /* NULL for an unused slot */
/* The geometry to draw (class +0x58) and its matrix now. */
const void *weapon_geometry(const Weapon *w);

/* WPiUpdate 0x5192b0, every weapon once a tick (after the characters). env: the floor's. */
void weapons_update(const void *env);

/* The character side (ONrCharacter_*): installs the hooks of character.c and chr_anim.c. */
void weapons_install(void);
/* ONrCharacter_SetWeapon 0x4e4c00: the weapon in the character's hand (NULL none). */
void chr_set_weapon(Character *c, Weapon *w);
/* The slots: 0 the hand (C+0x194), 1 and 2 the stowed ones (C+0x198, 0x19c). */
Weapon *chr_slot(const Character *c, int i);
/* 0x4eff60: the hand's weapon holstered (forced: stowed even with slot 1 taken, else dropped); 0x4effa0:
   the stowed one drawn into the empty hand. Both by the overlay animations (Holster, Draw_*). */
void chr_force_holster(Character *c);
void chr_force_draw(Character *c);
/* ONrCharacter_PickupWeapon 0x4ed770 without a weapon: at once, the hand's stowed (forced: into an empty
   slot, dropped with none) or traded for the stowed one. */
void chr_stow(Character *c, bool force);
/* The player's first weapon of a class (ONrInGameUI_NewWeapon 0x505050, set by the app to
   igui_new_weapon). */
extern void (*weapon_new_hook)(const void *onwc);

/* 0x519b30: the ammo at a percentage of the class's shots. */
void weapon_set_ammo_percent(Weapon *w, unsigned percent);
/* ONrGameState_NewCharacter 0x4dac50's weapon: a new weapon of the class (an ONWC name) in c's hand,
   its ammo at percent (the CHAR object's). False without the class. */
bool weapon_give_class(Character *c, const char *name, unsigned percent);

/* weapon_spawn, chr_inv_reset, chr_giveweapon, wp_disable_fade. */
void weapons_register_commands(void);
