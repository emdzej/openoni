/* The particle system on level 1 (OPENONI_DATA=<dir>, default ./game; skipped without it): the classes
   load; the pistol's projectile (w1_tap_p01) flies at its velocity and dies on the wall it hits, through
   its hit-wall actions; a shooter (w1_tap_e01) emits a projectile at its pulse. The textures' materials
   make the shooting range's 18 targets breakable glass. */
#include "akev.h"
#include "exe.h"
#include "material.h"
#include "particle.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const void *environment(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("AKEV") && in->data) return in->data;
    }
    return NULL;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("particle_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "particle_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    void *env = (void *)environment();
    /* the glass: the textures' materials make the 18 targets of the shooting range breakable, and only them */
    material_level_load();
    material_mark_breakable(env);
    int breakable = 0, targets = 0;
    for (uint32_t q = 0; q < akev_quad_count(env); q++) {
        AkevQuad k;
        if (akev_quad(env, q, &k) && (k.flags & 0x200000)) breakable++;
    }
    for (uint32_t i = 0; i < akev_script_count(env); i++) {
        uint32_t q, id;
        AkevQuad k;
        if (akev_script_entry(env, i, &q, &id) && id >= 3001 && id <= 3018 && akev_quad(env, q, &k) && (k.flags & 0x200000)) targets++;
    }
    printf("particle_test: %d breakable quads, %d of the 18 targets\n", breakable, targets);
    if (breakable != 18 || targets != 18) { fprintf(stderr, "particle_test: the glass isn't the targets\n"); failed++; }
    p3_load();
    p3_set_environment(env);
    printf("particle_test: %d classes\n", p3_class_count());
    if (p3_class_count() < 100) { fprintf(stderr, "particle_test: too few classes\n"); failed++; }
    P3Class *pc = p3_class("w1_tap_p01"), *ec = p3_class("w1_tap_e01");
    if (!pc || !ec) { fprintf(stderr, "particle_test: no pistol classes\n"); return 1; }

    /* the projectile: from the training room's shooting range, level, along -z */
    float floor_y = 0;
    if (!akev_floor(env, -692, 0, -560, 100, 100, &floor_y)) floor_y = 0;
    uint32_t tick = 1000;
    P3Particle *p = p3_create(pc, tick);
    float *pos = p3_position(pc, p);
    pos[0] = -692, pos[1] = floor_y + 10, pos[2] = -560;
    float *v = p3_block(pc, p, P3_X_VELOCITY);
    v[0] = 0, v[1] = 0, v[2] = -900;
    float *o = p3_block(pc, p, P3_X_ORIENTATION);
    if (o) { static const float m[9] = { 1, 0, 0, 0, 0, -1, 0, 1, 0 }; memcpy(o, m, sizeof m); }
    uint32_t h = p3_handle(p);
    p3_send_event(pc, p, P3_EV_CREATE, tick / 60.0f);
    float last[3] = { pos[0], pos[1], pos[2] };
    int lived = 0;
    for (int k = 1; k <= 120; k++) {
        p3_update(tick + k, true, NULL, 0);
        P3Particle *q = p3_from_handle(h, NULL);
        if (!q) break;
        float *qp = p3_position(pc, q);
        memcpy(last, qp, sizeof last);
        lived = k;
    }
    printf("particle_test: projectile lived %d ticks, last at %.1f %.1f %.1f, %d alive\n", lived, last[0], last[1], last[2],
           p3_live_count());
    if (lived == 0 || lived >= 120) { fprintf(stderr, "particle_test: the projectile didn't fly and hit\n"); failed++; }
    if (lived && fabsf(last[2] - (-560 - 15.0f * lived)) > 16.0f) {
        fprintf(stderr, "particle_test: moved %.1f in %d ticks, not 15 a tick\n", -560 - last[2], lived);
        failed++;
    }

    /* the shooter: its pulse emits a projectile */
    int before = p3_live_count();
    tick += 200;
    P3Particle *s = p3_create(ec, tick);
    float *sp = p3_position(ec, s);
    sp[0] = -692, sp[1] = floor_y + 10, sp[2] = -560;
    float *so = p3_block(ec, s, P3_X_ORIENTATION);
    if (so) { static const float m[9] = { 1, 0, 0, 0, 0, -1, 0, 1, 0 }; memcpy(so, m, sizeof m); }
    p3_send_event(ec, s, P3_EV_CREATE, tick / 60.0f);
    p3_send_event(ec, s, P3_EV_PULSE, tick / 60.0f);
    int after = p3_live_count();
    printf("particle_test: the pulse: %d -> %d particles\n", before, after);
    if (after < before + 2) { fprintf(stderr, "particle_test: the pulse emitted nothing\n"); failed++; }
    for (int k = 1; k <= 120; k++) p3_update(tick + k, true, NULL, 0);
    printf("particle_test: after 2 s: %d alive\n", p3_live_count());
    if (failed) return 1;
    printf("particle_test: ok\n");
    return 0;
}
