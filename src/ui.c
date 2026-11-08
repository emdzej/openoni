/* Screen state: see ui.h. */
#include "ui.h"

#include "sl.h"
#include "text.h"
#include "chars.h"
#include "exe.h"
#include "objc.h"
#include "sound.h"
#include "tm.h"
#include "font.h"

#include <math.h>
#include <stdarg.h>

#include "gametime.h"

#include <stdio.h>
#include <string.h>

#define now game_time()     /* GS+0x144 */
static int32_t slowmo;             /* G+0x108: the slow motion timer's ticks */

/* The fade: from and to (a, r, g, b), between two times. */
static struct { float from[4], to[4]; uint32_t start, end; bool on; } fade;
/* The letterbox: the last update's time, the bars' height, going on or off, moving. */
static struct { uint32_t time; float height; bool on, moving; } box;
static bool input_on = true;
static uint64_t keys_allowed = ~(uint64_t)0;  /* G+0x1679b0 (low) and G+0x1679b4 (high) */
static int level_end;            /* G+0x1679bc: 1 won, 2 lost */
static bool cutscene, cutscene_sync;
/* The HUD (ONrInGameUI): the left and right elements shown (0x5ed0dc, 0x5ed0a0), the elements flashing
   (0x5ed02c, by ui_flash_element's bits) since when (0x5ecfb8), the elements drawn full (0x5ed09c, by
   ui_fill_element's), and the compass's target (target_set:
   0x5ed02a on, the flag's place 0x5ecfd0, the radius squared 0x5ecfc4, the distance squared when set
   0x5ed008). */
static bool show_left, show_right;
static uint32_t flash_bits, flash_since, fill_bits;
static bool objective_unread;     /* 0x5ed098, igui's */
static struct { bool on; float pos[3], r2, d0; } target;
static char splash[32];            /* G+0x118: the splash screen to show at the frame's end */
/* The timer (ONrGameState's, 0x5ece7c): 0 off, 1 counting down, 2 ended (its "0:00.0" shown for 300
   ticks); the function run at 0 (32 bytes), the ticks left, the blink and its counter. */
static struct { int state; char func[32]; uint32_t ticks; bool shown; uint32_t blink; } timer;
static struct { UiLine lines[UI_AREA_LINES]; int n; } areas[UI_AREAS];
static UiArea layout[UI_AREAS];
static int16_t layout_w;            /* the width the layout is for (0: none yet) */
static bool areas_hidden;           /* 0x5cb484 (the console's), 0x5cb485 (the messages' and subtitles') */

/* The messages lately asked for (a debug aid for the routes: ui_message_time). */
enum { SEEN_MAX = 32 };
static struct { char id[32]; uint32_t time; } seen[SEEN_MAX];
static int n_seen;

uint32_t ui_message_time(const char *id)
{
    for (int i = n_seen - 1; i >= 0; i--)
        if (!strcmp(seen[i % SEEN_MAX].id, id) && n_seen - i <= SEEN_MAX) return seen[i % SEEN_MAX].time;
    return 0;
}

/* The condition sounds (G+0x1679c0's bits, their ids at G+0x1679c4): the ambients named in the game
   settings at +0xa10, 32 bytes each (SSrImpulse_UpdateGroupPointers 0x4f49b0 into 0x631188): low health,
   over health, the shield (nothing starts it), invisibility and the timer's (blank in v1.0). */
enum { CONDITIONS = 5 };
static uint32_t cond_on;
static uint32_t cond_id[CONDITIONS];

static const void *cond_ambient(int i)
{
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!gs) return NULL;
    char name[33];
    memcpy(name, gs + 0xa10 + 32 * i, 32);
    name[32] = 0;
    return name[0] ? sound_ambient_find(name) : NULL;
}

/* ONrGameState_ConditionSound_Start 0x4fdae0: on, started at the volume (the ambient missing: the bit set, no id); already on, to the
   volume over a second. */
static void cond_start(int i, float volume)
{
    if (!(cond_on & 1u << i)) {
        cond_on |= 1u << i;
        cond_id[i] = sound_ambient_play(cond_ambient(i), volume);
    } else if (cond_id[i] != UINT32_MAX)
        sound_ambient_volume(cond_id[i], volume, 1.0f);
}

