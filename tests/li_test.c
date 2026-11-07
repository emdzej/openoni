/* The input layer's bindings (OPENONI_DATA=<dir>, default ./game; skipped without it): with no
   key_config.txt the exe's default lines are written (unbindall first, "bind w to forward" next) and
   bound: W forward, the second set's P forward too, F punch, the left mouse button fire1, the mouse's x
   the aim; escape always the escape action. A config's line rebinds a key (one action a key), "to" is
   needed, unknown actions and keys do nothing, and "bind" to console works (the action is in the
   table). The keyboard and the mouse are this test's own (the plat_* below). */
#include "exe.h"
#include "li.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t held[KEY_COUNT], taps[8];
static int ntaps, failed;

void plat_log(const char *msg) { (void)msg; }
bool plat_keys(uint8_t keys[KEY_COUNT])
{
    memcpy(keys, held, KEY_COUNT);
    return true;
}
int plat_key_presses(uint8_t *codes, int cap)
{
    int n = ntaps < cap ? ntaps : cap;
    memcpy(codes, taps, (size_t)n);
    return n;
}

static char *saved;
static size_t saved_n;
static uint8_t *load_none(const char *name, size_t *size) { (void)name, (void)size; return NULL; }
static bool store(const char *name, const void *data, size_t size)
{
    if (strcmp(name, "key_config.txt")) return false;
    free(saved);
    saved = malloc(size + 1);
    memcpy(saved, data, size), saved[size] = 0, saved_n = size;
    return true;
}

static void check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "li_test: %s\n", what);
        failed++;
    }
}

static LiActions frame(const PlatMouse *m)
{
    LiActions a;
    li_actions(&a, m, false);
    return a;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("li_test: no game folder, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { fprintf(stderr, "li_test: %s\n", err); return 1; }

    li_config_load(load_none, store, load_none);
    check(saved && !strncmp(saved, "unbindall\n\nbind w to forward\n", 29), "the defaults' first lines");
    check(saved && strstr(saved, "bind enter to walk\n\n") && saved[saved_n - 1] == '\n', "the defaults' last line");

    const uint64_t forward = (uint64_t)1 << 21, punch = (uint64_t)1 << 29, fire1 = (uint64_t)1 << 38;
    memset(held, 0, sizeof held);
    held[0x11] = 1;     /* W */
    LiActions a = frame(NULL);
    check(a.held == forward && a.down == forward, "W: forward, gone down");
    a = frame(NULL);
    check(a.held == forward && !a.down, "W held: not down again");
    held[0x11] = 0, held[0x19] = 1;     /* P, the second set */
    a = frame(NULL);
    check(a.held == forward, "P: forward");
    held[0x19] = 0;
    taps[0] = 0x21, ntaps = 1;          /* F tapped within the frame */
    a = frame(NULL);
    check(!a.held && a.down == punch, "F tapped: punch down");
    ntaps = 0;
    taps[0] = 0x01, ntaps = 1;          /* Escape */
    a = frame(NULL);
    check(a.down == 1, "escape");
    ntaps = 0;

    PlatMouse m = { .dx = 8, .dy = -4, .buttons = MOUSE_LEFT, .pressed = MOUSE_LEFT, .captured = true };
    a = frame(&m);
    check(a.held == fire1 && a.aim_lr == 2.0f && a.aim_ud == -1.0f, "the mouse: fire1, the aim by 0.25");
    li_actions(&a, &m, true);
    check(a.aim_ud == 1.0f, "the inverted mouse");

    const char cfg[] = "# mine\nbind w to kick\nbind x forward\nbind y to nothing\nbind nokey to jump\n"
                       "bind backslash to console\r\n";
    li_config_run(cfg, sizeof cfg - 1);
    m = (PlatMouse){ 0 };
    held[0x11] = 1;
    a = frame(NULL);
    check(a.held == (uint64_t)1 << 30, "W rebound to kick, no longer forward");
    held[0x11] = 0, held[0x2d] = 1;     /* X: the line without "to" */
    a = frame(NULL);
    check(!a.held, "a bind without to does nothing");
    held[0x2d] = 0, held[0x2b] = 1;     /* backslash */
    a = frame(NULL);
    check(a.held == 2, "backslash to console (bit 1)");
    held[0x2b] = 0;

    li_config_run("unbindall\n", 10);
    held[0x19] = 1;
    a = frame(NULL);
    check(!a.held, "unbindall: P unbound");
    held[0x19] = 0;
    taps[0] = 0x01, ntaps = 1;
    a = frame(NULL);
    check(a.down == 1, "unbindall: escape still bound");

    printf("li_test: %s\n", failed ? "FAILED" : "ok");
    free(saved);
    exe_free();
    vfs_unmount();
    return failed != 0;
}
