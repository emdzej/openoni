/* The decals (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1's training room floor, a
   small decal lies flat on it, just above (its points off the floor along the normal, 0.05), cut into
   triangles; a big one from the room's middle reaches the walls and folds up them (points off the
   floor's plane), and deleting both leaves none. */
#include "akev.h"
#include "akev_collide.h"
#include "decal.h"
#include "exe.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { float floor_y; unsigned verts, tris, off_floor; float lo, hi; } Stats;

static void count(const DecalDraw *d, void *user)
{
    Stats *s = user;
    s->verts += d->nverts, s->tris += d->ntris;
    for (uint32_t i = 0; i < d->nverts; i++) {
        float dy = d->pos[3 * i + 1] - s->floor_y;
        if (fabsf(dy) > 0.2f) s->off_floor++;
        else {
            if (dy < s->lo) s->lo = dy;
            if (dy > s->hi) s->hi = dy;
        }
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("decal_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "decal_test: %s\n", err);
        return 1;
    }
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    const void *tex = tm_instance_get(TM_TAG("TXMP"), "notfoundtex");
    if (!env || !tex) {
        fprintf(stderr, "decal_test: no environment or texture\n");
        return 1;
    }
    decal_set_environment(env);
    int failed = 0;
    static AkevHit hits[AKEV_HITS_MAX];
    const float from[3] = { -1101.0f, -330.0f, -126.3f }, down[3] = { 0.0f, -20.0f, 0.0f };
    if (!akev_collide_point(env, from, down, 0x846800, 1, hits)) {
        fprintf(stderr, "decal_test: no floor\n");
        return 1;
    }
    float at[3] = { hits[0].point[0], hits[0].point[1] + 0.5f, hits[0].point[2] };
    const float up[3] = { 0, 1, 0 }, ax[3] = { 1, 0, 0 }, az[3] = { 0, 0, 1 };
    uint32_t small = decal_create(tex, -1, DECAL_UNSHADED, 1.0471976f, NULL, at, up, az, ax, 1.0f, 1.0f, 255, 0xffffffff);
    Stats s = { hits[0].point[1], 0, 0, 0, 1e9f, -1e9f };
    decal_each(count, &s);
    printf("decal_test: small: id %u, %u points, %u triangles, %u off the floor, %.3f..%.3f above it\n", small, s.verts, s.tris,
           s.off_floor, s.lo, s.hi);
    if (!small || s.tris < 2 || s.off_floor || s.lo < 0.04f || s.hi > 0.06f) failed |= 1;
    uint32_t big = decal_create(tex, -1, 0, 1.6f, NULL, at, up, az, ax, 60.0f, 60.0f, 255, 0xffffffff);
    Stats b = { hits[0].point[1], 0, 0, 0, 1e9f, -1e9f };
    decal_each(count, &b);
    printf("decal_test: big: id %u, %u points, %u triangles in all, %u off the floor; %d decals\n", big, b.verts, b.tris, b.off_floor,
           decal_count());
    if (!big || b.verts <= s.verts || !b.off_floor || decal_count() != 2) failed |= 2;
    decal_delete(small);
    decal_delete(big);
    if (decal_count() != 0) failed |= 4;
    if (!failed) printf("decal_test: ok\n");
    return failed;
}
