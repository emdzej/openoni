/* Every geometry of every level (OPENONI_DATA=<dir>, default ./game; skipped without it): its arrays and
   its strips decoded to triangles, with the same summary lines as tools/onigeom.py.
   OPENONI_M3GM_SUMMARY=<hash> checks the "all" hash. */
#include "crc32.h"
#include "exe.h"
#include "tm.h"
#include "m3gm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int levels[] = { 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19 };
enum { N_LEVELS = sizeof levels / sizeof levels[0] };

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("m3gm_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err)) {
        fprintf(stderr, "m3gm_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    uint32_t all = 0;
    for (int k = 0; k < N_LEVELS; k++) {
        int lv = levels[k];
        /* each level on its own, so instance indices are the file's (as the tool's) */
        if (!tm_initialize(err, sizeof err) || !tm_level_load(lv, err, sizeof err)) {
            fprintf(stderr, "m3gm_test: level%d: %s\n", lv, err);
            failed++;
            continue;
        }
        TmFile *f = tm_file_at(tm_file_slots() - 1);
        uint32_t h = 0, count = 0;
        for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag != M3GM_TAG || !in->data) continue;
            uint32_t np, nvn, nuv;
            const uint8_t *pts = m3gm_points(in->data, &np), *vn = m3gm_vertex_normals(in->data, &nvn);
            const uint8_t *uv = m3gm_texcoords(in->data, &nuv);
            int32_t nt = m3gm_triangles(in->data, NULL, NULL, 0);
            uint32_t *tris = nt > 0 ? malloc((size_t)nt * 16) : NULL;
            if (nt < 0 || (nt > 0 && !tris)) {
                fprintf(stderr, "m3gm_test: level%d %s: bad strips\n", lv, in->name ? in->name : "-");
                failed++;
                continue;
            }
            m3gm_triangles(in->data, tris, tris + 3 * nt, nt);
            uint32_t c = crc32_update(crc32_update(crc32(pts, np * 12), uv, nuv * 8), vn, nvn * 12);
            for (int32_t t = 0; t < nt; t++) {
                uint8_t q[16];
                uint32_t v[4] = { tris[3 * t], tris[3 * t + 1], tris[3 * t + 2], tris[3 * nt + t] };
                for (int j = 0; j < 4; j++)
                    for (int b = 0; b < 4; b++) q[4 * j + b] = (uint8_t)(v[j] >> (8 * b));
                c = crc32_update(c, q, 16);
            }
            free(tris);
            char line[256];
            snprintf(line, sizeof line, "%u %s %u %d %08x\n", (unsigned)i, in->name ? in->name : "-", (unsigned)np, (int)nt,
                     (unsigned)c);
            h = crc32_update(h, line, strlen(line));
            count++;
        }
        printf("level%d: geometries=%u hash=%08x\n", lv, (unsigned)count, (unsigned)h);
        char line[64];
        snprintf(line, sizeof line, "level%d %08x\n", lv, (unsigned)h);
        all = crc32_update(all, line, strlen(line));
    }
    printf("all: hash=%08x\n", (unsigned)all);
    const char *want = getenv("OPENONI_M3GM_SUMMARY");
    if (want && *want && strtoul(want, NULL, 16) != all) {
        fprintf(stderr, "m3gm_test: the all hash differs from OPENONI_M3GM_SUMMARY\n");
        failed++;
    }
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("m3gm_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
