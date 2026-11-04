/* The turrets (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 2 (manplant) every Turret
   object finds its class and starts off; one, activated with Konoko 60 in front of it and off to its
   side, turns its head toward her within its limits, fires its shooters (the targeting's trigger) and
   its shots reach her; deactivated, it turns back to rest. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "particle.h"
#include "tm.h"
#include "trac.h"
#include "turret.h"
#include "ui.h"
#include "vfs.h"
#include "vfs_host.h"
#include "weapon.h"

#include <math.h>
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
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("turret_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "turret_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(2);
    if (!tm_level_load(2, err, sizeof err)) {
        fprintf(stderr, "turret_test: %s\n", err);
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
    p3_load();
    p3_set_environment(env);
    weapons_install();
    chars_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    Chr *p = chars_player();
    int n = turret_load(), off = 0;
    for (int i = 0; i < n; i++) off += turret_get(i)->state == 0 && turret_get(i)->cls != NULL;
    printf("level2: %d turrets, %d off with their classes\n", n, off);
    if (n != 7 || off != n || !p) failed |= 1;
    Turret *t = turret_get(1);     /* id 2, a sap_turret_wall with a clear view (the first has a wall at its side) */
    if (t && p) {
        /* 60 ahead of its face (the base's z axis) and 20 to its side, at its height less 10 */
        float at[3];
        for (int j = 0; j < 3; j++) at[j] = t->pos[j] + t->base[6 + j] * 60.0f + t->base[j] * 20.0f;
        at[1] = t->pos[1] - 25.0f;     /* (the walkway's floor is about 40 below the turret) */
        memcpy(p->c.pos, at, sizeof at);
        uint32_t hp0 = p->c.hit_points;
        turret_activate(t->id);
        int fired = -1, firing_ticks = 0;
        for (int k = 0; k < 400; k++) {
            ui_update();
            turret_update(env);
            Character *pl = &p->c;
            p3_update(ui_time(), true, &pl, 1);
            if (t->flags & TURRET_FIRING) {
                firing_ticks++;
                if (fired < 0) fired = k;
            }
            memcpy(p->c.pos, at, sizeof at);       /* she stands */
        }
        float want = atan2f(20.0f, 60.0f);
        printf("active: target %s, yaw %.3f (her side %.3f), pitch %.3f, los %d, firing from tick %d (%d ticks), hp %u -> %u\n",
               t->t.target == &p->c ? "Konoko" : "none", t->yaw, want, t->pitch, t->los, fired, firing_ticks, hp0, p->c.hit_points);
        if (t->t.target != &p->c || fabsf(t->yaw - want) > 0.1f || fired < 0) failed |= 2;
        if (p->c.hit_points >= hp0) failed |= 4;
        turret_deactivate(t->id);
        for (int k = 0; k < 300; k++) turret_update(env);
        printf("off: state %d, yaw %.3f pitch %.3f\n", t->state, t->yaw, t->pitch);
        if (t->state != 0 || t->yaw > 0.01f || t->pitch > 0.01f) failed |= 8;
    }
    turret_unload();
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("turret_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
