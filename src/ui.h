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
/* The slow motion timer (G+0x108, the slowmo command) running: the game's clock at half speed
   (ONrGameState_UpdateServerTime 0x4fbeb0). */
bool ui_slow_motion(void);
/* G+0x1679ad, the player's input (the input command, begin_cutscene and end_cutscene). */
void ui_set_input(bool on);
/* The keys the scripts let through (lock_keys; all at the level's start), as the action bits
   (CHR_KEY_*): ONiRunGame 0x4d4640 masks each input event's held bits with them before the frame. */
uint64_t ui_keys_allowed(void);
/* G+0x30: the letterbox going on (0x4fce70) or off (0x4fce90). */
void ui_set_letterbox(bool on);
/* G+0x3c: a cutscene is on (begin_cutscene to end_cutscene); G+0x48: its sync (cutscene_sync). */
bool ui_cutscene(void);
void ui_set_cutscene(bool on, bool sync);

int ui_area_count(int area);
/* Line i of the area, oldest first; alpha in 0..1 (lines fade out over their last 64 ticks). */
const UiLine *ui_area_line(int area, int i, float *alpha);
/* How the level ended (G+0x1679bc, the win and lose commands): 0 not yet, 1 won, 2 lost. */
int ui_level_end(void);
/* The level ended, won (1) or lost (2), as win and lose do (the cheats' "winlevel", "loselevel"). */
void ui_set_level_end(int end);
/* When the message (by its name) was last asked for by a script, 0 not lately (the port's). */
uint32_t ui_message_time(const char *id);
/* Adds a line (the oldest goes when the area is full). */
void ui_print(int area, const char *text, const char *id, int32_t timer, uint32_t colour);
/* COrMessage_Remove 0x430640: the lines of that name (NULL: all) fade out; failing that, the subtitles'. */
void ui_message_remove(const char *id);
bool ui_message_shown(const char *id);
bool ui_message_print(const char *id, int32_t timer);

/* The HUD's elements: the left (0) or right (1) shown; an element's alpha (its ui_flash_element bit:
   1 health, 0x80 compass, ...) while flashing, else 255; the compass's target (its place, the radius
   squared within which the arc is whole, the distance squared when it was set), false none. */
bool ui_element_shown(int right);
uint8_t ui_flash_alpha(uint32_t bit);
bool ui_flashing(uint32_t bit);
/* The flash's alpha now (0x5ecfe8), 255 while nothing flashes. */
uint8_t ui_flash_value(void);
/* A new objective not read yet (0x5ed098, set by igui.c): the flash's clock runs, the compass flashes
   (whole without a target). */
void ui_set_objective_unread(bool on);
bool ui_objective_unread(void);
/* An element's ui_fill_element bit (the same bits): drawn full. */
bool ui_filled(uint32_t bit);
bool ui_target(float pos[3], float *r2, float *d0);
/* The splash screen a script asked for (G+0x118), taken (cleared); false none. */
bool ui_splash_take(char name[32]);
/* The timer's text now (timer_start), false when off or blinked out. */
bool ui_timer_text(char out[32]);
/* 0x4fd360: the timer's state (GS+0) is on. */
bool ui_timer_running(void);
/* fade_in, fade_out, letterbox, input, message, message_remove, win, lose, timer_start, timer_stop. */
void ui_register_commands(void);
