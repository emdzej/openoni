/* The window manager: see wm.h and docs/re/wm.md. */
#include "wm.h"

#include "exe.h"
#include "font.h"
#include "li.h"
#include "platform.h"
#include "tm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const WmDrawOps *ops;
void wm_set_draw_ops(const WmDrawOps *o) { ops = o; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- windows ---- */

enum { SLOTS = 255, QUEUE = 32, CLASSES = 24 };
static struct { bool used; WmWindow *w; } slots[SLOTS];     /* 0x5e99b0 */
static uint32_t last_slot;                                  /* 0x5e9994 */
static WmClass classes[CLASSES];                            /* 0x5ea1b0 */
static int class_count;                                     /* 0x5e99a8 */
static WmWindow *desktop;                                   /* 0x5e9970 */
static WmWindow *active;                                    /* 0x5ea4c8: the active top-level window */
static WmWindow *capture;                                   /* 0x5e9978: the mouse's */
static WmWindow *focus;                                     /* 0x5e999c: the keys' */
static struct { WmWindow *w; uint32_t msg; uintptr_t p1, p2; } queue[QUEUE];   /* 0x5e9770 */
static uint32_t q_read, q_write;                            /* 0x5e99ac, 0x5ea4c4 */
static int16_t mouse_x, mouse_y;                            /* 0x5ea4cc */
static const void *cursor;                                  /* 0x5ea1a8: the cursor's part spec */
static bool takes_input;                                    /* 0x5e99a0 (and the cursor shown, 0x5e96bc) */

bool wm_window_valid(const WmWindow *w)    /* 0x474c30 */
{
    return w && w->handle < SLOTS && slots[w->handle].used && slots[w->handle].w == w;
}

uintptr_t wm_send(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)    /* WMrMessage_Send 0x4760d0 */
{
    return wm_window_valid(w) ? w->cls->proc(w, msg, p1, p2) : 0;
}

void wm_post(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)        /* WMrMessage_Post 0x476080 */
{
    queue[q_write].w = w, queue[q_write].msg = msg, queue[q_write].p1 = p1, queue[q_write].p2 = p2;
    if (++q_write >= QUEUE) q_write = 0;
}

/* 0x476010: the next queued message, if any. */
static bool queue_get(WmWindow **w, uint32_t *msg, uintptr_t *p1, uintptr_t *p2)
{
    if (!queue[q_read].msg) return false;
    *w = queue[q_read].w, *msg = queue[q_read].msg, *p1 = queue[q_read].p1, *p2 = queue[q_read].p2;
    queue[q_read].msg = 0;
    if (q_read != q_write && ++q_read >= QUEUE) q_read = 0;
    return true;
}

static void class_register(uint16_t number, WmProc proc)   /* WMrWindowClass_Register 0x475a70 */
{
    for (int i = 0; i < class_count; i++)
        if (classes[i].number == number) return;
    if (class_count < CLASSES) classes[class_count++] = (WmClass){ number, proc };
}

/* 0x474830: the window moved in front of its siblings (the desktop's children for a top-level one), unless
   it stays behind (0x8000) or a sibling stays in front (0x4000). */
static void bring_to_front(WmWindow *w)
{
    if (!wm_window_valid(w) || w->cls->number == 1) return;
    if (w->parent) bring_to_front(w->parent);
    WmWindow *list = (w->flags & WM_STYLE_CHILD) ? w->parent : desktop;
    if ((w->flags & 0x8000) || !list || list->child == w) return;
    /* unlink, then in front (the first child), after the ones kept in front */
    if (w->prev) w->prev->next = w->next;
    if (w->next) w->next->prev = w->prev;
    if (list->child == w) list->child = w->next;
    WmWindow *after = NULL;
    for (WmWindow *s = list->child; s && (s->flags & 0x4000); s = s->next) after = s;
    w->prev = after;
    w->next = after ? after->next : list->child;
    if (w->next) w->next->prev = w;
    if (after) after->next = w;
    else list->child = w;
}

/* WMrWindow_SetCapture 0x474900: the old capture is told. */
static void set_capture(WmWindow *w)
{
    if (capture == w) return;
    WmWindow *old = capture;
    capture = w;
    if (wm_window_valid(old)) old->cls->proc(old, WM_CAPTURELOST, (uintptr_t)w, 0);
}

/* WMrWindow_Activate 0x4746d0: the top-level window over w made the active one. */
static void activate(WmWindow *w)
{
    while (wm_window_valid(w) && (w->flags & WM_STYLE_CHILD)) w = w->parent;
    if (!wm_window_valid(w) || w == active) return;
    if (active) {
        wm_send(active, WM_ACTIVATE, 0, 0);
        wm_send(active, WM_NCACTIVATE, 0, 0);
    }
    if (capture) {
        WmWindow *c = capture;
        capture = NULL;
        wm_send(c, WM_CAPTURELOST, 0, 0);
    }
    active = w;
    bring_to_front(w);
    wm_send(w, WM_NCACTIVATE, 1, 0);
    wm_send(w, WM_ACTIVATE, 1, 0);
}

/* WMrWindow_SetFocus 0x4755c0. */
static void set_focus(WmWindow *w)
{
    if (wm_window_valid(focus)) wm_send(focus, WM_KILLFOCUS, (uintptr_t)w, 0);
    focus = w;
    if (wm_window_valid(w)) wm_send(w, WM_SETFOCUS, (uintptr_t)w, 0);
}

/* The default font: the "Tahoma" family (the string at 0x534354), size 7, black. */
static void default_font(WmFont *f)
{
    memset(f, 0, sizeof *f);
    f->family = font_family_get(exe_str(0x534354));
    f->size = 7;
    f->colour = 0xff000000u;
}

static void text_extent(const WmFont *f, const char *s, int16_t *w, int16_t *h);

WmWindow *wm_window_new(uint16_t cls, uint32_t flags, uint32_t style, uint16_t id, int16_t x, int16_t y,
                        int16_t w, int16_t h, WmWindow *parent, uintptr_t param)   /* WMrWindow_New 0x475160 */
{
    if (cls == 1 && desktop) return NULL;
    uint32_t slot = last_slot + 1;
    while (slot < SLOTS && slots[slot].used) slot++;
    if (slot >= SLOTS)
        for (slot = 0; slot < last_slot && slots[slot].used; slot++) {}
    if (slot >= SLOTS || slots[slot].used) return NULL;
    const WmClass *c = NULL;
    for (int i = 0; i < class_count; i++)
        if (classes[i].number == cls) c = &classes[i];
    if (!c) return NULL;
    last_slot = slot;
    WmWindow *win = calloc(1, sizeof *win);
    if (!win) return NULL;
    slots[slot].used = true, slots[slot].w = win;
    win->cls = c, win->flags = flags, win->style = style, win->id = id, win->handle = slot;
    win->x = x, win->y = y, win->w = w, win->h = h;
    default_font(&win->font);
    text_extent(&win->font, win->title, &win->title_w, &win->title_h);
    if (flags & WM_STYLE_CHILD) {
        win->parent = parent;
        for (WmWindow *p = parent; p; p = p->parent)
            if (!(p->flags & WM_STYLE_CHILD)) { win->owner = p; break; }
    } else win->owner = parent;
    if (c->number != 1) {
        WmWindow *list = (flags & WM_STYLE_CHILD) ? win->parent : desktop;
        if (list) {
            if (!list->child) list->child = win;
            else {
                WmWindow *last = list->child;
                while (last->next) last = last->next;
                last->next = win, win->prev = last;
            }
        }
    }
    bool ok = c->proc(win, WM_PRECREATE, param, 0) != 1;
    if (ok) {
        int16_t r[4] = { 0, 0, w, h };
        c->proc(win, WM_CLIENTRECT, (uintptr_t)r, 0);
        memcpy(win->client, r, sizeof r);
        ok = c->proc(win, WM_CREATE, param, 0) != 1;
    }
    if (!ok) {
        wm_window_delete(win);
        return NULL;
    }
    if (win->parent) wm_send(win->parent, WM_PARENTNOTIFY, WM_CREATE, (uintptr_t)win);
    if (wm_window_valid(win) && c->number != 1) activate(win);
    return win;
}

/* WMrWindow_Delete 0x474950: the activation passed on (to the next or the previous window, the owner or
   the desktop's first), the children deleted, the window told and gone. */
void wm_window_delete(WmWindow *w)
{
    if (!wm_window_valid(w)) return;
    if (w == active && w->cls->number != 1) {
        WmWindow *to = w->next ? w->next : w->prev ? w->prev : w->owner ? w->owner : desktop ? desktop->child : NULL;
        if (to == w) to = NULL;
        active = NULL;
        if (to) activate(to);
    }
    while (w->child) wm_window_delete(w->child);
    wm_send(w, WM_DESTROY, 0, 0);
    if (capture == w) capture = NULL;
    if (focus == w) focus = NULL;
    if (active == w) active = NULL;
    WmWindow *list = (w->flags & WM_STYLE_CHILD) ? w->parent : desktop;
    if (w->prev) w->prev->next = w->next;
    if (w->next) w->next->prev = w->prev;
    if (list && list->child == w) list->child = w->next;
    for (uint32_t i = 0; i < QUEUE; i++)
        if (queue[i].w == w) queue[i].msg = 0;
    slots[w->handle].used = false, slots[w->handle].w = NULL;
    free(w);
}

void wm_set_visible(WmWindow *w, bool on)      /* WMrWindow_SetVisible 0x475a10 */
{
    if (!wm_window_valid(w)) return;
    if (on) w->flags |= WM_STYLE_VISIBLE;
    else w->flags &= ~(uint32_t)WM_STYLE_VISIBLE;
    wm_send(w, 0x2f, on, 0);
}

void wm_set_enabled(WmWindow *w, bool on)      /* WMrWindow_SetEnabled 0x475580 */
{
    if (!wm_window_valid(w)) return;
    if (on) w->flags &= ~(uint32_t)2;
    else w->flags |= 2;
}

/* WMrDialog_CheckRadioButton 0x468bb0: the items first..last let go (WM_HILITE 0), then the one checked
   held down. */
void wm_check_radio(WmWindow *dialog, uint16_t first, uint16_t last, uint16_t checked)
{
    for (uint32_t id = first; id <= last; id++) {
        WmWindow *b = wm_item(dialog, (uint16_t)id);
        if (b) wm_send(b, WM_HILITE, 0, 0);
    }
    WmWindow *b = wm_item(dialog, checked);
    if (b) wm_send(b, WM_HILITE, 1, 0);
}

/* 0x46b530: a button held down with the mouse on it. */
bool wm_button_pressed(const WmWindow *w) { return wm_window_valid(w) && (w->data & 0x10000); }

void wm_set_location(WmWindow *w, int16_t x, int16_t y)     /* WMrWindow_SetLocation 0x4756d0 */
{
    if (wm_window_valid(w)) w->x = x, w->y = y;
}

void wm_set_font(WmWindow *w, const WmFont *f)      /* WMrWindow_SetFont 0x475630 */
{
    if (!wm_window_valid(w)) return;
    w->font = *f;
    if (!w->font.family) default_font(&w->font);
    wm_send(w, WM_FONTCHANGED, 0, 0);
}

WmWindow *wm_item(WmWindow *dialog, uint16_t id)   /* WMrDialog_GetItemByID 0x4689a0 */
{
    WmWindow *c = dialog ? dialog->child : NULL;
    while (c && c->id != id) c = c->next;
    return c;
}

WmWindow *wm_desktop(void) { return desktop; }

/* 0x474e60: the window's rectangle on the desktop (each parent's place and client origin added). */
static void screen_rect(const WmWindow *w, int16_t r[4])
{
    int16_t x = w->x, y = w->y;
    for (const WmWindow *c = w; (c->flags & WM_STYLE_CHILD) && c->parent; c = c->parent) {
        x = (int16_t)(x + c->parent->client[0] + c->parent->x);
        y = (int16_t)(y + c->parent->client[1] + c->parent->y);
    }
    r[0] = x, r[1] = y, r[2] = (int16_t)(x + w->w), r[3] = (int16_t)(y + w->h);
}

static bool in_rect(const int16_t r[4], int16_t x, int16_t y) { return x >= r[0] && x < r[2] && y >= r[1] && y < r[3]; }

/* 0x475f50: the deepest window with all of the flags under the point; one disabled gives none. */
static WmWindow *window_at(WmWindow *w, uint32_t want, int16_t x, int16_t y)
{
    if ((w->flags & want) != want) return NULL;
    int16_t r[4];
    screen_rect(w, r);
    if (!in_rect(r, x, y)) return NULL;
    WmWindow *hit = w;
    int16_t cr[4] = { (int16_t)(r[0] + w->client[0]), (int16_t)(r[1] + w->client[1]),
                      (int16_t)(r[0] + w->client[0] + w->client[2]), (int16_t)(r[1] + w->client[1] + w->client[3]) };
    if (in_rect(cr, x, y))
        for (WmWindow *c = w->child; c; c = c->next) {
            WmWindow *h = window_at(c, want, x, y);
            if (h) { hit = h; break; }
        }
    return (w->flags & WM_STYLE_DISABLED) && hit == w ? NULL : hit;
}

/* ---- the default procedure (0x473350) ---- */

static uintptr_t default_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    switch (msg) {
    case WM_HITTEST:
        return WM_HIT_CLIENT;   /* 0x473a50: the frames' parts not ported: all client */
    case WM_NCLBUTTONDOWN:
        set_capture(w);
        return 0;
    case WM_NCLBUTTONUP:
        set_capture(NULL);
        return 0;
    case WM_CLIENTRECT: {
        /* the frame's parts (title, scroll bars) not ported: the client is the window */
        int16_t *r = (int16_t *)p1;
        r[0] = 0, r[1] = 0, r[2] = w->w, r[3] = w->h;
        return 0;
    }
    case WM_CLOSE:
        wm_window_delete(w);
        return 0;
    }
    return 0;
}

