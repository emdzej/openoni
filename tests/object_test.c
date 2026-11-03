/* The level's objects (OPENONI_DATA=<dir>, default ./game; skipped without it): every level's starting
   objects load with their geometry, and on level 1 a script's env_setanim and env_anim play the
   forklifts as the warehouse's cutscene asks (the commands through the script engine). */
#include "exe.h"
#include "object.h"
#include "sl.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const int levels[] = { 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };

static int find(uint32_t id)
{
    for (int i = 0; i < object_count(); i++) if (object_id(i) == id) return i;
    return -1;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("object_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "object_test: %s\n", err);
        return 1;
    }
    int failed = 0, total = 0;
    for (size_t l = 0; l < sizeof levels / sizeof levels[0]; l++) {
        if (!tm_level_load(levels[l], err, sizeof err)) {
            fprintf(stderr, "object_test: level %d: %s\n", levels[l], err);
            failed++;
            continue;
        }
        int elements = object_load(), n = object_create(0, UINT32_MAX), geoms = 0, bad = 0;
        for (int i = 0; i < n; i++) {
            float m[12];
            object_matrix(i, m);
            for (int k = 0; k < 12; k++) bad += !isfinite(m[k]);
            for (uint32_t k = 0; k < object_geometry_count(i); k++) geoms += object_geometry(i, k) != NULL;
        }
        printf("level%d: elements=%d objects=%d geometries=%d\n", levels[l], elements, n, geoms);
        if (bad) {
            fprintf(stderr, "object_test: level %d: %d bad matrices\n", levels[l], bad);
            failed++;
        }
        total += n;
        if (levels[l] != 1) tm_level_unload(levels[l]);
        else {
            /* the cutscene's "env_setanim 101 TwoForkLift01" then "env_anim 101 109" */
            sl_reset();
            object_register_commands();
            object_load();
            const char *src = "func void go(void)\n{\n\tobj_create 101 109\n\tenv_setanim 101 TwoForkLift01\n\tenv_anim 101 109\n}\n";
            int i = 0;
            if (!sl_load_text("test.bsl", src, err, sizeof err) || !sl_call("go", 0, NULL)) {
                fprintf(stderr, "object_test: %s\n", err);
                failed++;
            } else {
                float before[12], after[12];
                sl_update();
                i = find(101);
                object_matrix(i, before);
                uint32_t t = 0;
                for (; t < 600; t++) object_update(t + 1);
                object_matrix(i, after);
                float moved = 0;
                for (int k = 9; k < 12; k++) moved += fabsf(after[k] - before[k]);
                printf("object 101 %s: moved %.1f in %u ticks\n", object_name(i), moved, (unsigned)t);
                if (i < 0 || moved <= 0) failed++;
            }
            tm_level_unload(1);
        }
    }
    printf("all: objects=%d\n", total);
    object_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("object_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
