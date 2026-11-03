/* The camera (OPENONI_DATA=<dir>, default ./game; skipped without it), level 1 over level 0: the follow
   camera behind the player walking from its start, then the warehouse cutscene's moves through the script
   commands: cm_interpolate to KonCamFoot01 (a cut), cm_interpolate_block to KonCamFoot02 over 300 ticks
   with cm_wait, cm_anim of OutroCam01 (the warehouse's outro) with cm_wait, cm_reset. Prints the eye at key ticks and checks that each move
   ends at its OBAN's matrix. */
#include "akev.h"
#include "camera.h"
#include "exe.h"
#include "oban.h"
#include "objc.h"
#include "sl.h"
#include "tm.h"
#include "vfs.h"
#include "vfs_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool find_player(const ObjcObject *o, void *user)
{
    CameraTarget *t = user;
    if (o->tag != TM_TAG("CHAR") || !(objc_char_flags(o) & OBJC_CHAR_PLAYER)) return true;
    memcpy(t->pos, o->pos, sizeof t->pos);
    t->facing = o->rot[1] * 0.017453293f;
    return false;
}

static void *level_environment(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) return in->data;
    }
    return NULL;
}

static void show(const char *what, uint32_t tick)
{
    CameraView v;
    if (!camera_view(&v)) { printf("%-12s tick %4u: no view\n", what, (unsigned)tick); return; }
    printf("%-12s tick %4u: mode %d%s eye %9.3f %9.3f %9.3f view %6.3f %6.3f %6.3f up %6.3f %6.3f %6.3f\n", what,
           (unsigned)tick, camera_mode(), camera_busy() ? " busy" : "     ", v.location[0], v.location[1], v.location[2],
           v.view[0], v.view[1], v.view[2], v.up[0], v.up[1], v.up[2]);
}

/* Whether the view is at the OBAN's frame (the location, and the view along -z). */
static bool at_frame(const char *name, int frame, const char *what)
{
    float m[12];
    CameraView v;
    const void *oban = tm_instance_get(OBAN_TAG, name);
    if (!oban || !oban_matrix(oban, frame, false, m) || !camera_view(&v)) return false;
    float lz = sqrtf(m[6] * m[6] + m[7] * m[7] + m[8] * m[8]);
    float d = 0;
    for (int i = 0; i < 3; i++) d = fmaxf(d, fabsf(v.location[i] - m[9 + i]));
    for (int i = 0; i < 3; i++) d = fmaxf(d, fabsf(v.view[i] + m[6 + i] / lz));
    printf("%s: %s frame %d, off by %.6f\n", what, name, frame, d);
    return d < 1e-3f;
}

static uint32_t now;
static int cuts;
static void cut(void) { cuts++; }

