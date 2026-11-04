/* The player's persistent data (ONrPersist, the block at 0x5ed340 that is persist.dat): the difficulty,
   0 easy, 1 normal, 2 hard (+0x48, 1 at start-up), the weapons and items the player has had (+0x2c,
   +0x30: bits by the weapon's class index, ONWC+0x136, and the powerup type) and the furthest diary
   page read (+0x34 level, +0x38 page). Saving them to persist.dat is not yet ported. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ONrPersist_GetDifficulty 0x50f240: clamped to 0..2. */
int persist_difficulty(void);
/* ONrPersist_SetDifficulty 0x50f260: clamped to 0..2 (and saved, not yet ported). */
void persist_set_difficulty(int d);

/* 0x50f4e0, 0x50f500: a weapon class (its index, ONWC+0x136) had, and its recording. */
bool persist_weapon_seen(uint16_t index);
void persist_set_weapon_seen(uint16_t index);
/* 0x50f560, 0x50f580: a powerup type had. */
bool persist_item_seen(int type);
void persist_set_item_seen(int type);
/* 0x50f5d0: a diary page read, kept if further than the furthest. */
void persist_diary_read(uint32_t level, uint32_t page);
