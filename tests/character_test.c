/* Konoko's animation engine (OPENONI_DATA=<dir>, default ./game; skipped without it), level 1 over level 0:
   konoko_generic, fed a scripted input (stand, run 60 ticks, let go, punch, kick), prints the
   (type, state, frame) trace at each change and checks the transitions: run start -> run -> stop ->
   stand, and the punch and the kick play and return to a stand. */
#include "akev.h"
#include "character.h"
#include "chr_anim.h"
#include "body.h"
#include "exe.h"
#include "tm.h"
#include "trac.h"
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

static const char *name_of(const void *t)
{
    const TmInstance *in = t ? tm_handle_instance(tm_handle_of(t)) : NULL;
    return in && in->name ? in->name : "?";
}

enum { TICKS = 420, PUNCH_AT = 170, KICK_AT = 280 };

/* The input script. */
static CharacterInput input_at(int t)
{
    CharacterInput in = { 0 };
    if (t >= 30 && t < 90) in.forward = 1;
    if (t == PUNCH_AT) in.punch = true;
    if (t == KICK_AT) in.kick = true;
    return in;
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("character_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        fprintf(stderr, "character_test: %s\n", err);
        return 1;
    }
    prepare();
    trac_set_level(1);
    if (!tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "character_test: %s\n", err);
        return 1;
    }
    prepare();
    int failed = 0;
    const void *oncc = tm_instance_get(ONCC_TAG, "konoko_generic");
    static Character c;
    const float pos[3] = { 0, 0, 0 };
    if (!oncc || !character_init(&c, oncc, pos, 0.0f)) {
        fprintf(stderr, "character_test: Konoko doesn't start\n");
        return 1;
    }
    /* what was seen, in order */
    bool run_start = false, run = false, stop = false, stood = false, punch = false, punch_back = false,
         kick = false, kick_back = false, posed = true;
    const void *last = NULL;
    for (int t = 0; t < TICKS; t++) {
        CharacterInput in = input_at(t);
        character_update(&c, &in, NULL);
        for (int k = 0; k < 4 * CHR_BONES; k += 4) {
            float n = c.quats[k] * c.quats[k] + c.quats[k + 1] * c.quats[k + 1] + c.quats[k + 2] * c.quats[k + 2] +
                      c.quats[k + 3] * c.quats[k + 3];
            if (fabsf(n - 1.0f) > 1e-3f) posed = false;
        }
        if (c.anim != last || t == 0 || t == 89 || t == PUNCH_AT || t == KICK_AT || t == TICKS - 1) {
            printf("tick %3d: type 0x%02x state 0x%02x frame %2u%s queued 0x%02x var 0x%04x %-28s pos %7.2f %7.2f\n", t,
                   c.type, c.state, (unsigned)c.frame, c.interpolating ? " interp" : "       ", c.queued, c.varient,
                   name_of(c.anim), c.pos[0], c.pos[2]);
            last = c.anim;
        }
        if (t >= 30 && t < 90) {
            if (c.type == CHR_TYPE_RUN_START) run_start = true;
            if (c.type == CHR_TYPE_RUN && run_start) run = true;
        }
        if (t >= 90 && t < PUNCH_AT) {
            if (run && (c.type == CHR_TYPE_RUN_STOP || c.type == CHR_TYPE_1STEP_STOP)) stop = true;
            if (stop && c.type == CHR_TYPE_STAND) stood = true;
        }
        if (t >= PUNCH_AT && t < KICK_AT) {
            if (c.type == CHR_TYPE_PUNCH) punch = true;
            if (punch && c.type == CHR_TYPE_STAND) punch_back = true;
        }
        if (t >= KICK_AT) {
            if (c.type == CHR_TYPE_KICK) kick = true;
            if (kick && c.type == CHR_TYPE_STAND) kick_back = true;
        }
    }
    struct { const char *what; bool ok; } checks[] = {
        { "run start", run_start }, { "run", run }, { "run stop", stop }, { "stand after the run", stood },
        { "punch", punch }, { "stand after the punch", punch_back }, { "kick", kick },
        { "stand after the kick", kick_back }, { "unit quaternions", posed },
    };
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++)
        if (!checks[i].ok) {
            fprintf(stderr, "character_test: no %s\n", checks[i].what);
            failed++;
        }
    /* a jump on the training room's floor: Konoko at the level's start, a tick of jump with forward
       held, then forward until she is down again; she leaves the ground, rises, lands */
    const void *env = NULL;
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *ins = tm_file_instance(f, i);
        if (ins->tmpl->tag == AKEV_TAG && ins->data) env = ins->data;
    }
    const float start[3] = { -1101.0f, -340.0f, -126.3f };
    static Character j;
    if (!env || !character_init(&j, oncc, start, 3.0f)) {
        fprintf(stderr, "character_test: no level 1 for the jump\n");
        failed++;
    } else {
        CharacterInput still = { 0 };
        for (int t = 0; t < 30; t++) character_update(&j, &still, env);
        float ground = j.pos[1], top = ground;
        bool left = false, landed = false, jump_type = false;
        int air = 0, t;
        for (t = 0; t < 240 && !landed; t++) {
            CharacterInput in = { .forward = 1, .jump = t < 1 };
            character_update(&j, &in, env);
            if (j.type == 0x12 || j.type == 5) jump_type = true;
            if (getenv("JUMP_TRACE")) printf("  jump tick %d: type 0x%02x state 0x%02x air %u y %.2f\n", t, j.type, j.state, j.air_ticks, j.pos[1]);
            if (j.air_ticks) left = true, air++;
            if (j.pos[1] > top) top = j.pos[1];
            if (left && !j.air_ticks) landed = true;
        }
        bool land_type = false;
        for (int k = 0; k < 20; k++) {
            CharacterInput in = { .forward = 1 };
            character_update(&j, &in, env);
            if (j.type == 0x18 || (j.type >= 0x28 && j.type <= 0x2b)) land_type = true;
        }
        if (!land_type) {
            fprintf(stderr, "character_test: no landing animation after the jump\n");
            failed++;
        }
        printf("jump: ground %.2f top %.2f (+%.2f), %d ticks in the air, landed at tick %d at %.2f, landing %d\n",
               ground, top, top - ground, air, t, j.pos[1], land_type);
        if (!jump_type || !left || !landed || top - ground < 5.0f) {
            fprintf(stderr, "character_test: the jump failed (type=%d air=%d landed=%d)\n", jump_type, left, landed);
            failed++;
        }
    }
    trac_forget();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("character_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
