/* The game's screen state the scripts drive, apart from the HUD: the fade (ONrGameState +0x54..+0x7c),
   the letterbox (ONrGameState_LetterBox_Display 0x4fceb0), input on or off (+0x1679ad), and the
   console's text areas (COrConfigure 0x42fb70, COrTextArea_*): the console's (0x5cb468, COrConsole_PrintIdentified
   0x430480), the messages' (0x5cb398, COrMessage_Print 0x4304b0) and the subtitles' (0x5cb470, 0x4304e0),
   the line add 0x431340. Drawn by src/render/ui_draw.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { UI_LINE_TEXT = 0x1f6, UI_AREA_LINES = 32 };
enum { UI_AREA_MESSAGE, UI_AREA_SUBTITLE, UI_AREA_CONSOLE, UI_AREAS };

/* A line, 0x214 bytes in the original: +0 the type, +4 the timer, +0x12 the text, +0x208 the name,
   +0x20c the colour, +0x210 the shadow's. */
typedef struct {
    int32_t type;           /* 1 a subtitle in the letterbox, 2 the others; under 0x533f60 (1) not kept */
    char text[UI_LINE_TEXT];
    char id[32];            /* the message's name (what message_remove takes) */
    int32_t timer;          /* ticks left; 0 stays until removed */
    uint32_t colour, shadow;    /* ARGB: 0x533f70 (white) and 0x533f74 (0xff3f3f3f) */
} UiLine;

/* A text area (COrTextArea_New 0x430f00), in 640 by 480 pixels: the rectangle, its font's size (scaled
   by the display's height over 480: +0xc), the text's format (+8: 1 at the pen, 2 centred), the lines
   from the bottom up (+0xd), an overflowing line drawn fading rather than the drawing stopped (+0xf),
   the most lines. */
typedef struct {
    int16_t rect[4];
    uint16_t size, format;
    bool bottom_up, fade;
    int lines;
} UiArea;
const UiArea *ui_area(int area);
/* COrTextArea_Display 0x430fd0 for the areas shown (the subtitles' or the messages', then the
   console's) on a display w wide (and 480 high): each run of glyphs of the font at the pen (x, y on the
   baseline), n bytes of s, in the colour (ARGB, the line's alpha in it). A line overflowing the
   console's area is left to fade, as the original's display does. */
typedef void (*UiGlyphs)(const void *font, int16_t x, int16_t y, const char *s, uint16_t n, uint32_t argb, void *u);
void ui_area_display(int16_t w, UiGlyphs draw, void *u);
/* The text areas not drawn and not counting down (0x4303c0, 0x4303d0: the pause screen's, and at the
   level's load). */
void ui_hide_areas(bool hide);
bool ui_areas_hidden(void);

void ui_reset(void);
/* One tick: the lines' timers count down. */
void ui_update(void);
/* The game time (gametime.h). */
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
/* Line i of the area, oldest first; alpha in 0..1 (4 * the ticks left out of 255, over the last 63). */
const UiLine *ui_area_line(int area, int i, float *alpha);
/* How the level ended (G+0x1679bc, the win and lose commands): 0 not yet, 1 won, 2 lost. */
int ui_level_end(void);
/* The level ended, won (1) or lost (2), as win and lose do (the cheats' "winlevel", "loselevel"). */
void ui_set_level_end(int end);
/* When the message (by its name) was last asked for by a script, 0 not lately (the port's). */
uint32_t ui_message_time(const char *id);
/* Adds a line of the type, white on its shadow (the oldest goes when the area is full). */
void ui_print(int area, int32_t type, const char *text, const char *id, int32_t timer);
/* A subtitle (the dialog's, OSiDialog_Play 0x5125c0 and 0x514290): in the letterbox into the console's
   area as type 1 (which the letterbox's end and end_cutscene clear, 0x431d50), else into the subtitles'. */
void ui_print_subtitle(const char *text, int32_t timer);
/* 0x431d50: the console's lines of type 1 gone. */
void ui_console_clear_subtitles(void);
/* COrMessage_Remove 0x430640: the lines of that name (NULL: all) fade out; failing that, the console's
   (0x430510). */
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
/* ONrGameState_Update 0x4fa6a0's condition sounds for the player, a tick (follow: the follow camera's on). */
struct Character;
void ui_condition_sounds(const struct Character *player, bool follow);