/* ---- the desktop (class 1, 0x476d70) ---- */

static const void *background;      /* the desktop's: a PSpc or a texture (WMrSetDesktopBackground, 0x3c) */
static uintptr_t desktop_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);

/* ---- the drawing context ---- */

struct WmDc {
    bool used;
    WmWindow *w;
    int16_t clip[4];                /* on the desktop */
    float ox, oy;                   /* the client's origin on the desktop */
};
static WmDc dcs[3];                 /* 0x5e96d0 */

/* The text context the DCs share (0x5e9754): the font, its colour and the format. */
static struct { WmFont font; const void *tsft; uint32_t shade; uint16_t format; } ts;

static void clip_intersect(int16_t a[4], const int16_t b[4])
{
    if (b[0] > a[0]) a[0] = b[0];
    if (b[1] > a[1]) a[1] = b[1];
    if (b[2] < a[2]) a[2] = b[2];
    if (b[3] < a[3]) a[3] = b[3];
}

/* WMrDC_Get 0x4694b0: the client area's DC, clipped by the parents up to the top-level window. */
WmDc *wm_dc_begin(WmWindow *w)
{
    WmDc *dc = NULL;
    for (int i = 0; i < 3 && !dc; i++)
        if (!dcs[i].used) dc = &dcs[i];
    if (!dc || !wm_window_valid(w)) return NULL;
    int16_t r[4];
    screen_rect(w, r);
    int16_t c[4] = { (int16_t)(r[0] + w->client[0]), (int16_t)(r[1] + w->client[1]),
                     (int16_t)(r[0] + w->client[0] + w->client[2]), (int16_t)(r[1] + w->client[1] + w->client[3]) };
    dc->ox = c[0], dc->oy = c[1];
    for (const WmWindow *p = w; (p->flags & WM_STYLE_CHILD) && p->parent; p = p->parent) {
        int16_t pr[4];
        screen_rect(p->parent, pr);
        int16_t pc[4] = { (int16_t)(pr[0] + p->parent->client[0]), (int16_t)(pr[1] + p->parent->client[1]),
                          (int16_t)(pr[0] + p->parent->client[0] + p->parent->client[2]),
                          (int16_t)(pr[1] + p->parent->client[1] + p->parent->client[3]) };
        clip_intersect(c, pc);
    }
    memcpy(dc->clip, c, sizeof c);
    dc->used = true, dc->w = w;
    return dc;
}

