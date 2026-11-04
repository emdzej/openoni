/* Hits (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, Konoko faces a training
   dummy a step away and punches and kicks; the blows land once per animation, take hit points, turn
   the dummy and play its reactions, and enough of them knock it down for good. */
#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "exe.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("hit_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "hit_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "hit_test: %s\n", err);
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
    Chr *p = chars_player(), *d = chars_spawn("Train_TD_1", false);
    if (!p || !d || !env) {
        fprintf(stderr, "hit_test: no player or dummy\n");
        return 1;
    }
    d->ai.patrol = -1;      /* off its patrol (patrol_test) */
    d->ai.flags |= AI_FLAG_PASSIVE;     /* and not to answer back (knowledge_test) */
    /* on the training room's floor, 9 apart along z, facing each other */
    const float at[3] = { -1101.0f, -340.0f, -126.3f };
    const float at9[3] = { at[0], at[1], at[2] + 9.0f };
    physics_place(&p->c, at);
    physics_place(&d->c, at9);
    p->c.facing = p->c.desired_facing = 0.0f;
    d->c.facing = d->c.desired_facing = 3.14159265f;
    uint32_t hp0 = d->c.hit_points;
    int hits = 0, reactions = 0, failed = 0;
    uint32_t last = hp0;
    for (int t = 0; t < 1200 && !(d->c.flags & 1); t++) {
        CharacterInput in = { 0 };
        in.punch = (t % 40) == 0 && (t / 40) % 2 == 0;
        in.kick = (t % 40) == 0 && (t / 40) % 2 == 1;
        chars_update(&in, env);
        if (d->c.hit_points < last) {
            hits++;
            printf("tick %4d: hit for %u, %u left; dummy type 0x%02x, player type 0x%02x\n", t, last - d->c.hit_points,
                   d->c.hit_points, d->c.type, p->c.type);
            last = d->c.hit_points;
        }
        if (chr_is_victim_type(d->c.type)) reactions++;
    }
    printf("dummy: %u hit points, %d hits, %d ticks reacting\n", hp0, hits, reactions);
    if (hits < 5 || !reactions || hp0 - last != (uint32_t)(hits / 2) * 9 + (hits % 2) * 4) failed++;
    /* the blow that kills: back in reach, a few hit points left */
    const float near[3] = { p->c.pos[0] + 9.0f * sinf(p->c.facing), p->c.pos[1], p->c.pos[2] + 9.0f * cosf(p->c.facing) };
    physics_place(&d->c, near);
    d->c.hit_points = 3;
    int t = 0;
    for (; t < 120 && !(d->c.flags & 1); t++) {
        CharacterInput in = { .punch = t == 0 };
        chars_update(&in, env);
    }
    for (int k = 0; k < 120; k++) {
        chars_update(&(CharacterInput){ 0 }, env);
        if (getenv("HIT_TRACE") && k % 10 == 0) printf("  after %d: type 0x%02x state 0x%02x frame %u\n", k, d->c.type, d->c.state, (unsigned)d->c.frame);
    }
    printf("dummy: interpolating=%d tick %u/%u queued 0x%02x\n", d->c.interpolating, d->c.interp_tick, d->c.interp_length, d->c.queued);
    printf("dummy: dead=%d after %d ticks, type 0x%02x, hit points %u\n", (d->c.flags & 1) != 0, t, d->c.type, d->c.hit_points);
    /* down for good: dying (flag 2) in the pose it fell to */
    if (!(d->c.flags & 1) || !(d->c.flags & 2) || d->c.hit_points) failed++;
    /* blocking: a second dummy punches Konoko (the input goes to it for the test); facing it while
       standing she blocks, facing away she is hit */
    Chr *e = chars_spawn("Train_TD_1", true);
    if (e) e->ai.patrol = -1, e->ai.flags |= AI_FLAG_PASSIVE;
    int blocked_hp = -1, hit_hp = -1;
    for (int round = 0; round < 2 && e; round++) {
        physics_place(&p->c, at);
        physics_place(&e->c, at9);
        e->c.facing = e->c.desired_facing = 3.14159265f;
        p->c.facing = p->c.desired_facing = round == 0 ? 0.0f : 3.14159265f;
        p->c.hit_points = 100;
        memset(p->c.knockback, 0, sizeof p->c.knockback);
        for (int k = 0; k < 60; k++) chars_update(&(CharacterInput){ 0 }, env);    /* settle */
        p->player = false, e->player = true;
        e->c.flags &= ~(uint32_t)CHR_FLAG_AI_MOVEMENT;     /* the keys drive it, not its AI */
        bool block_seen = false;
        for (int k = 0; k < 60; k++) {
            CharacterInput in = { .punch = k == 0 };
            chars_update(&in, env);
            if (p->c.type == 0x17) block_seen = true;
        }
        p->player = true, e->player = false;
        e->c.flags |= CHR_FLAG_AI_MOVEMENT;
        if (round == 0) blocked_hp = (int)p->c.hit_points;
        else hit_hp = (int)p->c.hit_points;
        printf("block round %d: facing %s, hit points %u, block animation %d\n", round, round ? "away" : "the dummy",
               p->c.hit_points, block_seen);
        if (round == 0 && !block_seen) failed++;
    }
    if (blocked_hp != 100 || hit_hp >= 100) failed++;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("hit_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
