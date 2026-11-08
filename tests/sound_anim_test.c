/* The animation sounds (OPENONI_DATA=<dir>, default ./game; skipped without it): level 0's SABD records
   over the variant list, an animation's own sound and its frame, and the impulses the variants' punches
   find (a modifier from the damage, the variant's own records before its parents'). */
#include "chr_anim.h"
#include "exe.h"
#include "sound.h"
#include "sound_anim.h"
#include "tm.h"
#include "tram.h"
#include "vfs.h"
#include "vfs_host.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void check(bool ok, const char *what)
{
    if (!ok) fprintf(stderr, "sound_anim_test: %s\n", what), failed++;
}

static const void *variant(const char *name) { return tm_instance_get(TM_TAG("ONCV"), name); }

static uint16_t largest_damage(const void *tram)
{
    uint16_t d = 0;
    const uint8_t *a;
    for (unsigned i = 0; (a = tr_attack(tram, i)) != NULL; i++)
        if ((uint16_t)(a[0xc] | a[0xd] << 8) > d) d = (uint16_t)(a[0xc] | a[0xd] << 8);
    return d;
}

static void *trams[4096];

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("sound_anim_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "sound_anim_test: %s\n", err);
        return 1;
    }
    sound_initialize();
    check(sound_level_load(0), "no sound data");
    sound_anim_load();

    /* KONOKOidle_spec3: kon_snz at frame 45 (a record of the kind "Animation") */
    const void *idle = tm_instance_get(TRAM_TAG, "KONOKOidle_spec3");
    check(idle && sound_anim_at_frame(idle, 45) && !sound_anim_at_frame(idle, 0), "idle_spec3's frame");
    check(idle && sound_anim_impulse(variant("konoko"), idle) == sound_impulse_find("kon_snz"), "idle_spec3's impulse");

    /* a heavy punch (the kind 13 by the type table, damage 15 and over): heavy_arm through Any for a
       striker, kon_pnch_lt from Konoko's own records (theirs for any modifier) */
    int n = tm_instance_list(TRAM_TAG, trams, 4096), punches = 0;
    for (int i = 0; i < n; i++) {
        uint16_t type = tr_type(trams[i]);
        if (type >= 0xe9 || (int32_t)exe_u32(0x557ad0 + 12u * type + 4) != 13 || largest_damage(trams[i]) < 15) continue;
        if (sound_anim_at_frame(trams[i], 0) != true) continue;
        punches++;
        check(sound_anim_impulse(variant("striker"), trams[i]) == sound_impulse_find("heavy_arm"), "striker's heavy punch");
        check(sound_anim_impulse(variant("konoko"), trams[i]) == sound_impulse_find("kon_pnch_lt"), "Konoko's punch");
    }
    check(punches > 0, "no heavy punch");
    check(sound_impulse_find("heavy_arm") && sound_impulse_find("kon_pnch_lt"), "the impulses");

    sound_anim_unload();
    check(!sound_anim_impulse(variant("konoko"), idle), "unloaded");
    printf(failed ? "sound_anim_test: FAILED\n" : "sound_anim_test: ok (%d heavy punches)\n", punches);
    return failed ? 1 : 0;
}
