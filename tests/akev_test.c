/* The environment of every level (OPENONI_DATA=<dir>, default ./game; skipped without it), each loaded
   over level 0 as the game loads it: every quad decoded, with the same summary lines as tools/onienv.py.
   OPENONI_AKEV_SUMMARY=<hash> checks the "all" hash. */
#include "akev.h"
#include "crc32.h"
#include "exe.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

static void put32(uint8_t *p, uint32_t v)
{
    for (int b = 0; b < 4; b++) p[b] = (uint8_t)(v >> (8 * b));
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("akev_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "akev_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        if (!tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "akev_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        const void *env = NULL;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
        }
        if (!env) {
            fprintf(stderr, "akev_test: level%d: no environment\n", lv);
            failed++;
            tm_level_unload(lv);
            continue;
        }
        uint32_t np, nuv, nq = akev_quad_count(env), drawn = 0, h = 0;
        const uint8_t *pts = akev_points(env, &np), *uv = akev_texcoords(env, &nuv);
        for (uint32_t i = 0; i < nq; i++) {
            AkevQuad q;
            if (!akev_quad(env, i, &q)) {
                fprintf(stderr, "akev_test: level%d: quad %u is inconsistent\n", lv, (unsigned)i);
                failed++;
                break;
            }
            drawn += akev_quad_drawn(q.flags);
            uint8_t b[16 + 4 * 24];
            size_t n = 16;
            put32(b, q.corners);
            put32(b + 4, q.flags);
            put32(b + 8, q.object);
            put32(b + 12, q.texture);
            for (uint32_t c = 0; c < q.corners; c++) {
                memcpy(b + n, pts + 12 * q.point[c], 12);
                memcpy(b + n + 12, uv + 8 * q.texcoord[c], 8);
                put32(b + n + 20, q.shade[c]);
                n += 24;
            }
            h = crc32_update(h, b, n);
            const void *tex = akev_texture(env, q.texture);
            const char *name = tex ? tm_handle_instance(tm_handle_of(tex))->name : NULL;
            if (!name) name = "-";
            h = crc32_update(crc32_update(h, name, strlen(name)), "\n", 1);
        }
        printf("level%d: points=%u quads=%u drawn=%u textures=%u hash=%08x\n", lv, (unsigned)np, (unsigned)nq,
               (unsigned)drawn, (unsigned)akev_texture_count(env), (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
        tm_level_unload(lv);
    }
    printf("all: hash=%08x\n", (unsigned)all);
    const char *want = getenv("OPENONI_AKEV_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "akev_test: the all hash differs from OPENONI_AKEV_SUMMARY\n");
        failed++;
    }
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("akev_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
