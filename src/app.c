/* The application (app.h). Step 1 of docs/re/port-plan.md: the start of ONiMain 0x4d3280 as far as the
   port has come (the exe check), then a test screen that runs its own loop the way the original's loops
   do: draw, plat_wait_frame, until Esc. The bars are the level files' sizes, read through the file layer. */
#include "app.h"
#include "exe.h"
#include "platform.h"
#include "vfs.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>

enum { DIK_ESCAPE = 0x01 };

/* The levels the CD installs, in the order of the level numbers. */
static const int LEVELS[] = {0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19};
enum { NLEVELS = sizeof LEVELS / sizeof *LEVELS };

static void test_screen(void)
{
    int64_t size[NLEVELS * 2], biggest = 1;
    for (int i = 0; i < NLEVELS * 2; i++) {
        char path[64];
        snprintf(path, sizeof path, "GameDataFolder/level%d_Final.%s", LEVELS[i / 2], i & 1 ? "raw" : "dat");
        size[i] = vfs_size(path);
        if (size[i] > biggest) biggest = size[i];
    }
    for (;;) {
        uint8_t keys[64];
        int n = plat_key_presses(keys, (int)sizeof keys);
        for (int i = 0; i < n; i++)
            if (keys[i] == DIK_ESCAPE) return;
        int w, h;
        plat_display_size(&w, &h);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        int bw = w / (NLEVELS * 2 + 1);
        for (int i = 0; i < NLEVELS * 2; i++) {
            int bh = size[i] > 0 ? (int)((int64_t)(h * 3 / 4) * size[i] / biggest) : 4;
            glScissor(bw / 2 + i * bw, h / 8, bw - 2, bh > 0 ? bh : 1);
            if (size[i] < 0) glClearColor(0.8f, 0.1f, 0.1f, 1.0f);           /* missing */
            else if (i & 1) glClearColor(0.71f, 0.33f, 0.16f, 1.0f);         /* .raw */
            else glClearColor(0.85f, 0.85f, 0.80f, 1.0f);                    /* .dat */
            glClear(GL_COLOR_BUFFER_BIT);
        }
        plat_wait_frame();
    }
}

int app_main(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: %s", err);
        plat_log(m);
        return 1;
    }
    plat_log("OpenOni: Oni.exe v1.0 found");
    test_screen();
    plat_log("OpenOni: quit");
    return 0;
}

void app_exit(void) {}

void app_audio(float *stereo, unsigned frames) { memset(stereo, 0, (size_t)frames * 2 * sizeof *stereo); }
