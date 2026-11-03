/* Patrol paths (OPENONI_DATA=<dir>, default ./game; skipped without it): level 1's paths read whole,
   and a thug with a patrol job walks its path's first waypoints. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "objc.h"
#include "patrol.h"
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

static bool list_char(const ObjcObject *o, void *user)
{
    (void)user;
    if (objc_char_job(o) == OBJC_JOB_PATROL)
        printf("  %s: patrol %u\n", objc_char_name(o), objc_char_job_path(o));
    return true;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("patrol_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "patrol_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "patrol_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    int n = patrol_load(), points = 0;
    for (int i = 0; i < n; i++) {
        const PatrolPoint *p;
        int k = patrol_points(i, &p);
        points += k;
        printf("path %d %s: %d waypoints:", i, patrol_name(i), k);
        for (int j = 0; j < k; j++) printf(" %x", p[j].type);
        printf("\n");
    }
    objc_each("Character", list_char, NULL);
    printf("paths=%d waypoints=%d\n", n, points);
    int failed = n == 0;
    /* Bay2_Thug_1's job is patrol_62: two waypoints 0x13 (to a flag, face it, wait) and a loop. It
       walks to the first flag, waits, walks to the second, waits, and is back at the first. */
    chars_load();
    Chr *t = chars_find("Bay2_Thug_1");
    if (!t) t = chars_spawn("Bay2_Thug_1", false);     /* a script spawns it */
    const PatrolPoint *w;
    int path = patrol_find("patrol_62");
    if (!t || path < 0 || patrol_points(path, &w) != 3 || t->ai.patrol != path) failed |= 2;
    else {
        int reached = 0, last = 0;
        for (int tick = 0; tick < 6000 && reached < 3; tick++) {
            chars_update(&(CharacterInput){ 0 }, env);
            if (t->ai.point != last) {
                ObjcObject flag;
                const PatrolPoint *done = &w[last];
                if (done->type == 0x13 && objc_flag_find(done->a, &flag)) {
                    float dx = t->c.pos[0] - flag.pos[0], dz = t->c.pos[2] - flag.pos[2];
                    printf("tick %d: waypoint %d (flag %u, %u frames) done %.2f from the flag\n", tick, last, done->a,
                           done->b, sqrtf(dx * dx + dz * dz));
                    if (dx * dx + dz * dz > 64.0f) failed |= 4;
                    reached++;
                }
                last = t->ai.point;
            }
        }
        if (reached < 3) failed |= 8;
        printf("thug at %.1f %.1f %.1f, waypoint %d, moving %d, wait %u, failed %d\n", t->c.pos[0], t->c.pos[1], t->c.pos[2], t->ai.point, t->ai.moving, t->ai.wait, failed);
    }
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("patrol_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
