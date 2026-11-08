/* The characters' activity. A character without its active record (C+0x02 0, the 64 records at
   GS+0xb6268) has no animation, physics nor draw: an AI goes inactive once out of the camera's sight
   for 90 ticks (ONrGameState_UpdateCharacters 0x4dccf0), its movement stub moving it along its path
   meanwhile (AI2rMovementStub, movement.h), and is made active again when seen (the characters' display
   0x4e1540) or when something needs its record (ONrForceActiveCharacter 0x4f11b0: a hit, an animation,
   a fight). An AI starts inactive (ONrGameState_NewCharacter 0x4dac50 makes the player's record only).
   Notes: docs/re/character.md "The activity". */
#pragma once
#include "chars.h"

#include <stdbool.h>

/* chr_all_active (0x5ecbac): every character kept active; chr_disable_visactive (0x5ecbad): sight
   doesn't make them active. */
extern bool chr_all_active, chr_disable_visactive;

/* The hook's install (chr_activate_hook: ONrGameState_NewActiveCharacter 0x4db830). */
void activity_install(void);
/* The level's environment (AKEV), for the new records' floor: set before the level's characters. */
void activity_set_environment(const void *env);
/* 0x4db830 for a new character: the player's record (from no stub); an AI is left inactive, its stub
   off (AI2rMovementStub_Initialize 0x4ac0f0 without a record). */
void activity_new_character(Chr *ch);
/* ONrCharacter_MakeInactive 0x4f1120: an AI back to its job (AI2rReturnToJob 0x48af80, forced), its
   stub set from its local path and the record deleted (0x4dc580). */
void activity_make_inactive(Chr *ch);
/* The end of ONrGameState_UpdateCharacters 0x4dccf0: with chr_all_active every inactive one made active
   (at its start, in the original); each one's ticks counted, an AI unseen for more than 0x59 going
   inactive (not in a film, a fight's state, the air or a conversation; not locked active nor dead). */
void activity_update(void);
/* The display's part (0x4e1540): a character in the camera's view is made active. */
void activity_seen(Chr *ch);
