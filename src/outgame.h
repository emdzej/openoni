/* The out-of-game UI (ONrOutGameUI): the main menu (dialog 150, ONrOutGameUI_MainMenu_Display 0x4d2c70)
   before a game (OWrOniWindow_Startup 0x4bebd0) and over a level when the player asks for it (0x4bec00):
   New Game (its dialog 154, Start), Load Game (151), Options (152), Resume (in a level, in Options'
   place), Quit (153, yes or no). docs/re/wm.md.

   Not ported yet: Load Game and Options (their list box, sliders, popup menus and check boxes), the menu's
   music. */
#pragma once
#include <stdbool.h>

enum { OUTGAME_RESUME, OUTGAME_NEW_GAME, OUTGAME_QUIT };

/* The menu run to its end (in a level: Resume shown, Escape resumes); what the player chose. */
int outgame_main_menu(bool in_level);
