/* The AI's melee (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, the warehouse's
   melee profiles read whole; a thug (THUG_Wh) 40 from Konoko on the training room's floor sees her,
   closes in and fights: it picks techniques, presses their attacks' keys and hurts her; and it blocks
   by its profile's chance and the difficulty. */
#include "akev.h"
#include "chars.h"
#include "ui.h"
#include "chr_anim.h"
#include "exe.h"
#include "impact.h"
#include "melee.h"
#include "persist.h"
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
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("melee_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "melee_test: %s\n", err);
        return 1;
    }
    prepare();
    impact_types_load();        /* the impact types' and materials' trees, as at the start */
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "melee_test: %s\n", err);
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
    chars_load();
    const MeleeProfile *mp = melee_find(0x27);
    printf("profile 0x27: %s, %u attacks, %u evades, %u maneuvers, %u moves\n", mp ? mp->name : "none", mp ? mp->nattack : 0,
           mp ? mp->nevade : 0, mp ? mp->nmaneuver : 0, mp ? mp->nmoves : 0);
    if (!mp || strcmp(mp->name, "THUG_Wh") || mp->nattack != 10 || mp->nmoves != 38) failed |= 1;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    if (!p || !t || !env) {
        fprintf(stderr, "melee_test: no player or thug\n");
        return 1;
    }
    const float at[3] = { -1101.0f, -340.0f, -126.3f };
    physics_place(&p->c, at);
    physics_place(&t->c, (const float[3]){ at[0], at[1], at[2] - 40.0f });
    t->c.facing = t->c.desired_facing = 0.0f;
    t->ai.patrol = -1;
    t->ai.state = AI_STATE_IDLE;
    uint32_t hp0 = p->c.hit_points;
    int attacks = 0, techniques = 0, combat_at = -1;
    uint16_t last = 0;
    const void *last_tech = NULL;
    for (int k = 0; k < 900; k++) {
        ui_update();
        chars_tick(&(CharacterInput){ 0 }, env);
        if (combat_at < 0 && t->ai.state == AI_STATE_COMBAT) combat_at = k;
        if (t->c.anim && tr_is_attack(t->c.anim) && t->c.type != last) attacks++;
        last = t->c.anim && tr_is_attack(t->c.anim) ? t->c.type : 0;
        if (t->ai.melee.tech && t->ai.melee.tech != last_tech) {
            techniques++;
            if (techniques <= 8) printf("tick %d: technique %s at %.1f\n", k, t->ai.melee.tech->name, t->ai.melee.dist);
        }
        last_tech = t->ai.melee.tech;
        if (p->c.flags & 1) break;
    }
    /* AI2rMelee_PrepareForUse's part: the class's steps and transitions, each technique's flags */
    if (mp && mp->prepared) {
        printf("prepared: jump %.2f long %.2f, steps %.2f %.2f %.2f %.2f %.2f, %u transitions\n", mp->jump_time,
               mp->long_jump_time, mp->dist[2], mp->dist[3], mp->dist[4], mp->dist[5], mp->dist[6], mp->ntransitions);
        for (uint32_t i = 0; i < mp->nattack + mp->nevade + mp->nmaneuver; i++) {
            const MeleeTech *k = &mp->tech[i];
            printf("  %-20s %08x damage %u reach %.1f near %.1f\n", k->name, k->computed, k->damage, k->reach, k->near);
        }
        if (mp->ntransitions < 4) failed |= 8;
    }
    printf("combat after %d ticks, %d techniques, %d attacks, konoko %u -> %u hit points\n", combat_at, techniques, attacks, hp0,
           p->c.hit_points);
    if (combat_at < 0 || techniques < 2 || attacks < 2 || p->c.hit_points >= hp0) failed |= 2;
    /* the block (AI2rBlockFunction): THUG_Wh blocks 25% alone, times 0.6, 1 or 1.3 by the difficulty */
    if (t->ai.state == AI_STATE_COMBAT) {
        const float want[3] = { 15.0f, 25.0f, 32.5f };
        for (int d = 0; d < 3; d++) {
            persist_set_difficulty(d);
            int n = 0;
            for (int k = 0; k < 20000; k++) n += melee_block(&t->ai);
            float pc = n / 200.0f;
            printf("difficulty %d: blocks %.1f%% (%.1f%%)\n", d, pc, want[d]);
            if (fabsf(pc - want[d]) > 1.5f) failed |= 4;
        }
        persist_set_difficulty(1);
        t->ai.flags |= AI_FLAG_PASSIVE;
        if (melee_block(&t->ai)) failed |= 4;
        t->ai.flags &= ~AI_FLAG_PASSIVE;
    } else {
        failed |= 4;
    }
    /* the blows' impact types (HandleAttackMask): Konoko's "konflash1" hit by its class's attack impacts
       (Kon_Super_Punch, heavy); a right fist's (part 18) hit, block and kill by her body part impacts;
       a blocked "konflash1" by the body parts too */
    {
        int m1, m2, m3, m4, m5;
        int super = impact_melee_type(&p->c, "konflash1", 0, 1u << 18, &m1, false);
        int hit = impact_melee_type(&p->c, "", 0, 1u << 18, &m2, false);
        int block = impact_melee_type(&p->c, "", 1, 1u << 18, &m3, false);
        int kill = impact_melee_type(&p->c, "", 3, 1u << 18, &m4, false);
        int blocked_super = impact_melee_type(&p->c, "konflash1", 1, 1u << 18, &m5, false);
        printf("impacts: konflash1 %d (Kon_Super_Punch %d) modifier %d; fist hit %d, blocked %d, killed %d; "
               "konflash1 blocked %d\n", super, impact_type_by_name("Kon_Super_Punch"), m1, hit, block, kill, blocked_super);
        if (super < 0 || super != impact_type_by_name("Kon_Super_Punch") || m1 != 1 || hit < 0 || block < 0 || kill < 0 ||
            blocked_super != block || m3 != 0)
            failed |= 16;
    }
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("melee_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
