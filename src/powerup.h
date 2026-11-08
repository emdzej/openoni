/* Powerups (WPrPowerup, ONI_Weapon.c): ammo clips, energy cells, hypos, shields, invisibility and the
   LSI lying in the level, 64 at most (0x74 bytes each at 0x620460); picked up with the pickup key into
   the character's inventory (C+0x1a2..0x1ba). Their geometry and glow come from the game settings
   (ONGS). A dropped one falls through its physics context (src/physobj.c) until it rests; the
   player's pickups play the event sounds (7 to 10). Not ported yet: the HUD's pickup display. */
#pragma once
#include "character.h"
#include "physobj.h"

#include <stdbool.h>
#include <stdint.h>

enum { POWERUP_AMMO, POWERUP_CELL, POWERUP_HYPO, POWERUP_SHIELD, POWERUP_INVISIBILITY, POWERUP_LSI, POWERUP_TYPES };
enum { POWERUPS_MAX = 64 };

typedef struct Powerup {
    uint16_t flags;         /* +0x00: 1 moving (its physics), 4 on a floor this tick, 8 placed (not recycled) */
    uint16_t amount;        /* +0x02 */
    int type;               /* +0x04 */
    float pos[3];           /* +0x08 */
    const void *glow;       /* +0x14: TXMP */
    float glow_size[2];     /* +0x18 */
    const void *geometry;   /* +0x20: M3GM */
    uint16_t still;         /* +0x28: ticks resting or slow */
    float facing;           /* +0x2c */
    uint32_t time;          /* +0x30: when made */
    bool has_phys;          /* +0x34: its physics context (flags bit 1 with it) */
    PhysObject phys;
} Powerup;

/* The level's start and end (0x51b0f0, 0x51b150): none. */
void powerups_reset(void);
/* 0x51b0f0's second part (the callback 0x51b080): the level's PowerUp objects placed, each with its
   type's default amount, facing its y rotation. Its type is read from the object's tag (OBJiPowerUp_Read
   0x4c9f40: the exe's table of the types' tags). */
void powerups_level_begin(void);
/* 0x51c160: a new powerup of the type (WPrPowerup_SetType 0x51cc10) at pos, facing, with the amount;
   with all taken, the oldest one that isn't placed goes. NULL for a bad type or amount 0. */
Powerup *powerup_new(int type, uint16_t amount, const float pos[3], float facing);
/* 0x51c270: placed: the geometry's bottom at pos. */
Powerup *powerup_place(int type, uint16_t amount, const float pos[3], float facing);
/* 0x51c4f0: gone (the last one takes its slot). */
void powerup_delete(Powerup *p);
/* WPrPowerup_Drop 0x51bd80: the amount of the type dropped from c's pelvis, falling. */
void powerup_drop(const Character *c, int type, uint16_t amount, bool ahead);
/* 0x5190f0, after the weapons' update: the moving ones' places from their physics; one resting or slow
   for 120 ticks loses it. Then their physics (OBiPhysics_Update 0x43af50). */
void powerups_update(void);
void powerups_physics(const void *env);
/* WPrPowerup_DefaultAmount 0x51d000: 1, a shield 100, invisibility 1800 (ticks). */
uint16_t powerup_default_amount(int type);
/* 0x51b3e0: the nearest powerup c can take something of, between the heights, nearer than sqrt(r2)
   across (only `only` if given). *full: one was there that c can't take more of. */
Powerup *powerup_find(const Character *c, float y_lo, float y_hi, const Powerup *only, float r2, bool *full);
/* 0x51ca30: up to amount of the type into c's inventory (clips, cells and hypos up to 6; the shield
   and invisibility up to amount; the LSI for the player only); force takes it all. Returns what went. */
uint16_t powerup_give(Character *c, int type, uint16_t amount, bool force, bool sound);
/* A powerup type the player hadn't had (or the LSI): the pause screen's new item (0x5050a0, set by the
   app to igui_new_item). */
extern void (*powerup_new_hook)(int type);
/* The powerup's type name ("ammo", "cell", ...: the exe's table at 0x5588f8), NULL for none. */
const char *powerup_type_name(int type);
int powerup_count(void);
Powerup *powerup_get(int i);
/* Its matrix (3x3 by columns, then the place): turned by its facing (0x51c2b0). */
void powerup_matrix(const Powerup *p, float m[12]);
/* Its glow's place (0x51c2b0): the geometry's bottom, 0.1 up; false with no glow. The glow lies along
   the matrix's x and z, glow_size out each way, at alpha 0x55. */
bool powerup_glow(const Powerup *p, float pos[3]);
/* The script commands: powerup_spawn (AI2iScript_PowerupSpawn 0x4b85e0). */
void powerup_register_commands(void);
