/* The screen state the scripts drive (OPENONI_DATA=<dir>, default ./game; skipped without it): a fade
   out and in, the letterbox going on and off, and the training's welcome message found, shown with its
   timer and removed, through the script commands. */
#include "exe.h"
#include "sl.h"
#include "tm.h"
#include "ui.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void ticks(int n) { for (int i = 0; i < n; i++) { sl_update(); ui_update(); } }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("ui_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "ui_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    sl_reset();
    ui_reset();
    ui_register_commands();
    const char *src =
        "func void go(void)\n{\n\tfade_out 0 0 0 30\n\tsleep 60\n\tfade_in 30\n\tletterbox 1\n\tsleep 120\n"
        "\tletterbox 0\n\tmessage xwelcome 100\n}\n";
    if (!sl_load_text("ui.bsl", src, err, sizeof err) || !sl_call("go", 0, NULL)) {
        fprintf(stderr, "ui_test: %s\n", err);
        return 1;
    }
    float f[4] = { 0 };
    ticks(15);
    bool half = ui_fade(f) && fabsf(f[3] - 0.5f) < 0.05f;
    ticks(30);
    bool full = ui_fade(f) && f[3] == 1.0f;
    ticks(30);                      /* fade_in started at 61: 15 ticks in */
    bool coming = ui_fade(f) && f[3] < 1.0f && f[3] > 0.0f;
    ticks(30);
    bool gone = !ui_fade(f);
    float lb = ui_letterbox();      /* 45 ticks in: 56.25 */
    ticks(60);
    float lb2 = ui_letterbox();     /* at the top */
    printf("fade: half=%d full=%d coming=%d gone=%d; letterbox %.2f then %.2f\n", half, full, coming, gone, lb, lb2);
    if (!half || !full || !coming || !gone || lb2 != 75.0f) failed++;
    ticks(30);                      /* letterbox 0 and the message at 181 */
    const UiLine *l = ui_area_line(UI_AREA_MESSAGE, 0, NULL);
    printf("message: %s\n", l ? l->text : "(none)");
    if (!l) failed++;
    ticks(120);
    printf("after its timer: %d lines\n", ui_area_count(UI_AREA_MESSAGE));
    if (ui_area_count(UI_AREA_MESSAGE)) failed++;
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("ui_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