void wm_dc_end(WmDc *dc) { if (dc) dc->used = false; }     /* 0x469650 */

/* The text context's font (TSrContext_SetFontFamily 0x42ece0, _SetFontStyle 0x42ed50, _SetFontSize
   0x42edc0): the family's font of the size and style, else its nearest (0x42ebd0). */
void wm_dc_set_font(const WmFont *f)        /* 0x46a570 */
{
    ts.font = *f;
    ts.shade = f->colour;
    uint16_t size = f->size;
    uint32_t style = f->style;
    ts.tsft = f->family ? font_family_font(f->family, size, style) : NULL;
    if (!ts.tsft && f->family) ts.tsft = font_family_best_font(f->family, &size, &style);
}
void wm_dc_set_colour(uint32_t argb) { ts.shade = argb; }  /* 0x46a5c0 */
void wm_dc_set_format(uint16_t format) { ts.format = format; }  /* DCrText_SetFormat 0x46a5b0 */

int16_t wm_dc_ascending(void)       /* DCrText_GetAscendingHeight 0x46a350 */
{
    if (!ts.tsft) return 0;
    FontInfo fi;
    font_info(ts.tsft, &fi);
    return (int16_t)fi.ascending;
}

/* ---- the text (TSrContext_FormatString 0x42e210, TSrContext_DrawText_DC 0x42e050) ---- */

enum { MAX_RUNS = 24, MAX_RUN = 255 };
typedef struct { const char *s; uint16_t n; int16_t x, w; uint32_t colour; uint16_t line; } Run;

static uint16_t next_char(const char **p) { return font_next_char(p); }

static int16_t char_width(const void *tsft, uint16_t ch)
{
    FontGlyph g;
    return tsft && font_glyph(tsft, ch, &g) ? g.advance : 0;
}

static bool is_breaking(uint16_t ch)
{
    const char *b = ts.font.family ? font_family_breaking(ts.font.family) : NULL;
    if (!b) return false;
    uint16_t c;
    while ((c = next_char(&b)))
        if (c == ch) return true;
    return false;
}

/* The colour code letter's colour (the table at 0x533da0: 8 bytes an entry, the letter and the ARGB; 0
   ends it). */
static bool code_colour(uint16_t ch, uint32_t *argb)
{
    for (uint32_t va = 0x533da0;; va += 8) {
        const uint8_t *e = exe_data(va, 8);
        if (!e || !rd16(e)) return false;
        if (rd16(e) == ch) {
            *argb = rd32(e + 4);
            return true;
        }
    }
}

/* The string into runs and lines within the rectangle from the pen: the runs between the colour codes
   ("[" a letter of the table sets the colour, "." ends the code, "]" puts the colour back), the lines
   broken where a run would pass the rectangle's width, at the last of the family's breaking characters
   in the run (else the run starts the next line, unless it is the line's first), at most 24 runs;
   each line aligned by the format (2 centred, 4 right, else at the pen), the lines from the pen's
   baseline down (format 8: centred in the rectangle's height). Format 0x30 is one line, as it is. */
static int format_text(const char *s, const int16_t rect[4], const int16_t pen[2], Run *runs, int16_t *line_h)
{
    const void *f = ts.tsft;
    FontInfo fi = { 0 };
    if (f) font_info(f, &fi);
    *line_h = (int16_t)(fi.ascending + fi.descending + fi.leading);
    int16_t width = (int16_t)(rect[2] - rect[0]);
    bool one_line = (ts.format & 0x30) != 0;
    uint32_t colour = ts.shade;
    int nruns = 0, lines = 0, line_first = 0;
    int16_t line_w = 0;
    bool code = false, done = false;
    const char *p = s;
    while (!done && nruns + 1 < MAX_RUNS) {
        const char *start = p, *after_break = NULL;
        uint16_t n = 0, n_break = 0;
        int16_t w = 0, w_break = 0;
        bool reset = false, ended_by_code = false, overflow = false;
        for (;;) {
            const char *q = p;
            uint16_t ch = next_char(&q);
            if (!ch) { done = true; break; }
            if (ch == '[') { code = true; p = q; ended_by_code = true; break; }
            if (ch == ']') { code = false; p = q; reset = true; ended_by_code = true; break; }
            if (code) {
                uint32_t c;
                if (ch == '.') { code = false; p = q; start = p; continue; }
                if (code_colour(ch, &c)) { colour = c; p = q; start = p; continue; }
                code = false;
            }
            int16_t cw = char_width(f, ch);
            if (!one_line && (w + cw + line_w > width || n + 1 > MAX_RUN)) { overflow = true; break; }
            w = (int16_t)(w + cw), n++, p = q;
            if (is_breaking(ch)) n_break = n, w_break = w, after_break = p;
        }
        if (overflow) {
            if (n_break) n = n_break, w = w_break, p = after_break;
            else if (nruns > line_first) n = 0, p = start;     /* the run goes to the next line */
            /* else: the line's first run, kept whole */
            else if (!n) { /* a character wider than the line */ uint16_t ch = next_char(&p); (void)ch; n = 1, w = char_width(f, ch); }
        }
        if (n) {
            runs[nruns] = (Run){ start, n, 0, w, colour, (uint16_t)lines };
            line_w = (int16_t)(line_w + w);
            nruns++;
        }
        if (reset) colour = ts.shade;
        if (!ended_by_code || done) {
            /* the line ends: its runs placed */
            int16_t x = (ts.format & 2) ? (int16_t)(rect[0] + ((width - line_w) >> 1))
                      : (ts.format & 4) ? (int16_t)(rect[2] - line_w) : pen[0];
            for (int i = line_first; i < nruns; i++) runs[i].x = x, x = (int16_t)(x + runs[i].w);
            lines++, line_first = nruns, line_w = 0;
        }
        if (one_line && done) break;
    }
    return nruns;
}

static void text_extent(const WmFont *fnt, const char *s, int16_t *w, int16_t *h)  /* TSrContext_GetStringRect 0x42e920 */
{
    const void *f = fnt->family ? font_family_font(fnt->family, fnt->size, fnt->style) : NULL;
    FontInfo fi = { 0 };
    if (f) font_info(f, &fi);
    *w = f ? font_string_width(f, s) : 0;
    *h = (int16_t)(fi.ascending + fi.descending + fi.leading);
}

