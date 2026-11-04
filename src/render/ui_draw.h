/* The screen's 2D layer over the 3D view: the letterbox, the message and subtitle lines, the fade
   (ONi_GameState_Display_All_UI_Elements 0x4fbaa0's order), from ui.h's state. */
#pragma once
#include <stdint.h>

void ui_draw(int w, int h);
/* A string with a font at (x, the baseline y), its pixels scaled; returns the pen's x after it. c: RGBA. */
float ui_draw_text(const void *font, float x, float y, float scale, const char *s, const uint8_t c[4]);

/* splash_screen_internal 0x4f5220: the big texture (TXMB) at its size, as at 640x480 scaled and centred
   (inferred: 0x41f6e0's placement isn't decoded), under a black blanket of that alpha. */
void ui_draw_splash(int w, int h, const char *name, float blanket);
/* The HUD (ONrInGameUI_Display 0x4fff50) of the player: the left element (the ammo, the cells, the
   compass), the right (the "right" element, then ONiInGameUI_DisplayHealth 0x500970); time in ticks for
   the low-health pulse. */
struct Character;
void ui_draw_hud(int w, int h, const struct Character *c, uint32_t time);
