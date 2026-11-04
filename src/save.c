/* The continue: see save.h. */
#include "save.h"

#include "chars.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "ui.h"
#include "weapon.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

/* The continue (0x5ecc10, 0x81 words): its name ("Save Point %d"), the player's health and maximum, place
   and facing, inventory, the LSI, and the weapons by class name and ammo. */
static struct {
    char name[64];
    bool valid;                     /* 0x5ecc50 bit 1 */
    uint32_t hp, max_hp;            /* 0x5ecc54 (with C+0x1ae, the port's 0), 0x5ecc58 */
    float pos[3], facing;           /* 0x5ecc5c, 0x5ecc68 */
    uint16_t ammo, cells, shield, invisibility, hypos;  /* 0x5ecc6c..0x5ecc7c */
    bool lsi;                       /* 0x5ecc84 */
    char weapon[64];                /* 0x5ecc88: the hand's class, empty none */
    uint16_t weapon_ammo;           /* 0x5ecd08 */
} cont;
static int32_t point;               /* 0x5ece88: the scripts' save_point */

void save_clear(void)
{
    memset(&cont, 0, sizeof cont);
    point = 0;
}

int32_t save_point(void) { return point; }

/* The weapon class's name (TMrInstance_GetInstanceName), without its tag. */
static bool class_name(const void *cls, char out[64])
{
    for (int f = 0; f < tm_file_slots(); f++) {
        TmFile *file = tm_file_at(f);
        for (uint32_t i = 0; file && i < tm_file_instance_count(file); i++) {
            const TmInstance *in = tm_file_instance(file, i);
            if (in->data == cls && in->name && strlen(in->name) > 4) {
                snprintf(out, 64, "%s", in->name + 4);
                return true;
            }
        }
    }
    return false;
}

/* save_game n [autosave] (AIiScript_SaveGame 0x4bd2e0, "%s game %d"): the continue at save point n;
   an autosave shows the "autosave" message for 120 ticks. Level 1's first point is "Syndicate
   Warehouse", its others one less (the training isn't one). */
static bool cmd_save(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    int32_t n = c >= 1 ? (v[0].type == SL_FLOAT ? (int32_t)v[0].f : v[0].i) : 0;
    bool autosave = c >= 2 && v[1].type == SL_STRING && v[1].s && !strcmp(v[1].s, "autosave");
    if (autosave) {
        const char *s = text_message("autosave");
        if (s) ui_print(UI_AREA_MESSAGE, s, "autosave", 120, 0xffffffffu);
    }
    Chr *p = chars_player();
    memset(&cont, 0, sizeof cont);      /* ("invalid save point" for 0, kept all the same) */
    point = n;
    if (!p) return true;
    snprintf(cont.name, sizeof cont.name, "Save Point %d", (int)n);
    cont.hp = p->c.hit_points;
    cont.max_hp = p->c.max_hit_points;
    memcpy(cont.pos, p->c.pos, sizeof cont.pos);
    cont.facing = p->c.facing;
    cont.ammo = p->c.ammo;
    cont.cells = p->c.cells;
    cont.shield = p->c.shield;
    cont.invisibility = p->c.invisibility;
    cont.hypos = p->c.hypos;
    cont.lsi = p->c.lsi;
    cont.valid = true;
    if (p->c.weapon && class_name(p->c.weapon->cls, cont.weapon)) cont.weapon_ammo = p->c.weapon->ammo;
    return true;
}

/* restore_game (0x4bd360 -> ONrGameState_UseContinue): with a save point, the player's health (scaled if
   the maximum changed), place, facing, inventory, the weapons gone and the saved one in the hand. */
static bool cmd_restore(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    Chr *p = chars_player();
    if (!p || !point || !cont.valid) return true;   /* "failed to continue, invalid save point" */
    if (p->c.max_hit_points == cont.max_hp) p->c.hit_points = cont.hp;
    else if (cont.max_hp) p->c.hit_points = (uint32_t)((uint64_t)p->c.max_hit_points * cont.hp / cont.max_hp);
    physics_place(&p->c, cont.pos);
    p->c.facing = p->c.desired_facing = cont.facing;
    p->c.ammo = cont.ammo;
    p->c.cells = cont.cells;
    p->c.shield = cont.shield;
    p->c.invisibility = cont.invisibility;
    p->c.hypos = cont.hypos;
    p->c.lsi = cont.lsi;
    if (p->c.weapon) {
        Weapon *w = p->c.weapon;
        chr_set_weapon(&p->c, NULL);
        weapon_delete(w);       /* 0x518f90 */
    }
    if (cont.weapon[0]) {
        const void *cls = tm_instance_get(TM_TAG("ONWC"), cont.weapon);
        Weapon *w = cls ? weapon_new(cls, p->c.pos, 0.0f) : NULL;  /* ONrCharacter_UseWeapon_NameAmmo */
        if (w) {
            chr_set_weapon(&p->c, w);
            w->ammo = cont.weapon_ammo;
        }
    }
    return true;
}

void save_register_commands(void)
{
    sl_register_int("save_point", &point);
    sl_register("save_game", cmd_save, NULL);
    sl_register("restore_game", cmd_restore, NULL);
}
