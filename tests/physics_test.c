/* The characters' physics (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, in the
   training room, Konoko stands on the floor, runs into a wall and stops there, and dropped from above
   falls and lands on the floor (OBiPhysics_Update, 0x4ebd70, FindGroundCollision 0x4ec040). */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gametime.h"

/* A game tick: the characters' update, then the game time on (0x4fafb0). */
static void chars_tick(const CharacterInput *in, const void *env)
{
    chars_update(in, env);
    game_time_advance();
}

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("physics_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "physics_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "physics_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    chars_load();
    Chr *p = chars_player();
    if (!p || !env) {
        fprintf(stderr, "physics_test: no player\n");
        return 1;
    }
    int failed = 0;
    const float at[3] = { -1101.0f, -340.0f, -126.3f };     /* the training room's floor, y = -340 */
    physics_place(&p->c, at);
    p->c.facing = p->c.desired_facing = 0.0f;
    for (int t = 0; t < 120; t++) chars_tick(&(CharacterInput){ 0 }, env);
    printf("standing: (%.2f, %.2f, %.2f) air %u floor quad %d\n", p->c.pos[0], p->c.pos[1], p->c.pos[2],
           (unsigned)p->c.air_ticks, (int)p->c.ground_quad);
    if (fabsf(p->c.pos[1] - at[1]) > 0.1f || p->c.air_ticks) failed |= 1;
    /* run until a wall: the run stops there, on the floor */
    float z400 = 0.0f;
    unsigned walls = 0;
    for (int t = 0; t < 600; t++) {
        chars_tick(&(CharacterInput){ .forward = 1 }, env);
        walls += p->c.wall_hits;
        if (t == 399) z400 = p->c.pos[2];
    }
    printf("ran: (%.2f, %.2f, %.2f), %.2f at tick 400, %u wall hits\n", p->c.pos[0], p->c.pos[1], p->c.pos[2], z400, walls);
    if (!walls || fabsf(p->c.pos[2] - z400) > 2.0f || fabsf(p->c.pos[1] - at[1]) > 0.5f) failed |= 2;
    /* dropped 40 above the floor: a fall, then the landing on it */
    physics_place(&p->c, (const float[3]){ at[0], at[1] + 40.0f, at[2] });
    int landed = -1;
    bool fell = false;
    for (int t = 0; t < 200; t++) {
        chars_tick(&(CharacterInput){ 0 }, env);
        if (p->c.air_ticks) fell = true;
        if (fell && !p->c.air_ticks && landed < 0) landed = t;
    }
    printf("dropped: fell %d, landed at tick %d, y %.2f, %u hit points\n", fell, landed, p->c.pos[1], p->c.hit_points);
    if (!fell || landed < 0 || fabsf(p->c.pos[1] - at[1]) > 0.1f) failed |= 4;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("physics_test: %s (%d)\n", failed ? "FAILED" : "ok", failed);
    return failed != 0;
}
