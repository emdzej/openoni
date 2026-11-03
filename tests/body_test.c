/* Konoko's body (OPENONI_DATA=<dir>, default ./game; skipped without it), level 1 over level 0: every
   class's bodies build, and Konoko's highest-detail body posed by KONOKOidle_spec3 at frame 37 prints as
   `tools/onibody.py game level1 --matrices konoko_generic KONOKOidle_spec3 37` does. */
#include "body.h"
#include "exe.h"
#include "m3gm.h"
#include "tm.h"
#include "tram.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("body_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "body_test: %s\n", err);
        return 1;
    }
    int failed = 0, classes = 0;
    static const BodyMatrix ident = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    BodyMatrix m[BODY_MAX_BONES];
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag != ONCC_TAG || !in->data) continue;
        for (int d = 0; d < 5; d++) {
            const void *b = body_of_class(in->data, d);
            bool ok = b && body_matrices(b, ident, NULL, m);
            for (uint32_t k = 0; ok && k < body_bones(b); k++) ok = body_geometry(b, k) && m3gm_triangles(body_geometry(b, k), NULL, NULL, 0) > 0;
            if (!ok) {
                fprintf(stderr, "body_test: %s body %d doesn't build\n", in->name, d);
                failed++;
            }
        }
        classes++;
    }
    const void *oncc = tm_instance_get(ONCC_TAG, "konoko_generic"), *anim = tm_instance_get(TRAM_TAG, "KONOKOidle_spec3");
    const void *body = oncc ? body_of_class(oncc, 4) : NULL;
    float q[4 * BODY_MAX_BONES];
    if (!body || !anim || !tram_rotations(anim, 37, q) || !body_matrices(body, ident, q, m)) {
        fprintf(stderr, "body_test: Konoko doesn't pose\n");
        failed++;
    } else {
        for (uint32_t b = 0; b < body_bones(body); b++) {
            printf("bone %2u:", (unsigned)b);
            for (int k = 0; k < 12; k++) printf(" %8.3f", m[b][k]);
            printf("\n");
        }
    }
    printf("level1: %d classes\n", classes);
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("body_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
