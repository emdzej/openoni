/* The screen's 2D layer over the 3D view: the letterbox, the message and subtitle lines, the fade
   (ONi_GameState_Display_All_UI_Elements 0x4fbaa0's order), from ui.h's state. */
#pragma once
#include <stdint.h>

void ui_draw(int w, int h);
/* A string with a font at (x, the baseline y), its pixels scaled; returns the pen's x after it. c: RGBA. */
float ui_draw_text(const void *font, float x, float y, float scale, const char *s, const uint8_t c[4]);

/* The HUD's health (ONrInGameUI_Display 0x4fff50: the "right" element, then ONiInGameUI_DisplayHealth
   0x500970): hit points of max, time in ticks for the low-health pulse. */
void ui_draw_hud(int w, int h, uint32_t hp, uint32_t max, uint32_t time);