void wm_dc_text(WmDc *dc, const int16_t rect[4], const int16_t pen[2], const char *s, int16_t out[4])
{
    if (!dc || !s) return;
    int16_t r[4];
    if (rect) memcpy(r, rect, sizeof r);
    else r[0] = 0, r[1] = 0, r[2] = dc->w->client[2], r[3] = dc->w->client[3];
    int16_t pn[2] = { pen ? pen[0] : r[0], pen ? pen[1] : (int16_t)(r[1] + wm_dc_ascending()) };
    if (out) memset(out, 0, 4 * sizeof *out);
    if (r[2] <= r[0] || r[3] <= r[1]) return;
    Run runs[MAX_RUNS];
    int16_t lh;
    int n = format_text(s, r, pn, runs, &lh);
    int lines = n ? runs[n - 1].line + 1 : 0;
    int16_t base = (ts.format & 8) ? (int16_t)(r[1] + ((r[3] - r[1] - lines * lh) >> 1) + wm_dc_ascending()) : pn[1];
    int16_t maxw = 0;
    for (int i = 0; i < n; i++) {
        int16_t right = (int16_t)(runs[i].x + runs[i].w - r[0]);
        if (right > maxw) maxw = right;
        if (ops && ops->glyphs && ts.tsft)
            ops->glyphs(ts.tsft, dc->ox + runs[i].x, dc->oy + base + runs[i].line * lh, runs[i].s, runs[i].n,
                        runs[i].colour, dc->clip);
    }
    if (out) out[0] = 0, out[1] = 0, out[2] = maxw, out[3] = (int16_t)(lines * lh);
}

/* ---- the part specs (PSpc, PSUI) ---- */

static const void *psui;            /* 0x5e9758 */
const void *wm_psui_get(const char *name) { return tm_instance_get(TM_TAG("PSUI"), name); }
const void *wm_psui_active(void)    /* PSrPartSpecUI_GetActive 0x46aa60: "psui_g206" (0x53c45c) at first */
{
    if (!psui) psui = wm_psui_get(exe_str(0x53c45c));
    return psui;
}
void wm_psui_set_active(const void *p) { psui = p; }     /* 0x46aa90 */
const void *wm_psui_part(unsigned offset)
{
    const uint8_t *p = wm_psui_active();
    return p ? tm_link(p + offset) : NULL;
}

/* A PSpc: 9 parts by columns (part = column * 3 + row), each a top left (i16 x, y from +0) and a bottom
   right (from +0x24) in its texture (+0x48). */
static void part_corners(const uint8_t *ps, int k, int16_t tl[2], int16_t br[2])
{
    tl[0] = (int16_t)rd16(ps + 4 * k), tl[1] = (int16_t)rd16(ps + 4 * k + 2);
    br[0] = (int16_t)rd16(ps + 0x24 + 4 * k), br[1] = (int16_t)rd16(ps + 0x24 + 4 * k + 2);
}

static int16_t iabs(int v) { return (int16_t)(v < 0 ? -v : v); }

/* PSrPartSpec's private data (0x46ab70): the left and right columns' widths, the top and bottom rows'
   heights. */
void wm_part_size(const void *pspc, int16_t *w, int16_t *h)    /* 0x46a980: the top left part's size */
{
    int16_t tl[2], br[2];
    part_corners(pspc, 0, tl, br);
    if (w) *w = iabs(br[0] - tl[0]);
    if (h) *h = iabs(br[1] - tl[1]);
}

/* A sprite clipped to the rectangle, its texture coordinates cut alike (WMrClipRect 0x469340). */
static void clipped_sprite(const void *tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                           float v1, const int16_t clip[4], uint32_t argb)
{
    if (x0 > clip[2] || x1 < clip[0] || y0 > clip[3] || y1 < clip[1] || x1 <= x0 || y1 <= y0) return;
    if (x0 < clip[0]) u0 = u1 - (u1 - u0) * ((x1 - clip[0]) / (x1 - x0)), x0 = clip[0];
    if (y0 < clip[1]) v0 = v1 - (v1 - v0) * ((y1 - clip[1]) / (y1 - y0)), y0 = clip[1];
    if (x1 > clip[2]) u1 = u0 + (u1 - u0) * ((clip[2] - x0) / (x1 - x0)), x1 = clip[2];
    if (y1 > clip[3]) v1 = v0 + (v1 - v0) * ((clip[3] - y0) / (y1 - y0)), y1 = clip[3];
    float r[4] = { x0, y0, x1, y1 }, uv[4] = { u0, v0, u1, v1 };
    if (ops && ops->sprite) ops->sprite(tex, r, uv, argb);
}

/* A size from the part's: as it is when the whole fits, else e * e / size (or the size itself when
   smaller than the edge). */
static int16_t edge(int16_t e, int16_t size, int16_t other)
{
    if (size >= e + other) return e;
    return e <= size ? (int16_t)(e * e / size) : size;
}

void wm_dc_part(WmDc *dc, const void *pspc, uint16_t mask, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t alpha)
{
    if (!dc || !pspc || !w || !h) return;
    const uint8_t *ps = pspc;
    const void *tex = tm_link(ps + 0x48);
    if (!tex) return;
    int16_t tl[2], br[2];
    part_corners(ps, 0, tl, br);
    int16_t L = iabs(br[0] - tl[0]), T = iabs(br[1] - tl[1]);
    part_corners(ps, 6, tl, br);
    int16_t R = iabs(br[0] - tl[0]);
    part_corners(ps, 2, tl, br);
    int16_t B = iabs(br[1] - tl[1]);
    int16_t top = edge(T, h, B), bottom = edge(B, h, T), left = edge(L, w, R), right = edge(R, w, L);
    int16_t mid_h = (int16_t)(h - bottom - top), mid_w = (int16_t)(w - right - left);
    if (mid_h < 0) mid_h = 0;
    if (mid_w < 0) mid_w = 0;
    if (mask == 0x10) mid_w = w, mid_h = h;     /* the centre alone fills the rectangle */
    float tw = (float)rd16((const uint8_t *)tex + 0x84), th = (float)rd16((const uint8_t *)tex + 0x86);
    uint32_t argb = (uint32_t)alpha << 24 | 0xffffff;
    static const int16_t cols[3] = { 7, 0x38, 0x1c0 };
    for (int c = 0; c < 3; c++) {
        int16_t cx = (int16_t)(x + (int16_t)dc->ox), cw = c == 0 ? left : c == 1 ? mid_w : right;
        if (c >= 1 && (mask & 7)) cx = (int16_t)(cx + left);
        if (c == 2 && (mask & 0x38)) cx = (int16_t)(cx + mid_w);
        (void)cols;
        int16_t ry = (int16_t)(y + (int16_t)dc->oy);
        for (int r = 0; r < 3; r++) {
            int k = c * 3 + r;
            if (!(mask & (1u << k))) continue;
            int16_t rh = r == 0 ? top : r == 1 ? mid_h : bottom;
            part_corners(ps, k, tl, br);
            clipped_sprite(tex, cx, ry, cx + cw, ry + rh, tl[0] / tw, tl[1] / th, br[0] / tw, br[1] / th, dc->clip, argb);
            ry = (int16_t)(ry + rh);
        }
    }
}

static uint32_t instance_tag(const void *data)
{
    const TmInstance *in = data ? tm_handle_instance(tm_handle_of(data)) : NULL;
    return in && in->tmpl ? in->tmpl->tag : 0;
}

