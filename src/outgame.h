/* The out-of-game UI (ONrOutGameUI): the main menu (dialog 150, ONrOutGameUI_MainMenu_Display 0x4d2c70)
   before a game (OWrOniWindow_Startup 0x4bebd0) and over a level when the player asks for it (0x4bec00):
   New Game (its dialog 154, Start), Load Game (151), Options (152), Resume (in a level, in Options'
   place), Quit (153, yes or no). docs/re/wm.md.

   Load Game lists the levels reached and their continues (persist.dat). Not ported yet: Options (its
   sliders, popup menus and check boxes), the menu's music. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { OUTGAME_RESUME, OUTGAME_NEW_GAME, OUTGAME_QUIT, OUTGAME_LOAD };

/* The menu run to its end (in a level: Resume shown, Escape resumes); what the player chose. For Load,
   *start is OWrGame_Start's: the level in the low byte, the continue's slot in the high (0: the
   level's start). */
int outgame_main_menu(bool in_level, uint32_t *start);
