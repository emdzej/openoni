/* The window manager (WM): Bungie's GUI toolkit under the menus, the pause screen and the in-game text
   consoles. Windows of registered classes in a tree, messages to their procedures, dialogs made from WMDD
   instances, and the original's modal loop. Drawing goes through a drawing context (DC) and the part
   specs (PSpc, PSUI) to the renderer's operations (WmDrawOps), so the core has no renderer.
   docs/re/wm.md has the layouts and messages.

   Not ported yet: the frames' title bars, close, zoom and grow boxes and their dragging (0x474180), the
   timers' users, and the classes other than the dialog, box, button and text. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Messages (the numbers are the original's). */
enum {
    WM_PRECREATE = 0x01, WM_HITTEST = 0x03, WM_NCPAINT = 0x04, WM_NCMOUSEMOVE = 0x05, WM_NCLBUTTONDOWN = 0x06,
    WM_NCLBUTTONUP = 0x07, WM_NCACTIVATE = 0x0f, WM_CLIENTRECT = 0x11, WM_CREATE = 0x12, WM_DESTROY = 0x13,
    WM_CLOSE = 0x14, WM_COMMAND = 0x17, WM_KEYDOWN = 0x1c, WM_KEYUP = 0x1d, WM_MOUSEMOVE = 0x1e,
    WM_LBUTTONDOWN = 0x1f, WM_LBUTTONUP = 0x20, WM_ACTIVATE = 0x28, WM_SETFOCUS = 0x29, WM_KILLFOCUS = 0x2a,
    WM_PAINT = 0x2b, WM_CAPTURELOST = 0x30, WM_PARENTNOTIFY = 0x31, WM_RESOLUTION = 0x34, WM_FONTCHANGED = 0x35,
    WM_DRAWITEM = 0x36, WM_INITDIALOG = 0x3d, WM_WANTKEYS = 0x38, WM_HILITE = 0x3a,
    WM_HIT_CLIENT = 0x8000,
};

/* The flags (+0x18, WMDD items' +0x104) and the classes' styles (+0x1c, the items' +0x108). */
enum {
    WM_STYLE_VISIBLE = 0x1, WM_STYLE_DISABLED = 0x6, WM_STYLE_CHILD = 0x2000,
    WM_DIALOG_CENTRED = 0x10000,                        /* a dialog's flags */
    WM_BUTTON_PART = 0x10000, WM_BUTTON_TITLE = 0x20000, WM_BUTTON_DEFAULT = 0x100000,
    WM_TEXT_OWNERDRAW = 0x800000,
};

/* A window's font (+0x148): the family (TSFF), style, colour (ARGB), a word, the size. */
typedef struct { const void *family; uint32_t style, colour; uint16_t unknown, size; } WmFont;

typedef struct WmWindow WmWindow;
typedef uintptr_t (*WmProc)(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);
typedef struct { uint16_t number; WmProc proc; } WmClass;

struct WmWindow {
    WmWindow *prev, *next, *parent, *owner, *child;     /* +0x00..0x10 */
    const WmClass *cls;                                  /* +0x14 */
    uint32_t flags, style;                               /* +0x18 (visible, disabled, child), +0x1c (the class's) */
    uint16_t id;                                         /* +0x20 */
    uint32_t handle;                                     /* +0x24 */
    int16_t x, y, w, h;                                  /* +0x28: in the parent */
    int16_t client[4];                                   /* +0x30: the client area, from WM_CLIENTRECT */
    char title[256];                                     /* +0x38 */
    int16_t title_w, title_h;                            /* +0x140 */
    uintptr_t data;                                      /* the class's long (WMrWindow_GetLong) */
    WmFont font;                                         /* +0x148 */
};

/* A dialog's item's paint (WM_DRAWITEM's p2): the DC, the item, its id. */
typedef struct WmDc WmDc;
typedef struct { WmDc *dc; WmWindow *item; uint16_t id; } WmDrawItem;

