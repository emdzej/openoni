/* The player's persistent data (ONrPersist): the block at 0x5ed340, 0x326a0 bytes, that is persist.dat,
   read at start-up (ONrPersistance_Initialize 0x50ee80: kept if its version, +0, is 15 and its mark,
   +4, 0xd0d00b0e, else made anew) and written whole after each change. Its fields: +0x08 the levels
   reached (bits), +0x2c and +0x30 the weapon classes and powerup types had, +0x34/+0x38 the furthest
   diary page, +0x3c the graphics quality, +0x40 the overall volume, +0x44 flags, +0x48 the difficulty,
   +0x4c the resolution, +0x50 the depth, +0x54 the gamma, +0x58 the place (level, continue), +0x60
   the continues: 0x204 bytes each, ten for each of 40 levels (a continue's +0x40 bit 1: there is
   one). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PERSIST_CONTINUE_SIZE = 0x204, PERSIST_LEVELS = 0x28, PERSIST_CONTINUES = 10 };

/* ONrPersistance_Initialize 0x50ee80: the file read and written through these (the platform's user
   files; NULL: kept in memory only). */
void persist_initialize(uint8_t *(*load)(const char *name, size_t *size), bool (*store)(const char *name, const void *data, size_t size));

/* ONrPersist_GetDifficulty 0x50f240, ONrPersist_SetDifficulty 0x50f260: clamped to 0..2. */
int persist_difficulty(void);
void persist_set_difficulty(int d);

/* 0x50f4e0, 0x50f500: a weapon class (its index, ONWC+0x136) had, and its recording. */
bool persist_weapon_seen(uint16_t index);
void persist_set_weapon_seen(uint16_t index);
/* 0x50f560, 0x50f580: a powerup type had. */
bool persist_item_seen(int type);
void persist_set_item_seen(int type);
/* 0x50f5d0: a diary page read, kept if further than the furthest. */
void persist_diary_read(uint32_t level, uint32_t page);

/* 0x50f030: a level reached (level 1 always; above 40 never); 0x50efd0: recorded (2 to 40). */
bool persist_level_reached(int level);
void persist_set_level_reached(int level);
/* ONrPersist_GetContinue 0x50f320: the continue of the level's slot (its 0x204 bytes, its name first),
   NULL if none; ONrPersist_SetContinue 0x50f360. */
const uint8_t *persist_continue(int level, int slot);
void persist_set_continue(int level, int slot, const uint8_t rec[PERSIST_CONTINUE_SIZE]);
/* 0x50f490, ONrPersist_SetPlace 0x50f4a0: the last place played (the level and its continue). */
void persist_place(int *level, int *slot);
void persist_set_place(int level, int slot);
