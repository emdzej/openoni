/* The game time (ONrGameState's +0x144, ONrGameState_GetGameTime 0x4fc880): the ticks the level has
   run. The game's loop (0x4fafb0) adds one after each tick's update (ONrGameState_Update 0x4fa6a0), so
   during the level's first tick it is 0. */
#pragma once
#include <stdint.h>

uint32_t game_time(void);
/* The level's start: 0. */
void game_time_reset(void);
/* The end of a tick. */
void game_time_advance(void);
