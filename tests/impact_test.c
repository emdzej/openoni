/* The impact effects on level 1 (OPENONI_DATA=<dir>, default ./game; skipped without it): the impact
   types' and materials' trees (one root each, every type after its parent, Blunt and Glass marked down
   their branches), the ONIE block read at its exact size with every name in it found, its particle
   classes (but three the data hasn't) and impulses found once the level's classes are loaded, and a lookup that climbs the trees:
   a pistol's bullet on stone finds its effects, and on a material below another it finds the parent's
   for what the material has none of; and the animation types' impacts read from the exe's code. */
#include "exe.h"
#include "impact.h"
#include "particle.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "impact_test: %s\n", what);
        failed++;
    }
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("impact_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "impact_test: %s\n", err);
        return 1;
    }
    impact_types_load();
    int nt = impact_type_count(), nm = impact_material_count();
    check(nt == 94 && nm == 30, "every Impt and Mtrl in its tree");
    check(impact_type_parent(0) == -1 && impact_material_parent(0) == -1, "the roots first");
    for (int t = 1; t < nt; t++) check(impact_type_parent(t) >= 0 && impact_type_parent(t) < t, "an impact type after its parent");
    for (int m = 1; m < nm; m++) {
        int p = impact_material_parent(m);
        check(p >= 0 && p < m && impact_material_depth(m) == impact_material_depth(p) + 1, "a material after its parent");
    }
    int blunt = impact_type_by_name("Blunt"), glass = impact_material_by_name("Glass"), unbreak = impact_material_by_name("Unbreak_Glass");
    check(blunt > 0 && impact_type_blunt(blunt) && !impact_type_blunt(0), "Blunt marked");
    check(glass > 0 && impact_material_glass(glass) && unbreak > 0 && !impact_material_glass(unbreak), "Glass marked, not Unbreak_Glass");
    for (int t = 0; t < nt; t++)
        if (impact_type_parent(t) == blunt) check(impact_type_blunt(t), "Blunt's children blunt");

    impact_effects_load();
    uint32_t n[5];
    check(impact_effects_counts(n), "the ONIE block read");
    printf("impact_test: %d types, %d materials; ONIE %u impacts, %u materials, %u particles, %u sounds, %u entries\n", nt, nm,
           n[0], n[1], n[2], n[3], n[4]);

    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "impact_test: %s\n", err);
        return 1;
    }
    p3_load();
    impact_effects_level_begin();
    printf("impact_test: %d particle classes not found\n", impact_effects_unresolved());
    /* the table names three hit flashes no file has (h2h_thuflash_e01, h2h_tclflash_e01, h2h_griflash_e01) */
    check(impact_effects_unresolved() == 6, "every impact particle's class found but the three missing ones");

    int bullet = impact_type_by_name("Bullet"), stone = impact_material_by_name("Stone");
    check(bullet >= 0 && stone >= 0, "Bullet and Stone");
    int32_t e[3];
    impact_lookup(bullet, stone, 0, e);
    printf("impact_test: Bullet on Stone: %d %d %d\n", e[0], e[1], e[2]);
    check(e[0] >= 0 || e[1] >= 0 || e[2] >= 0, "a bullet on stone has effects");
    /* a child material with nothing of its own takes the parent's */
    int found = 0, inherited = 0;
    for (int m = 1; m < nm; m++) {
        int32_t a[3], b[3];
        impact_lookup(bullet, m, 0, a);
        impact_lookup(bullet, impact_material_parent(m), 0, b);
        for (int k = 0; k < 3; k++) {
            if (a[k] >= 0) found++;
            if (a[k] >= 0 && a[k] == b[k]) inherited++;
        }
    }
    printf("impact_test: Bullet: %d effects over the materials, %d of them a parent's\n", found, inherited);
    check(found > 0 && inherited > 0, "the lookup climbs the materials");
    /* the animation types' impacts, read out of 0x4f04f0's instructions: 136 pairs; a run (3) the
       class's impact 1, the turns (7, 8) 9 */
    int nanim = 0;
    for (int t = 0; t < 0x1d2; t++) nanim += impact_animation_index((uint16_t)t) >= 0;
    printf("impact_test: %d animation types with impacts; run %d, turns %d %d\n", nanim, impact_animation_index(3),
           impact_animation_index(7), impact_animation_index(8));
    check(nanim == 136 && impact_animation_index(3) == 1 && impact_animation_index(7) == 9 && impact_animation_index(8) == 9,
          "the animation types' impacts");
    if (!failed) printf("impact_test: ok\n");
    return failed ? 1 : 0;
}