/* ONrGameState_ConditionSound_Stop 0x4fdb50: off, its ambient stopped. */
static void cond_stop(int i)
{
    if (!(cond_on & 1u << i)) return;
    cond_on &= ~(1u << i);
    if (cond_id[i] != UINT32_MAX) sound_ambient_end(cond_id[i]);
    cond_id[i] = UINT32_MAX;
}

/* ONrGameState_Update 0x4fa6a0's condition sounds, a tick: in the follow camera without the letterbox
   (G+0x80's mode 1, G+0x38), the player's low health (under a fifth: 1 - 0.6 f^2 of f = 5 hp / max), her
   health over the maximum (1 - 0.6 (1 - f)^2 of f, the excess over (the game settings' +0 - 1) times the
   maximum, held to 0..1) and her invisibility (1 - 0.6 f^2 of f = 1 - ticks / 1800); else every one on
   goes to nothing over a second, staying on. */
void ui_condition_sounds(const struct Character *player, bool follow)
{
    if (follow && !box.on) {
        if (!player) return;
        uint32_t hp = player->hit_points, max = player->max_hit_points;
        if (hp < max / 5 && hp != 0) {
            float f = (float)hp * 5.0f / (float)max;
            cond_start(0, 1.0f - f * f * 0.6f);
        } else
            cond_stop(0);
        if (hp > max) {
            const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
            float over = 0.0f;
            if (gs) memcpy(&over, gs, 4);
            float f = (float)(hp - max) / ((over - 1.0f) * (float)(int32_t)max);
            if (!(f >= 0.0f)) f = 0.0f;
            else if (f > 1.0f) f = 1.0f;
            cond_start(1, 1.0f - (1.0f - f) * (1.0f - f) * 0.6f);
        } else
            cond_stop(1);
        if (player->invisibility) {
            float f = 1.0f - (float)player->invisibility * (1.0f / 1800.0f);
            cond_start(3, 1.0f - f * f * 0.6f);
        } else
            cond_stop(3);
        return;
    }
    for (int i = 0; i < CONDITIONS; i++)
        if ((cond_on & 1u << i) && cond_id[i] != UINT32_MAX) sound_ambient_volume(cond_id[i], 0.0f, 1.0f);
}

void ui_reset(void)
{
    slowmo = 0;
    cond_on = 0;
    for (int i = 0; i < CONDITIONS; i++) cond_id[i] = UINT32_MAX;     /* ONrGameState_LevelBegin 0x4f54f1 */
    n_seen = 0;
    memset(&fade, 0, sizeof fade);
    memset(&box, 0, sizeof box);
    memset(areas, 0, sizeof areas);
    areas_hidden = false;           /* ONrLevel_Load 0x506647 */
    input_on = true;
    keys_allowed = ~(uint64_t)0;     /* ONrGameState_LevelBegin 0x4f5350 */
    level_end = 0;
    cutscene = cutscene_sync = false;
    memset(&timer, 0, sizeof timer);
    splash[0] = 0;
    show_left = show_right = true;      /* ONrInGameUI's level start */
    flash_bits = flash_since = fill_bits = 0;
    memset(&target, 0, sizeof target);
    target.d0 = 75000.0f;
}

bool ui_element_shown(int right) { return right ? show_right : show_left; }

/* ONrInGameUI_Display's flash (0x5ecfe8): 255 e^(-0.03 t), t the sixtieths since anything began
   flashing, modulo 60; 255 for an element not flashing. */
uint8_t ui_flash_alpha(uint32_t bit)
{
    uint8_t a = ui_flash_value();
    return (flash_bits & bit) ? a : 255;
}

uint8_t ui_flash_value(void)
{
    /* the clock held while nothing flashes and no new objective waits (0x4fff50's start) */
    if (!flash_bits && !objective_unread) flash_since = now;
    return (uint8_t)lrintf(expf(-0.03f * (float)((now - flash_since) % 60u)) * 255.0f);
}

bool ui_flashing(uint32_t bit) { return (flash_bits & bit) != 0; }

bool ui_filled(uint32_t bit) { return (fill_bits & bit) != 0; }

void ui_set_objective_unread(bool on) { objective_unread = on; }

bool ui_objective_unread(void) { return objective_unread; }

bool ui_target(float pos[3], float *r2, float *d0)
{
    if (!target.on) return false;
    memcpy(pos, target.pos, sizeof target.pos);
    *r2 = target.r2;
    *d0 = target.d0;
    return true;
}

uint32_t ui_time(void) { return now; }

