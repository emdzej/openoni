/* Powerups (WPrPowerup, ONI_Weapon.c): ammo clips, energy cells, hypos, shields, invisibility and the
   LSI lying in the level, 64 at most (0x74 bytes each at 0x620460); picked up with the pickup key into
   the character's inventory (C+0x1a2..0x1ba). Their geometry and glow come from the game settings
   (ONGS). Not ported yet: their physics (a spawned powerup is placed, as powerup_spawn makes them),
   the glow sprite, the HUD's pickup display and the event sounds. */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

enum { POWERUP_AMMO, POWERUP_CELL, POWERUP_HYPO, POWERUP_SHIELD, POWERUP_INVISIBILITY, POWERUP_LSI, POWERUP_TYPES };
enum { POWERUPS_MAX = 64 };

typedef struct Powerup {
    uint16_t flags;         /* +0x00: 8 placed (not recycled) */
    uint16_t amount;        /* +0x02 */
    int type;               /* +0x04 */
    float pos[3];           /* +0x08 */
    const void *glow;       /* +0x14: TXMP */
    float glow_size[2];     /* +0x18 */
    const void *geometry;   /* +0x20: M3GM */
    float facing;           /* +0x2c */
    uint32_t time;          /* +0x30: when made */
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
void powerup_drop(const Character *c, int type, uint16_t amount, bool ahead);
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
/* The script commands: powerup_spawn (AI2iScript_PowerupSpawn 0x4b85e0). */
void powerup_register_commands(void);
