/* The environment's collisions (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, a
   sphere dropped onto the training room's floor hits it facing up, one hanging in the air hits nothing,
   and one run into the room's walls hits them facing back (AKrCollision_Sphere, AKrGQ_SphereVector); a segment down meets the floor (AKrCollision_Point). */
#include "akev.h"
#include "akev_collide.h"
#include "exe.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("collide_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "collide_test: %s\n", err);
        return 1;
    }
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    if (!env) {
        fprintf(stderr, "collide_test: no environment\n");
        return 1;
    }
    int failed = 0;
    static AkevHit hits[AKEV_HITS_MAX];
    /* down onto the floor */
    const float drop[4] = { -1101.0f, -335.0f, -126.3f, 2.0f }, down[3] = { 0.0f, -10.0f, 0.0f };
    unsigned n = akev_collide_sphere(env, drop, down, 0x44800, 2, hits);
    printf("drop: %u hits", n);
    if (n) printf(", first quad %u plane (%.2f %.2f %.2f %.1f) at (%.1f %.1f %.1f) distance %.2f", hits[0].quad, hits[0].plane[0],
                  hits[0].plane[1], hits[0].plane[2], hits[0].plane[3], hits[0].point[0], hits[0].point[1], hits[0].point[2], hits[0].dist);
    printf("\n");
    if (!n || hits[0].plane[1] < 0.9f) failed |= 1;
    /* hanging in the air, still */
    const float air[4] = { -1101.0f, -320.0f, -126.3f, 2.0f }, still[3] = { 0.0f, 0.0f, 0.0f };
    n = akev_collide_sphere(env, air, still, 0x44800, 2, hits);
    printf("air: %u hits\n", n);
    if (n) failed |= 2;
    /* run along each axis until a wall */
    const float dirs[4][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    int walls = 0;
    for (int k = 0; k < 4; k++) {
        const float at[4] = { -1101.0f, -330.0f, -126.3f, 2.0f };
        float mv[3] = { dirs[k][0] * 300.0f, 0.0f, dirs[k][2] * 300.0f };
        n = akev_collide_sphere(env, at, mv, 0x44800, 2, hits);
        if (!n) {
            printf("run %d: nothing\n", k);
            continue;
        }
        float facing = hits[0].plane[0] * dirs[k][0] + hits[0].plane[2] * dirs[k][2];
        printf("run %d: %u hits, nearest at %.1f, plane (%.2f %.2f %.2f) against the run %.2f\n", k, n, hits[0].dist, hits[0].plane[0],
               hits[0].plane[1], hits[0].plane[2], facing);
        if (facing < -0.5f) walls++;
    }
    if (walls < 2) failed |= 4;
    /* a segment down onto the floor (AKrCollision_Point): the nearest hit on it, 5 below */
    const float from[3] = { -1101.0f, -335.0f, -126.3f };
    n = akev_collide_point(env, from, down, 0x48800, 1, hits);
    printf("point: %u hits", n);
    if (n) printf(", quad %u at y %.2f, distance %.2f", hits[0].quad, hits[0].point[1], hits[0].dist);
    printf("\n");
    if (n != 1 || fabsf(hits[0].point[1] + 340.0f) > 0.01f || fabsf(hits[0].dist - 5.0f) > 0.01f) failed |= 8;
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("collide_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
