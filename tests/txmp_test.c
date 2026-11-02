/* Every texture of every level (OPENONI_DATA=<dir>, default ./game; skipped without it), decoded to RGBA
   bytes with all its mip levels: the same summary lines as tools/onitex.py. OPENONI_TXMP_SUMMARY=<hash>
   checks the "all" hash. */
#include "crc32.h"
#include "exe.h"
#include "im.h"
#include "tm.h"
#include "txmp.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("txmp_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !im_initialize(err, sizeof err) || !tm_initialize(err, sizeof err)) {
        fprintf(stderr, "txmp_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        /* each level on its own, so instance indices are the file's (as the tool's) */
        if (!tm_initialize(err, sizeof err) || !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "txmp_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        uint32_t h = 0, count = 0;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag != TXMP_TAG || !in->data) continue;
            TxmpInfo t;
            txmp_info(in->data, &t);
            uint32_t n;
            uint8_t *px = txmp_rgba(in->data, &n);
            if (!px) {
                fprintf(stderr, "txmp_test: level%d %s: can't decode\n", lv, in->name ? in->name : "-");
                failed++;
                continue;
            }
            char line[256];
            snprintf(line, sizeof line, "%u %s %u %u %d %x %08x\n", (unsigned)i, in->name ? in->name : "-", t.width,
                     t.height, t.type, (unsigned)t.flags, (unsigned)crc32(px, n));
            free(px);
            h = crc32_update(h, line, strlen(line));
            count++;
        }
        printf("level%d: textures=%u hash=%08x\n", lv, (unsigned)count, (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
    }
    printf("all: hash=%08x\n", (unsigned)all);
    const char *want = getenv("OPENONI_TXMP_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "txmp_test: the all hash differs from OPENONI_TXMP_SUMMARY\n");
        failed++;
    }
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("txmp_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
