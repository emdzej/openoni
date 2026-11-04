/* The AI's pursuit (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, a blind thug on
   the training room's floor hears gunfire from Konoko 60 behind it: the alert goes high, it pursues,
   hurrying to the sound's place (the pursuit's go-to mode), then looks around there; as the contact
   fades it gives up and goes back to its job. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "knowledge.h"
#include "physics.h"
#include "pursuit.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void prepare(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

static const float AT[3] = { -1101.0f, -340.0f, -126.3f };

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("pursuit_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "pursuit_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "pursuit_test: %s\n", err);
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
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    knowledge_reset();
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    if (!p || !t || !env) {
        fprintf(stderr, "pursuit_test: no player or thug\n");
        return 1;
    }
    physics_place(&p->c, AT);
    physics_place(&t->c, (const float[3]){ AT[0], AT[1], AT[2] - 60.0f });
    t->c.facing = t->c.desired_facing = 3.14159265f;      /* away from her */
    t->ai.patrol = -1;
    t->ai.state = t->ai.job = AI_STATE_IDLE;
    t->ai.alert = 0;
    t->ai.flags |= AI_FLAG_BLIND;
    printf("thug: pursuit modes %d %d %d %d, lost %d, least alert %d, distance %.0f\n", t->ai.pursue_strong_low,
           t->ai.pursue_weak_low, t->ai.pursue_strong_high, t->ai.pursue_weak_high, t->ai.pursue_lost, t->ai.pursue_alert,
           t->ai.combat_set.pursuit_distance);
    knowledge_post(t, KNOW_SOUND_GUNFIRE, KNOW_STRONG, p->c.pos, p, 0, false, false);
    int pursuit_at = -1, goto_at = -1, ran = 0, reached_at = -1, look_at = -1, back_at = -1;
    float d0 = 0.0f, dmin = 1e9f;
    for (int k = 0; k < 6000 && back_at < 0; k++) {
        ui_update();
        knowledge_update(env);
        chars_update(&(CharacterInput){ 0 }, env);
        float dx = p->c.pos[0] - t->c.pos[0], dz = p->c.pos[2] - t->c.pos[2], d = sqrtf(dx * dx + dz * dz);
        if (!k) d0 = d;
        if (t->ai.state == AI_STATE_PURSUIT) {
            if (pursuit_at < 0) pursuit_at = k;
            if (t->ai.pursuit.mode == PURSUIT_GOTO && goto_at < 0) goto_at = k;
            if (t->ai.pursuit.mode == PURSUIT_GOTO && pursuit_hurrying(&t->ai)) ran++;
            if (t->ai.pursuit.reached && reached_at < 0) reached_at = k;
            if (t->ai.pursuit.mode == PURSUIT_LOOK && look_at < 0) look_at = k;
            if (d < dmin) dmin = d;
        } else if (pursuit_at >= 0) {
            back_at = k;
        }
        if (getenv("PURSUIT_TRACE") && k % 30 == 0)
            printf("T %d state %d mode %d timer %u alert %d d %.1f know %d str %d look %d\n", k, t->ai.state,
                   t->ai.pursuit.mode, t->ai.pursuit.timer, t->ai.alert, d, t->ai.know_count,
                   t->ai.target_entry ? t->ai.target_entry->strength : -1, t->ai.aim.look);
    }
    printf("alert %d; pursuit from %d, going from %d (hurrying %d ticks), %.0f -> %.0f from her, reached at %d, looking "
           "from %d, back to state %d at %d\n", t->ai.alert, pursuit_at, goto_at, ran, d0, dmin, reached_at, look_at,
           t->ai.state, back_at);
    if (pursuit_at < 0 || goto_at < 0 || ran == 0) failed |= 1;
    if (reached_at < 0 || dmin > 20.0f || look_at < 0) failed |= 2;
    if (back_at < 0 || t->ai.state != AI_STATE_IDLE) failed |= 4;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("pursuit_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
