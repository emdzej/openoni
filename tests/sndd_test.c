/* Every sound of every level (OPENONI_DATA=<dir>, default ./game; skipped without it), decoded to s16
   samples: the same summary lines as tools/onisnd.py. Also checks that the frame count query agrees with
   the decode and that the stored durations equal the ones computed from the frames.
   OPENONI_SNDD_SUMMARY=<hash> checks the "all" hash. */
#include "crc32.h"
#include "exe.h"
#include "sndd.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("sndd_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err)) {
        fprintf(stderr, "sndd_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0, durations = 0;
    int16_t *pcm = NULL;
    size_t cap = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        if (!tm_initialize(err, sizeof err) || !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "sndd_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        uint32_t h = 0, count = 0;
        uint64_t frames = 0;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *ti = tm_file_instance(f, i);
            if (ti->tmpl->tag != SNDD_TAG || !ti->data) continue;
            const char *name = ti->name ? ti->name : "-";
            SnddInfo in;
            sndd_info(ti->data, &in);
            int64_t n = sndd_decode(ti->data, NULL, 0);
            size_t need = (size_t)(n > 0 ? n : 0) * in.channels;
            if (need > cap) {
                cap = need;
                pcm = realloc(pcm, cap * sizeof *pcm);
            }
            int64_t got = n < 0 || (need && !pcm) ? -1 : sndd_decode(ti->data, pcm, (uint32_t)n);
            if (got < 0 || got != n) {
                fprintf(stderr, "sndd_test: level%d %s: doesn't decode\n", lv, name);
                failed++;
                continue;
            }
            /* the crc over s16 little-endian */
            uint32_t c = 0;
            uint8_t buf[512];
            for (size_t at = 0; at < need;) {
                size_t m = 0;
                for (; m < sizeof buf / 2 && at < need; m++, at++) {
                    buf[2 * m] = (uint8_t)pcm[at];
                    buf[2 * m + 1] = (uint8_t)((uint16_t)pcm[at] >> 8);
                }
                c = crc32_update(c, buf, 2 * m);
            }
            if (in.duration_field) {
                uint64_t t = (uint64_t)in.frames * 60 / in.rate;
                if (in.duration_field == (t > 1 ? t : 1)) durations++;
                else fprintf(stderr, "sndd_test: level%d %s: %u ticks stored, %u computed\n", lv, name,
                             in.duration_field, (unsigned)t);
            }
            char line[256];
            snprintf(line, sizeof line, "%u %s %u %u %u %u %u %u %08x\n", (unsigned)i, name, (unsigned)in.flags,
                     in.format, in.channels, (unsigned)in.rate, (unsigned)in.frames, (unsigned)in.duration, (unsigned)c);
            h = crc32_update(h, line, strlen(line));
            frames += in.frames;
            count++;
        }
        printf("level%d: sounds=%u frames=%llu hash=%08x\n", lv, (unsigned)count, (unsigned long long)frames, (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
        tm_terminate();
    }
    free(pcm);
    printf("all: hash=%08x\n", (unsigned)all);
    printf("sndd_test: %u stored durations agree with the decoded frames\n", (unsigned)durations);
    const char *want = getenv("OPENONI_SNDD_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "sndd_test: the all hash differs from OPENONI_SNDD_SUMMARY\n");
        failed++;
    }
    exe_free();
    vfs_unmount();
    printf("sndd_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
