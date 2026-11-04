/* The game's screen state the scripts drive, apart from the HUD: the fade (ONrGameState +0x54..+0x7c),
   the letterbox (ONrGameState_LetterBox_Display 0x4fceb0), input on or off (+0x1679ad), and the
   console's text areas for messages and subtitles (COrMessage_Print 0x4304b0, COrConsole_PrintIdentified
   0x430480, the line add 0x431340). Drawn by src/render/ui_draw.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { UI_LINE_TEXT = 0x1f6, UI_AREA_LINES = 8 };
enum { UI_AREA_MESSAGE, UI_AREA_SUBTITLE, UI_AREAS };

typedef struct {
    char text[UI_LINE_TEXT];
    char id[32];            /* the message's name (what message_remove takes) */
    int32_t timer;          /* ticks left; 0 stays until removed */
    uint32_t colour;        /* ARGB */
} UiLine;

void ui_reset(void);
/* One tick: the game time moves on, the lines' timers count down. */
void ui_update(void);
uint32_t ui_time(void);

/* The fade's colour now (r, g, b, a in 0..1); false when there is none. */
bool ui_fade(float rgba[4]);
/* The letterbox bars' height now, in 640x480 pixels (0 to 75). */
float ui_letterbox(void);
bool ui_input_enabled(void);
/* G+0x1679ad, the player's input (the input command, begin_cutscene and end_cutscene). */
void ui_set_input(bool on);
/* G+0x30: the letterbox going on (0x4fce70) or off (0x4fce90). */
void ui_set_letterbox(bool on);
/* G+0x3c: a cutscene is on (begin_cutscene to end_cutscene); G+0x48: its sync (cutscene_sync). */
bool ui_cutscene(void);
void ui_set_cutscene(bool on, bool sync);

int ui_area_count(int area);
/* Line i of the area, oldest first; alpha in 0..1 (lines fade out over their last 64 ticks). */
const UiLine *ui_area_line(int area, int i, float *alpha);
/* When the message (by its name) was last asked for by a script, 0 not lately (the port's). */
uint32_t ui_message_time(const char *id);
/* Adds a line (the oldest goes when the area is full). */
void ui_print(int area, const char *text, const char *id, int32_t timer, uint32_t colour);

/* fade_in, fade_out, letterbox, input, message, message_remove. */
void ui_register_commands(void);