/* A line out of its area (0x431760). */
static void line_remove(int a, int i)
{
    if (i + 1 < areas[a].n) memmove(&areas[a].lines[i], &areas[a].lines[i + 1], sizeof(UiLine) * (size_t)(areas[a].n - i - 1));
    areas[a].n--;
}

void ui_update(void)
{
    /* COrConsole_Update 0x4306f0's countdown (0x4316d0 for the messages and the subtitles): a line whose
       time is up goes, and the one after it, moved into its place, waits for the next tick */
    if (!areas_hidden)
        for (int a = 0; a < UI_AREAS; a++)
            for (int i = 0; i < areas[a].n; i++) {
                UiLine *l = &areas[a].lines[i];
                if (l->timer <= 0) continue;
                if (l->timer > 1) l->timer--;
                else line_remove(a, i);
            }
    /* the timer's tick (0x4fd3d0). Its sounds (the game settings' condition ambient 4 under 600 ticks
       left, the event impulse 16 every second, 17 when it is stopped) are blank in v1.0: none played. */
    if (timer.state == 1) {
        timer.blink++;
        if (--timer.ticks == 0) {
            timer.state = 2;
            timer.ticks = 300;
            /* SLrScript_ExecuteOnce: the function's thread, as a trigger's */
            if (timer.func[0]) sl_call(timer.func, 0, NULL);
        }
    } else if (timer.state == 2) {
        timer.blink++;
        if (--timer.ticks == 0) timer.state = 0;
    }
    if (slowmo > 0) slowmo--;       /* 0x4fa771, after the timer's tick */
}

bool ui_slow_motion(void) { return slowmo > 0; }

/* ONrGameState_Timer_Display 0x4fd500's text: blinking under 1200 ticks left (every 15 ticks, 8 under
   600, 4 under 300; and every 15 once ended), tenths under ten seconds. */
bool ui_timer_text(char out[32])
{
    if (timer.state == 0) return false;
    if (timer.state == 2 || timer.ticks < 1200) {
        uint32_t period = timer.state == 1 && timer.ticks < 300 ? 4 : timer.state == 1 && timer.ticks < 600 ? 8 : 15;
        if (period < timer.blink) { timer.blink = 0; timer.shown = !timer.shown; }
        if (!timer.shown) return false;
    }
    if (timer.state == 2) { snprintf(out, 32, "0:00.0"); return true; }
    uint32_t t = timer.ticks, sec = t / 60 % 60, min = t / 3600;
    if (min == 0 && sec < 10) snprintf(out, 32, "%01u:%02u.%01u", 0u, sec, t / 6 % 10);
    else snprintf(out, 32, "%01u:%02u", min, sec);
    return true;
}

/* The draw's part (ONi_GameState_Display_All_UI_Elements 0x4fbaa0): t from the start to the end time,
   alpha and colour lerped; a fade at alpha 0 is over. */
bool ui_fade(float rgba[4])
{
    if (!fade.on) return false;
    float t = now >= fade.end || fade.end == fade.start ? 1.0f : (float)(now - fade.start) / (float)(fade.end - fade.start);
    float a = t * fade.to[0] + (1.0f - t) * fade.from[0];
    if (a <= 0.0f) {
        fade.on = false;
        return false;
    }
    for (int k = 0; k < 3; k++) rgba[k] = t * fade.to[k + 1] + (1.0f - t) * fade.from[k + 1];
    rgba[3] = a;
    return true;
}

/* ONrGameState_LetterBox_Display 0x4fceb0: while moving, the height goes 1.25 a tick (0x52b3c8) up to 75
   (0x52ade8) or down to 0. The original moves it as it draws; the port when asked, which is once a frame. */
float ui_letterbox(void)
{
    if (box.moving) {
        float dt = (float)(now - box.time) * 1.25f;
        box.time = now;
        if (box.on) {
            box.height += dt;
            if (box.height >= 75.0f) box.height = 75.0f, box.moving = false;
        } else {
            box.height -= dt;
            if (box.height <= 0.0f) box.height = 0.0f, box.moving = false;
        }
    }
    return box.height;
}

bool ui_input_enabled(void) { return input_on; }
void ui_set_input(bool on) { input_on = on; }

void ui_set_letterbox(bool on)
{
    ui_letterbox();
    box.on = on;
    box.time = now;
    box.moving = true;
    if (!on) ui_console_clear_subtitles();     /* 0x4fcea5 */
}

