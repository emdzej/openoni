/* The character list (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, the characters
   there at the start, then scripts that spawn one twice (the second refused), teleport it to a flag,
   animate the player and wait for an animation type, stage the combat demo (a team change, a fight to
   the death, the die script, the winner back to its job), throw a dummy, fire a pistol, and delete the spawned one. */
#include "chars.h"
#include "chr_hit.h"
#include "exe.h"
#include "ai.h"
#include "objc.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"
#include "weapon.h"
#include "particle.h"
#include "powerup.h"
#include "chr_anim.h"
#include "akev.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int count(void)
{
    int n = 0;
    for (int i = 0; i < chars_slots(); i++) n += chars_get(i) != NULL;
    return n;
}

static bool first_flag(const ObjcObject *o, void *user)
{
    if (o->tag != TM_TAG("FLAG") || o->size < 8) return true;
    *(ObjcObject *)user = *o;
    return false;
}

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

static bool done;
static bool cmd_done(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    done = true;
    return true;
}

static int died_script;
static bool cmd_died(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c >= 1) died_script = v[0].type == SL_FLOAT ? (int)v[0].f : v[0].i;
    return true;
}

typedef struct { float at[3]; int id; float best; } NearFlag;

static bool near_flag(const ObjcObject *o, void *user)
{
    NearFlag *f = user;
    float dx = o->pos[0] - f->at[0], dy = o->pos[1] - f->at[1], dz = o->pos[2] - f->at[2];
    float d = sqrtf(dx * dx + dz * dz);
    if (d > 20.0f && d < 60.0f && fabsf(dy) < 3.0f && d < f->best) f->best = d, f->id = (int)(o->data[6] | o->data[7] << 8);
    return true;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("chars_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "chars_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "chars_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;     /* the level's environment, for the fight's local paths */
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    int failed = 0;
    int n = chars_load();
    Chr *p = chars_player();
    printf("level1: start=%d player=%s script_id=%d\n", n, p ? p->name : "-", p ? p->script_id : 0);
    if (n != 1 || !p || strcmp(p->name, "char_0") || p->script_id != 0) {
        printf("chars_test: FAILED\n");
        return 1;
    }

    ObjcObject flag = { 0 };
    objc_each("Flag", first_flag, &flag);
    unsigned flag_id = (unsigned)(flag.data[6] | flag.data[7] << 8);
    char script[1024];
    snprintf(script, sizeof script,
             "func void main(void) {\n"
             "  ai2_spawn Train_TD_1\n"
             "  ai2_spawn Train_TD_1\n"     /* flag 0x10: may respawn */
             "  ai2_spawn shinatama\n"
             "  ai2_spawn shinatama\n"      /* no 0x10: refused */
             "  chr_teleport shinatama %u\n"
             "  chr_animate 0 KONOKOwatch_start 60\n"
             "  chr_wait_animtype 0 stand\n"
             "  test_done\n"
             "}\n", flag_id);
    sl_reset();
    chars_register_commands();
    sl_register("test_done", cmd_done, NULL);
    sl_register("test_died", cmd_died, NULL);
    if (!sl_load_text("chars_test", script, err, sizeof err) || !sl_call("main", 0, NULL)) {
        fprintf(stderr, "chars_test: %s\n", err);
        return 1;
    }
    sl_update();
    Chr *s = chars_find("shinatama");
    bool at_flag = s && fabsf(s->c.pos[0] - flag.pos[0]) < 0.01f && fabsf(s->c.pos[2] - flag.pos[2]) < 0.01f;
    int after_spawn = count();
    printf("level1: after spawns=%d shinatama at flag %u=%d player type=%u hold=%u\n", after_spawn, flag_id,
           at_flag, p->c.type, (unsigned)p->c.hold);
    if (after_spawn != 4 || !at_flag || p->c.hold != 61) failed++;
    /* the hold runs out, the player stands, the wait ends */
    int ticks = 0;
    for (; ticks < 600 && !done; ticks++) {
        chars_update(&(CharacterInput){ 0 }, NULL);
        sl_update();
    }
    printf("level1: waited %d ticks for stand: done=%d\n", ticks, done);
    if (!done) failed++;
    /* the warehouse's combat demo: demo2 made Syndicate fights demo3 (TCTF); a death runs the CHAR's die
       script (demo_over, defined here), and the winner, its target lost, waits the class's time and
       goes back to its job */
    snprintf(script, sizeof script,
             "func void demo_over(string who) { test_died 1 }\n"
             "func void demo3dies(string who) { test_died 3 }\n"
             "func void fight(void) {\n"
             "  ai2_spawn demo2\n  ai2_spawn demo3\n"
             "  chr_changeteam demo2 Syndicate\n  chr_nocollision demo2 1\n"
             "  ai2_setjobstate demo3\n"
             "}\n");
    if (!sl_load_text("chars_fight", script, err, sizeof err) || !sl_call("fight", 0, NULL)) failed++;
    sl_update();
    Chr *d2 = chars_find("demo2"), *d3 = chars_find("demo3");
    printf("level1: demo2 team=%u nocollision=%d demo3 job=%d\n", d2 ? d2->team : 99u, d2 && (d2->c.flags & 0x20000),
           d3 ? d3->ai.job : -1);
    if (!d2 || !d3 || d2->team != 2 || !(d2->c.flags & 0x20000)) failed++;
    int fight_ticks = 0, fought = 0, back = -1;
    uint32_t d2_hp = d2 ? d2->c.hit_points : 0;
    bool hurt = false;
    for (; d2 && d3 && fight_ticks < 9000; fight_ticks++) {
        /* the port's melee positioning is a stand-in: a fight may stall with neither down. Once demo2
           has been hurt, a last blow from demo3 at 6000 ticks ends it */
        if (d2->c.hit_points < d2_hp) hurt = true;
        if (fight_ticks == 6000 && hurt && !((d2->c.flags | d3->c.flags) & 1)) chr_take_damage(&d2->c, d2->c.hit_points, 0.0f, NULL, 0, &d3->c);
        ui_update();    /* the game time: the combat state thinks every fifth tick of it */
        chars_update(&(CharacterInput){ 0 }, env);
        sl_update();
        if (d3->ai.state == AI_STATE_COMBAT || d2->ai.state == AI_STATE_COMBAT) fought = 1;
        bool one_dead = (d2->c.flags & 1) || (d3->c.flags & 1);
        Chr *alive = (d2->c.flags & 1) ? d3 : d2;
        if (fought && one_dead && alive->ai.state != AI_STATE_COMBAT) { back = alive->ai.state; break; }
    }
    printf("level1: fight %d ticks: fought=%d hurt=%d demo2 dead=%d demo3 dead=%d die script of %d, winner's state=%d\n",
           fight_ticks, fought, hurt, d2 && (d2->c.flags & 1), d3 && (d3->c.flags & 1), died_script, back);
    if (!fought || !hurt || back < 0 || died_script != ((d2->c.flags & 1) ? 1 : 3)) failed++;
    /* a throw: the throw dummy 10 units in front of the player, facing her; forward and punch (Punch_Forward)
       become Throw_Forward_Punch (0x6a), the dummy takes the victim's animation (type 96) and its own
       damage (its flag 0x1000, being thrown, lasts until its AI asks for an animation: the same tick) */
    /* the fight next to her may have hit her and left her facing off the way she wants to face, and its
       survivor would go for the (Syndicate) dummy, hitting her too: where it ended is chance, so the
       throw starts with her standing (a player knocked down lies, state 18, until a key: forward gets her
       up), her facing settled and the survivor far away */
    if (d2 && d3) ((d2->c.flags & 1) ? d3 : d2)->c.pos[0] += 1000.0f;
    for (int k = 0, stood = 0; k < 900 && stood < 60; k++) {
        stood = p->c.state == CHR_STATE_STANDING ? stood + 1 : 0;
        chars_update(&(CharacterInput){ .forward = p->c.state == 18 }, NULL);
        sl_update();
    }
    p->c.facing = p->c.desired_facing;
    snprintf(script, sizeof script, "func void dummy(void) {\n  ai2_spawn dum_train_throw\n}\n");
    if (!sl_load_text("chars_dummy", script, err, sizeof err) || !sl_call("dummy", 0, NULL)) failed++;
    sl_update();
    Chr *dum = chars_find("dum_train_throw");
    int threw = 0, thrown = 0;
    uint32_t hp0 = dum ? dum->c.hit_points : 0;
    if (dum) {
        float f = p->c.facing;
        dum->c.pos[0] = p->c.pos[0] + 10.0f * sinf(f);
        dum->c.pos[1] = p->c.pos[1];
        dum->c.pos[2] = p->c.pos[2] + 10.0f * cosf(f);
        dum->c.facing = f < 3.1415927f ? f + 3.1415927f : f - 3.1415927f;
        dum->c.desired_facing = dum->c.facing;
        dum->ai.flags |= AI_FLAG_BLIND;     /* seeing her, it would startle out of the throw (by chance of the tick) */
        for (int k = 0; k < 300; k++) {
            CharacterInput in = { 0 };
            if (k < 12) in.forward = 1, in.punch = true;
            chars_update(&in, NULL);
            sl_update();
            if (p->c.type == 0x6a && p->c.threw == &dum->c) threw = 1;
            if (dum->c.type == 96 && dum->c.thrown_by == &p->c) thrown = 1;
        }
    }
    printf("level1: throw: thrower=%d thrown=%d dummy hp %u -> %u\n", threw, thrown, hp0, dum ? dum->c.hit_points : 0);
    if (!threw || !thrown || dum->c.hit_points >= hp0) failed++;
    /* a weapon: the pistol in the player's hand (the 0x800 variant), a shot takes one ammo and plays the
       recoil overlay (Autopistol_Recoil, 149); its shooters are particles, and the shot emits a projectile */
    p3_load();
    weapons_reset();
    weapons_register_commands();
    snprintf(script, sizeof script, "func void gun(void) {\n  chr_giveweapon 0 w1_tap\n}\n");
    if (!sl_load_text("chars_gun", script, err, sizeof err) || !sl_call("gun", 0, NULL)) failed++;
    sl_update();
    int recoil = 0, most = 0, shooters = -1;
    uint16_t full = p->c.weapon ? p->c.weapon->ammo : 0;
    for (int k = 0; k < 60 && p->c.weapon; k++) {
        CharacterInput in = { 0 };
        in.fire = k == 30;
        chars_update(&in, NULL);
        weapons_update(NULL);
        Character *pl = &p->c;
        p3_update(300 + (uint32_t)k, true, &pl, 1);
        if (k == 29) shooters = p3_live_count();
        if (p3_live_count() > most) most = p3_live_count();
        sl_update();
        if (p->c.overlay[CHR_OVERLAY_RECOIL].anim && tr_type(p->c.overlay[CHR_OVERLAY_RECOIL].anim) == 149) recoil = 1;
    }
    printf("level1: weapon: varient=%04x ammo %u -> %u recoil=%d particles %d -> %d\n", p->c.varient, full,
           p->c.weapon ? p->c.weapon->ammo : 0, recoil, shooters, most);
    if (!p->c.weapon || !(p->c.varient & 0x800) || p->c.weapon->ammo + 1 != full || !recoil || shooters <= 0 || most <= shooters)
        failed++;
    /* the reload: emptied, the weapon is "empty" to the scripts until a clip comes; a powerup of
       ammo at her feet goes into the inventory with the pickup key; the reload key then plays the
       class's reload overlay (0xa4..0xab), which fills the weapon at its action frame */
    int empty_before = 0, empty_after = 0, reloaded = 0;
    uint16_t clips = 0;
    if (p->c.weapon) {
        p->c.weapon->ammo = 0;
        chr_force_stand(&p->c);     /* from the throws' end state */
        for (int k = 0; k < 30; k++) {
            CharacterInput in = { 0 };
            chars_update(&in, NULL);
        }
        powerups_reset();
        powerup_register_commands();
        empty_before = weapon_has_empty(&p->c);
        float at[3] = { p->c.pos[0], p->c.pos[1], p->c.pos[2] };
        powerup_new(POWERUP_AMMO, powerup_default_amount(POWERUP_AMMO), at, 0.0f);
        for (int k = 0; k < 120; k++) {
            CharacterInput in = { 0 };
            in.swap = k == 0;
            chars_update(&in, NULL);
            weapons_update(NULL);
            sl_update();
        }
        clips = p->c.ammo;
        empty_after = weapon_has_empty(&p->c);
        for (int k = 0; k < 200 && !reloaded; k++) {
            CharacterInput in = { 0 };
            in.reload = k == 0;
            chars_update(&in, NULL);
            weapons_update(NULL);
            sl_update();
            if (p->c.weapon->ammo == full) reloaded = k;
        }
    }
    printf("level1: reload: empty %d, clips %u (powerups %d), empty %d, refilled after %d ticks, clips %u\n", empty_before, clips,
           powerup_count(), empty_after, reloaded, p->c.ammo);
    if (!empty_before || clips != 1 || powerup_count() || empty_after || !reloaded || p->c.ammo) failed++;
    /* ai2_takecontrol: the player's AI walks her to a flag 20 to 60 away (the nearest such), the keys
       doing nothing meanwhile; ai2_takecontrol 0 gives them back */
    {
        NearFlag nf = { { p->c.pos[0], p->c.pos[1], p->c.pos[2] }, -1, 1e9f };
        objc_each("Flag", near_flag, &nf);
        snprintf(script, sizeof script, "func void ctl(void) {\n  ai2_takecontrol 1\n  ai2_movetoflag 0 %d\n}\n"
                 "func void ctl_off(void) {\n  ai2_takecontrol 0\n}\n", nf.id);
        if (!sl_load_text("chars_ctl", script, err, sizeof err) || !sl_call("ctl", 0, NULL)) failed++;
        sl_update();
        ObjcObject fl;
        float d0 = 0.0f, d1 = 0.0f;
        bool flag_on = (p->c.flags & CHR_FLAG_AI_MOVEMENT) != 0;
        if (nf.id >= 0 && objc_flag_find((uint16_t)nf.id, &fl)) {
            d0 = sqrtf((fl.pos[0] - p->c.pos[0]) * (fl.pos[0] - p->c.pos[0]) + (fl.pos[2] - p->c.pos[2]) * (fl.pos[2] - p->c.pos[2]));
            for (int k = 0; k < 600; k++) {
                CharacterInput in = { .forward = -1 };     /* back, held: ignored */
                chars_update(&in, NULL);
                sl_update();
            }
            d1 = sqrtf((fl.pos[0] - p->c.pos[0]) * (fl.pos[0] - p->c.pos[0]) + (fl.pos[2] - p->c.pos[2]) * (fl.pos[2] - p->c.pos[2]));
        }
        if (!sl_call("ctl_off", 0, NULL)) failed++;
        sl_update();
        printf("level1: takecontrol: flag %d, %.1f -> %.1f, on %d off %d\n", nf.id, d0, d1, flag_on, (p->c.flags & CHR_FLAG_AI_MOVEMENT) != 0);
        /* arrived within 4 (the move's), the walk's stop carries her a few units on */
        if (nf.id < 0 || !flag_on || d1 > 8.0f || (p->c.flags & CHR_FLAG_AI_MOVEMENT)) failed++;
    }
    snprintf(script, sizeof script, "func void del(void) {\n  chr_delete shinatama\n  chr_delete \"0\"\n}\n");
    if (!sl_load_text("chars_del", script, err, sizeof err) || !sl_call("del", 0, NULL)) failed++;
    sl_update();
    printf("level1: after deletes=%d player=%d\n", count(), chars_player() != NULL);
    if (count() != 5 || chars_player()) failed++;  /* Train_TD_1 twice, demo2 (a body stays: ONCC+0xc8a is 0), demo3, the dummy */
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("chars_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
