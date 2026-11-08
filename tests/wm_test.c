/* The window manager's list box and its scroll bar, on the game's Load Game dialog (151, "psui_oniUI"):
   its list (id 100, style 0x90000) gets a scroll bar (class 16) along its client's right edge and is
   made as tall as its whole lines; filled past them, a selection, the scroll bar's messages and a press
   on its down arrow scroll it (the line under a click on its first row tells which line is on top), and
   its keys move the selection. */
#include "exe.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"
#include "platform.h"
#include "wm.h"

#include <math.h>
#include <stdio.h>

/* The platform the window manager asks (no display here). */
void plat_wait_frame(void) {}
uint32_t plat_frames(void) { return 0; }
void plat_log(const char *msg) { (void)msg; }
void plat_display_size(int *w, int *h) { *w = 640, *h = 480; }
bool plat_frame_shown(void) { return false; }
bool plat_keys(uint8_t keys[KEY_COUNT]) { (void)keys; return false; }
int plat_key_presses(uint8_t *codes, int cap) { (void)codes, (void)cap; return 0; }
bool plat_mouse(PlatMouse *m) { (void)m; return false; }

static void origin(const WmWindow *w, int *x, int *y)
{
    if (!w->parent) {
        *x = w->x, *y = w->y;
        return;
    }
    origin(w->parent, x, y);
    *x += w->parent->client[0] + w->x, *y += w->parent->client[1] + w->y;
}

static uintptr_t point(const WmWindow *w, int dx, int dy)
{
    int x, y;
    origin(w, &x, &y);
    x += w->client[0] + dx, y += w->client[1] + dy;
    return (uintptr_t)((uint32_t)(uint16_t)x << 16 | (uint16_t)y);
}

/* The data of the line under a click on the list's first row (its top line). */
static uint32_t top_line(WmWindow *list)
{
    wm_send(list, WM_LBUTTONDOWN, point(list, 10, 2), 1);
    wm_send(list, WM_LBUTTONUP, point(list, 10, 2), 0);
    return (uint32_t)wm_send(list, 0x47, 0xffffffffu, 0);
}

int main(void)
{
    char err[512];
    if (!vfs_mount_default()) {
        puts("wm_test: no game data, skipped");
        return 0;
    }
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        printf("wm_test: %s\n", err);
        return 1;
    }
    wm_initialize(640, 480);
    wm_psui_set_active(wm_psui_get(exe_str(0x54c898)));
    WmWindow *d = wm_dialog_new(151, NULL, 0), *list = d ? wm_item(d, 100) : NULL;
    WmWindow *bar = list ? list->child : NULL;
    if (!list || !bar || bar->cls->number != 16) {
        puts("wm_test: no list box with a scroll bar");
        return 1;
    }
    int failed = 0;
    for (uint32_t i = 0; i < 40; i++) {
        char s[16];
        snprintf(s, sizeof s, "line %u", (unsigned)i);
        uintptr_t k = wm_send(list, 0x45, 0, (uintptr_t)s);
        wm_send(list, 0x51, k, i);
    }
    /* (the list isn't sorted, style 0x20000: a line's data is its index) */
    uint32_t first = top_line(list);
    wm_send(list, 0x53, 39, 0);                 /* the last line selected: scrolled into view */
    uint32_t after_select = top_line(list);
    wm_send(list, 0x58, (uintptr_t)10 | 0 << 16, 0);   /* a line back */
    uint32_t line_back = top_line(list);
    wm_send(list, 0x58, (uintptr_t)10 | 4u << 16, 0);  /* to the position 0 */
    uint32_t to_start = top_line(list);
    /* the down arrow (the bar's bottom) pressed and let go */
    int16_t arrow_y = (int16_t)(bar->h - 3);
    uintptr_t p = point(bar, bar->w / 2, arrow_y);
    wm_send(bar, WM_LBUTTONDOWN, p, 1);
    wm_send(bar, WM_LBUTTONUP, p, 0);
    uint32_t arrow = top_line(list);
    /* the keys: the selection moved from line 5, then the lines typed */
    uint32_t keys[6];
    const uint32_t codes[6] = { 0xb2, 0xaf, 0xae, 0xb0, 0xac, 0xab };
    wm_send(list, 0x53, 5, 0);
    for (int i = 0; i < 6; i++) {
        wm_send(list, WM_KEYDOWN, codes[i], 0);
        keys[i] = (uint32_t)wm_send(list, 0x47, 0xffffffffu, 0);
    }
    for (const char *t = "LINE 3"; *t; t++) wm_send(list, WM_KEYDOWN, (uint8_t)*t, 0);
    uint32_t typed = (uint32_t)wm_send(list, 0x47, 0xffffffffu, 0);
    printf("list %dx%d client %d,%d %dx%d; bar at %d,%d %dx%d\n", list->w, list->h, list->client[0], list->client[1],
           list->client[2], list->client[3], bar->x, bar->y, bar->w, bar->h);
    printf("tops: first %u, after selecting the last %u, a line back %u, at 0 %u, the down arrow %u\n",
           (unsigned)first, (unsigned)after_select, (unsigned)line_back, (unsigned)to_start, (unsigned)arrow);
    int lh = (int)after_select;        /* the top once the last line is in view: the lines that can be scrolled */
    int visible = 40 - lh;
    /* the scroll bar's units are the count's: a step is a fortieth of the list, the top its position by
       the lines that can be scrolled over the count, rounded */
    float frac = (float)((40.0 - visible) / 40.0);
    int pos = (int)trunc((double)lh / (double)frac);
    int back = (int)trunc((double)(pos - 1) * (double)frac + 0.5), down = (int)trunc((double)frac + 0.5);
    printf("visible %d, position %d: a line back to %d, the arrow to %d\n", visible, pos, back, down);
    if (first != 0 || visible <= 1 || visible >= 40) failed |= 1;
    if ((int)line_back != back || to_start != 0 || (int)arrow != down) failed |= 2;
    printf("keys: down %u, page down %u, end %u, up %u, page up %u, home %u; typed %u\n", (unsigned)keys[0],
           (unsigned)keys[1], (unsigned)keys[2], (unsigned)keys[3], (unsigned)keys[4], (unsigned)keys[5], (unsigned)typed);
    if (keys[0] != 6 || keys[1] != (uint32_t)(6 + visible) || keys[2] != 39 || keys[3] != 38 ||
        keys[4] != (uint32_t)(38 - visible) || keys[5] != 0 || typed != 3)
        failed |= 16;
    if (list->client[3] % visible) failed |= 4;  /* whole lines */
    if (bar->h != list->client[3] + 2 || bar->y != -1) failed |= 8;
    printf("wm_test: %s\n", failed ? "FAILED" : "ok");
    return failed ? 1 : 0;
}