bool ui_cutscene(void) { return cutscene; }
void ui_set_cutscene(bool on, bool sync) { cutscene = on, cutscene_sync = sync; }

int ui_area_count(int area) { return area >= 0 && area < UI_AREAS ? areas[area].n : 0; }

const UiLine *ui_area_line(int area, int i, float *alpha)
{
    if (area < 0 || area >= UI_AREAS || i < 0 || i >= areas[area].n) return NULL;
    const UiLine *l = &areas[area].lines[i];
    /* COrTextArea_Display 0x430fd0: 4 * the ticks left while that is within 255, else opaque */
    if (alpha) *alpha = l->timer > 0 && l->timer * 4 <= 255 ? (float)(l->timer * 4) / 255.0f : 1.0f;
    return l;
}

void ui_hide_areas(bool hide) { areas_hidden = hide; }
bool ui_areas_hidden(void) { return areas_hidden; }

static const void *area_family(void) { return font_family_get(exe_str(0x534354)); }   /* "Tahoma" */

/* The area's font (TSrContext_New 0x42ea30 in COrTextArea_Resize 0x431460): the size, plain, else the
   nearest. */
static const void *area_font(const UiArea *a)
{
    const void *family = area_family();
    uint16_t size = a->size;
    uint32_t style = FONT_STYLE_PLAIN;
    return family ? font_family_best_font(family, &size, &style) : NULL;
}

/* COrConfigure 0x42fb70 for a display w wide and 480 high (the port's): the console's area along the
   bottom eighth, 2 in from the sides, as many lines as its font's line height and 2 fit (-1 to
   COrTextArea_Resize); the messages' and the subtitles' 148 in from the sides (at least 178 wide), 2
   up from the bottom, 10 lines of the 7 point font high (23 without it) or from the top's 2, 13
   lines. With the console's level (0x533f60) under 1 the console would take the screen above 25. */
static void configure(int16_t w)
{
    if (layout_w == w) return;
    const int16_t h = 480;
    UiArea *c = &layout[UI_AREA_CONSOLE];
    *c = (UiArea){ { 2, (int16_t)(h - h * 60 / 480), (int16_t)(w - 2), h }, 7, 1, true, true, 0 };
    const void *f = area_font(c);
    c->lines = (c->rect[3] - c->rect[1]) / ((f ? font_line_height(f) : 0) + 2);
    if (c->lines > UI_AREA_LINES) c->lines = UI_AREA_LINES;
    const void *family = area_family(), *f7 = family ? font_family_font(family, 7, FONT_STYLE_PLAIN) : NULL;
    int lh = f7 ? font_line_height(f7) : 0x17;
    int16_t bottom = (int16_t)(h - 2), right = (int16_t)(w - 0x94 > 0xb2 ? w - 0x94 : 0xb2);
    int16_t top = (int16_t)(bottom > lh * 10 + 2 ? bottom - lh * 10 : 2);
    layout[UI_AREA_MESSAGE] = (UiArea){ { 0x94, top, right, bottom }, 12, 2, true, false, 13 };
    layout[UI_AREA_SUBTITLE] = (UiArea){ { 0x94, top, right, bottom }, 7, 1, true, false, 13 };
    if (family) layout_w = w;
}

const UiArea *ui_area(int area)
{
    if (area < 0 || area >= UI_AREAS) return NULL;
    configure(layout_w ? layout_w : 640);
    return &layout[area];
}

/* 0x431340: a type under the console's level (0x533f60, 1) isn't kept; a full area drops its oldest
   line; a text longer than a line goes on in the next ones. */
void ui_print(int area, int32_t type, const char *text, const char *id, int32_t timer)
{
    if (area < 0 || area >= UI_AREAS || !text || type < (int32_t)exe_u32(0x533f60)) return;
    int cap = ui_area(area)->lines;
    while (*text) {
        if (areas[area].n >= cap) {
            if (areas[area].n > 1) memmove(&areas[area].lines[0], &areas[area].lines[1], sizeof(UiLine) * (size_t)(areas[area].n - 1));
            areas[area].n--;
        }
        if (areas[area].n >= cap || areas[area].n < 0) return;
        UiLine *l = &areas[area].lines[areas[area].n++];
        size_t n = strlen(text);
        if (n > UI_LINE_TEXT - 1) n = UI_LINE_TEXT - 1;
        memcpy(l->text, text, n);
        l->text[n] = 0;
        text += n;
        strncpy(l->id, id ? id : "", sizeof l->id - 1);
        l->id[sizeof l->id - 1] = 0;
        l->type = type;
        l->timer = timer;
        l->colour = exe_u32(0x533f70);
        l->shadow = exe_u32(0x533f74);
    }
}

