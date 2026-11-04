/* The inventory over time (OPENONI_DATA=<dir>, default ./game; skipped without it), on Konoko of level 1
   with the game settings (ONGS: the over-health cap 2, a hypo's dose 0.25, the over part 1): a hypo
   used at half health heals a quarter of the maximum, a point at a time at the class's delay; one used
   at full health takes her over it, a point a tick, and the over part runs down a point every 11 ticks;
   none is used past the maximum. Invisibility counts down, the body fading to nothing and back in its
   last 120 ticks; past its first 90 ticks the AI's sight passes her by (knowledge.c). */
#include "chars.h"
#include "exe.h"
#include "inventory.h"
#include "tm.h"
#include "trac.h"
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

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("inventory_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "inventory_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "inventory_test: %s\n", err);
        return 1;
    }
    prepare();
    chars_load();
    Chr *p = chars_player();
    if (!p) {
        fprintf(stderr, "inventory_test: no player\n");
        return 1;
    }
    Character *c = &p->c;
    int failed = 0;
    uint32_t max = c->max_hit_points;
    const uint8_t *k = (const uint8_t *)c->oncc + 0x5c;
    uint16_t delay = (uint16_t)(k[0] | k[1] << 8);

    /* half health, a hypo: a quarter of the maximum to come */
    c->hit_points = max / 2;
    c->hypos = 2;
    bool used = inventory_use_hypo(c);
    uint32_t heal = c->hypo_heal;
    int ticks = 0;
    while (c->hypo_heal && ticks < 100000) inventory_update(c), ticks++;
    printf("max %u, heal delay %u: a hypo at %u heals %u, done in %d ticks, to %u\n", max, delay, max / 2, heal, ticks,
           c->hit_points);
    if (!used || heal != max / 4 || c->hit_points != max / 2 + max / 4 || c->hypos != 1) failed |= 1;
    if (ticks < (int)(heal - 1) * (int)delay) failed |= 2;

    /* full health: over the maximum, a point a tick, then down a point every 11 */
    c->hit_points = max;
    used = inventory_use_hypo(c);
    heal = c->hypo_heal;
    ticks = 0;
    while (c->hypo_heal && ticks < 100000) inventory_update(c), ticks++;
    uint32_t top = c->hit_points;
    bool again = inventory_use_hypo(c);
    int down = 0;
    while (c->hit_points > max && down < 100000) inventory_update(c), down++;
    printf("a hypo at full health: +%u in %d ticks (to %u), another refused %d, back to %u in %d ticks\n", heal, ticks,
           top, !again, c->hit_points, down);
    if (!used || heal != max || top != 2 * max || ticks > (int)heal + 1) failed |= 4;
    if (again || c->hit_points != max || down < (int)max * 10 || down > (int)max * 12) failed |= 8;

    /* invisibility for 600 ticks */
    c->invisibility = 600;
    float lowest = 1.0f;
    int gone_at = -1, back_at = -1, end_at = -1, unseen_at = -1;
    for (int t = 0; t < 800; t++) {
        inventory_update(c);
        inventory_invis_update(c);
        if (c->visibility < lowest) lowest = c->visibility;
        if (c->visibility == 0.0f && gone_at < 0) gone_at = t;
        if (gone_at >= 0 && c->visibility == 1.0f && back_at < 0) back_at = t;
        if (!c->invisibility && end_at < 0) end_at = t;
        if (c->invisibility && c->invisible_ticks >= 90 && unseen_at < 0) unseen_at = t;
    }
    printf("invisible 600: gone at %d (lowest %.3f), all shown again at %d, over at %d, unseen from %d\n", gone_at, lowest,
           back_at, end_at, unseen_at);
    if (gone_at < 100 || gone_at > 130 || lowest != 0.0f) failed |= 16;
    if (end_at != 599 || back_at < 530 || back_at > 540) failed |= 32;
    if (unseen_at != 89) failed |= 64;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("inventory_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
