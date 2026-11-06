/* The cheat codes (OPENONI_DATA=<dir>, default ./game; skipped without it), on Konoko of level 1: no
   code works before the game is won; then a code typed (after other letters too) switches its cheat
   and shows the exe's message for the new state, a second time back; capitals are not letters to it.
   Fat loot fills the inventory, last man standing turns every team against every other, regeneration
   heals the player a point a second. */
#include "chars.h"
#include "cheat.h"
#include "chr_hit.h"
#include "exe.h"
#include "inventory.h"
#include "knowledge.h"
#include "persist.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"
#include "vfs.h"
#include "vfs_host.h"

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

static void type(const char *s)
{
    while (*s) cheat_key((uint8_t)*s++);
}

static int failed;

static void check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "cheat_test: %s\n", what);
        failed++;
    }
}

static bool says(const char *m) { return cheat_message() && !strcmp(cheat_message(), m); }

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("cheat_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "cheat_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "cheat_test: %s\n", err);
        return 1;
    }
    prepare();
    chars_load();
    Chr *p = chars_player();
    if (!p) {
        fprintf(stderr, "cheat_test: no player\n");
        return 1;
    }
    Character *c = &p->c;
    c->ammo = c->cells = c->hypos = 0;

    type("fatloot");
    check(!cheat_message() && c->ammo == 0, "a code before the game is won");

    persist_set_won_game();
    type("FATLOOT");
    check(!cheat_message() && c->ammo == 0, "capitals");
    type("xyzfatloot");
    check(says("Fat Loot Received") && c->ammo == 6 && c->cells == 6 && c->hypos == 6, "fatloot");

    type("liveforever");
    check(says("Invincibility Enabled") && chr_cheat_invincible, "liveforever on");
    type("liveforever");
    check(says("Invincibility Disabled") && !chr_cheat_invincible, "liveforever off");

    check(ai_team_relation(0, 1) == 0, "Konoko and the TCTF");
    type("reservoirdogs");
    check(says("Last Man Standing Enabled") && ai_team_relation(0, 1) == 2, "reservoirdogs on");
    type("reservoirdogs");
    check(ai_team_relation(0, 1) == 0, "reservoirdogs off");

    type("elderrune");
    check(says("Regeneration Enabled") && inventory_cheat_regeneration, "elderrune");
    c->hit_points = c->max_hit_points - 20;
    c->hypo_heal = 0;
    c->heal_wait = 0;
    for (int i = 0; i < 600; i++) ui_update(), inventory_update(c);
    check(c->hit_points == c->max_hit_points - 10, "regeneration: ten points in ten seconds");

    /* shapeshifter: F8 steps the class through the ONCC instances, Shift+F8 back */
    const void *k0 = c->oncc;
    check(!cheat_shapeshift(c, false) && c->oncc == k0, "no shapeshifting without the code");
    type("shapeshifter");
    check(cheat_change_characters, "shapeshifter");
    bool next = cheat_shapeshift(c, false);
    const void *k1 = c->oncc;
    bool back = cheat_shapeshift(c, true);
    printf("cheat_test: shapeshifter: %s -> %s -> %s\n", tm_handle_instance(tm_handle_of(k0))->name,
           tm_handle_instance(tm_handle_of(k1))->name, tm_handle_instance(tm_handle_of(c->oncc))->name);
    check(next && k1 != k0 && back && c->oncc == k0 && c->anim, "the class one on and back");

    cheat_message_clear();
    check(!cheat_message(), "the message cleared");
    if (!failed) printf("cheat_test: ok\n");
    return failed ? 1 : 0;
}