void ui_console_printf(const char *fmt, ...)
{
    char b[0x800];
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    ui_print(UI_AREA_CONSOLE, 0, b, NULL, (int32_t)exe_u32(0x533f68));
}

void ui_print_subtitle(const char *text, int32_t timer)
{
    if (box.on) ui_print(UI_AREA_CONSOLE, 1, text, NULL, timer);   /* 0x4fd050: G+0x38 */
    else ui_print(UI_AREA_SUBTITLE, 2, text, NULL, timer);
}

void ui_console_clear_subtitles(void)
{
    for (int i = 0; i < areas[UI_AREA_CONSOLE].n;)
        if (areas[UI_AREA_CONSOLE].lines[i].type == 1) line_remove(UI_AREA_CONSOLE, i);
        else i++;
}

/* COrTextArea_Display 0x430fd0 (from 0x430400: the subtitles' area when it has lines, else the
   messages'; 0x4303e0: the console's, both while shown (0x533f6c, 0x533f6d) and not hidden). Each line,
   the newest first from the bottom up, is formatted in the area's rectangle from its top left (the pen
   on the first line's baseline) and its block, its lines' height and 2, goes above the last; the runs
   moved down by as much as the block is below the top: so the text stands an ascent above its block.
   A block past the top stops the drawing, or in a fading area (the console's) is drawn and its line
   left 64 ticks (or fewer) to fade. A line is drawn twice: in its shadow's colour at 1, 1, then in its
   own (the runs' colour codes). */
void ui_area_display(int16_t w, UiGlyphs draw, void *u)
{
    if (areas_hidden || !draw) return;
    configure(w);
    int order[2] = { areas[UI_AREA_SUBTITLE].n ? UI_AREA_SUBTITLE : UI_AREA_MESSAGE, UI_AREA_CONSOLE };
    for (int k = 0; k < 2; k++) {
        int a = order[k];
        const UiArea *ar = &layout[a];
        const void *family = area_family(), *f = area_font(ar);
        if (!f) continue;
        int16_t cursor = ar->rect[3];
        for (int j = 0; j < areas[a].n; j++) {
            UiLine *l = &areas[a].lines[areas[a].n - j - 1];
            uint8_t alpha = l->timer > 0 && l->timer * 4 <= 255 ? (uint8_t)(l->timer * 4) : 255;
            FontRun runs[FONT_RUNS];
            int16_t lh, pen[2] = { ar->rect[0], ar->rect[1] };
            int n = font_format_text(family, f, l->colour, ar->format, l->text, ar->rect, pen, runs, &lh);
            if (!n) continue;
            int16_t block = (int16_t)((runs[n - 1].line + 1) * lh + 2);
            cursor = (int16_t)(cursor - block);
            if (cursor < ar->rect[1]) {
                if (!ar->fade) break;
                if (l->timer == 0 || l->timer > 0x40) l->timer = 0x40;
            }
            int16_t dy = (int16_t)(cursor - ar->rect[1]);
            for (int pass = 0; pass < 2; pass++)
                for (int i = 0; i < n; i++) {
                    uint32_t c = pass ? runs[i].colour : l->shadow;
                    c = (c & 0xffffffu) | (uint32_t)alpha << 24;
                    draw(f, (int16_t)(runs[i].x + !pass), (int16_t)(pen[1] + dy + runs[i].line * lh + !pass),
                         runs[i].s, runs[i].n, c, u);
                }
        }
    }
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }
static float fnum(const SlValue *v) { return v->type == SL_FLOAT ? v->f : (float)v->i; }