static const char script[] =
    "func void cut1(void)\n"
    "{\n"
    "\tcm_interpolate KonCamFoot01 0\n"
    "\tcm_interpolate_block KonCamFoot02 300\n"
    "\tcm_wait\n"
    "}\n"
    "func void anim1(void)\n"
    "{\n"
    "\tcm_anim both OutroCam01\n"
    "\tcm_wait\n"
    "}\n"
    "func void back(void)\n"
    "{\n"
    "\tcm_reset\n"
    "}\n";

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists(EXE_PATH)) { printf("camera_test: no game folder, skipped\n"); return 0; }
    char err[512];
    if (!exe_init(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err) ||
        !tm_level_load(1, err, sizeof err)) {
        fprintf(stderr, "camera_test: %s\n", err);
        return 1;
    }
    int failed = 0;
    void *env = level_environment();
    CameraTarget t = { .active = true };
    objc_each("Character", find_player, &t);
    t.bottom = t.pos[1];
    printf("player at %.3f %.3f %.3f facing %.3f\n", t.pos[0], t.pos[1], t.pos[2], t.facing);

    sl_reset();
    camera_init();
    camera_register_commands();
    camera_set_environment(env);
    camera_set_cut_hook(cut);
    if (!sl_load_text("camera_test", script, err, sizeof err)) {
        fprintf(stderr, "camera_test: %s\n", err);
        failed++;
    }

    /* ONrLevel_Load: the target, then the reset; then the follow camera behind the player walking
       forward and turning a little each tick */
    t.aim[0] = sinf(t.facing), t.aim[1] = 0, t.aim[2] = cosf(t.facing);
    camera_set_target(&t);
    camera_reset();
    camera_update(1, 0, NULL, ++now);
    show("reset", now);
    int jello_max = 0;
    for (int k = 0; k < 240; k++) {
        t.facing = fmodf(t.facing + 0.004f, 6.2831855f);
        t.aim[0] = sinf(t.facing), t.aim[2] = cosf(t.facing);
        t.pos[0] += t.aim[0] * 0.6f, t.pos[2] += t.aim[2] * 0.6f;
        t.bottom = t.pos[1];
        camera_set_target(&t);
        sl_update();
        camera_update(1, 0, NULL, ++now);
        camera_update(0, 0, NULL, now);      /* the frame's own call: only the jello */
        const uint32_t *q;
        int nj = camera_jello_quads(&q);
        if (nj > jello_max) jello_max = nj;
        if (k == 59 || k == 119 || k == 239) show("follow", now);
    }
    printf("follow: at most %d jello quads\n", jello_max);
    CameraView v;
    camera_view(&v);
    float dx = v.location[0] - t.pos[0], dz = v.location[2] - t.pos[2];
    float back = -(dx * t.aim[0] + dz * t.aim[2]);
    printf("follow: %.3f behind, %.3f over the feet\n", back, v.location[1] - t.pos[1]);
    if (camera_mode() != CAMERA_FOLLOW || back < 20.0f || back > 40.0f) {
        fprintf(stderr, "camera_test: the follow camera isn't behind the player\n");
        failed++;
    }

    /* the cut, then the 300-tick move */
    if (!sl_call("cut1", 0, NULL)) failed++;
    for (int k = 0; k < 320; k++) {
        sl_update();
        camera_update(1, 0, NULL, ++now);
        if (k == 0) {
            show("cut", now);
            failed += !at_frame("KonCamFoot01", 0, "cut");   /* the 0-tick interpolate lands at once */
        }
        if (k == 1 || k == 150 || k == 299 || k == 319) show("interpolate", now);
    }
    failed += !at_frame("KonCamFoot02", 0, "interpolate");
    if (sl_threads() != 0) { fprintf(stderr, "camera_test: cm_wait still waiting\n"); failed++; }

    /* the animation */
    ObanInfo info;
    oban_info(tm_instance_get(OBAN_TAG, "OutroCam01"), &info);
    printf("OutroCam01: %u frames, %u keys\n", info.frames, info.keys);
    if (!sl_call("anim1", 0, NULL)) failed++;
    int ticks = 0;
    for (int k = 0; k < (int)info.frames + 20; k++) {
        sl_update();
        camera_update(1, 0, NULL, ++now);
        if (sl_threads()) ticks++;
        if (k == 0 || k == (int)info.frames / 4 || k == (int)info.frames / 2 || k == (int)info.frames + 19) show("anim", now);
    }
    printf("anim: cm_wait waited %d ticks\n", ticks);
    failed += !at_frame("OutroCam01", info.frames - 1, "anim");
    if (sl_threads() != 0) { fprintf(stderr, "camera_test: cm_wait still waiting\n"); failed++; }

    if (!sl_call("back", 0, NULL)) failed++;
    sl_update();
    camera_update(1, 0, NULL, ++now);
    show("cm_reset", now);
    if (camera_mode() != CAMERA_FOLLOW || cuts != 3) {
        fprintf(stderr, "camera_test: cm_reset didn't go back to follow (%d cuts)\n", cuts);
        failed++;
    }

    sl_reset();
    tm_terminate();
    exe_free();
    vfs_unmount();
    printf("camera_test: %s\n", failed ? "FAILED" : "ok");
    return failed != 0;
}
