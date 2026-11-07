/* The bosses' behaviours (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 3, Barabbas
   (his Combat profile, Barabbas_TCTF: behaviours 6, 5, 7) fights Konoko standing 60 away: he takes his
   behaviours by the range; hurt below 40 percent he backs off, then, far enough, heals (the animation
   0xdb), gaining hit points. On level 19 Mutant Muro's shield and thunderbolt; on level 12 the super
   ninja's fireball, teleports (behind her, to a flag when hit hard) and his return from below the
   level. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "knowledge.h"
#include "objc.h"
#include "particle.h"
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
        weapons_physics(env);
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

    /* level 19: Mutant Muro (Mutant_Muro: 13, 12, 12, 12, 13) with Konoko 250 away (beyond his medium range, 120): his shield rises to
       full (written 1 each weights message, falling 0.06 a tick between: 0.7 at the samples); at that range he casts the thunderbolt (0xe7), and his shield drops for it */
    chars_unload();
    trac_set_level(19);
    if (!tm_level_load(19, err, sizeof err)) {
        fprintf(stderr, "boss_test: %s\n", err);
        return 1;
    }
    prepare();
    env = NULL;
    f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    chars_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    knowledge_reset();
    p = chars_player();
    Chr *m = chars_spawn("MutantMuro", true);
    if (!p || !m || !env) {
        fprintf(stderr, "boss_test: no player or Muro\n");
        return 1;
    }
        p->c.flags |= 0x20;
    printf("Muro: medium %.0f, combat %u, behaviours %d %d %d %d %d, hp %u\n", m->ai.combat_set.medium, m->ai.combat_set.id, m->ai.combat_set.behaviour[0],
           m->ai.combat_set.behaviour[1], m->ai.combat_set.behaviour[2], m->ai.combat_set.behaviour[3],
           m->ai.combat_set.behaviour[4], m->c.hit_points);
    float f0 = m->c.facing;
    const float away[3] = { m->c.pos[0] + 250.0f * sinf(f0), m->c.pos[1], m->c.pos[2] + 250.0f * cosf(f0) };
    knowledge_post(m, KNOW_SEEN_CENTRAL, KNOW_DEFINITE, p->c.pos, p, 0, false, false);
    int full_at = -1, bolt_at = -1, dropped_at = -1;
    for (int k = 0; k < 3000 && dropped_at < 0; k++) {
        ui_update();
        knowledge_update(env);
        chars_update(&(CharacterInput){ 0 }, env);
        /* she keeps away (he waits 300 ticks after entering his thunderbolt behaviour before the first) */
        if (k % 30 == 0) {
            /* (250 from where he is now: he advances on her) */
            const float aw[3] = { m->c.pos[0] + 250.0f * sinf(f0), away[1], m->c.pos[2] + 250.0f * cosf(f0) };
            physics_place(&p->c, aw);
        }
        if (m->c.super_level > 0.65f && full_at < 0) full_at = k;
        if (m->c.type == 0xe7 && bolt_at < 0) bolt_at = k;
        if (bolt_at >= 0 && m->c.super_level < 0.1f) dropped_at = k;
        if (getenv("BOSS_TRACE") && k % 30 == 0)
            printf("M %d state %d beh %d shield %.2f type 0x%x dist %.0f charge %u cool %u maneuver %d\n", k, m->ai.state,
                   m->ai.combat.behaviour, m->c.super_level, m->c.type, m->ai.combat.dist, m->ai.combat.muro.charge,
                   m->ai.combat.muro.cooldown, m->ai.combat.maneuver);
    }
    printf("Muro: shield full at %d, thunderbolt at %d, shield down at %d\n", full_at, bolt_at, dropped_at);
    if (full_at < 0) failed |= 8;
    if (bolt_at < 0 || dropped_at < 0) failed |= 16;

    /* level 12: the super ninja (OutroNinja, the combat SuperNinja: 8, 9, 10, 9, 8) with Konoko kept 120 away: his fireball (0xe0),
       and advancing, the alternate (0xe1, invisible after it) or the teleport behind her (0xde, 0xdf: a
       jump of his position) */
    chars_unload();
    trac_set_level(12);
    if (!tm_level_load(12, err, sizeof err)) {
        fprintf(stderr, "boss_test: %s\n", err);
        return 1;
    }
    prepare();
    env = NULL;
    f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    p3_reset();
    p3_load();
    p3_set_environment(env);
    chars_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    knowledge_reset();
    p = chars_player();
    Chr *n = chars_spawn("OutroNinja", true);
    if (!p || !n || !env) {
        fprintf(stderr, "boss_test: no player or ninja\n");
        return 1;
    }
    p->c.flags |= 0x20;
    printf("ninja: combat %u, behaviours %d %d %d %d %d, hp %u\n", n->ai.combat_set.id, n->ai.combat_set.behaviour[0],
           n->ai.combat_set.behaviour[1], n->ai.combat_set.behaviour[2], n->ai.combat_set.behaviour[3],
           n->ai.combat_set.behaviour[4], n->c.hit_points);
    f0 = n->c.facing;
    const float off[3] = { n->c.pos[0] + 120.0f * sinf(f0), n->c.pos[1], n->c.pos[2] + 120.0f * cosf(f0) };
    physics_place(&p->c, off);
    knowledge_post(n, KNOW_SEEN_CENTRAL, KNOW_DEFINITE, p->c.pos, p, 0, false, false);
    int fire_at = -1, alt_at = -1, tele_at = -1, jump_at = -1, invis_at = -1, nseen[7] = { 0 };
    int fire_particles_at = -1, hurt_at = -1;
    uint32_t hp_start = p->c.hit_points;
    int hard_at = -1, flag_at = -1, flag_jump_at = -1, below_at = -1, back_at = -1;
    float back[3] = { 0 };
    ObjcObject fl;
    printf("ninja: flags 9000 %d, 9500 %d, at y %.0f\n", objc_flag_find(9000, &fl), objc_flag_find(9500, &fl), n->c.pos[1]);
    for (int k = 0; k < 4000; k++) {
        float was[3] = { n->c.pos[0], n->c.pos[1], n->c.pos[2] };
        ui_update();
        knowledge_update(env);
        chars_update(&(CharacterInput){ 0 }, env);
        weapons_update(env);
        weapons_physics(env);
        Character *both[2] = { &p->c, &n->c };
        p3_update(ui_time() + 1, true, both, 2);
        p3_update(ui_time(), false, both, 2);      /* the decorative ones */
        if (n->c.type == 0xe0 && n->c.anim_particles && n->c.anim_particles_on && fire_particles_at < 0) fire_particles_at = k;
        if (p->c.hit_points < hp_start && hurt_at < 0) hurt_at = k;
        if (n->ai.state == AI_STATE_COMBAT && n->ai.combat.ninja.mode >= 0 && n->ai.combat.ninja.mode < 7)
            nseen[n->ai.combat.ninja.mode] = 1;
        if (n->c.type == 0xe0 && fire_at < 0) fire_at = k;
        if (n->c.type == 0xe1 && alt_at < 0) alt_at = k;
        if (n->c.type == 0xde && tele_at < 0) tele_at = k;
        float dx = n->c.pos[0] - was[0], dz = n->c.pos[2] - was[2];
        if (n->c.type == 0xdf && dx * dx + dz * dz > 100.0f && jump_at < 0) jump_at = k;
        if (n->c.invisibility && invis_at < 0) invis_at = k;
        if (getenv("BOSS_TRACE") && k % 30 == 0)
            printf("N %d state %d beh %d mode %d type 0x%x dist %.0f cool %u/%u/%u inv %u maneuver %d\n", k, n->ai.state,
                   n->ai.combat.behaviour, n->ai.combat.ninja.mode, n->c.type, n->ai.combat.dist, n->ai.combat.ninja.fire_cool,
                   n->ai.combat.ninja.alt_cool, n->ai.combat.ninja.teleport_cool, n->c.invisibility, n->ai.combat.maneuver);
        /* she keeps away */
        if (k % 30 == 0 && k < 1000) physics_place(&p->c, off);
        /* hit hard (40 lately, from 140 on, after the teleport, until he goes): close and free to act, the teleport to a flag
           (9500 on) */
        if (k >= 140 && flag_at < 0 && k < 2400) {
            n->ai.combat.ninja.damage = 40;
            n->ai.combat.ninja.damage_hold = 240;
            if (hard_at < 0) hard_at = k;
        }
        if (hard_at >= 0 && n->ai.combat.ninja.mode == 5 && flag_at < 0) flag_at = k;
        if (flag_at >= 0 && n->c.type == 0xdf && dx * dx + dz * dz > 100.0f && flag_jump_at < 0) flag_jump_at = k;
        /* below the level (550): hidden, then back by the flags from 9000 on */
        if (k == 2500) {
            physics_place(&n->c, (const float[3]){ n->c.pos[0], 540.0f, n->c.pos[2] });
            below_at = k;
        }
        if (below_at >= 0 && n->c.type == 0xdf && back_at < 0) {
            back_at = k;
            memcpy(back, n->c.pos, sizeof back);
        }
    }
    printf("ninja: modes");
    for (int i = 0; i < 7; i++) if (nseen[i]) printf(" %d", i);
    printf("; fireball at %d, alternate at %d (invisible at %d), teleport at %d (moved at %d)\n", fire_at, alt_at, invis_at,
           tele_at, jump_at);
    printf("ninja: the fireball's particles started at %d, Konoko hurt at %d (%u of %u)\n", fire_particles_at, hurt_at,
           p->c.hit_points, hp_start);
    printf("ninja: hit hard at %d, to a flag at %d (moved at %d); below at %d, back at %d (%.0f %.0f %.0f)\n", hard_at, flag_at,
           flag_jump_at, below_at, back_at, back[0], back[1], back[2]);
    if (fire_at < 0) failed |= 32;
    if ((alt_at < 0 || invis_at < 0) && (tele_at < 0 || jump_at < 0)) failed |= 64;
    if (flag_at < 0 || flag_jump_at < 0) failed |= 128;
    if (back_at < 0 || back[1] < 550.0f) failed |= 256;
    if (fire_particles_at < 0) failed |= 512;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("boss_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