/* slowmo duration (0x4bd230): the slow motion timer (G+0x108) to at least duration ticks; 0 stops it. */
static bool cmd_slowmo(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    int32_t d = c >= 1 ? (int32_t)num(&v[0]) : 0;
    if (d == 0) slowmo = 0;
    else if (slowmo < d) slowmo = d;
    return true;
}
static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* fade_out r g b ticks (0x4bccb0, 0x4fcc10): from clear to the colour, each clamped to 0..1. */
static bool cmd_fade_out(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    float col[3] = { 0, 0, 0 };
    for (int k = 0; k < 3 && k < c; k++) col[k] = clamp01(fnum(&v[k]));
    int32_t ticks = c >= 4 ? num(&v[3]) : 0;
    fade.from[0] = 0.0f;
    fade.to[0] = 1.0f;
    for (int k = 0; k < 3; k++) fade.from[k + 1] = fade.to[k + 1] = col[k];
    fade.on = true;
    fade.start = now;
    fade.end = now + (uint32_t)ticks;
    return true;
}

/* fade_in ticks (0x4bcc90, 0x4fcca0): from where the fade went to, back to clear; nothing without one. */
static bool cmd_fade_in(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (!fade.on) return true;
    int32_t ticks = c >= 1 ? num(&v[0]) : 0;
    fade.start = now;
    fade.end = now + (uint32_t)ticks;
    memcpy(fade.from, fade.to, sizeof fade.from);
    fade.to[0] = 0.0f;
    return true;
}

/* letterbox on (0x4bca50: 0x4fce70 on, 0x4fce90 off) */
static bool cmd_letterbox(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    ui_set_letterbox(c >= 1 && num(&v[0]) != 0);
    return true;
}

/* input on (0x4bcc50) */
int ui_level_end(void) { return level_end; }
void ui_set_level_end(int end) { level_end = end; }

/* win (0x4bd1f0) and lose (0x4bd210): the level ends, won or lost; ONiRunGame 0x4d4640 acts on it at
   the end of the frame. */
static bool cmd_win(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    level_end = 1;
    return true;
}

static bool cmd_lose(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    level_end = 2;
    return true;
}

/* timer_start seconds function (0x4bd270): the seconds as ticks, 0 to an hour, then 0x4fd370 starts
   the timer. */
bool ui_timer_running(void) { return timer.state != 0; }

static bool cmd_timer_start(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    float s = c >= 1 ? fnum(&v[0]) * 60.0f : 0.0f;
    int32_t ticks = (int32_t)s;
    if (ticks < 0) ticks = 0;
    if (ticks > 216000) ticks = 216000;
    timer.state = 1;
    memset(timer.func, 0, sizeof timer.func);
    if (c >= 2 && v[1].type == SL_STRING && v[1].s) strncpy(timer.func, v[1].s, sizeof timer.func - 1);
    timer.ticks = (uint32_t)ticks;
    timer.blink = 0;
    timer.shown = true;
    return true;
}

/* timer_stop (0x4bd2d0, 0x4fd3a0): off (its sound, event 17, is blank in v1.0). */
static bool cmd_timer_stop(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    timer.state = 0;
    return true;
}

/* splash_screen name (AIiScript_SplashScreen 0x4bd460): the big texture's name (31 at most) for the game
   loop to show (ONrGameState_SplashScreen 0x4f4d40). */
static bool cmd_splash(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING || !v[0].s) return true;   /* "expected name of splash screen" */
    snprintf(splash, sizeof splash, "%s", v[0].s);
    return true;
}

bool ui_splash_take(char name[32])
{
    if (!splash[0]) return false;
    memcpy(name, splash, sizeof splash);
    splash[0] = 0;
    return true;
}

/* ui_show_element left|right on (ONiInGameUI_ShowElement 0x504d50) */
static bool cmd_show_element(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[0].type != SL_STRING || !v[0].s) return true;
    bool on = num(&v[1]) > 0;
    if (!strcmp(v[0].s, "left")) show_left = on;
    else if (!strcmp(v[0].s, "right")) show_right = on;   /* else "unknown element" */
    return true;
}

/* The elements' bits (ONiInGameUI_GetElementMask, the table at 0x5556d0: health 0, damage 1, invis 2,
   shield 3, hypo 4, weapon 5, ammo 6, compass 7, arrow 8, ballistic 9, energy 10, lsi 11; "all" 0xfff),
   0 for none ("No such in-game UI element"). */
static uint32_t element_bits(const char *name)
{
    static const char *const names[] = { "health", "damage", "invis", "shield", "hypo", "weapon", "ammo",
                                         "compass", "arrow", "ballistic", "energy", "lsi" };
    if (!strcmp(name, "all")) return 0xfff;
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(name, names[i])) return 1u << i;
    return 0;
}

