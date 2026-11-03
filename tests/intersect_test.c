/* Predicting an attack (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, Konoko's
   punch from standing against a standing thug ahead, far ahead, to the side and behind
   (TRrCheckAnimationBounds, TRrIntersectAnimations, from ONrSetupAnimIntersectionContext). */
#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_intersect.h"
#include "exe.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

typedef struct { const char *what; float dx, dz; bool hits; } Case;

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("intersect_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "intersect_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "intersect_test: %s\n", err);
        return 1;
    }
    prepare();
    chars_load();
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    if (!p || !t) {
        fprintf(stderr, "intersect_test: no player or thug\n");
        return 1;
    }
    int failed = 0;
    const float at[3] = { -1101.0f, -340.0f, -126.3f };
    const void *punch = chr_lookup_varient(&p->c, CHR_TYPE_PUNCH, CHR_STATE_STANDING, 0x200);
    printf("punch: %s\n", punch ? "found" : "none");
    if (!punch) failed |= 1;
    /* Konoko faces +z (facing 0); the thug faces her */
    const Case cases[] = {
        { "ahead 8", 0.0f, 8.0f, true },
        { "ahead 12", 0.0f, 12.0f, true },
        { "ahead 14", 0.0f, 14.0f, false },
        { "ahead 40", 0.0f, 40.0f, false },
        { "diagonal 9", 6.4f, 6.4f, false },
        { "side 10", 10.0f, 0.0f, false },
        { "behind 8", 0.0f, -8.0f, false },
    };
    for (unsigned k = 0; punch && k < sizeof cases / sizeof cases[0]; k++) {
        const Case *cs = &cases[k];
        memcpy(p->c.pos, at, sizeof at);
        memcpy(p->c.prev_pos, at, sizeof at);
        p->c.facing = 0.0f;
        p->c.anim = punch;
        p->c.frame = 0;
        t->c.pos[0] = at[0] + cs->dx;
        t->c.pos[1] = at[1];
        t->c.pos[2] = at[2] + cs->dz;
        memcpy(t->c.prev_pos, t->c.pos, sizeof t->c.pos);
        t->c.facing = atan2f(-cs->dx, -cs->dz);
        if (t->c.facing < 0.0f) t->c.facing += 6.2831855f;
        ChrIntersect x;
        chr_intersect_setup(&p->c, &t->c, true, &x);
        bool b = chr_intersect_bounds(&x), h = b && chr_intersect(&x);
        printf("%-10s: target at (%.1f %.1f %.1f) radius %.2f heights %.1f..%.1f, bounds %d, hit %d (result %d, frames %u)\n",
               cs->what, x.m[9], x.m[10], x.m[11], x.t_radius, x.t_bottom, x.t_top, b, h, x.result, x.frames);
        if (h != cs->hits) failed |= 2;
    }
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("intersect_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
