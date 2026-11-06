/* The AI's alarm (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, ai2_doalarm sends
   Bay4_Comguy_1 to alarm console 6, as the warehouse's hurt1 does: the alarm state runs it to the
   console's marker, where it uses the console (the console action, then OnActivate). */
#include "akev.h"
#include "chars.h"
#include "action.h"
#include "alarm.h"
#include "console.h"
#include "ui.h"
#include "chr_anim.h"
#include "exe.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"
#include "weapon.h"

#include <math.h>
#include <stdlib.h>
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
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("alarm_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "alarm_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "alarm_test: %s\n", err);
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
    action_install();
    chars_load();
    console_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    Chr *p = chars_player(), *t = chars_spawn("Bay4_Comguy_1", true);
    int ci = -1;
    for (int i = 0; i < console_count(); i++) if (console_id(i) == 6) ci = i;
    if (!p || !t || !env || ci < 0) {
        fprintf(stderr, "alarm_test: no player, comguy or console 6\n");
        return 1;
    }
    physics_place(&p->c, (const float[3]){ -1101.0f, -340.0f, -126.3f });   /* far away, in the training room */
    console_activate(6);
    printf("console 6: flags 0x%x, %d markers; comguy at %.0f %.0f %.0f, groups 0x%x\n", console_flags(ci),
           console_marker_count(), t->c.pos[0], t->c.pos[1], t->c.pos[2], t->ai.alarm_groups);
    t->ai.patrol = -1;
    t->ai.state = AI_STATE_IDLE;
    alarm_script(t, 6);
    int alarm_at = t->ai.state == AI_STATE_ALARM ? 0 : -1, use_at = -1, used_at = -1;
    float d0 = -1.0f;
    for (int k = 0; k < 3000 && used_at < 0; k++) {
        ui_update();
        chars_update(&(CharacterInput){ 0 }, env);
        weapons_update(env);
        weapons_physics(env);
        const ActionMarker *m = console_marker(t->ai.alarm_marker);
        if (m && d0 < 0) d0 = sqrtf((m->pos[0] - t->c.pos[0]) * (m->pos[0] - t->c.pos[0]) + (m->pos[2] - t->c.pos[2]) * (m->pos[2] - t->c.pos[2]));
        if (getenv("ALARM_TRACE") && k % 30 == 0)
            printf("T %d state %d %.1f %.1f %.1f type 0x%x moving %d tries %u\n", k, t->ai.state, t->c.pos[0], t->c.pos[1], t->c.pos[2],
                   t->c.type, t->ai.moving, t->ai.alarm_tries);
        if (use_at < 0 && t->c.console_marker >= 0) use_at = k;
        if (console_flags(ci) & CONSOLE_USED) used_at = k;
    }
    printf("alarm state %d, %.0f from the marker, console action at %d, console used at %d\n", alarm_at, d0, use_at, used_at);
    if (alarm_at < 0) failed |= 1;
    if (use_at < 0 || used_at < 0) failed |= 2;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("alarm_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
