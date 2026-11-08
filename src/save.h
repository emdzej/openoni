/* The continue (ONrGameState_MakeContinue 0x4fd690, ONrGameState_UseContinue 0x4fd8b0): what a save point
   keeps of the player (health, place, facing, inventory, the weapon in the hand) for the level's scripts
   to put back when the level is played again from it. The scripts' save_point (0x5ece88) is the
   continue's: save_game sets it, a won level clears it (0x4fda80), a lost one is loaded again with it, and
   the level's main script picks the part to start from by it and calls restore_game.

   Each one is kept in persist.dat for the menu's Load Game (ONrPersist_SetContinue). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The won level's clear (0x4fda80): no save point. */
void save_clear(void);
int32_t save_point(void);
/* The scripts' save_point set (a debug aid's: --param save=N). */
void save_set_point(int32_t n);
void save_register_commands(void);
/* ONrGameState_LevelBegin 0x4f5350's part: the level, the place recorded, the level reached. */
void save_level_begin(int level);
/* OWrGame_Start 0x4beb40's part: the persisted continue of the level's slot made the game's (0x4fdaa0),
   false if there is none. */
bool save_use_persisted(int level, int slot);
/* What restore_game tells the in-game UI (ONrInGameUI_NotifyRestoreGame 0x5050b0), restored or not.
   Set by the app. */
extern void (*save_restore_hook)(void);
