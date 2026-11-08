/* The AI's executor (OPENONI_DATA=<dir>, default ./game; skipped without it): on level 1, a thug told
   to walk to a point behind it on the training room's floor turns round, walks there and stops. */
#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "exe.h"
#include "graph.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
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

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("ai_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "ai_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "ai_test: %s\n", err);
        return 1;
    }
    prepare();
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) env = in->data;
    }
    chars_load();
    Chr *p = chars_player(), *t = chars_spawn("Bay2_Thug_1", false);
    if (!p || !t || !env) {
        fprintf(stderr, "ai_test: no player or thug\n");
        return 1;
    }
    t->ai.patrol = -1;      /* off its patrol (patrol_test) */
    const float at[3] = { -1101.0f, -340.0f, -126.3f };
    physics_place(&p->c, (const float[3]){ p->c.pos[0] + 200.0f, p->c.pos[1], p->c.pos[2] });  /* out of the way */
    physics_place(&t->c, at);
    t->c.facing = t->c.desired_facing = 0.0f;
    float goal[3] = { at[0], at[1], at[2] - 60.0f };     /* behind it */
    ai_move_to(&t->ai, goal, true, 1.5707964f);
    bool walked = false;
    int ticks = 0;
    for (; ticks < 1200 && t->ai.moving; ticks++) {
        chars_tick(&(CharacterInput){ 0 }, env);
        if (t->c.type == CHR_TYPE_WALK) walked = true;
    }
    for (int k = 0; k < 120; k++) chars_tick(&(CharacterInput){ 0 }, env);
    float dx = t->c.pos[0] - goal[0], dz = t->c.pos[2] - goal[2];
    printf("thug: arrived=%d after %d ticks, %.2f from the goal, facing %.2f, walked=%d, type 0x%02x\n", !t->ai.moving, ticks,
           sqrtf(dx * dx + dz * dz), t->c.facing, walked, t->c.type);
    int failed = t->ai.moving || !walked || dx * dx + dz * dz > 64.0f || fabsf(t->c.facing - 1.5707964f) > 0.05f;
    /* room to room along the graph's path: to the first room three links from the warehouse start
       (by the graph's links, the blocked ones left out), at its floor's middle */
    const float start[3] = { 48.0f, 1.0f, 10.0f };
    float far[3] = { 0 };
    {
        int depth[1024], queue[1024], qh = 0, qt = 0, r0 = graph_room_at(start);
        for (int i = 0; i < 1024; i++) depth[i] = -1;
        if (graph_for(env) && r0 >= 0) depth[r0] = 0, queue[qt++] = r0;
        while (qh < qt) {
            int r = queue[qh++];
            float box[6];
            graph_room_box(r, box);
            float c[3] = { (box[0] + box[3]) * 0.5f, box[1] + 2.0f, (box[2] + box[5]) * 0.5f };
            if (depth[r] == (getenv("AI_DEPTH") ? atoi(getenv("AI_DEPTH")) : 3) && graph_room_at(c) == r) { memcpy(far, c, sizeof c); break; }
            for (int k = 0; k < graph_out_count(r); k++) {
                const GraphLink *l = graph_link(graph_out(r, k));
                if (!(l->flags & 1) && depth[l->to] < 0) depth[l->to] = depth[r] + 1, queue[qt++] = l->to;
            }
        }
    }
    physics_place(&t->c, start);
    ai_move_to(&t->ai, far, false, 0.0f);
    int rooms = 0, last = -1;
    for (ticks = 0; ticks < (getenv("AI_DEPTH") ? 30000 : 4000) && t->ai.moving; ticks++) {
        chars_tick(&(CharacterInput){ 0 }, env);
        int r = graph_room_at(t->c.pos);
        if (r != last) rooms++, last = r;
        if (getenv("AI_TRACE") && ticks % 100 == 0) {
            const GraphLink *l = t->ai.path_k < t->ai.path_n ? graph_link(t->ai.path[t->ai.path_k]) : NULL;
            printf("  %d: (%.1f %.1f %.1f) room %d, link %d/%d to %d at (%.1f %.1f %.1f) width %.1f\n", ticks, t->c.pos[0], t->c.pos[1],
                   t->c.pos[2], r, t->ai.path_k, t->ai.path_n, l ? l->to : -1, l ? l->mid[0] : 0, l ? l->mid[1] : 0, l ? l->mid[2] : 0,
                   l ? l->width : 0);
        }
    }
    dx = t->c.pos[0] - far[0], dz = t->c.pos[2] - far[2];
    printf("across: arrived=%d after %d ticks, %.2f from the goal, %d rooms, path of %d links, at (%.1f %.1f %.1f)\n",
           !t->ai.moving, ticks, sqrtf(dx * dx + dz * dz), rooms, t->ai.path_n, t->c.pos[0], t->c.pos[1], t->c.pos[2]);
    if (t->ai.moving || rooms < 4) failed |= 2;
    chars_unload();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("ai_test: %s\n", failed ? "FAILED" : "ok");
    return failed;
}