/* DCrDraw_TextureRef 0x469fc0: a TXMP over w by h (-1: its own size) at its texels' scale (the texture
   coordinates the size over the texture's), or stretched over it; a TXMB (+0x08 width, +0x0a height,
   +0x10 across, +0x12 down, +0x18 the tiles by rows) as its tiles, 256 apart, each cut to what is left. */
void wm_dc_texture(WmDc *dc, const void *tex, int16_t x, int16_t y, int16_t w, int16_t h, bool stretch, uint8_t alpha)
{
    if (!dc || !tex) return;
    const uint8_t *t = tex;
    if (instance_tag(tex) == TM_TAG("TXMB")) {
        if (w == -1) w = (int16_t)rd16(t + 0x08);
        if (h == -1) h = (int16_t)rd16(t + 0x0a);
        uint16_t across = rd16(t + 0x10), down = rd16(t + 0x12);
        for (int r = 0; r < down; r++)
            for (int c = 0; c < across; c++) {
                int tw = w - c * 256, th = h - r * 256;
                tw = tw < 0 ? 0 : tw > 255 ? 256 : tw;
                th = th < 0 ? 0 : th > 255 ? 256 : th;
                wm_dc_texture(dc, tm_link(t + 0x18 + 4 * (r * across + c)), (int16_t)(x + c * 256),
                              (int16_t)(y + r * 256), (int16_t)tw, (int16_t)th, false, alpha);
            }
        return;
    }
    float tw = (float)rd16(t + 0x84), th = (float)rd16(t + 0x86);
    if (w == -1) w = (int16_t)tw;
    if (h == -1) h = (int16_t)th;
    if (!w || !h || !tw || !th) return;
    float u1 = stretch ? 0.999f : w / tw, v1 = stretch ? 0.999f : h / th;
    float x0 = dc->ox + x, y0 = dc->oy + y;
    clipped_sprite(tex, x0, y0, x0 + w, y0 + h, 0, 0, u1, v1, dc->clip, (uint32_t)alpha << 24 | 0xffffff);
}

/* ---- the frame: drawing and input ---- */

/* WMiWindow_Draw 0x4762d0: a visible window's frame, its paint, then its children from the last. */
static void draw_window(WmWindow *w)
{
    if (!(w->flags & WM_STYLE_VISIBLE)) return;
    wm_send(w, WM_NCPAINT, 0, 0);
    wm_send(w, WM_PAINT, 0, 0);
    WmWindow *c = w->child;
    while (c && c->next) c = c->next;
    for (; c; c = c->prev) draw_window(c);
}

void wm_display(void)      /* WMrDisplay 0x476140 */
{
    if (!desktop || !(desktop->flags & WM_STYLE_VISIBLE)) return;
    wm_send(desktop, WM_NCPAINT, 0, 0);
    wm_send(desktop, WM_PAINT, 0, 0);
    WmWindow *c = desktop->child;
    while (c && c->next) c = c->next;
    for (; c; c = c->prev) draw_window(c);
    /* the cursor (0x468770): its part spec, 32 by 32 over the mouse */
    if (cursor && takes_input) {
        WmDc dc = { true, desktop, { 0, 0, desktop->w, desktop->h }, 0, 0 };
        wm_dc_part(&dc, cursor, 0xffff, (int16_t)(mouse_x - 16), (int16_t)(mouse_y - 16), 32, 32, 0xff);
    }
}

/* 0x477510: a key to the focused window, or its dialog when the window doesn't take that kind (WM_WANTKEYS:
   1 letters, 2 digits, 4 punctuation, 0x10 the arrows, 0x20 the page keys, 8 Delete and Backspace). */