/* What the renderer does for the WM: a textured rectangle (u, v over the texture; argb the tint) and a
   run of glyphs at a pen on the baseline, both in the desktop's pixels and clipped to clip (x0, y0, x1,
   y1). */
typedef struct {
    void (*sprite)(const void *txmp, const float r[4], const float uv[4], uint32_t argb);
    void (*glyphs)(const void *font, float x, float y, const char *s, size_t n, uint32_t argb, const int16_t clip[4]);
} WmDrawOps;
void wm_set_draw_ops(const WmDrawOps *ops);

/* WMrInitialize (0x475ad0): the classes and the desktop, w by h. */
void wm_initialize(int w, int h);
void wm_set_desktop_size(int w, int h);
WmWindow *wm_desktop(void);

WmWindow *wm_window_new(uint16_t cls, uint32_t flags, uint32_t style, uint16_t id, int16_t x, int16_t y,
                        int16_t w, int16_t h, WmWindow *parent, uintptr_t param);
void wm_window_delete(WmWindow *w);
bool wm_window_valid(const WmWindow *w);
uintptr_t wm_send(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);
void wm_post(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2);
void wm_set_visible(WmWindow *w, bool on);
void wm_set_location(WmWindow *w, int16_t x, int16_t y);
void wm_set_font(WmWindow *w, const WmFont *f);
WmWindow *wm_item(WmWindow *dialog, uint16_t id);

/* The frame's parts: the windows drawn (WMrDisplay 0x476140), the input taken (WMrUpdate 0x477070). The
   input: the mouse in the desktop's pixels, and the keys pressed as the LI's codes. */
void wm_display(void);
void wm_update(void);
void wm_key(uint8_t code);
/* WMrActivate 0x476110 / WMrDeactivate 0x476410: the WM takes the input, its cursor shown, or not. */
void wm_take_input(bool on);

/* WMrDialog_ModalBegin 0x4689d0: the dialog of that id run to its end; its result. The callback draws the
   game behind it (WMrDialog_SetDrawCallback 0x469330); the frame hook is called once a frame (the port's
   route). */
typedef bool (*WmDialogProc)(WmWindow *dialog, uint32_t msg, uintptr_t p1, uintptr_t p2);
uint32_t wm_dialog_modal(uint16_t id, WmDialogProc proc, uintptr_t param);
void wm_dialog_end(WmWindow *dialog, uint32_t result);
uintptr_t wm_dialog_param(WmWindow *dialog);
void wm_set_draw_callback(void (*draw)(void));
void wm_set_frame_hook(void (*hook)(void));

/* The DC (0x4694b0, 0x469720) and its drawing. */
WmDc *wm_dc_begin(WmWindow *w);
void wm_dc_end(WmDc *dc);
void wm_dc_set_font(const WmFont *f);
void wm_dc_set_colour(uint32_t argb);
void wm_dc_set_format(uint16_t format);
int16_t wm_dc_ascending(void);
/* DCrDraw_String2 0x469eb0: the text in the rectangle (NULL: the window's client area) from the pen;
   out, if not NULL, the rectangle it took. */
void wm_dc_text(WmDc *dc, const int16_t rect[4], const int16_t pen[2], const char *s, int16_t out[4]);
/* The part spec's parts (a mask of 9, column by column) over x, y, w, h (PSrPartSpec_Draw 0x4698b0). */
void wm_dc_part(WmDc *dc, const void *pspc, uint16_t mask, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t alpha);
void wm_dc_texture(WmDc *dc, const void *txmp, int16_t x, int16_t y, uint8_t alpha);
const void *wm_psui_part(unsigned offset);
const void *wm_psui_active(void);
void wm_psui_set_active(const void *psui);
const void *wm_psui_get(const char *name);
void wm_part_size(const void *pspc, int16_t *w, int16_t *h);
