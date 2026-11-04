/* Screen state: see ui.h. */
#include "ui.h"

#include "sl.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

static uint32_t now;

/* The fade: from and to (a, r, g, b), between two times. */
static struct { float from[4], to[4]; uint32_t start, end; bool on; } fade;
/* The letterbox: the last update's time, the bars' height, going on or off, moving. */
static struct { uint32_t time; float height; bool on, moving; } box;
static bool input_on = true;
static int level_end;            /* G+0x1679bc: 1 won, 2 lost */
static bool cutscene, cutscene_sync;
/* The timer (ONrGameState's, 0x5ece7c): 0 off, 1 counting down, 2 ended (its "0:00.0" shown for 300
   ticks); the function run at 0 (32 bytes), the ticks left, the blink and its counter. */
static struct { int state; char func[32]; uint32_t ticks; bool shown; uint32_t blink; } timer;
static struct { UiLine lines[UI_AREA_LINES]; int n; } areas[UI_AREAS];

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

void ui_reset(void)
{
    now = 0;
    n_seen = 0;
    memset(&fade, 0, sizeof fade);
    memset(&box, 0, sizeof box);
    memset(areas, 0, sizeof areas);
    input_on = true;
    level_end = 0;
    cutscene = cutscene_sync = false;
    memset(&timer, 0, sizeof timer);
}

uint32_t ui_time(void) { return now; }

void ui_update(void)
{
    now++;
    for (int a = 0; a < UI_AREAS; a++) {
        int k = 0;
        for (int i = 0; i < areas[a].n; i++) {
            UiLine *l = &areas[a].lines[i];
            if (l->timer > 0 && --l->timer == 0) continue;     /* its time is up: it goes */
            if (k != i) areas[a].lines[k] = *l;
            k++;
        }
        areas[a].n = k;
    }
    /* the timer's tick (0x4fd3d0). Not ported: its sounds (a tick every second, the music under 600
       ticks) until the sound's port plays them. */
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
}

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
}

bool ui_cutscene(void) { return cutscene; }
void ui_set_cutscene(bool on, bool sync) { cutscene = on, cutscene_sync = sync; }

int ui_area_count(int area) { return area >= 0 && area < UI_AREAS ? areas[area].n : 0; }

const UiLine *ui_area_line(int area, int i, float *alpha)
{
    if (area < 0 || area >= UI_AREAS || i < 0 || i >= areas[area].n) return NULL;
    const UiLine *l = &areas[area].lines[i];
    /* inferred: a line fades over its last 64 ticks (what message_remove leaves it, 0x430640) */
    if (alpha) *alpha = l->timer > 0 && l->timer < 64 ? (float)l->timer / 64.0f : 1.0f;
    return l;
}

/* 0x431340: a full area drops its oldest line; a text longer than a line goes on in the next ones. */
void ui_print(int area, const char *text, const char *id, int32_t timer, uint32_t colour)
{
    if (area < 0 || area >= UI_AREAS || !text) return;
    while (*text) {
        if (areas[area].n == UI_AREA_LINES) {
            memmove(&areas[area].lines[0], &areas[area].lines[1], sizeof(UiLine) * (UI_AREA_LINES - 1));
            areas[area].n--;
        }
        UiLine *l = &areas[area].lines[areas[area].n++];
        size_t n = strlen(text);
        if (n > UI_LINE_TEXT - 1) n = UI_LINE_TEXT - 1;
        memcpy(l->text, text, n);
        l->text[n] = 0;
        text += n;
        strncpy(l->id, id ? id : "", sizeof l->id - 1);
        l->id[sizeof l->id - 1] = 0;
        l->timer = timer;
        l->colour = colour;
    }
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }
static float fnum(const SlValue *v) { return v->type == SL_FLOAT ? v->f : (float)v->i; }
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

/* timer_stop (0x4bd2d0, 0x4fd3a0): off. Not ported: its sound when it was running. */
static bool cmd_timer_stop(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    timer.state = 0;
    return true;
}

static bool cmd_input(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    input_on = c >= 1 && num(&v[0]) != 0;
    return true;
}

/* cutscene_sync on|off|mark (0x4bcbc0): the cutscene's sync (G+0x48) on or off; mark (G+0x44, the
   sync's mark) is with the sync's port. Not ported: the sync itself (the frames held back to keep the
   cutscene on the clock's time, G+0x50), which a headless run doesn't need. */
static bool cmd_cutscene_sync(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    if (!strcmp(v[0].s, "on")) cutscene_sync = true;
    else if (!strcmp(v[0].s, "off")) cutscene_sync = false;
    return true;
}

/* message name [timer] (AIiScript_Message 0x4bd3b0): a message (SSrMessage_Find) into the message area,
   else a subtitle (SSrSubtitle_Find) into the subtitle area. */
static bool cmd_message(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    int32_t timer = c >= 2 ? num(&v[1]) : 0;
    snprintf(seen[n_seen % SEEN_MAX].id, sizeof seen[0].id, "%s", v[0].s);
    seen[n_seen++ % SEEN_MAX].time = now ? now : 1;
    const char *s = text_message(v[0].s);
    if (s) ui_print(UI_AREA_MESSAGE, s, v[0].s, timer, 0xffffffffu);
    else if ((s = text_subtitle(v[0].s))) ui_print(UI_AREA_SUBTITLE, s, v[0].s, timer, 0xffffffffu);
    return true;   /* else "failed to find message" */
}

/* message_remove [name] (0x4bd430, COrMessage_Remove 0x430640): the message's lines (every one without
   a name) are left 64 ticks to fade; failing that, the subtitle area's (0x430510). */
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

static bool cmd_message_remove(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    const char *id = c >= 1 && v[0].type == SL_STRING ? v[0].s : NULL;
    if (!remove_from(UI_AREA_MESSAGE, id) && id) remove_from(UI_AREA_SUBTITLE, id);
    return true;
}

void ui_register_commands(void)
{
    sl_register("fade_out", cmd_fade_out, NULL);
    sl_register("fade_in", cmd_fade_in, NULL);
    sl_register("letterbox", cmd_letterbox, NULL);
    sl_register("input", cmd_input, NULL);
    sl_register("win", cmd_win, NULL);
    sl_register("lose", cmd_lose, NULL);
    sl_register("timer_start", cmd_timer_start, NULL);
    sl_register("timer_stop", cmd_timer_stop, NULL);
    sl_register("cutscene_sync", cmd_cutscene_sync, NULL);
    sl_register("message", cmd_message, NULL);
    sl_register("message_remove", cmd_message_remove, NULL);
}
