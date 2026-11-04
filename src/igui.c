/* The in-game UI's pages and text consoles: see igui.h. */
#include "igui.h"

#include "exe.h"
#include "sl.h"
#include "text.h"
#include "tm.h"
#include "wm.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The text states (0x5ed020: a stack of 5, 0x5ed0b0 the top): what a page's strings are drawn in. */
static WmFont states[5];
static int top;

/* 0x5028d0: a state pushed over the top one, with the fields the IGSt's or IGPG's header sets (its flags
   at +0x0e: 1 the family, 2 the style, 4 the colour, 8 the size), and made the DC's. */
static void push(const uint8_t *h)
{
    if (top + 1 == 5) return;
    states[top + 1] = states[top];
    top++;
    uint16_t set = rd16(h + 0x0e);
    if (set & 1) states[top].family = tm_link(h);
    if (set & 8) states[top].size = rd16(h + 0x0c);
    if (set & 2) states[top].style = rd32(h + 4);
    if (set & 4) states[top].colour = rd32(h + 8);
    wm_dc_set_font(&states[top]);
}

static void pop(void)
{
    if (top) top--;
    wm_dc_set_font(&states[top]);
}

/* The strings of an IGSA, each under the last, each in its own state. */
static void strings(WmDc *dc, const uint8_t *igsa, int16_t pen[2])
{
    pen[1] = (int16_t)(pen[1] + wm_dc_ascending());
    for (uint32_t i = 0; i < rd32(igsa + 0x14); i++) {
        const uint8_t *s = tm_link(igsa + 0x18 + 4 * i);
        if (!s) continue;
        push(s);
        int16_t out[4];
        wm_dc_text(dc, NULL, pen, (const char *)s + 0x10, out);
        pen[1] = (int16_t)(pen[1] + out[3] - out[1]);
        pop();
    }
}

/* ONiIGUI_Page_Paint 0x5029a0: the page in its header's state. The item 110 (0x6e): its image (a PSpc's
   middle column, the parts 0x49, as tall as its top left part; a texture centred at its size), 10 below
   it the first IGSA's strings; the item 111 (0x6f): the second IGSA's. */
void igui_page_paint(const void *igpg, const WmDrawItem *item)
{
    const uint8_t *pg = igpg;
    if (!pg) return;
    push(pg);
    int16_t pen[2] = { 0, 0 };
    if (item->id == 0x6e) {
        const void *image = tm_link(pg + 0x10);
        if (image) {
            const TmInstance *in = tm_handle_instance(tm_handle_of(image));
            uint32_t tag = in && in->tmpl ? in->tmpl->tag : 0;
            if (tag == TM_TAG("PSpc")) {
                int16_t h;
                wm_part_size(image, NULL, &h);
                wm_dc_part(item->dc, image, 0x49, 0, 0, item->item->w, h, 0xff);
                pen[1] = (int16_t)(pen[1] + h);
            } else if (tag == TM_TAG("TXMP")) {
                int16_t tw = (int16_t)rd16((const uint8_t *)image + 0x84), th = (int16_t)rd16((const uint8_t *)image + 0x86);
                wm_dc_texture(item->dc, image, (int16_t)((item->item->w - tw) >> 1), pen[1], 0xff);
                pen[1] = (int16_t)(pen[1] + th);
            }
            pen[1] = (int16_t)(pen[1] + 10);
        }
        const uint8_t *igsa = tm_link(pg + 0x14);
        if (igsa) strings(item->dc, igsa, pen);
    } else if (item->id == 0x6f) {
        const uint8_t *igsa = tm_link(pg + 0x18);
        if (igsa) strings(item->dc, igsa, pen);
    }
    pop();
}

/* The console's dialog's data (the TxtC, the page). */
typedef struct { const uint8_t *txtc; uint32_t page; } Console;

static uint32_t page_count(const Console *c)
{
    const uint8_t *igpa = tm_link(c->txtc);
    return igpa ? rd32(igpa + 0x14) : 0;
}

/* Next (102) while pages follow, Close (2) on the last. */
static void buttons(WmWindow *d, const Console *c)
{
    bool more = (int32_t)c->page < (int32_t)page_count(c) - 1;
    wm_set_visible(wm_item(d, 0x66), more);
    wm_set_visible(wm_item(d, 2), !more);
}

/* The text console's dialog (0x5049a0). */
static bool console_proc(WmWindow *d, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    Console *c = (Console *)wm_dialog_param(d);
    switch (msg) {
    case WM_INITDIALOG:
        if (!c) {
            wm_dialog_end(d, 0);
            return true;
        }
        c->page = 0;
        buttons(d, c);
        return true;
    case WM_COMMAND:
        if ((uint16_t)p1 == 2) wm_dialog_end(d, 0);
        else if ((uint16_t)p1 == 0x66 && ++c->page >= page_count(c)) {
            c->page = page_count(c) ? page_count(c) - 1 : 0;
            wm_dialog_end(d, 0);
        }
        buttons(d, c);
        if ((uint16_t)p1 == 2) wm_dialog_end(d, 0);
        return true;
    case WM_KEYDOWN: {
        /* Escape is Close's, space Next's (on the last page too, where it ends the console); others
           are a command of no item */
        WmWindow *b = p1 == 0x1b ? wm_item(d, 2) : p1 == 0x20 ? wm_item(d, 0x66) : NULL;
        wm_send(d, WM_COMMAND, (uintptr_t)(b ? b->id : 0) | 0x10000, (uintptr_t)b);
        return true;
    }
    case WM_DRAWITEM: {
        if (p1 != 0x6e) return false;
        const WmDrawItem *item = (const WmDrawItem *)p2;
        const uint8_t *igpa = tm_link(c->txtc);
        top = 0;
        states[0] = item->item->font;
        wm_dc_set_font(&states[0]);
        wm_dc_set_format(1);
        if (igpa && c->page < rd32(igpa + 0x14)) igui_page_paint(tm_link(igpa + 0x18 + 4 * c->page), item);
        return true;
    }
    }
    return false;
}

static void (*modal_begin)(void), (*modal_end)(void);
void igui_set_modal_hooks(void (*begin)(void), void (*end)(void)) { modal_begin = begin, modal_end = end; }

/* text_console name (ONrInGameUI_TextConsole_Display 0x504c30): the TxtC's pages in the text console's
   dialog (71), in the game's look (the PSUI "psui_oniUI", 0x54c898), the game's input off meanwhile. */
static bool cmd_text_console(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c != 1 || v[0].type != SL_STRING || !v[0].s) return true;
    Console con = { tm_instance_get(TM_TAG("TxtC"), v[0].s), 0 };
    if (!con.txtc) return true;
    const void *old = wm_psui_active();
    const void *ui = wm_psui_get(exe_str(0x54c898));
    if (ui) wm_psui_set_active(ui);
    if (modal_begin) modal_begin();     /* LIrMode_Set(0) */
    wm_take_input(true);
    wm_dialog_modal(0x47, console_proc, (uintptr_t)&con);
    wm_take_input(false);
    if (modal_end) modal_end();
    wm_psui_set_active(old);
    return true;
}

void igui_register_commands(void)
{
    sl_register("text_console", cmd_text_console, NULL);
}
