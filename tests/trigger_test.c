/* Trigger volumes (OPENONI_DATA=<dir>, default ./game; skipped without it): every level's volumes load,
   with ids and names, and on level 1 a character walked into one and out again takes its slot and gives it
   back. */
#include "exe.h"
#include "sl.h"
#include "tm.h"
#include "trigger.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("trigger_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "trigger_test: %s\n", err);
        return 1;
    }
    int failed = 0, total = 0;
    static const int levels[] = { 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
    for (size_t l = 0; l < sizeof levels / sizeof levels[0]; l++) {
        if (!tm_level_load(levels[l], err, sizeof err)) {
            fprintf(stderr, "trigger_test: level %d: %s\n", levels[l], err);
            failed++;
            continue;
        }
        int n = trigger_load(), named = 0;
        uint32_t hash = 2166136261u;
        for (int i = 0; i < n; i++) {
            named += trigger_name(i)[0] != 0;
            for (const char *s = trigger_name(i); *s; s++) hash = (hash ^ (uint8_t)*s) * 16777619u;
            hash = (hash ^ trigger_id(i)) * 16777619u;
            hash = (hash ^ trigger_flags(i)) * 16777619u;
        }
        printf("level%d: volumes=%d named=%d hash=%08x\n", levels[l], n, named, hash);
        total += n;
        tm_level_unload(levels[l]);
    }
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "trigger_test: level 1: %s\n", err);
        return 1;
    }
    trigger_load();
    /* level 1 is loaded: walk a character into a volume and out again */
    int n = trigger_count();
    if (n <= 0) {
        fprintf(stderr, "trigger_test: level 1 has no volumes\n");
        failed++;
    } else {
        sl_reset();
        trigger_register_commands();
        if (!sl_load_folder("GameDataFolder/IGMD/global", err, sizeof err)) fprintf(stderr, "trigger_test: %s\n", err);
        /* a volume's centre, found by searching a grid of its points: the first volume any point falls in */
        int v = -1;
        float in[3] = { 0 };
        for (int x = -2000; x <= 2000 && v < 0; x += 8)
            for (int z = -2000; z <= 2000 && v < 0; z += 8)
                for (int y = -200; y <= 600 && v < 0; y += 8) {
                    float p[3] = { (float)x, (float)y, (float)z };
                    for (int i = 0; i < n; i++) if (trigger_contains(i, p, p[1], p[1] + 18.0f)) { v = i; memcpy(in, p, sizeof in); break; }
                }
        if (v < 0) {
            fprintf(stderr, "trigger_test: no volume found on the grid\n");
            failed++;
        } else {
            TriggerActor a = { .team = 0, .player = true, .alive = true, .name = "konoko",
                               .bottom = in[1], .top = in[1] + 18.0f };
            float away[3] = { in[0] + 5000, in[1], in[2] };
            memcpy(a.pos, away, sizeof a.pos);
            trigger_update(&a, 1);
            bool before = a.inside[0] == trigger_id(v);
            memcpy(a.pos, in, sizeof a.pos);
            trigger_update(&a, 1);
            bool during = false;
            for (int k = 0; k < 4; k++) during |= a.inside[k] == trigger_id(v);
            memcpy(a.pos, away, sizeof a.pos);
            trigger_update(&a, 1);
            bool after = false;
            for (int k = 0; k < 4; k++) after |= a.inside[k] == trigger_id(v);
            printf("level1: walked through %s (id %u, flags %#x): before=%d during=%d after=%d\n", trigger_name(v),
                   (unsigned)trigger_id(v), (unsigned)trigger_flags(v), before, during, after);
            /* disabled volumes and other teams don't take the character in */
            if (!(trigger_flags(v) & TRIGGER_DISABLED) && (before || !during || after)) failed++;
        }
    }
    printf("all: volumes=%d\n", total);
    trigger_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("trigger_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
