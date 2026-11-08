/* The AI's knowledge (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, a thug on
   the training room's floor facing Konoko 60 away sees her (centrally: strength 4) within a second
   and goes to combat; turned away, it doesn't; punched from behind, it knows of her; turned away, it
   hears her shot within the sound's radius. */
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
    for (int k = 0; k < 120; k++) chars_tick(&(CharacterInput){ 0 }, env);
    KnowEntry *e = knowledge_find(t, p);
    printf("away: contact %s, alert %d, state %d\n", e ? "yes" : "no", t->ai.alert, t->ai.state);
    if (e || t->ai.state != AI_STATE_IDLE) failed |= 2;
    /* facing her: seen within the 59 ticks of its turn, then combat */
    place(p, t, 0.0f);
    int seen_at = -1;
    for (int k = 0; k < 120 && seen_at < 0; k++) {
        chars_tick(&(CharacterInput){ 0 }, env);
        e = knowledge_find(t, p);
        if (e && e->strength == KNOW_DEFINITE) seen_at = k;
    }
    for (int k = 0; k < 3; k++) chars_tick(&(CharacterInput){ 0 }, env);
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
        chars_tick(&(CharacterInput){ 0 }, env);
    }
    e = knowledge_find(t, p);
    printf("after 400 ticks away: strength %d (was %d)\n", e ? e->strength : 0, s0);
    if (!e || e->strength != 3) failed |= 8;
    /* heard (AI2rKnowledge_Sound): turned away, it forgets her; her shot within 10 (times its class's
       hearing, ONCC +0x428) of 60 away goes unheard, within 120 it is heard: a gunfire contact, strong
       (beyond 40) */
    knowledge_forget_contacts(t, NULL);
    place(p, t, 3.14159265f);
    float scale;
    memcpy(&scale, (const uint8_t *)t->c.oncc + 0x428, 4);
    knowledge_sound(KNOW_SOUND_GUNFIRE, p->c.pos, 10.0f, p, NULL);
    chars_tick(&(CharacterInput){ 0 }, env);
    KnowEntry *near = knowledge_find(t, p);
    knowledge_sound(KNOW_SOUND_GUNFIRE, p->c.pos, 120.0f, p, NULL);
    chars_tick(&(CharacterInput){ 0 }, env);
    e = knowledge_find(t, p);
    printf("hearing %.2f: within 10 %s, within 120 strength %d, type %d\n", scale, near ? "heard" : "unheard",
           e ? e->strength : 0, e ? e->type : -1);
    if (near || !e || e->strength != KNOW_STRONG || e->type != KNOW_SOUND_GUNFIRE) failed |= 16;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("knowledge_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