void wm_key(uint8_t code)
{
    WmWindow *to = focus ? focus : active;
    if (!to) return;
    if (focus && (focus->flags & WM_STYLE_CHILD) && focus->parent && focus->parent->cls->number == 6) {
        uint32_t kind = 0;
        if ((code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z')) kind = 1;
        else if (code >= '0' && code <= '9') kind = 2;
        else if (code > 0x20 && code < 0x7f) kind = 4;
        else if (code >= 0xb0 && code <= 0xb3) kind = 0x10;
        else if (code == 0xab || code == 0xac || code == 0xae || code == 0xaf) kind = 0x20;
        else if (code == 8 || code == 0xad) kind = 8;
        uintptr_t want = wm_send(focus, WM_WANTKEYS, 0, 0);
        if (!kind || !(want & kind)) to = focus->parent;
    }
    wm_post(to, WM_KEYDOWN, code, 0);
}

/* The mouse as LI events: the window under it (or the capture), its hit test, and the message. */
static void mouse_event(int type, int16_t x, int16_t y)
{
    static const uint32_t client_msg[8] = { 0, 0x1e, 0x1f, 0x20, 0x22, 0x23, 0x25, 0x26 };  /* 0x53cac0 */
    static const uint32_t frame_msg[8] = { 0, 5, 6, 7, 9, 10, 12, 13 };                     /* 0x53cb00 */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (desktop && x > desktop->w) x = desktop->w;
    if (desktop && y > desktop->h) y = desktop->h;
    mouse_x = x, mouse_y = y;
    WmWindow *w = desktop ? window_at(desktop, WM_STYLE_VISIBLE, x, y) : NULL;
    if (capture) w = capture;
    uintptr_t point = (uintptr_t)((uint32_t)(uint16_t)x << 16 | (uint16_t)y);
    uintptr_t part = w ? wm_send(w, WM_HITTEST, point, 0) : WM_HIT_CLIENT;
    uint32_t msg = part == WM_HIT_CLIENT || !w ? client_msg[type] : frame_msg[type];
    if (capture && msg == 5) msg = 0x1e;
    wm_post(w, msg, point, part);
}

void wm_click(int16_t x, int16_t y)
{
    mouse_event(1, x, y);
    mouse_event(2, x, y);
    mouse_event(3, x, y);
}

static uint8_t mouse_buttons;
void wm_take_input(bool on) { takes_input = on; }     /* WMrActivate 0x476110, WMrDeactivate 0x476410 */

void wm_update(void)     /* WMrUpdate 0x477070: the timers (none yet), then the input's events */
{
    if (!takes_input) return;
    PlatMouse m;
    if (plat_mouse(&m) && m.inside && desktop) {     /* the pointer over the game's window */
        int dw, dh;
        plat_display_size(&dw, &dh);
        float s = dh ? (float)desktop->h / (float)dh : 1.0f;
        int16_t x = (int16_t)(m.x * s), y = (int16_t)(m.y * s);
        if (x != mouse_x || y != mouse_y) mouse_event(1, x, y);
        uint8_t now = (m.buttons & MOUSE_LEFT) != 0;
        if (now != mouse_buttons) mouse_event(now ? 2 : 3, x, y);     /* the left button (the runners' one) */
        mouse_buttons = now;
    }
    uint8_t keys[16];
    int n = li_key_presses(keys, (int)sizeof keys);     /* the LI's type 9 events (0x477510) */
    for (int i = 0; i < n; i++) wm_key(keys[i]);
}

/* ---- dialogs ---- */

typedef struct {
    WmDialogProc proc;              /* +0x00 */
    bool done;                      /* +0x04 */
    uint32_t result;                /* +0x08 */
    uint16_t default_id;            /* +0x0c: the default button's (0x3f) */
    const uint8_t *wmdd;
    uintptr_t param;                /* +0x10 */
} DialogData;

uintptr_t wm_dialog_param(WmWindow *d) { return d && d->data ? ((DialogData *)d->data)->param : 0; }
void wm_dialog_end(WmWindow *d, uint32_t result)       /* WMrDialog_ModalEnd 0x468b90 */
{
    if (d && d->data) ((DialogData *)d->data)->result = result, ((DialogData *)d->data)->done = true;
}

static bool enabled(const WmWindow *w) { return !(w->flags & 2); }     /* 0x474d00 */
static void picture_title(WmWindow *w);

/* The dialog's procedure (WMiDialog_Create 0x468cd0, class 6). */
static uintptr_t dialog_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    DialogData *d = (DialogData *)w->data;
    switch (msg) {
    case WM_CREATE: {
        const DialogData *src = (const DialogData *)p1;
        d = calloc(1, sizeof *d);
        if (!d) return 1;
        w->data = (uintptr_t)d;
        if (!src) return 0;
        *d = *src;
        const uint8_t *dd = d->wmdd;
        uint32_t n = rd32(dd + 0x114);
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *it = dd + 0x118 + i * 0x124;
            WmWindow *c = wm_window_new(rd16(it + 0x100), rd32(it + 0x104) | WM_STYLE_CHILD, rd32(it + 0x108),
                                        rd16(it + 0x102), (int16_t)rd16(it + 0x10c), (int16_t)rd16(it + 0x10e),
                                        (int16_t)rd16(it + 0x110), (int16_t)rd16(it + 0x112), w, 0);
            if (!c) return 1;
            snprintf(c->title, sizeof c->title, "%s", (const char *)it);
            picture_title(c);
            WmFont f = { tm_link(it + 0x114), rd32(it + 0x118), rd32(it + 0x11c), rd16(it + 0x120), rd16(it + 0x122) };
            wm_set_font(c, &f);
            text_extent(&c->font, c->title, &c->title_w, &c->title_h);
        }
        if (rd32(dd + 0x108) & WM_DIALOG_CENTRED) {      /* WMiDialog_Center 0x469180 */
            WmWindow *p = w->parent ? w->parent : desktop;
            if (p) wm_set_location(w, (int16_t)((p->w - w->w) >> 1), (int16_t)((p->h - w->h) >> 1));
        }
        return 0;
    }
    case WM_DESTROY:
        if (d && d->proc) d->proc(w, msg, p1, p2);
        free(d);
        w->data = 0;
        return 0;
    case WM_CLOSE:     /* the cancel's command */
        wm_send(w, WM_COMMAND, 0x10002, 0);
        return 0;
    case 0x3e:
        return d ? d->default_id : 0;
    case 0x3f:
        if (d) d->default_id = (uint16_t)p1;
        return 0;
    case WM_COMMAND:
        if (!d) return 0;
        if (d->proc && d->proc(w, msg, p1, p2)) return 0;
        if ((p1 >> 16) == 1 && ((uint16_t)p1 == 1 || (uint16_t)p1 == 2)) wm_dialog_end(w, (uint16_t)p1);
        return 0;
    case WM_KEYDOWN:
        if (d && d->proc && d->proc(w, msg, p1, p2)) return 0;
        if (p1 == 0x0d || p1 == 0xba) {      /* Return: the default button's command */
            uint16_t id = (uint16_t)wm_send(w, 0x3e, 0, 0);
            WmWindow *b = id ? wm_item(w, id) : NULL;
            if (b && enabled(b)) wm_post(w, WM_COMMAND, (uintptr_t)id | 0x10000, (uintptr_t)b);
        } else if (p1 == 0x1b || p1 == 0x2a) wm_post(w, WM_CLOSE, 0, 0);    /* Escape */
        return 0;
    case WM_HITTEST:
    case WM_CLIENTRECT:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
        return default_proc(w, msg, p1, p2);
    default:
        if (d && d->proc && d->proc(w, msg, p1, p2)) return 0;
        return default_proc(w, msg, p1, p2);
    }
}

static void (*draw_callback)(void);    /* 0x5e96cc */
static void (*frame_hook)(void);
void wm_set_draw_callback(void (*draw)(void)) { draw_callback = draw; }
void wm_set_frame_hook(void (*hook)(void)) { frame_hook = hook; }

/* 0x468830: the WMDD with the id among all of them. */
static const uint8_t *find_dialog(uint16_t id)
{
    for (int f = 0; f < tm_file_slots(); f++) {
        TmFile *file = tm_file_at(f);
        for (uint32_t i = 0; file && i < tm_file_instance_count(file); i++) {
            const TmInstance *in = tm_file_instance(file, i);
            if (in->tmpl && in->tmpl->tag == TM_TAG("WMDD") && in->data && rd16(in->data + 0x100) == id) return in->data;
        }
    }
    return NULL;
}

uint32_t wm_dialog_modal(uint16_t id, WmDialogProc proc, uintptr_t param)
{
    const uint8_t *dd = find_dialog(id);
    if (!dd || !desktop) return 0;
    DialogData init = { proc, false, 0, 0, dd, param };
    WmWindow *w = wm_window_new(6, rd32(dd + 0x104), rd32(dd + 0x108), id, (int16_t)rd16(dd + 0x10c),
                                (int16_t)rd16(dd + 0x10e), (int16_t)rd16(dd + 0x110), (int16_t)rd16(dd + 0x112),
                                NULL, (uintptr_t)&init);
    if (!w) return 0;
    snprintf(w->title, sizeof w->title, "%s", (const char *)dd);
    if (proc) proc(w, WM_INITDIALOG, param, 0);
    DialogData *d = (DialogData *)w->data;
    while (d && !d->done) {
        if (plat_frame_shown()) {
            if (draw_callback) draw_callback();
            wm_display();
        }
        wm_update();
        WmWindow *t;
        uint32_t msg;
        uintptr_t p1, p2;
        while (queue_get(&t, &msg, &p1, &p2)) {
            /* the mouse's and the keys' events for a window not of the dialog are dropped */
            bool input = (msg >= 6 && msg <= 0xe) || (msg >= 0x1e && msg <= 0x27);
            bool ours = t == w;
            for (WmWindow *p = t; p && !ours; p = p->parent) ours = p == w;
            if (input && !ours) continue;
            if (msg == WM_LBUTTONDOWN || msg == WM_NCLBUTTONDOWN) activate(t);     /* 0x475ca0 */
            wm_send(t, msg, p1, p2);
        }
        if (d->done) break;
        if (frame_hook) frame_hook();
        plat_wait_frame();
    }
    uint32_t result = d ? d->result : 0;
    wm_window_delete(w);
    return result;
}

/* The desktop: its background, a texture at its size or a part spec over it. */
static uintptr_t desktop_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    if (msg == 0x3c) {
        background = (const void *)p1;
        return 0;
    }
    if (msg != WM_PAINT) return default_proc(w, msg, p1, p2);
    if (!background) return 0;
    WmDc *dc = wm_dc_begin(w);
    if (instance_tag(background) == TM_TAG("PSpc")) wm_dc_part(dc, background, 0xffff, 0, 0, w->w, w->h, 0xff);
    else wm_dc_texture(dc, background, 0, 0, -1, -1, false, 0xff);
    wm_dc_end(dc);
    return 0;
}

void wm_set_desktop_background(const void *b) { wm_send(desktop, 0x3c, (uintptr_t)b, 0); }

/* PSrPartSpec_LoadByType 0x46aa10: the part spec of the type in "partspec_list" (PSpL, the string at
   0x53c44c: +0x14 the count, then the type and the PSpc, 8 bytes an entry). */
