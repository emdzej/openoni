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
/* A movie's frame (the texture, mw × mh pixels) centred on black, as on a 640x480 screen scaled (the
   BinkBuffer's offset, 0x488080; the GL player's scale 1, 0x488ff0). */
void ui_draw_movie(int w, int h, unsigned tex, int mw, int mh);
/* The HUD (ONrInGameUI_Display 0x4fff50) of the player: the left element (the ammo, the cells, the
   compass), the right (the "right" element, then ONiInGameUI_DisplayHealth 0x500970); time in ticks for
   the low-health pulse. */
struct Character;
void ui_draw_hud(int w, int h, const struct Character *c, uint32_t time);
/* The WM's drawing (wm.h's operations) for a frame of w by h: its desktop as at 480 tall, scaled. */
void ui_draw_wm_begin(int w, int h, int *desk_w, int *desk_h);
