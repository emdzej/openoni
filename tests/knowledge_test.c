/* The AI's knowledge (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, a thug on
   the training room's floor facing Konoko 60 away sees her (centrally: strength 4) within a second
   and goes to combat; turned away, it doesn't; punched from behind, it knows of her. */
#include "akev.h"
#include "chars.h"
#include "exe.h"
#include "knowledge.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

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

static const float AT[3] = { -1101.0f, -340.0f, -126.3f };

/* Konoko at AT, the thug 60 along z from her, turned by facing; ticks of updates. */
static void place(Chr *p, Chr *t, float facing)
{
    physics_place(&p->c, AT);
    physics_place(&t->c, (const float[3]){ AT[0], AT[1], AT[2] - 60.0f });
    t->c.facing = t->c.desired_facing = facing;
    t->ai.patrol = -1;
    t->ai.state = AI_STATE_IDLE;
    t->ai.alert = 0;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("knowledge_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "knowledge_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "knowledge_test: %s\n", err);
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
    printf("relations: konoko-syndicate %d, syndicate-konoko %d, neutral-konoko %d\n", ai_team_relation(0, 2),
           ai_team_relation(2, 0), ai_team_relation(3, 0));
    if (ai_team_relation(2, 0) != 2 || ai_team_relation(3, 0) != 1) failed |= 1;
    /* every other character out of the way: only the two */
    chars_load();
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && !c->player) chars_delete(c);
    }
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    if (!p || !t || !env) {
        fprintf(stderr, "knowledge_test: no player or thug\n");
        return 1;
    }
    /* turned away (facing -z, she is at +z): nothing in two seconds */
    place(p, t, 3.14159265f);
    for (int k = 0; k < 120; k++) chars_update(&(CharacterInput){ 0 }, env);
    KnowEntry *e = knowledge_find(t, p);
    printf("away: contact %s, alert %d, state %d\n", e ? "yes" : "no", t->ai.alert, t->ai.state);
    if (e || t->ai.state != AI_STATE_IDLE) failed |= 2;
    /* facing her: seen within the 59 ticks of its turn, then combat */
    place(p, t, 0.0f);
    int seen_at = -1;
    for (int k = 0; k < 120 && seen_at < 0; k++) {
        chars_update(&(CharacterInput){ 0 }, env);
        e = knowledge_find(t, p);
        if (e && e->strength == KNOW_DEFINITE) seen_at = k;
    }
    for (int k = 0; k < 3; k++) chars_update(&(CharacterInput){ 0 }, env);
    printf("facing: seen after %d ticks, strength %d, type %d, hostility %d, alert %d, state %d, target %s\n", seen_at,
           e ? e->strength : 0, e ? e->type : -1, e ? e->hostility : -1, t->ai.alert, t->ai.state,
           t->ai.target == p ? "konoko" : "other");
    if (seen_at < 0 || seen_at > 60 || !e || e->type != KNOW_SEEN_CENTRAL || e->hostility != 2 || t->ai.alert != 4 ||
        t->ai.state != AI_STATE_COMBAT || t->ai.target != p) failed |= 4;
    /* forgotten with time: 180 ticks at 4, then 900 at 3, ... unless seen again; turned away, the
       strength falls */
    place(p, t, 3.14159265f);
    t->ai.state = AI_STATE_COMBAT;
    int s0 = e ? e->strength : 0;
    for (int k = 0; k < 400; k++) {
        t->c.facing = t->c.desired_facing = 3.14159265f;
        physics_place(&t->c, (const float[3]){ AT[0], AT[1], AT[2] - 60.0f });
        t->ai.moving = false;
        chars_update(&(CharacterInput){ 0 }, env);
    }
    e = knowledge_find(t, p);
    printf("after 400 ticks away: strength %d (was %d)\n", e ? e->strength : 0, s0);
    if (!e || e->strength != 3) failed |= 8;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("knowledge_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