const void *wm_partspec_by_type(uint32_t type)
{
    const uint8_t *l = tm_instance_get(TM_TAG("PSpL"), exe_str(0x53c44c));
    for (uint32_t i = 0; l && i < rd32(l + 0x14); i++)
        if (rd32(l + 0x18 + 8 * i) == (type & 0xffff)) return tm_link(l + 0x1c + 8 * i);
    return NULL;
}

/* ---- the classes ---- */

static uintptr_t box_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);
static uintptr_t button_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);
static uintptr_t text_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);


/* The picture (class 11, WMiPicture_Paint 0x470260): its title names its image, a TXMP, else a TXMB
   (style 0x20000: none, set by 0x55 a part spec or 0x56 a texture); a part spec over the window, a
   texture at its size cut to the window (style 0x10000: stretched over it). */
static uintptr_t picture_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    switch (msg) {
    case WM_CREATE:
        w->data = 0;
        return 0;
    case WM_PAINT: {
        const void *img = (const void *)w->data;
        if (!img) return 0;
        WmDc *dc = wm_dc_begin(w);
        if (instance_tag(img) == TM_TAG("PSpc")) wm_dc_part(dc, img, 0xffff, 0, 0, w->w, w->h, 0xff);
        else if (w->style & 0x10000) wm_dc_texture(dc, img, 0, 0, w->w, w->h, true, 0xff);
        else {
            const uint8_t *t = img;
            bool big = instance_tag(img) == TM_TAG("TXMB");
            int16_t tw = (int16_t)rd16(t + (big ? 0x08 : 0x84)), th = (int16_t)rd16(t + (big ? 0x0a : 0x86));
            wm_dc_texture(dc, img, 0, 0, w->w < tw ? w->w : tw, w->h < th ? w->h : th, false, 0xff);
        }
        wm_dc_end(dc);
        return 0;
    }
    case 0x55:
        if (instance_tag((const void *)p1) == TM_TAG("PSpc")) w->data = p1;
        return 0;
    case 0x56:
        if (instance_tag((const void *)p1) == TM_TAG("TXMP") || instance_tag((const void *)p1) == TM_TAG("TXMB")) w->data = p1;
        return 0;
    }
    return default_proc(w, msg, p1, p2);
}

/* The picture's image from its title, once the dialog has set it (the original's create reads the title
   WMrWindow_New copied in). */
static void picture_title(WmWindow *w)
{
    if (w->cls->number != 11 || (w->style & 0x20000)) return;
    const void *img = tm_instance_get(TM_TAG("TXMP"), w->title);
    if (!img) img = tm_instance_get(TM_TAG("TXMB"), w->title);
    w->data = (uintptr_t)img;
}

/* The box (class 3, WMiBox_Paint 0x46af20): the PSUI's outline (+0x2c) over it, dimmed when disabled. Not
   ported: the title (style 0x40000). */
static uintptr_t box_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    if (msg != WM_PAINT) return default_proc(w, msg, p1, p2);
    const void *part = wm_psui_part(0x2c);
    WmDc *dc = wm_dc_begin(w);
    wm_dc_part(dc, part, 0xffff, 0, 0, w->w, w->h, enabled(w) ? 0xff : 0x7f);
    wm_dc_end(dc);
    return 0;
}

/* The button (class 4, 0x46b150): its part (the PSUI's +0x30, +0x34 the default button, +0x38 pressed
   or lit) and its title centred (format 0xa) in its font's colour, grey when disabled. A press takes
   the focus and the mouse; a release over it sends its parent the command (the id, 1 in the high word). */
static uintptr_t button_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    switch (msg) {
    case WM_HITTEST:
        return WM_HIT_CLIENT;
    case WM_CREATE:
        w->data = 0;
        if (w->style & WM_BUTTON_DEFAULT) wm_send(w->parent, 0x3f, w->id, 0);
        return 0;
    case WM_PAINT: {
        WmDc *dc = wm_dc_begin(w);
        if (!dc) return 0;
        bool on = enabled(w);
        if (w->style & WM_BUTTON_PART) {
            unsigned part = (w->data & 0x30000) ? 0x38 : (w->style & WM_BUTTON_DEFAULT) ? 0x34 : 0x30;
            wm_dc_part(dc, wm_psui_part(part), 0xffff, 0, 0, w->w, w->h, on ? 0xff : 0x7f);
        }
        if (w->style & WM_BUTTON_TITLE) {
            wm_dc_set_font(&w->font);
            if (!on) wm_dc_set_colour(0xff808080u);
            wm_dc_set_format(0xa);
            int16_t r[4] = { 0, 0, w->client[2], w->client[3] };
            wm_dc_text(dc, r, NULL, w->title, NULL);
        }
        wm_dc_end(dc);
        return 0;
    }
    case WM_WANTKEYS:
        return (w->style & WM_BUTTON_DEFAULT) ? 0x40 : 0;
    case WM_HILITE:
        if (w->style & 0x80000) w->data = p1 ? (w->data | 0x20000) : (w->data & ~(uintptr_t)0x20000);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: {
        int16_t r[4];
        screen_rect(w, r);
        bool inside = in_rect(r, (int16_t)(p1 >> 16), (int16_t)p1);
        if (msg == WM_MOUSEMOVE) {
            if ((uint16_t)w->data == 1 && (p2 & 1)) w->data = inside ? (w->data | 0x10000) : (w->data & ~(uintptr_t)0x10000);
        } else if (msg == WM_LBUTTONDOWN) {
            w->data |= 0x10001;
            set_focus(w);
            set_capture(w);
        } else {
            bool pressed = (uint16_t)w->data == 1;
            w->data &= ~(uintptr_t)0x10001;
            set_capture(NULL);
            /* Not ported: the click's impulse sound (0x5e975c) */
            if (inside && pressed) wm_send(w->parent, WM_COMMAND, (uintptr_t)w->id | 0x10000, (uintptr_t)w);
        }
        return 0;
    }
    }
    return default_proc(w, msg, p1, p2);
}

/* The list box (class 8, 0x46c720): lines of text, each with a long of data (0x45 adds one, sorted with
   style 0x20000; 0x51 sets its data; 0x53 selects one, -1 none; 0x47 the data of a line, -1 the
   selected one; 0x48 the count). A click selects the line under it and tells the parent (command, notice
   1), a double click notice 2. Its frame is the PSUI's edit field (+0x44), a selected line the list
   hilite (+0x4c) while it has the focus, else the background (+0x00). Not ported: the scroll bar (class
   10, style 0x10000) and the owner-drawn lines (style 0x100000): the lines scroll to keep the selected
   one in view. */
typedef struct { char *text; uint32_t data; bool selected; } ListLine;
typedef struct { ListLine *lines; uint32_t n, cap, top; bool focus, pressed, twice; } ListBox;

static int16_t list_line_h(const WmWindow *w)
{
    int16_t tw = 0, th = 0;
    text_extent(&w->font, "X", &tw, &th);
    return th > 0 ? th : 12;
}

static void list_show(WmWindow *w, ListBox *b, uint32_t i)
{
    int16_t lh = list_line_h(w);
    uint32_t rows = (uint32_t)((w->h - 16) / lh);
    if (!rows) rows = 1;
    if (i < b->top) b->top = i;
    else if (i >= b->top + rows) b->top = i - rows + 1;
}

