/* Every object animation of every level (OPENONI_DATA=<dir>, default ./game; skipped without it): the same
   summary lines as tools/onioban.py, every frame of every one turned into a matrix, a door and a forklift
   printed as `onioban.py game levelN --matrix NAME FRAME` does, and the playing of one through its frames.
   OPENONI_OBAN_SUMMARY=<hash> checks the "all" hash. */
#include "crc32.h"
#include "exe.h"
#include "oban.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

static void print_matrix(const char *name, int frame)
{
    float m[12];
    const void *a = tm_instance_get(OBAN_TAG, name);
    if (!a || !oban_matrix(a, frame, false, m)) { printf("%s: none\n", name); return; }
    printf("matrix:");
    for (int k = 0; k < 12; k++) printf(" %8.3f", fabsf(m[k]) < 0.0005f ? 0.0f : m[k]);   /* no "-0.000" */
    printf("\n");
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("oban_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) {
        fprintf(stderr, "oban_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0, frames = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        if (!tm_initialize(err, sizeof err) || !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "oban_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        uint32_t h = 0, count = 0;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *ti = tm_file_instance(f, i);
            if (ti->tmpl->tag != OBAN_TAG || !ti->data) continue;
            ObanInfo in;
            oban_info(ti->data, &in);
            const uint8_t *d = ti->data;
            uint32_t c = crc32_update(0, d + OBAN_START, (size_t)(OBAN_KEYS - OBAN_START) + (size_t)OBAN_KEY_SIZE * in.keys);
            char line[160];
            snprintf(line, sizeof line, "%u %s %u %u %u %u %08x\n", (unsigned)i, ti->name ? ti->name : "-",
                     (unsigned)in.flags, in.frames, in.door_open, in.keys, (unsigned)c);
            h = crc32_update(h, line, strlen(line));
            count++;
            for (int fr = 0; fr < in.frames; fr++) {
                float m[12];
                bool ok = oban_matrix(d, fr, false, m);
                for (int e = 0; ok && e < 12; e++) ok = isfinite(m[e]);
                if (!ok) {
                    fprintf(stderr, "oban_test: level%d %s frame %d: no matrix\n", lv, ti->name, fr);
                    failed++;
                    break;
                }
                frames++;
            }
        }
        printf("level%d: animations=%u hash=%08x\n", lv, (unsigned)count, (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
        if (lv == 1) {
            print_matrix("ForkLift01", 37);
            /* played: once through and stopped at the last frame */
            ObanPlayer p;
            oban_player_init(&p, tm_instance_get(OBAN_TAG, "ForkLift01"));
            oban_start(&p, 0);
            int ticks = 0;
            while (!oban_update(&p, (uint32_t)++ticks) && ticks < 10000) {}
            printf("ForkLift01: stopped after %d ticks at frame %d\n", ticks, p.frame);
        }
        if (lv == 2) print_matrix("door_10", 30);
        tm_terminate();
    }
    printf("all: hash=%08x (%u frames)\n", (unsigned)all, (unsigned)frames);
    const char *want = getenv("OPENONI_OBAN_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "oban_test: the all hash differs from OPENONI_OBAN_SUMMARY\n");
        failed++;
    }
    exe_free();
    vfs_unmount();
    printf("oban_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
