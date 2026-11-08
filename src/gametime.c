/* The game time: gametime.h. */
#include "gametime.h"

static uint32_t now;    /* GS+0x144 */

uint32_t game_time(void) { return now; }
void game_time_reset(void) { now = 0; }
void game_time_advance(void) { now++; }
