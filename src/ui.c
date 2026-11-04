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
static bool cutscene, cutscene_sync;
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
    cutscene = cutscene_sync = false;
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
    sl_register("cutscene_sync", cmd_cutscene_sync, NULL);
    sl_register("message", cmd_message, NULL);
    sl_register("message_remove", cmd_message_remove, NULL);
}