static uintptr_t list_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    ListBox *b = (ListBox *)w->data;
    switch (msg) {
    case WM_PRECREATE:
        w->data = (uintptr_t)calloc(1, sizeof(ListBox));
        return w->data ? 0 : 1;
    case WM_DESTROY:
        if (b) {
            for (uint32_t i = 0; i < b->n; i++) free(b->lines[i].text);
            free(b->lines);
            free(b);
            w->data = 0;
        }
        return 0;
    case WM_SETFOCUS: if (b) b->focus = true; return 0;
    case WM_KILLFOCUS: if (b) b->focus = false; return 0;
    case WM_WANTKEYS: return 0x33;
    case WM_PAINT: {
        if (!b) return 0;
        /* (the frame is the original's non-client part, 0x11 and 4: here drawn first, the lines inside its
           corners) */
        WmDc *dc = wm_dc_begin(w);
        const void *frame = wm_psui_part(0x44);
        int16_t in[2] = { 0, 0 };
        if (frame) {
            wm_dc_part(dc, frame, 0xffff, 0, 0, w->w, w->h, 0xff);
            wm_part_size(frame, &in[0], &in[1]);
        }
        wm_dc_set_font(&w->font);
        int16_t lh = list_line_h(w), y = in[1], cw = (int16_t)(w->w - in[0]), ch = (int16_t)(w->h - in[1]);
        for (uint32_t i = b->top; i < b->n && y < ch; i++, y = (int16_t)(y + lh)) {
            if (b->lines[i].selected) {
                const void *part = wm_psui_part(b->focus ? 0x4c : 0x00);
                if (part) wm_dc_part(dc, part, 0xffff, in[0], y, (int16_t)(cw - in[0]), lh, 0xff);
            }
            wm_dc_set_colour(w->font.colour);
            wm_dc_set_format(0);
            int16_t pen[2] = { (int16_t)(in[0] + 3), (int16_t)(y + wm_dc_ascending()) }, r[4] = { in[0], y, cw, (int16_t)(y + lh) };
            wm_dc_text(dc, r, pen, b->lines[i].text, NULL);
        }
        wm_dc_end(dc);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (!b || !enabled(w)) return 0;
        set_focus(w);
        b->pressed = true;
        b->twice = false;
        return 0;
    case 0x21:      /* a double click */
        if (b) b->twice = true;
        return 0;
    case WM_LBUTTONUP: {
        if (!b || !b->pressed) return 0;
        b->pressed = false;
        int16_t r[4];
        screen_rect(w, r);
        const void *frame = wm_psui_part(0x44);
        int16_t in[2] = { 0, 0 };
        if (frame) wm_part_size(frame, &in[0], &in[1]);
        int16_t y = (int16_t)((int16_t)p1 - r[1] - in[1]);
        if (y < 0) return 0;
        uint32_t i = b->top + (uint32_t)(y / list_line_h(w));
        if (i >= b->n) return 0;
        for (uint32_t k = 0; k < b->n; k++) b->lines[k].selected = k == i;
        wm_send(w->parent, WM_COMMAND, (uintptr_t)w->id | (b->twice ? 0x20000u : 0x10000u), (uintptr_t)w);
        return 0;
    }
    case 0x45: {    /* add a string */
        if (!b || !p2) return (uintptr_t)-1;
        if (b->n == b->cap) {
            uint32_t cap = b->cap ? b->cap * 2 : 16;
            ListLine *l = realloc(b->lines, cap * sizeof *l);
            if (!l) return (uintptr_t)-1;
            b->lines = l, b->cap = cap;
        }
        const char *s = (const char *)p2;
        uint32_t at = b->n;
        if (w->style & 0x20000)
            for (at = 0; at < b->n && strcmp(s, b->lines[at].text) >= 0; at++) {}
        memmove(b->lines + at + 1, b->lines + at, (b->n - at) * sizeof *b->lines);
        size_t len = strlen(s) + 1;
        b->lines[at].text = malloc(len);
        if (b->lines[at].text) memcpy(b->lines[at].text, s, len);
        b->lines[at].data = 0;
        b->lines[at].selected = false;
        b->n++;
        return at;
    }
    case 0x47: {    /* a line's data, -1 the selected one */
        if (!b) return (uintptr_t)-1;
        uint32_t i = (uint32_t)p1;
        if (i == 0xffffffffu)
            for (i = 0; i < b->n && !b->lines[i].selected; i++) {}
        return i < b->n ? b->lines[i].data : (uintptr_t)-1;
    }
    case 0x48: return b ? b->n : (uintptr_t)-1;
    case 0x51:
        if (!b || (uint32_t)p1 >= b->n) return (uintptr_t)-1;
        b->lines[p1].data = (uint32_t)p2;
        return 0;
    case 0x53: {
        if (!b) return (uintptr_t)-1;
        for (uint32_t k = 0; k < b->n; k++) b->lines[k].selected = false;
        if ((uint32_t)p1 == 0xffffffffu) return 0;
        if ((uint32_t)p1 >= b->n) return (uintptr_t)-1;
        b->lines[p1].selected = true;
        list_show(w, b, (uint32_t)p1);
        return 0;
    }
    }
    return default_proc(w, msg, p1, p2);
}

/* The text (class 20, 0x4731f0): drawn by its dialog (style 0x800000: WM_DRAWITEM to the parent), else its
   title in its font. */
static uintptr_t text_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    if (msg != WM_PAINT) return default_proc(w, msg, p1, p2);
    WmDc *dc = wm_dc_begin(w);
    if (!dc) return 0;
    if (w->style & WM_TEXT_OWNERDRAW) {
        WmDrawItem item = { dc, w, w->id };
        wm_send(w->parent, WM_DRAWITEM, w->id, (uintptr_t)&item);
    } else {
        int16_t pen[2] = { 0, wm_dc_ascending() };
        wm_dc_set_font(&w->font);
        pen[1] = wm_dc_ascending();
        if (!enabled(w)) wm_dc_set_colour(0xff808080u);
        uint16_t fmt = 0;
        if (w->style & 0x20000) fmt |= 2;
        if (w->style & 0x40000) fmt |= 4;
        if (w->style & 0x100000) fmt |= 8;
        wm_dc_set_format(fmt);
        int16_t r[4] = { 0, 0, w->client[2], w->client[3] };
        wm_dc_text(dc, r, pen, w->title, NULL);
    }
    wm_dc_end(dc);
    return 0;
}

void wm_set_desktop_size(int w, int h)
{
    if (desktop) desktop->w = (int16_t)w, desktop->h = (int16_t)h, desktop->client[2] = (int16_t)w, desktop->client[3] = (int16_t)h;
}

void wm_initialize(int w, int h)    /* WMrInitialize 0x475ad0 */
{
    if (desktop) {
        wm_set_desktop_size(w, h);
        return;
    }
    class_register(1, desktop_proc);
    class_register(3, box_proc);
    class_register(4, button_proc);
    class_register(6, dialog_proc);
    class_register(8, list_proc);
    class_register(11, picture_proc);
    class_register(20, text_proc);
    desktop = wm_window_new(1, WM_STYLE_VISIBLE, 0, 0, 0, 0, (int16_t)w, (int16_t)h, NULL, 0);
    /* WMrCursor_Get (0x4687d0): type 1 in the cursor list ("cursor_list", 0x53c304: +0x14 the count, then
       8 bytes an entry, the type and the PSpc) */
    const uint8_t *cl = tm_instance_get(TM_TAG("WMCL"), exe_str(0x53c304));
    for (uint32_t i = 0; cl && i < rd32(cl + 0x14); i++)
        if (rd32(cl + 0x18 + 8 * i) == 1) cursor = tm_link(cl + 0x1c + 8 * i);
}
