/* The AI's gun pickup (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, an unarmed
   thug (THUG_Wh) set to Hold And Fire at every range, never to fight unarmed, sees Konoko and goes for
   the pistol lying 50 away instead (the gun maneuver), plays its pickup animation and has it. */
#include "akev.h"
#include "chars.h"
#include "combat.h"
#include "ui.h"
#include "chr_anim.h"
#include "exe.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"
#include "weapon.h"

#include <math.h>
#include <stdlib.h>
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

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("gun_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "gun_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "gun_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    int failed = 0;
    weapons_install();
    chars_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    const void *pistol = tm_instance_get(TM_TAG("ONWC"), "w1_tap");
    if (!p || !t || !env || !pistol) {
        fprintf(stderr, "gun_test: no player, thug or pistol\n");
        return 1;
    }
    const float at[3] = { -1101.0f, -340.0f, -126.3f };
    physics_place(&p->c, at);
    physics_place(&t->c, (const float[3]){ at[0], at[1], at[2] - 60.0f });
    Weapon *gun = weapon_new(pistol, (const float[3]){ at[0] + 50.0f, at[1], at[2] - 60.0f }, 0.0f);
    if (!gun) {
        fprintf(stderr, "gun_test: no weapon made\n");
        return 1;
    }
    t->c.facing = t->c.desired_facing = 0.0f;
    t->ai.patrol = -1;
    t->ai.state = AI_STATE_IDLE;
    for (int r = 0; r < 5; r++) t->ai.combat_set.behaviour[r] = 2;     /* Hold And Fire */
    t->ai.combat_set.no_gun = 0;
    t->ai.combat_set.no_gun_else = 0;
    int gun_at = -1, pickup_at = -1, combat_at = -1;
    for (int k = 0; k < 1200 && !t->c.weapon; k++) {
        ui_update();
        chars_update(&(CharacterInput){ 0 }, env);
        weapons_update(env);
        weapons_physics(env);
        if (combat_at < 0 && t->ai.state == AI_STATE_COMBAT) combat_at = k;
        if (getenv("GUN_TRACE") && k % 20 == 0) {
            float h[3];
            weapon_pickup_position(gun, h);
            printf("T %d m%d %.1f %.1f %.1f thw %u below %u moving %d type 0x%x\n", k, t->ai.combat.maneuver, h[0] - t->c.pos[0], h[1] - t->c.pos[1], h[2] - t->c.pos[2],
                   t->ai.combat.gun_thwarted, t->ai.combat.gun_below, t->ai.moving, t->c.type);
        }
        /* the class's chance (30%) taken as won */
        if (t->ai.combat.gun_rolled && !t->ai.combat.gun_go) t->ai.combat.gun_go = true;
        if (gun_at < 0 && t->ai.combat.maneuver == COMBAT_MANEUVER_GUN && t->ai.combat.gun) {
            gun_at = k;
            printf("tick %d: goes for the gun\n", k);
        }
        if (pickup_at < 0 && t->c.pickup == gun) {
            pickup_at = k;
            float h[3];
            weapon_pickup_position(gun, h);
            printf("tick %d: pickup animation 0x%x, %.1f from the handle\n", k, t->c.type,
                   sqrtf((h[0] - t->c.pos[0]) * (h[0] - t->c.pos[0]) + (h[1] - t->c.pos[1]) * (h[1] - t->c.pos[1]) +
                         (h[2] - t->c.pos[2]) * (h[2] - t->c.pos[2])));
        }
    }
    printf("combat after %d ticks, gun maneuver %d, pickup %d, armed %s\n", combat_at, gun_at, pickup_at,
           t->c.weapon == gun ? "with it" : t->c.weapon ? "with another" : "no");
    if (combat_at < 0 || gun_at < 0) failed |= 1;
    if (t->c.weapon != gun) failed |= 4;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("gun_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
