/* The continue (ONrGameState_MakeContinue 0x4fd690, ONrGameState_UseContinue 0x4fd8b0): what a save point
   keeps of the player (health, place, facing, inventory, the weapon in the hand) for the level's scripts
   to put back when the level is played again from it. The scripts' save_point (0x5ece88) is the
   continue's: save_game sets it, a won level clears it (0x4fda80), a lost one is loaded again with it, and
   the level's main script picks the part to start from by it and calls restore_game.

   Not ported yet: the continue kept on disk for the menu's "load game" (ONrPersist_SetContinue), the
   autosave's sound (0x63117c), and the two holstered weapon slots (C+0x198, +0x19c) the port has no
   place for. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The won level's clear (0x4fda80): no save point. */
void save_clear(void);
int32_t save_point(void);
void save_register_commands(void);
