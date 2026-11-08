/* The level's characters: ONrGameState's character list (0x16a0 bytes each from GS+0x1260, a slot used
   while its flag 0x8000 is set), filled from the level's CHAR objects (objc.h) at the start and by
   ai2_spawn, and from the level's character setups (AISA) by chr_create; emptied by chr_delete; and the script commands that pick a character by its name or its
   script id (FUN_004bb880 and FUN_0048a5a0 through the living list).

   The AI is ai.c's; the chr_* and ai2_* commands are below. */
#pragma once
#include "ai.h"
#include "character.h"
#include "trigger.h"

#include <stdbool.h>
#include <stdint.h>

enum { CHARS_MAX = 64 };

typedef struct Chr {
    Character c;
    char name[32];          /* C+0x14 */
    int16_t script_id;      /* C+0x40: 0 the player, -1 the others from the level (AI2rInitializeCharacter 0x48a6b0) */
    uint32_t team;          /* C+0x12 */
    bool player;
    int object;             /* the CHAR object it came from (an index of chars_load's list) */
    uint32_t script_flags;  /* C+0x44: CHR_SCRIPTED_* */
    uint16_t corpse_time;   /* C+0x1644: ticks until a dead AI character is deleted, 0 none */
    AiState ai;             /* for the AI's characters */
} Chr;

enum { CHR_SCRIPTED_COMBAT = 0x02, CHR_SCRIPTED_DIED = 0x04, CHR_SCRIPTED_ALARM = 0x08, CHR_SCRIPTED_HURT = 0x10,
       CHR_SCRIPTED_DEFEATED = 0x20, CHR_SCRIPTED_OUT_OF_AMMO = 0x40, CHR_SCRIPTED_NO_PATH = 0x80 };   /* C+0x44: the scripts run */

/* Reads the level's CHAR objects and creates the ones there at the start (without flag 0x04): the
   player among them. Returns how many characters there are. */
int chars_load(void);
/* AI2rUpdateAI 0x489d40's first part for an AI character: the alarm, combat, out-of-ammo and no-path
   scripts due (C+0x1f0 0x10, 8, 0x40, 0x80), each deferred once with its name (C+0x44). */
void chars_ai_scripts(Chr *ch);
void chars_unload(void);
/* The characters' weapons (the CHAR objects'), once the weapons are set up; the ones spawned later
   get theirs at once. */
void chars_arm(void);
/* The slots: chars_get is NULL for an empty one. The player is chars_player(). */
int chars_slots(void);
Chr *chars_get(int i);
Chr *chars_player(void);
/* ONrGameState_SetPlayerNum 0x4fc8f0: the slot taken as the player from now on (0xffff none). */
void chars_set_focus(uint16_t index);
Chr *chars_find(const char *name);
/* Whether c is a living slot (C+0x04 flag 0x8000): false once deleted. */
bool chars_in_use(const Chr *c);
/* ai2_spawn's core (AI2rSpawnCharacter 0x48a450): the CHAR object of that name, unless already spawned
   (its flag 0x20) and not respawnable (0x10), or forced. */
Chr *chars_spawn(const char *name, bool force);
void chars_delete(Chr *c);

/* What the trigger volumes see, one per slot (an empty one not alive), brought up to date: the volumes
   the character is in are kept in it, as in the character (+0x168). */
TriggerActor *chars_trigger_actors(void);

/* One tick of every character (the loop of ONrGameState_UpdateCharacters 0x4dccf0 after the volumes):
   the player with in, the others through the AI. env: the level's AKEV. */
void chars_update(const CharacterInput *player_in, const void *env);

/* ai2_spawn, ai2_movetoflag, chr_delete, chr_teleport, chr_animate, chr_animate_block,
   chr_wait_animtype. */
void chars_register_commands(void);