/* ui_flash_element name on (ONiInGameUI_FlashElement 0x504f40): the element's flash bit set or cleared. */
static bool cmd_flash_element(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[0].type != SL_STRING || !v[0].s) return true;
    uint32_t bits = element_bits(v[0].s);
    if (!bits) return true;
    if (num(&v[1]) > 0) flash_bits |= bits;
    else flash_bits &= ~bits;
    return true;
}

/* ui_fill_element name on (ONiInGameUI_FillElement 0x504e30): the element drawn full or not (no level
   script uses it). */
static bool cmd_fill_element(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[0].type != SL_STRING || !v[0].s) return true;
    uint32_t bits = element_bits(v[0].s);
    if (!bits) return true;
    if (num(&v[1]) > 0) fill_bits |= bits;
    else fill_bits &= ~bits;
    return true;
}

/* target_set flag radius (ONiInGameUI_TargetSet 0x503d80): the compass toward the flag; a radius of 0
   turns it off. A flag within 10 of the one already set changes nothing; else the distance squared to
   the player now is kept (the arc narrows as the player gets nearer). Then (a flag missing too) the
   game's event sound 11, outside a cutscene (G+0x3c) once the game is 30 ticks in (G+0x144). */
static bool cmd_target_set(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    float radius = c >= 2 ? fnum(&v[1]) : 0.0f;
    if (radius == 0.0f) {
        target.on = false;
        return true;
    }
    ObjcObject flag;
    if (c < 1 || !objc_flag_find((uint16_t)num(&v[0]), &flag)) {
        target.on = false;      /* "No Flag %d exists" */
        if (!cutscene && now >= 30) sound_event_play(11);
        return true;
    }
    if (target.on) {
        float dx = flag.pos[0] - target.pos[0], dy = flag.pos[1] - target.pos[1], dz = flag.pos[2] - target.pos[2];
        if (dx * dx + dy * dy + dz * dz < 100.0f) return true;
    }
    memcpy(target.pos, flag.pos, sizeof target.pos);
    Chr *p = chars_player();
    if (p) {
        float dx = target.pos[0] - p->c.pos[0], dy = target.pos[1] - p->c.pos[1], dz = target.pos[2] - p->c.pos[2];
        target.d0 = dx * dx + dy * dy + dz * dz;
    }
    target.r2 = radius * radius;
    target.on = true;
    if (!cutscene && now >= 30) sound_event_play(11);
    return true;
}

static bool cmd_input(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    input_on = c >= 1 && num(&v[0]) != 0;
    return true;
}

uint64_t ui_keys_allowed(void) { return keys_allowed; }

/* lock_keys [group] (AIiScript_LockKeys 0x4bd4e0): with no argument only the system keys (0x10003:
   escape and the console's) stay; with a group's name its keys are let through again. The names are
   the exe's (0x54afec..0x54b068), the bits the handler's operands. */
static bool cmd_lock_keys(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    static const struct { uint32_t name; uint64_t keys; } groups[] = {
        { 0x54b068, 0x6600000 },                                  /* keys_movement */
        { 0x54b05c, 0x8000000 },                                  /* keys_jump */
        { 0x54b050, 0x10000000 | (uint64_t)0x100 << 32 },         /* keys_crouch */
        { 0x54b044, 0x60000000 | (uint64_t)0xc0 << 32 },          /* keys_attack: punch, kick, fire */
        { 0x54b038, 4 },                                          /* keys_pause */
        { 0x54b02c, (uint64_t)2 << 32 },                          /* keys_action */
        { 0x54b01c, (uint64_t)0x30 << 32 },                       /* keys_inventory: swap, drop */
        { 0x54b010, (uint64_t)1 << 32 },                          /* keys_walk */
        { 0x54b004, (uint64_t)4 << 32 },                          /* keys_hypo */
        { 0x54aff8, (uint64_t)8 << 32 },                          /* keys_reload */
        { 0x54afec, ~(uint64_t)0 },                               /* keys_all */
    };
    if (c == 0) {
        keys_allowed = 0x10003;
        return true;
    }
    if (c != 1 || v[0].type != SL_STRING) return true;     /* the usage line */
    for (size_t i = 0; i < sizeof groups / sizeof *groups; i++) {
        const char *name = exe_str(groups[i].name);
        if (name && !strcmp(name, v[0].s)) {
            keys_allowed |= groups[i].keys;
            return true;
        }
    }
    return true;        /* "unknown key group" and the list of the names */
}

