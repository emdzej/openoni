/* The bosses' behaviours (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 3, Barabbas
   (his Combat profile, Barabbas_TCTF: behaviours 6, 5, 7) fights Konoko standing 60 away: he takes his
   behaviours by the range; hurt below 40 percent he backs off, then, far enough, heals (the animation
   0xdb), gaining hit points. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "knowledge.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"
#include "vfs.h"
#include "vfs_host.h"
#include "weapon.h"

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
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("boss_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "boss_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(3);
    if (!tm_level_load(3, err, sizeof err)) {
        fprintf(stderr, "boss_test: %s\n", err);
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
    knowledge_reset();
    Chr *p = chars_player(), *b = chars_spawn("Barabus", true);
    if (!p || !b || !env) {
        fprintf(stderr, "boss_test: no player or Barabbas\n");
        return 1;
    }
    float at[3] = { b->c.pos[0], b->c.pos[1], b->c.pos[2] };
    physics_place(&p->c, (const float[3]){ at[0] + 60.0f * sinf(b->c.facing), at[1], at[2] + 60.0f * cosf(b->c.facing) });
    p->c.flags |= 0x20;     /* she stays up (chr_unkillable) */
    printf("Barabbas: combat %u, behaviours %d %d %d %d %d, hp %u/%u, weapon %d\n", b->ai.combat_set.id,
           b->ai.combat_set.behaviour[0], b->ai.combat_set.behaviour[1], b->ai.combat_set.behaviour[2],
           b->ai.combat_set.behaviour[3], b->ai.combat_set.behaviour[4], b->c.hit_points, b->c.max_hit_points,
           b->c.weapon != NULL);
    knowledge_post(b, KNOW_SEEN_CENTRAL, KNOW_DEFINITE, p->c.pos, p, 0, false, false);
    int combat_at = -1, seen_beh[14] = { 0 }, seen_mode[9] = { 0 }, heal_at = -1, healed_hp = 0, low_at = -1;
    uint32_t hp_low = 0;
    for (int k = 0; k < 4000; k++) {
        ui_update();
        knowledge_update(env);
        chars_update(&(CharacterInput){ 0 }, env);
        weapons_update(env);
        AiState *a = &b->ai;
        if (a->state == AI_STATE_COMBAT) {
            if (combat_at < 0) combat_at = k;
            if (a->combat.behaviour >= 0 && a->combat.behaviour < 14) seen_beh[a->combat.behaviour] = 1;
            if (a->combat.barabbas.mode >= 0 && a->combat.barabbas.mode < 9) seen_mode[a->combat.barabbas.mode] = 1;
        }
        if (k == 600 && !(b->c.flags & 1)) {
            /* hurt to a third */
            b->c.hit_points = b->c.max_hit_points / 3;
            hp_low = b->c.hit_points;
            low_at = k;
        }
        if (low_at >= 0 && b->c.type == 0xdb && heal_at < 0) heal_at = k;
        if (heal_at >= 0 && b->c.hit_points > hp_low) healed_hp = (int)(b->c.hit_points - hp_low);
        if (getenv("BOSS_TRACE") && k % 30 == 0)
            printf("T %d state %d beh %d mode %d hp %u type 0x%x dist %.0f maneuver %d\n", k, a->state, a->combat.behaviour,
                   a->combat.barabbas.mode, b->c.hit_points, b->c.type, a->combat.dist, a->combat.maneuver);
    }
    printf("combat from %d; behaviours", combat_at);
    for (int i = 0; i < 14; i++) if (seen_beh[i]) printf(" %d", i);
    printf("; modes");
    for (int i = 0; i < 9; i++) if (seen_mode[i]) printf(" %d", i);
    printf("; hurt to %u at %d, healing from %d, +%d hp\n", hp_low, low_at, heal_at, healed_hp);
    if (combat_at < 0 || !(seen_beh[5] || seen_beh[6] || seen_beh[7])) failed |= 1;
    if (!seen_mode[8] && heal_at < 0) failed |= 2;
    if (heal_at < 0 || healed_hp <= 0) failed |= 4;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("boss_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
