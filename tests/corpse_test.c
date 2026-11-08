/* The corpses (corpse.h): level 3's own (17 of 20, the cosmetic ones named "_lvl_3_..."), then a dead
   AI's body kept as one once it has settled and is deleted. */
#include "chars.h"
#include "chr_hit.h"
#include "corpse.h"
#include "exe.h"
#include "objc.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <stdlib.h>
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

/* the level's first character after the player's */
typedef struct { char name[32]; int seen; } FirstChar;
static bool first_char(const ObjcObject *o, void *user)
{
    FirstChar *f = user;
    if (f->seen++ == 0 || f->name[0]) return true;
    strncpy(f->name, objc_char_name(o), sizeof f->name - 1);
    return true;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("corpse_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "corpse_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(3);
    if (!tm_level_load(3, err, sizeof err)) {
        fprintf(stderr, "corpse_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("AKEV") && in->data) env = in->data;
    }
    chars_load();
    int n = corpse_count(), level = 0;
    for (int i = 0; i < n; i++)
        if (corpse_get(i)->oncc) level++;
    const Corpse *k0 = corpse_get(0);
    printf("level 3: %d slots, %d corpses, the first %s at (%.1f %.1f %.1f)\n", n, level, k0 ? k0->name : "-",
           k0 ? k0->m[0][9] : 0.0f, k0 ? k0->m[0][10] : 0.0f, k0 ? k0->m[0][11] : 0.0f);
    int failed = n != 20 || level != 17 || !k0 || strcmp(k0->name, "_lvl_3_Intro_TCL_A_corpse.dat");
    /* a dead AI: it settles, its body becomes the 18th */
    FirstChar fc = { "", 0 };
    objc_each("Character", first_char, &fc);
    Chr *t = fc.name[0] ? chars_spawn(fc.name, true) : NULL;
    int ticks = 0, made = -1;
    if (t) {
        chr_die(&t->c, NULL);
        for (; ticks < 2000; ticks++) {
            chars_tick(&(CharacterInput){ 0 }, env);
            if (getenv("CORPSE_TRACE") && ticks % 50 == 0)
                printf("  %d: flags %x air %u settled %u dying %u lod %d used %d ct %u inv %d\n", ticks, t->c.flags, t->c.air_ticks,
                       t->c.settled_ticks, t->c.dying_ticks, t->c.lod_shown, chars_in_use(t), t->corpse_time, t->c.invisible_shown);
            if (corpse_get(17)->oncc) {
                made = ticks;
                break;
            }
        }
    }
    const Corpse *k = corpse_get(17);
    printf("a dead AI's corpse after %d ticks: %s\n", made, k && k->oncc ? k->name : "-");
    if (made < 120 || !k || strcmp(k->name, "dynamic_corpse") || k->box[3] < k->box[0]) failed |= 2;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("corpse_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