/* cutscene_sync on|off|mark (0x4bcbc0): the cutscene's sync (G+0x48) on or off; mark (G+0x44, the
   frames behind, cleared) has none to clear. The sync itself (iComputeDeltaTicks 0x4fafb0: at most 6 ticks a frame, the
   rest carried in G+0x44 while the sync is on, dropped past 300 behind) has nothing to carry here: the
   machine's sixtieths are gasm's frame clock (virtual, one a frame), so a frame is always one tick. */
static bool cmd_cutscene_sync(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    if (!strcmp(v[0].s, "on")) cutscene_sync = true;
    else if (!strcmp(v[0].s, "off")) cutscene_sync = false;
    return true;
}

/* message name [timer] (AIiScript_Message 0x4bd3b0): a message (SSrMessage_Find) into the messages'
   area, else a subtitle (SSrSubtitle_Find) into the console's, type 2. */
static bool cmd_message(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    int32_t timer = c >= 2 ? num(&v[1]) : 0;
    snprintf(seen[n_seen % SEEN_MAX].id, sizeof seen[0].id, "%s", v[0].s);
    seen[n_seen++ % SEEN_MAX].time = now ? now : 1;
    const char *s = text_message(v[0].s);
    if (s) ui_print(UI_AREA_MESSAGE, 2, s, v[0].s, timer);      /* COrMessage_Print 0x4304b0 */
    else if ((s = text_subtitle(v[0].s))) ui_print(UI_AREA_CONSOLE, 2, s, v[0].s, timer);   /* 0x430480 */
    return true;   /* else "failed to find message" */
}

/* message_remove [name] (0x4bd430, COrMessage_Remove 0x430640): the message's lines (every one without
   a name) are left 64 ticks to fade; failing that, the console's (0x430510). */
static bool remove_from(int area, const char *id)
{
    bool any = false;
    for (int i = 0; i < areas[area].n; i++) {
        UiLine *l = &areas[area].lines[i];
        if (id && strcmp(l->id, id)) continue;
        if (l->timer == 0 || l->timer > 64) l->timer = 64;
        any = true;
    }
    return any;
}

void ui_message_remove(const char *id)
{
    if (!remove_from(UI_AREA_MESSAGE, id) && id) remove_from(UI_AREA_CONSOLE, id);
}

/* 0x4305c0: a line of the message area with that name. */
bool ui_message_shown(const char *id)
{
    for (int i = 0; i < areas[UI_AREA_MESSAGE].n; i++)
        if (!strcmp(areas[UI_AREA_MESSAGE].lines[i].id, id)) return true;
    return false;
}

/* COrMessage_Print: the message of that name (SSrMessage_Find), for timer ticks. False if there is none. */
bool ui_message_print(const char *id, int32_t timer)
{
    const char *s = text_message(id);
    if (s) ui_print(UI_AREA_MESSAGE, 2, s, id, timer);
    return s != NULL;
}

static bool cmd_message_remove(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    const char *id = c >= 1 && v[0].type == SL_STRING ? v[0].s : NULL;
    if (!remove_from(UI_AREA_MESSAGE, id) && id) remove_from(UI_AREA_CONSOLE, id);
    return true;
}

void ui_register_commands(void)
{
    sl_register("fade_out", cmd_fade_out, NULL);
    sl_register("slowmo", cmd_slowmo, NULL);
    sl_register("fade_in", cmd_fade_in, NULL);
    sl_register("letterbox", cmd_letterbox, NULL);
    sl_register("input", cmd_input, NULL);
    sl_register("lock_keys", cmd_lock_keys, NULL);
    sl_register("win", cmd_win, NULL);
    sl_register("lose", cmd_lose, NULL);
    sl_register("timer_start", cmd_timer_start, NULL);
    sl_register("splash_screen", cmd_splash, NULL);
    sl_register("ui_show_element", cmd_show_element, NULL);
    sl_register("ui_flash_element", cmd_flash_element, NULL);
    sl_register("ui_fill_element", cmd_fill_element, NULL);
    sl_register("target_set", cmd_target_set, NULL);
    sl_register("timer_stop", cmd_timer_stop, NULL);
    sl_register("cutscene_sync", cmd_cutscene_sync, NULL);
    sl_register("message", cmd_message, NULL);
    sl_register("message_remove", cmd_message_remove, NULL);
}
