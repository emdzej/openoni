/* The cheat codes (ONrGameState_HandleCheats 0x4f6110, ONrCheater 0x4f5c30): typed on the pause
   screen once the game has been won, each switches one of the game's cheats. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The pause screen's key (an LI code; the original's input event hook, 0x55fd48, set by ONiRunGame
   0x4d4640 around ONrPauseScreen_Display 0x5018c0). */
void cheat_key(uint8_t code);
/* ONrCheater 0x4f5c30: the cheat of the id switched; whether it is now on (the message's choice). */
bool cheat_apply(int id);
/* The message the last code gave (0x5ed00c), drawn in the pause screen's item 0x6f; NULL for none.
   Cleared as the pause screen closes and at the level's load. */
const char *cheat_message(void);
void cheat_message_clear(void);

/* The switches the cheats keep that the rest of the game reads (the others are chr_hit.h's, the big
   head's in character.h and the glass furniture's in env.c). */
extern bool cheat_change_characters;    /* 0x5ece6f, shapeshifter: the player's class changed by a key (not ported) */
extern bool cheat_ultra_mode;           /* 0x5eb849, killmequick: the melee AI's choice (not ported) */
extern bool cheat_slow_motion;          /* G+0x104, carousel: the game's clock at half speed */
