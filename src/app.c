/* The application (app.h). The start of ONiMain 0x4d3280 as far as the port has come (docs/re/port-plan.md):
   the exe check, the instance manager (TMrInitialize, from ONiInitializeAll) and level 0 (ONrLevel_LoadZero
   loads the shared data with TMrLevel_Load), then a test screen that runs its own loop the way the original's loops
   do: draw, plat_wait_frame, until Esc. The bars are the level files' sizes, read through the file layer. */
#include "app.h"
#include "akev.h"
#include "bink.h"
#include "akev_collide.h"
#include "akev_vis.h"
#include "body.h"
#include "camera.h"
#include "cheat.h"
#include "cinematic.h"
#include "character.h"
#include "chars.h"
#include "chr_lod.h"
#include "combat.h"
#include "knowledge.h"
#include "char_draw.h"
#include "action.h"
#include "console.h"
#include "door.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "weapon.h"
#include "env.h"
#include "envparticle.h"
#include "impact.h"
#include "laser.h"
#include "turret.h"
#include "persist.h"
#include "physics.h"
#include "save.h"
#include "laser_draw.h"
#include "env_draw.h"
#include "sky_draw.h"
#include "decal_draw.h"
#include "exe.h"
#include "geom_draw.h"
#include "graph.h"
#include "gl_dev.h"
#include "im.h"
#include "localpath.h"
#include "objc.h"
#include "m3gm.h"
#include "material.h"
#include "particle.h"
#include "particle_draw.h"
#include "powerup.h"
#include "object.h"
#include "obstacle.h"
#include "platform.h"
#include "tm.h"
#include "trac.h"
#include "trigger.h"
#include "ui.h"
#include "ui_draw.h"
#include "sl.h"
#include "sound.h"
#include "vfs.h"
#include "igui.h"
#include "li.h"
#include "outgame.h"
#include "wm.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DIK_ESCAPE = 0x01, DIK_TAB = 0x0f, DIK_W = 0x11, DIK_S = 0x1f, DIK_A = 0x1e, DIK_D = 0x20, DIK_Q = 0x10, DIK_E = 0x12,
    DIK_F = 0x21, DIK_C = 0x2e, DIK_R = 0x13, DIK_CAPITAL = 0x3a, DIK_LSHIFT = 0x2a, DIK_RSHIFT = 0x36, DIK_LCONTROL = 0x1d, DIK_RCONTROL = 0x9d, DIK_SPACE = 0x39, DIK_UP = 0xc8, DIK_DOWN = 0xd0, DIK_LEFT = 0xcb, DIK_RIGHT = 0xcd, DIK_F1 = 0x3b, DIK_F8 = 0x42,
};

/* The levels the CD installs, in the order of the level numbers. */
static const int LEVELS[] = {0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19};
enum { NLEVELS = sizeof LEVELS / sizeof *LEVELS };

static void test_screen(void)
{
    int64_t size[NLEVELS * 2], biggest = 1;
    for (int i = 0; i < NLEVELS * 2; i++) {
        char path[64];
        snprintf(path, sizeof path, "GameDataFolder/level%d_Final.%s", LEVELS[i / 2], i & 1 ? "raw" : "dat");
        size[i] = vfs_size(path);
        if (size[i] > biggest) biggest = size[i];
    }
    for (;;) {
        uint8_t keys[64];
        int n = plat_key_presses(keys, (int)sizeof keys);
        for (int i = 0; i < n; i++)
            if (keys[i] == DIK_ESCAPE) return;
        int w, h;
        plat_display_size(&w, &h);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        int bw = w / (NLEVELS * 2 + 1);
        for (int i = 0; i < NLEVELS * 2; i++) {
            int bh = size[i] > 0 ? (int)((int64_t)(h * 3 / 4) * size[i] / biggest) : 4;
            glScissor(bw / 2 + i * bw, h / 8, bw - 2, bh > 0 ? bh : 1);
            if (size[i] < 0) glClearColor(0.8f, 0.1f, 0.1f, 1.0f);           /* missing */
            else if (i & 1) glClearColor(0.71f, 0.33f, 0.16f, 1.0f);         /* .raw */
            else glClearColor(0.85f, 0.85f, 0.80f, 1.0f);                    /* .dat */
            glClear(GL_COLOR_BUFFER_BIT);
        }
        plat_wait_frame();
    }
}

/* ---- the level viewer (--param viewer=level&level=N[&cam=x,y,z,yaw,pitch]): the environment, flown
   through with WASD, Q/E, the arrows or the mouse (held left button). Not part of the original: the check of
   the renderer (docs/re/port-plan.md, step 4). ---- */

static void perspective(float m[16], float fovy, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fovy / 2);
    memset(m, 0, 16 * sizeof *m);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
}

/* The view from eye looking along yaw (about +y, 0 looking down -z) and pitch (up positive). */
static void view_matrix(float m[16], const float eye[3], float yaw, float pitch)
{
    float f[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch) };
    float r[3] = { cosf(yaw), 0, sinf(yaw) };
    float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    float t[3] = { -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]), -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
                   f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2] };
    float v[16] = { r[0], u[0], -f[0], 0, r[1], u[1], -f[1], 0, r[2], u[2], -f[2], 0, t[0], t[1], t[2], 1 };
    memcpy(m, v, sizeof v);
}

static const void *level_environment(int level)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == AKEV_TAG && in->data) return in->data;
    }
    (void)level;
    return NULL;
}

/* The level's characters (CHAR objects), each with its class and an idle animation. */
typedef struct {
    const void *oncc, *idle;
    float pos[3], facing;
    bool player;
    char name[32];
    uint32_t team;
} ViewChar;
enum { MAX_VIEW_CHARS = 256 };
static ViewChar view_chars[MAX_VIEW_CHARS];
static int n_view_chars;

static bool add_char(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->tag != TM_TAG("CHAR") || n_view_chars == MAX_VIEW_CHARS) return true;
    const void *oncc = tm_instance_get(TM_TAG("ONCC"), objc_char_class(o));
    if (!oncc) return true;
    ViewChar *c = &view_chars[n_view_chars++];
    c->oncc = oncc;
    c->idle = char_find_animation(oncc, 6);
    memcpy(c->pos, o->pos, sizeof c->pos);
    c->facing = o->rot[1] * 0.017453293f;
    c->player = objc_char_flags(o) & OBJC_CHAR_PLAYER;
    snprintf(c->name, sizeof c->name, "%s", objc_char_name(o));
    c->team = objc_char_team(o);
    return true;
}

/* The level's objects, each geometry of its array under its matrix (OBiDraw 0x438fb0, without its
   visibility test yet). */
/* The box round a geometry's bounds under m, in the world (none: false). */
static bool world_box(const void *g, const float m[12], float box[6])
{
    float b[6], sph[4];
    if (!g || !m3gm_bounds(g, b, sph)) return false;
    for (int k = 0; k < 8; k++) {
        float p[3] = { (k & 1) ? b[3] : b[0], (k & 2) ? b[4] : b[1], (k & 4) ? b[5] : b[2] }, w[3];
        for (int r = 0; r < 3; r++) w[r] = m[r] * p[0] + m[3 + r] * p[1] + m[6 + r] * p[2] + m[9 + r];
        for (int r = 0; r < 3; r++) {
            if (!k || w[r] < box[r]) box[r] = w[r];
            if (!k || w[r] > box[3 + r]) box[3 + r] = w[r];
        }
    }
    return true;
}

/* 0x440900 then 0x440b50: one of the leaves of the box (up to max - 1, more stand for any) visible. */
static bool box_visible(const float box[6], uint32_t max)
{
    VisList l = { 0 };
    vis_list(box, max, &l);
    return vis_list_visible(&l);
}

/* OBiIsVisible 0x439160: forced (obj_force_draw), or one of the leaves of its box visible within the
   last 6 frames (its frames unseen, +0x1c). (Its far and view-cone tests, 0x529d9c, 0x529d90, are left
   to the GL's clipping.) */
static uint32_t *object_unseen;
static int n_object_unseen;
static bool object_visible(int i, const float m[12])
{
    if (object_forced(i)) return true;
    if (i >= n_object_unseen) {
        uint32_t *n = realloc(object_unseen, sizeof *n * (size_t)(i + 64));
        if (!n) return true;
        memset(n + n_object_unseen, 0, sizeof *n * (size_t)(i + 64 - n_object_unseen));
        object_unseen = n, n_object_unseen = i + 64;
    }
    float box[6] = { 0 }, b[6];
    bool any = false;
    for (uint32_t k = 0; k < object_geometry_count(i); k++)
        if (world_box(object_geometry(i, k), m, b)) {
            for (int r = 0; r < 3; r++) {
                if (!any || b[r] < box[r]) box[r] = b[r];
                if (!any || b[3 + r] > box[3 + r]) box[3 + r] = b[3 + r];
            }
            any = true;
        }
    if (any && box_visible(box, VIS_LIST_MAX)) object_unseen[i] = 0;
    else object_unseen[i]++;
    return object_unseen[i] < 6;
}

static void draw_objects(void)
{
    for (int i = 0; i < object_count(); i++) {
        if (!object_shown(i)) continue;
        float m[12];
        object_matrix(i, m);
        if (!object_visible(i, m)) continue;
        uint32_t shade;
        bool shaded = object_shade(i, &shade);
        if (shaded) geom_set_shade(shade);
        /* the camera's jello (OBiDraw: the geometry state 5, the alpha, 0x1f; the mark cleared) */
        bool jello = object_jello_take(i);
        for (uint32_t k = 0; k < object_geometry_count(i); k++) {
            if (jello) geom_draw_alpha(object_geometry(i, k), m, NULL, 0x1f);
            else geom_draw(object_geometry(i, k), m, NULL);
        }
        if (shaded) geom_default_lighting();
    }
    /* the consoles' screens (0x4c0c80) */
    for (int i = 0; i < console_count(); i++) {
        float m[12];
        const void *txmp = NULL, *g = console_screen(i, &txmp, m);
        if (g) geom_draw(g, m, txmp);
    }
    /* the laser triggers' emitters (the beams tested by game_display) */
    for (int i = 0; i < laser_count(); i++)
        for (int k = 0; k < laser_emitters(i); k++) {
            float m[12];
            const void *g = laser_emitter(i, k, m);
            if (g) geom_draw(g, m, NULL);
        }
    /* the weapons (WPrDisplay 0x519710: those in the world, turned by their facing, fading; the hands'
       with their characters; the stowed ones not drawn), their glows first */
    for (int i = 0; i < weapon_slots(); i++) {
        const Weapon *w = weapon_get(i);
        if (!w) continue;
        if (!w->owner) {
            float m[12], gp[3], gs[2], box[6];
            const void *gt[2];
            uint8_t a = weapon_world_matrix(w, m), ga;
            /* WPrDisplay 0x519710: one in the world drawn while a leaf of its box (0x10 at most) is visible */
            if (world_box(weapon_geometry(w), m, box) && !box_visible(box, 0x10)) continue;
            if (weapon_glow(w, m, a, gt, gp, gs, &ga))
                for (int k = 0; k < 2; k++) sprite_draw_flat(gt[k], gp, m, gs[0], gs[1], ga, p3_tick());
            if (a < 0xff) geom_draw_alpha(weapon_geometry(w), m, NULL, a);
            else geom_draw(weapon_geometry(w), m, NULL);
        } else if (w->owner->weapon == w)
            geom_draw(weapon_geometry(w), w->m, NULL);
    }
    /* the powerups (0x51c2b0: the glow lying under each, then its geometry turned by its facing) */
    for (int i = 0; i < powerup_count(); i++) {
        const Powerup *p = powerup_get(i);
        float m[12], box[6];
        powerup_matrix(p, m);
        /* 0x51c2b0: while a leaf of its box (0xc at most) is visible */
        if (world_box(p->geometry, m, box) && !box_visible(box, 0xc)) continue;
        float gp[3];
        if (powerup_glow(p, gp)) sprite_draw_flat(p->glow, gp, m, p->glow_size[0], p->glow_size[1], 0x55, p3_tick());
        if (p->geometry) geom_draw(p->geometry, m, NULL);
    }
    /* the turrets' heads and barrels (0x4ce300) */
    for (int i = 0; i < turret_count(); i++)
        for (int k = 0; k < 2; k++) {
            float m[12];
            const void *g = turret_geometry(i, k, m);
            if (g) geom_draw(g, m, NULL);
        }
    /* the doors' panels, while a door is open or moving */
    for (int i = 0; i < door_count(); i++)
        for (int k = 0; k < 2; k++) {
            float m[12];
            const uint8_t *ga = door_panel(i, k, m);
            uint32_t n = ga ? (uint32_t)ga[0x14] | (uint32_t)ga[0x15] << 8 | (uint32_t)ga[0x16] << 16 | (uint32_t)ga[0x17] << 24 : 0;
            for (uint32_t g = 0; g < n; g++) geom_draw(tm_link(ga + 0x18 + 4 * g), m, NULL);
        }
}

static int level_viewer(int level)
{
    char err[256], m[300];
    if (!tm_level_load(level, err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: level %d: %s", level, err);
        plat_log(m);
        return 1;
    }
    const void *env = level_environment(level);
    if (!env || !gl_dev_init() || !env_draw_load(env)) {
        plat_log("OpenOni: the level's environment doesn't load");
        return 1;
    }
    object_load();
    /* the viewer shows every object a script could create */
    snprintf(m, sizeof m, "OpenOni: level %d: %u quads, %d objects", level, (unsigned)akev_quad_count(env), object_create(0, UINT32_MAX));
    plat_log(m);
    float box[6], eye[3], yaw = 0, pitch = 0;
    akev_bounds(env, box);
    float span = fmaxf(box[3] - box[0], box[5] - box[2]);
    eye[0] = (box[0] + box[3]) / 2 + span * 0.6f;
    eye[1] = box[4] + span * 0.3f;
    eye[2] = (box[2] + box[5]) / 2 + span * 0.6f;
    yaw = -0.785398f;
    pitch = -0.45f;
    n_view_chars = 0;
    objc_each("Character", add_char, NULL);
    snprintf(m, sizeof m, "OpenOni: %d characters", n_view_chars);
    plat_log(m);
    for (int i = 0; i < n_view_chars; i++)
        if (view_chars[i].player) {   /* start behind the player, looking the way the player faces */
            const ViewChar *c = &view_chars[i];
            yaw = -c->facing;
            pitch = -0.25f;
            eye[0] = c->pos[0] - sinf(yaw) * 45;
            eye[1] = c->pos[1] + 25;
            eye[2] = c->pos[2] + cosf(yaw) * 45;
        }
    char cam[128];
    if (plat_param("cam", cam, sizeof cam)) sscanf(cam, "%f,%f,%f,%f,%f", &eye[0], &eye[1], &eye[2], &yaw, &pitch);
    for (;;) {
        uint8_t keys[KEY_COUNT] = { 0 }, presses[64];
        int n = plat_key_presses(presses, (int)sizeof presses);
        for (int i = 0; i < n; i++)
            if (presses[i] == DIK_ESCAPE) return 0;
        plat_keys(keys);
        PlatMouse mouse;
        if (plat_mouse(&mouse) && (mouse.buttons & MOUSE_LEFT)) yaw += mouse.dx * 0.005f, pitch -= mouse.dy * 0.005f;
        float turn = 1.5f / 60, speed = (keys[DIK_LSHIFT] ? 1200.0f : 300.0f) / 60;
        yaw += (keys[DIK_RIGHT] - keys[DIK_LEFT]) * turn;
        pitch += (keys[DIK_UP] - keys[DIK_DOWN]) * turn;
        pitch = fmaxf(-1.5f, fminf(1.5f, pitch));
        float fw = (float)(keys[DIK_W] - keys[DIK_S]), st = (float)(keys[DIK_D] - keys[DIK_A]), up = (float)(keys[DIK_E] - keys[DIK_Q]);
        eye[0] += (sinf(yaw) * cosf(pitch) * fw + cosf(yaw) * st) * speed;
        eye[1] += (sinf(pitch) * fw + up) * speed;
        eye[2] += (-cosf(yaw) * cosf(pitch) * fw + sinf(yaw) * st) * speed;
        if (plat_frame_shown()) {
            int w, h;
            plat_display_size(&w, &h);
            float proj[16], view[16];
            perspective(proj, 0.785398f, h ? (float)w / (float)h : 1.0f, 2.0f, 20000.0f);
            view_matrix(view, eye, yaw, pitch);
            gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
            gl_dev_matrices(proj, view);
            geom_set_eye(eye);
            env_draw();
            draw_objects();
            for (int i = 0; i < n_view_chars; i++)
                char_draw(view_chars[i].oncc, view_chars[i].pos, view_chars[i].facing, view_chars[i].idle, plat_frames());
        }
        plat_wait_frame();
    }
}

/* iAnimationCollection_DiskToMemory 0x428250 runs for each collection as its file loads. */
static void prepare_collections(TmFile *f)
{
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TRAC_TAG && in->data) trac_prepare(in->data);
    }
}

/* ---- the level's scripts ---- */

static void sl_say(const char *msg)
{
    char m[300];
    snprintf(m, sizeof m, "[%u] %s", (unsigned)plat_frames(), msg);
    plat_log(m);
}

/* dprint: the scripts' debug print (to the console in the original's developer build). */
static bool cmd_dprint(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    char m[256] = "BSL: ";
    for (int i = 0; i < argc; i++) {
        size_t n = strlen(m);
        if (argv[i].type == SL_STRING) snprintf(m + n, sizeof m - n, "%s ", argv[i].s);
        else if (argv[i].type == SL_FLOAT) snprintf(m + n, sizeof m - n, "%g ", argv[i].f);
        else snprintf(m + n, sizeof m - n, "%d ", argv[i].i);
    }
    plat_log(m);
    return true;
}

/* The fog (the GL context's +0x5f4 colour, +0x5d8 start, +0x5dc end; gl_dev_fog): as
   gl_reset_fog_parameters 0x40ca50 leaves it (M3rDraw_Reset_Fog, from ONrLevel_Unload), a grey of a
   quarter from 0.975 to 1 of the depth, until the level's scripts set it. */
static float fog_rgb[3], fog_start, fog_end, far_clip = 10000.0f;

static void fog_reset(void)
{
    fog_rgb[0] = fog_rgb[1] = fog_rgb[2] = 0.25f;
    fog_start = 0.975f;
    fog_end = 1.0f;
}

/* The fog's smooth changes (gl_fog_start_changeto_func 0x40cb20, gl_fog_end_changeto_func 0x40cb90):
   to the value over the frames (none, or not above 0: at once), by a step a tick. */
static struct { bool on; float to, step; } fog_change[2];

static bool fog_changeto(int k, float *v, int argc, const SlValue *argv)
{
    float to = argc >= 1 ? (argv[0].type == SL_FLOAT ? argv[0].f : (float)argv[0].i) : 0.0f;
    int frames = argc >= 2 ? (argv[1].type == SL_FLOAT ? (int)argv[1].f : argv[1].i) : 0;
    if (frames <= 0) {
        *v = to;
        return true;
    }
    fog_change[k].to = to;
    fog_change[k].step = (to - *v) / (float)frames;
    fog_change[k].on = true;
    return true;
}
/* killed_griffen murder:bool (0x4b9bd0, ONrWeKilledGriffen 0x50f060): "we killed griffen" or "we did
   not kill griffen" on the console, the answer kept in persist.dat */
static bool cmd_killed_griffen(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    bool killed = argc >= 1 && (argv[0].type == SL_STRING ? atoi(argv[0].s) : argv[0].type == SL_FLOAT ? argv[0].f != 0.0f : argv[0].i) != 0;
    plat_log(killed ? "OpenOni: we killed griffen" : "OpenOni: we did not kill griffen");
    persist_set_killed_griffin(killed);
    return true;
}

/* did_kill_griffen (0x4b9bf0): the bool persist.dat keeps */
static bool cmd_did_kill_griffen(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)argc, (void)argv, (void)u;
    r->type = SL_BOOL;
    r->i = persist_killed_griffin();
    return true;
}

/* p3_removedangerous (0x50aaf0) */
static bool cmd_remove_dangerous(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)argc, (void)argv, (void)r, (void)u;
    p3_remove_dangerous();
    return true;
}

/* marketing_line_off (0x5ecb9a, ONrGameState_InstallConsoleVariables 0x4d89b0): "turns the laser line
   off", the weapons' laser sight, which the port doesn't draw yet */
static bool marketing_line_off;

static bool cmd_fog_start_to(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return fog_changeto(0, &fog_start, argc, argv);
}
static bool cmd_fog_end_to(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return fog_changeto(1, &fog_end, argc, argv);
}

/* gl_fog_update 0x40cc00, from the draw's frame start (0x405898) with the ticks since the last: each change steps,
   stopping at its value. As the original, the end's change moves by the start's step (+0x5e8). */
static void fog_step(uint32_t ticks)
{
    float *v[2] = { &fog_start, &fog_end };
    for (int k = 0; k < 2; k++) {
        if (!fog_change[k].on) continue;
        float d = (float)ticks * fog_change[0].step;
        if (d > 0.0f ? fog_change[k].to - d <= *v[k] : d < 0.0f ? fog_change[k].to - d >= *v[k] : false) {
            *v[k] = fog_change[k].to;
            fog_change[k].on = false;
        } else if (d != 0.0f) {
            *v[k] += d;
        }
    }
}

static bool cmd_farclip(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (argc >= 1) far_clip = argv[0].type == SL_FLOAT ? argv[0].f : (float)argv[0].i;
    return true;
}

/* ONrLevel_Load 0x5059d0 runs the level's scripts: the folder named by the level's ONLV, after the global
   ones, starting at main. */
static bool first = true;       /* the debug params (route, at, call) are for the first level only */

static void start_scripts(void)
{
    sl_reset();
    sl_set_log(sl_say);
    char tr[8];
    sl_set_trace(plat_param("sltrace", tr, sizeof tr));
    sl_register("dprint", cmd_dprint, NULL);
    sl_register("gs_farclipplane_set", cmd_farclip, NULL);
    trigger_register_commands();
    igui_register_commands();
    object_register_commands();
    door_register_commands();
    weapons_register_commands();
    powerup_register_commands();
    console_register_commands();
    env_register_commands();
    ui_register_commands();
    camera_register_commands();
    sound_register_commands();
    envparticle_register_commands();
    p3_register_commands();
    laser_register_commands();
    turret_register_commands();
    cinematic_register_commands();
    chars_register_commands();
    save_register_commands();   /* save_point, save_game, restore_game */
    sl_register_float("gl_fog_red", &fog_rgb[0]);
    sl_register_float("gl_fog_green", &fog_rgb[1]);
    sl_register_float("gl_fog_blue", &fog_rgb[2]);
    sl_register_float("gl_fog_start", &fog_start);
    sl_register_float("gl_fog_end", &fog_end);
    sl_register_int("chr_lod", &chr_lod_forced);
    sl_register_bool("unstoppable", &chr_cheat_unstoppable);   /* the game's variables (0x4fbf10) */
    sl_register_bool("invincible", &chr_cheat_invincible);
    sl_register_bool("omnipotent", &chr_cheat_omnipotent);
    sl_register_bool("chr_big_head", &chr_big_head);     /* ONrGameState_InstallConsoleVariables 0x4d89b0 */
    sl_register_bool("chr_mini_me", &chr_mini_me);
    sl_register_bool("marketing_line_off", &marketing_line_off);
    marketing_line_off = false;
    sl_register("killed_griffen", cmd_killed_griffen, NULL);
    sl_register("did_kill_griffen", cmd_did_kill_griffen, NULL);
    sl_register("p3_removedangerous", cmd_remove_dangerous, NULL);
    sl_register("gl_fog_start_changeto", cmd_fog_start_to, NULL);
    sl_register("gl_fog_end_changeto", cmd_fog_end_to, NULL);
    memset(fog_change, 0, sizeof fog_change);
    fog_reset();
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    const char *name = NULL;
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONLV") && in->data) name = (const char *)in->data;   /* the name, 64 bytes */
    }
    char err[300], dir[128];
    if (!sl_load_folder("GameDataFolder/IGMD/global", err, sizeof err)) plat_log(err);
    snprintf(dir, sizeof dir, "GameDataFolder/IGMD/%.63s", name ? name : "");
    if (name && !sl_load_folder(dir, err, sizeof err)) plat_log(err);
    /* a debug aid: --param save=N starts the level's scripts at save point N (as a continue from it
       sets save_point; the continue itself, without one, puts nothing back) */
    char sp[16];
    if (first && plat_param("save", sp, sizeof sp)) save_set_point(atoi(sp));
    if (!sl_call("main", 0, NULL)) plat_log("OpenOni: the level has no main script");
    /* a debug aid: --param call=<function>[,<function>...] runs the level's functions at the start */
    char fn[192];
    if (first && plat_param("call", fn, sizeof fn))
        for (char *f = strtok(fn, ","); f; f = strtok(NULL, ","))
            if (!sl_call(f, 0, NULL)) plat_log("OpenOni: no such function");
}

/* ---- a debug aid: --param route=<steps> drives the player, for unattended runs through a level.
   Steps, separated by ';': "@N" waits for frame N; "wN" waits N frames; "x,z" runs to the point (within
   3, or "x,z,r,R" within R), "x,z,s[,R]" sprints there (forward tapped twice), "x,z,k[,R]" walks; "x,z,J[,R]" runs 20
   frames, then jumps (held 30) on the way ("x,z,j[,R]" jumps at once); "a" taps the action key; "hD" turns to
   D degrees (the facing: 0 is +z); "vD" aims D degrees up; "p"/"k" a punch/kick tap; "j" a jump tap; "q"/"g"/"e"/"R" a swap (pick up),
   fire, drop or reload tap; "c" holds crouch for the next step; "H<keys>,N" holds keys for N frames (f forward,
   b back, l/r step left/right, c crouch, j jump, p punch, k kick, w walk, a action, q swap, g fire);
   "U<keys>,x,V" (or ",z,V") holds them until her x (z) passes V (a take-off point; "Hfjc" in the air flips).
   "R" as a mode runs from up to 1.3 rad off; "P" too, walking the last 10. "T<name>,N[,L]" aims at the character for N frames (where it
   will be in L frames) and taps fire every 20 frames; the shots leave at about 1.13 times the aim's angle
   from the facing (measured), which the aim allows for. "M<message>,F" waits for a script's message and
   shifts the later "@" frames by when it came against F (when the route was made). "P" presses the pause
   key (the pause screen). "C" waits for the end of a cutscene (and of a script's "input 0"). "Gfrom,to,N" shoots
   the unbroken glass targets of those script ids, aiming the weapon's barrel at each. "F<name>,N" fights
   the character hand to hand for up to N frames, until it is down; "L<type>" picks up the nearest
   powerup of the type ("lsi"); "A<N>" the nearest one in combat
   with her, while there is one. "N<name>,N" talks with a neutral (the action key near it) until the
   conversation is over. "X<name>,N" hunts the character (through the rooms, then "F"). "K<id>" uses the console (its marker, the action key, again until it is used; an inactive one is
   waited by). "Wx,y,z" runs to the point through the rooms, the way found from where she is (while there is none,
   tried again each second). Knocked down, any step first gets
   up (forward). ---- */

/* The level's environment, for the route's glass targets. */
static const void *route_env;

/* Floor 12 ahead of p the way (dx, dz) goes, within 30 below: the fights' runs keep off the edges (a
   run's stop carries her some 12). */
static bool route_floor_ahead(const float p[3], float dx, float dz)
{
    float l = sqrtf(dx * dx + dz * dz);
    if (!route_env || l < 1e-3f) return true;
    static AkevHit hit[AKEV_HITS_MAX];
    const float at[3] = { p[0] + dx / l * 12.0f, p[1] + 4.0f, p[2] + dz / l * 12.0f }, down[3] = { 0.0f, -30.0f, 0.0f };
    return akev_collide_point(route_env, at, down, 0x48800, 1, hit) != 0;
}

/* The centre of the unbroken glass (quad flag 0x200000 without 0x40000) of the script id, false if none. */
static bool glass_centre(uint32_t id, float out[3])
{
    const void *env = route_env;
    if (!env) return false;
    uint32_t np, quad, qid, n = 0;
    const uint8_t *pts = akev_points(env, &np);
    float sum[3] = { 0 };
    for (uint32_t i = 0; i < akev_script_count(env); i++) {
        AkevQuad q;
        if (!akev_script_entry(env, i, &quad, &qid) || qid != id || !akev_quad(env, quad, &q)) continue;
        if (!(q.flags & 0x200000) || (q.flags & 0x40000)) continue;
        for (uint32_t k = 0; k < q.corners && k < 4; k++) {
            if (q.point[k] >= np) continue;
            for (int j = 0; j < 3; j++) {
                float f;
                memcpy(&f, pts + 12 * q.point[k] + 4 * j, 4);
                sum[j] += f;
            }
            n++;
        }
    }
    if (!n) return false;
    for (int j = 0; j < 3; j++) out[j] = sum[j] / (float)n;
    return true;
}

typedef struct {
    char buf[16384];
    char *step;             /* the current step, NULL when done */
    uint32_t until;         /* for waits: the frame */
    int phase;              /* within a step */
    bool crouch;
    int32_t shift;          /* added to the "@N" frames after an "M" step */
} Route;
static Route route;

static void route_next(void)
{
    char *s = route.step;
    if (!s) return;
    char *e = strchr(s, ';');
    route.step = e ? e + 1 : NULL;
    route.phase = 0;
    route.until = 0;
}

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

/* Whether p is in an obstacle's cell of the room's grid (6, 8 or 9: walls, crates). */
static bool route_blocked(int room, const float p[3])
{
    GraphGrid g;
    if (!graph_grid(room, &g) || !g.cells) return false;
    int16_t x, z;
    graph_to_cell(&g, p, &x, &z);
    if ((uint16_t)x >= g.w || (uint16_t)z >= g.h) return false;
    uint8_t v = g.cells[(uint32_t)(uint16_t)z * g.w + (uint16_t)x];
    return v == 6 || (v > 7 && v < 10);
}

/* "Wx,y,z": the steps to the point through the rooms' graph, planned from where the player is (the
   doors locked now kept out, the manual ones opened with the action key on the way), into out. */
static bool route_plan(const Character *c, const float b[3], char *out, size_t size)
{
    const void *env = route_env;
    if (!env || !graph_for(env)) return false;
    /* the locked doors' links blocked while the path is found; the graph's own doors on the links (the
       AI's test, which keeps out a door jammed open too) set aside meanwhile */
    static uint32_t saved[4096];
    static int saved_door[4096];
    int nl = graph_link_count() < 4096 ? graph_link_count() : 4096;
    static bool door_blocked[4096];
    for (int i = 0; i < nl; i++) {
        GraphLink *l = (GraphLink *)graph_link(i);
        saved[i] = l->flags, saved_door[i] = l->door, door_blocked[i] = false;
        l->door = -1;
    }
    for (int d = 0; d < door_count(); d++) {
        if (!(door_flags(d) & DOOR_LOCKED)) continue;
        if ((door_flags(d) & DOOR_JAMMED) && door_state(d) != 0) continue;     /* jammed open */
        /* in the door's plane and within its width (garage doors are wide) */
        float dp[3], n[2] = { sinf(door_facing(d)), cosf(door_facing(d)) }, w = door_half_width(d);
        door_position(d, dp);
        if (w > 12.0f) {    /* from the frame's middle, at the door's floor */
            float y = dp[1];
            door_centre(d, dp);
            dp[1] = y;
        }
        for (int i = 0; i < nl; i++) {
            GraphLink *l = (GraphLink *)graph_link(i);
            float dx = l->mid[0] - dp[0], dz = l->mid[2] - dp[2];
            float across = fabsf(dx * n[0] + dz * n[1]), along = fabsf(dz * n[0] - dx * n[1]);
            if (across >= 12 || along >= (w > 12.0f ? w : 12.0f) || fabsf(l->mid[1] - dp[1]) >= 10) continue;
            /* a one-way lock: blocked only from the side it keeps (the link's room centre on it) */
            if (door_flags(d) & DOOR_ONE_WAY) {
                float box[6];
                graph_room_box(l->from, box);
                float from[3] = { (box[0] + box[3]) * 0.5f, box[1], (box[2] + box[5]) * 0.5f };
                if (door_lock_passes(d, from)) continue;
            }
            l->flags |= 1, door_blocked[i] = true;
        }
    }
    float a[3] = { c->pos[0], c->pos[1], c->pos[2] };
    /* the target's room looked up from a little above it (a console marker's floor can be just under
       the room's) */
    float bu[3] = { b[0], b[1] + 3.0f, b[2] };
    int ra = graph_room_at(a), rb = graph_room_at(bu);
    if (rb < 0) rb = graph_room_at(b);
    static int path[GRAPH_PATH_MAX];
    int n = ra < 0 || rb < 0 ? 0 : graph_find_path(ra, a, rb, b, true, path);
    /* no way from under her (none there, or a lower room her feet stand over: on a ledge's edge, their
       square reaching 1.5 out): from the room of the ledge next to her, its floor at her feet */
    if (n == 0 && rb >= 0) {
        static const float off[8][2] = { { 3, 0 }, { -3, 0 }, { 0, 3 }, { 0, -3 }, { 3, 3 }, { -3, 3 }, { 3, -3 }, { -3, -3 } };
        for (int k = 0; k < 8 && n == 0; k++) {
            float q[3] = { a[0] + off[k][0], a[1], a[2] + off[k][1] }, rbox[6];
            int r = graph_room_at(q);
            if (r < 0 || r == ra) continue;
            graph_room_box(r, rbox);
            if (rbox[1] < a[1] - 10.0f) continue;
            if ((n = graph_find_path(r, q, rb, b, true, path)) > 0) ra = r, memcpy(a, q, sizeof a);
        }
    }
    /* none for a character: then through the links the graph itself marks blocked too (a stair's foot
       the body can still take), the locked doors' still kept out */
    if (n == 0 && ra >= 0 && rb >= 0) {
        for (int i = 0; i < nl; i++) {
            GraphLink *l = (GraphLink *)graph_link(i);
            l->flags = (l->flags & ~1u) | (door_blocked[i] ? 1u : 0u);
        }
        n = graph_find_path(ra, a, rb, b, true, path);
    }
    size_t len = 0;
    out[0] = 0;
#define EMIT(...) (len += (size_t)snprintf(out + len, len < size ? size - len : 0, __VA_ARGS__))
    float seg[3] = { a[0], a[1], a[2] };
    int room = ra;
    for (int k = 0; k < n; k++) {
        float in[3], o[3];
        const GraphLink *l = k + 1 < n ? graph_link(path[k]) : NULL;
        static LocalPoint lp[LOCAL_PATH_MAX];
        int m = 0;
        /* the crossing point as the AI's (PHrWaypointFromGunk, the nearest of the edge), else the
           link's middle if no local path gets to that */
        for (int t = 0; t < 2; t++) {
            if (l) {
                float w[3], pl[4];
                if (t == 0) graph_waypoint(l->quad, seg, w);
                else w[0] = l->mid[0], w[1] = l->mid[1], w[2] = l->mid[2];
                akev_quad_plane(env, l->quad, pl);
                /* which way across: the side whose room is the link's next */
                float q1[3] = { w[0] + pl[0] * 12, w[1] + 4, w[2] + pl[2] * 12 };
                float q2[3] = { w[0] - pl[0] * 12, w[1] + 4, w[2] - pl[2] * 12 };
                int r1 = graph_room_at(q1), r2 = graph_room_at(q2), sign;
                if (r2 == l->to && r1 != l->to) sign = -1;
                else if (r1 == l->to && r2 != l->to) sign = 1;
                else {
                    /* (a stair's foot: both sides in the room under it) toward where the way goes next */
                    const float *next = k + 2 < n ? graph_link(path[k + 1])->mid : b;
                    sign = pl[0] * (next[0] - w[0]) + pl[2] * (next[2] - w[2]) > 0 ? 1 : -1;
                }
                /* slid along the link's edge (4 at a time) while the way across meets an obstacle of
                   the rooms' grids (a pillar on the crossing) */
                float lo[3], lo2[3], e[3];
                graph_quad_low_pair(l->quad, lo, lo2);
                for (int j = 0; j < 3; j++) e[j] = lo2[j] - lo[j];
                float elen = sqrtf(e[0] * e[0] + e[2] * e[2]);
                if (elen > 0.0f) e[0] /= elen, e[2] /= elen;
                float along = (w[0] - lo[0]) * e[0] + (w[2] - lo[2]) * e[2];
                for (int k = 0; k < 21; k++) {
                    float shift = (float)((k + 1) / 2) * 4.0f * (k & 1 ? 1.0f : -1.0f);
                    if (along + shift < 4.0f || along + shift > elen - 4.0f) { if (k) continue; shift = 0.0f; }
                    float ww[3] = { w[0] + e[0] * shift, w[1], w[2] + e[2] * shift };
                    for (int j = 0; j < 3; j++) in[j] = o[j] = ww[j];
                    in[0] += pl[0] * 12 * sign, in[2] += pl[2] * 12 * sign;
                    o[0] -= pl[0] * 8 * sign, o[2] -= pl[2] * 8 * sign;
                    bool clear = true;
                    for (int f = 0; f <= 4 && clear; f++) {
                        float q[3];
                        for (int j = 0; j < 3; j++) q[j] = o[j] + (in[j] - o[j]) * (float)f / 4.0f;
                        clear = !route_blocked(f < 2 ? room : l->to, q);
                    }
                    /* and (not on stairs, nor through a door, closed as it may be) a body's sphere
                       (radius 6, 14 up) swept across meets no wall */
                    bool door = false;
                    for (int d = 0; d < door_count() && !door; d++) {
                        float dp[3];
                        door_position(d, dp);
                        door = fabsf(l->mid[0] - dp[0]) < 12 && fabsf(l->mid[2] - dp[2]) < 12 && fabsf(l->mid[1] - dp[1]) < 10;
                    }
                    if (clear && !door && (l->flags & 0xe) != 0xe) {
                        float sph[4] = { o[0], ww[1] + 14.0f, o[2], 6.0f }, mv[3] = { in[0] - o[0], 0.0f, in[2] - o[2] };
                        static AkevHit hits[AKEV_HITS_MAX];
                        unsigned nh = akev_collide_sphere(env, sph, mv, 0x48800, 0xff, hits);
                        for (unsigned h = 0; h < nh && clear; h++) clear = fabsf(hits[h].plane[1]) > 0.5f;
                    }
                    if (clear) break;
                }
            } else {
                for (int j = 0; j < 3; j++) o[j] = b[j];
            }
            m = local_path(env, room, seg, o, NULL, false, true, lp);
            if (m || !l) break;
        }
        for (int i = 1; i + 1 < m; i++) EMIT("%.0f,%.0f,R,4;", lp[i].p[0], lp[i].p[2]);
        if (!l) break;
        EMIT("%.0f,%.0f,R,6;", o[0], o[2]);
        for (int d = 0; d < door_count(); d++) {
            float dp[3];
            door_position(d, dp);
            if ((door_flags(d) & DOOR_MANUAL) && fabsf(l->mid[0] - dp[0]) < 12 && fabsf(l->mid[2] - dp[2]) < 12 &&
                fabsf(l->mid[1] - dp[1]) < 10) {
                float h = atan2f(in[0] - o[0], in[2] - o[2]) * 57.29578f;
                EMIT("h%.0f;a;w50;", h < 0 ? h + 360 : h);
            }
        }
        EMIT("%.0f,%.0f,R,6;", in[0], in[2]);
        for (int j = 0; j < 3; j++) seg[j] = in[j];
        room = l->to;
    }
#undef EMIT
    for (int i = 0; i < nl; i++) ((GraphLink *)graph_link(i))->flags = saved[i], ((GraphLink *)graph_link(i))->door = saved_door[i];
    if (!n) return false;
    if (len < size) len += (size_t)snprintf(out + len, size - len, "%.1f,%.1f,P,3", b[0], b[2]);
    return len < size;
}

/* The input the route asks for this frame (in is all zero on entry). */
static uint32_t splash_frames;      /* the frames the splash screens and the dialogs took: the route's clock leaves them out */

static bool route_pause;     /* the route's "P": the pause key */

/* The keys of "H" and "U", up to the comma. */
static void hold_keys(const char *k, CharacterInput *in)
{
    for (; *k && *k != ',' && *k != ';'; k++) {
        switch (*k) {
        case 'f': in->forward = 1; break;
        case 'b': in->forward = -1; break;
        case 'l': in->strafe = -1; break;
        case 'r': in->strafe = 1; break;
        case 'c': in->crouch = true; break;
        case 'j': in->jump = true; break;
        case 'p': in->punch = true; break;
        case 'k': in->kick = true; break;
        case 'w': in->walk = true; break;
        case 'a': in->action = true; break;
        case 'q': in->swap = true; break;
        case 'g': in->fire = true; break;
        }
    }
}

static void route_input(const Character *c, CharacterInput *in)
{
    if (route.step && c->state == 0x32) {   /* knocked down, whatever the step: up again (forward, a getup move) */
        in->forward = 1;
        return;
    }
    for (int guard = 0; route.step && guard < 8; guard++) {
        char *s = route.step;
        uint32_t now = plat_frames() - splash_frames;
        if (*s == '@') {
            if ((int64_t)now < (int64_t)atoi(s + 1) + route.shift) return;
            route_next();
            continue;
        }
        if (*s == 'M') {
            char id[32];
            const char *comma = strchr(s, ',');
            size_t n = comma ? (size_t)(comma - s - 1) : 0;
            if (!comma || n >= sizeof id) { route_next(); continue; }
            memcpy(id, s + 1, n);
            id[n] = 0;
            uint32_t at = ui_message_time(id);
            if (!at) return;
            route.shift = (int32_t)at - atoi(comma + 1);
            route_next();
            continue;
        }
        if (*s == 'C' && (s[1] == ';' || !s[1])) {
            /* "C": waits for the end of a cutscene and for the input on again */
            if (ui_cutscene() || !ui_input_enabled()) return;
            route_next();
            continue;
        }
        if (*s == 'S') {
            /* "S<N>": waits (up to N frames) until the player stands */
            if (!route.phase) route.until = now + (uint32_t)atoi(s + 1), route.phase = 1;
            if (c->state != CHR_STATE_STANDING && now < route.until) return;
            route_next();
            continue;
        }
        if (*s == 'P') {
            route_pause = true;
            plat_log("OpenOni: route: the pause key");
            route_next();
            return;
        }
        if (*s == 'w') {
            if (!route.phase) route.until = now + (uint32_t)atoi(s + 1), route.phase = 1;
            if (now < route.until) return;
            route_next();
            continue;
        }
        if (*s == 'a' || *s == 'p' || *s == 'j' || *s == 'q' || *s == 'g' || *s == 'e' || *s == 'R' || (*s == 'k' && (s[1] == ';' || !s[1]))) {
            /* a tap: down one frame, up the next */
            if (route.phase == 0) {
                if (*s == 'a') in->action = true;
                else if (*s == 'q') in->swap = true;
                else if (*s == 'g') in->fire = true;
                else if (*s == 'e') in->drop = true;
                else if (*s == 'R') in->reload = true;
                else if (*s == 'p') in->punch = true;
                else if (*s == 'j') in->jump = true;
                else in->kick = true;
                route.phase = 1;
                return;
            }
            route_next();
            return;
        }
        if (*s == 'H') {
            const char *comma = strchr(s, ',');
            if (!route.phase) route.until = now + (uint32_t)(comma ? atoi(comma + 1) : 1), route.phase = 1;
            if (now >= route.until) { route_next(); continue; }
            hold_keys(s + 1, in);
            return;
        }
        if (*s == 'U') {
            /* "U<keys>,x|z,V": the keys held until the position's x (or z) passes V, from the side it
               starts on */
            const char *comma = strchr(s, ',');
            if (!comma || (comma[1] != 'x' && comma[1] != 'z') || comma[2] != ',') { route_next(); continue; }
            float v = (float)atof(comma + 3), p = c->pos[comma[1] == 'x' ? 0 : 2];
            if (!route.phase) route.phase = p < v ? 1 : 2;
            if (route.phase == 1 ? p >= v : p <= v) { route_next(); continue; }
            hold_keys(s + 1, in);
            return;
        }
        if (*s == 'T') {
            char name[64];
            int n = 0;
            float lead = 0.0f;
            static float last[2];
            const char *comma = strchr(s, ',');
            size_t len = comma ? (size_t)(comma - s - 1) : 0;
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, s + 1, len), name[len] = 0;
            if (comma) sscanf(comma + 1, "%d,%f", &n, &lead);
            const Chr *t = chars_find(name);
            if (!t) { route_next(); continue; }
            if (!route.phase) route.until = now + (uint32_t)n, route.phase = 1, last[0] = t->c.pos[0], last[1] = t->c.pos[2];
            if (now >= route.until) { route_next(); continue; }
            float dx = t->c.pos[0] + (t->c.pos[0] - last[0]) * lead - c->pos[0];
            float dz = t->c.pos[2] + (t->c.pos[2] - last[1]) * lead - c->pos[2];
            last[0] = t->c.pos[0], last[1] = t->c.pos[2];
            float want = c->facing + wrap_pi(atan2f(dx, dz) - c->facing) / 1.13f;
            float err = wrap_pi(want - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            in->fire = (now - route.until + (uint32_t)n) % 20 == 0;
            return;
        }
        if (*s == 'A') {
            /* "A<N>": for up to N frames, fights (as "F", run at past 9 unless touching) the nearest
               character within 80 that is in combat (alert 4), takes her for an enemy and has hit points;
               done when there is none */
            if (!route.phase) route.until = now + (uint32_t)atoi(s + 1), route.phase = 1;
            const Chr *t = NULL;
            float best = 80.0f * 80.0f;
            for (int i = 0; i < chars_slots(); i++) {
                const Chr *o = chars_get(i);
                if (!o || o->player || !chars_in_use(o) || !o->c.hit_points || o->ai.alert < 4) continue;
                if (chars_player() && !ai_is_hostile(o, chars_player(), true)) continue;   /* an ally's fight (Kerr's) */
                float ox = o->c.pos[0] - c->pos[0], oy = o->c.pos[1] - c->pos[1], oz = o->c.pos[2] - c->pos[2];
                float d2 = ox * ox + oz * oz;
                if (fabsf(oy) < 20.0f && d2 < best) best = d2, t = o;
            }
            if (!t || now >= route.until) { route_next(); continue; }
            float dx = t->c.pos[0] - c->pos[0], dz = t->c.pos[2] - c->pos[2];
            float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            if (dx * dx + dz * dz > 81.0f && !c->pushes) {
                /* as "R": up to 1.3 off (standing in fight mode the body turns on the spot only past 70
                   degrees) */
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 1.3f;
                return;
            }
            if ((route.until - now) % 8 == 0) {
                if ((route.until - now) / 8 % 4 == 3) in->kick = true;
                else in->punch = true;
            }
            return;
        }
        if (*s == 'W') {
            /* "Wx,y,z": run there through the rooms (route_plan), the steps put in its place */
            float b[3];
            static char steps[16384], rest[16384];
            char *e = strchr(s, ';');
            snprintf(rest, sizeof rest, "%s", e ? e + 1 : "");
            if (sscanf(s + 1, "%f,%f,%f", &b[0], &b[1], &b[2]) != 3) { route_next(); continue; }
            /* no way yet (a door still locked): tried again every 60 frames */
            if (route.phase && now < route.until) return;
            if (!route_plan(c, b, steps, sizeof steps)) {
                if (!route.phase) {
                    char m[400];
                    float bu[3] = { b[0], b[1] + 3.0f, b[2] };
                    int n = snprintf(m, sizeof m, "OpenOni: route: no way yet from room %d to %d: %.40s (locked:",
                                     graph_room_at(c->pos), graph_room_at(bu) >= 0 ? graph_room_at(bu) : graph_room_at(b), s);
                    for (int d = 0; d < door_count() && n < (int)sizeof m - 8; d++)
                        if (door_flags(d) & DOOR_LOCKED) n += snprintf(m + n, sizeof m - (size_t)n, " %u", door_id(d));
                    snprintf(m + n, sizeof m - (size_t)n, ")");
                    plat_log(m);
                }
                route.phase = 1;
                route.until = now + 60;
                return;
            }
            snprintf(route.buf, sizeof route.buf, "%s%s%s", steps, rest[0] ? ";" : "", rest);
            route.step = route.buf;
            route.phase = 0;
            route.until = 0;
            continue;
        }
        if (*s == 'L') {
            /* "L<type>": runs to the nearest powerup of the type ("lsi", "ammo", ...) on her floor (as
               "W", to within 6) and taps the swap key there (pick up); none: on */
            static char steps[16384], rest[16384];
            char type[16] = { 0 };
            const char *e = strchr(s, ';');
            snprintf(rest, sizeof rest, "%s", e ? e + 1 : "");
            size_t n = e ? (size_t)(e - s - 1) : strlen(s + 1);
            memcpy(type, s + 1, n < sizeof type - 1 ? n : sizeof type - 1);
            const Powerup *best = NULL;
            float bd = 1e30f;
            for (int i = 0; i < powerup_count(); i++) {
                const Powerup *pu = powerup_get(i);
                const char *tn = powerup_type_name(pu->type);
                if (!tn || strcmp(tn, type) || fabsf(pu->pos[1] - c->pos[1]) > 20.0f) continue;
                float dx = pu->pos[0] - c->pos[0], dz = pu->pos[2] - c->pos[2];
                if (dx * dx + dz * dz < bd) bd = dx * dx + dz * dz, best = pu;
            }
            if (!best) { route_next(); continue; }
            /* the way there planned as "W" does, its last step within 6 (the pickup reaches 8); one close
               by on her own level (within 16, 4 up or down: on the same crate) walked to straight */
            float at[3] = { best->pos[0], best->pos[1], best->pos[2] };
            size_t len;
            if (bd < 256.0f && fabsf(best->pos[1] - c->pos[1]) < 4.0f)
                snprintf(steps, sizeof steps, "%.1f,%.1f,P,6", at[0], at[2]);
            else if (!route_plan(c, at, steps, sizeof steps - 16) || (len = strlen(steps)) < 2 || strcmp(steps + len - 2, ",3"))
                snprintf(steps, sizeof steps, "%.1f,%.1f,P,6", at[0], at[2]);
            else
                steps[len - 1] = '6';
            strncat(steps, ";w60;q;w90", sizeof steps - strlen(steps) - 1);
            snprintf(route.buf, sizeof route.buf, "%s%s%s", steps, rest[0] ? ";" : "", rest);
            route.step = route.buf;
            route.phase = 0;
            route.until = 0;
            continue;
        }
        if (*s == 'K') {
            /* "K<console id>[,tries]": uses the console: to its marker (as "W"), turned as it says, the
               action key; again (up to 5 times in all) until the console is used, the fighters near
               fought off first (as "A600") on a retry (one in the way blocks the marker) */
            int id = atoi(s + 1), tries = 5;
            const char *comma = strchr(s, ',');
            const char *semi = strchr(s, ';');
            if (comma && (!semi || comma < semi)) tries = atoi(comma + 1);
            int ci = -1;
            for (int i = 0; i < console_count(); i++) if (console_id(i) == id) ci = i;
            const ActionMarker *m = ci >= 0 ? console_marker(ci) : NULL;
            if (!m || (console_flags(ci) & CONSOLE_USED) || tries <= 0) { route_next(); continue; }
            if (c->console_marker >= 0) return;     /* using one */
            /* not active yet (a script or an alarm's runner turns it on): waits by it */
            if (!(console_flags(ci) & CONSOLE_ACTIVE) && tries < 5) return;
            static char rest[16384];
            snprintf(rest, sizeof rest, "%s", semi ? semi + 1 : "");
            float h = m->facing * 57.29578f;
            snprintf(route.buf, sizeof route.buf, "%sW%.1f,%.1f,%.1f;h%.0f;w10;S120;a;w40;K%d,%d%s%s",
                     tries < 5 ? "A600;" : "", m->pos[0], m->floor, m->pos[2], h < 0 ? h + 360 : h, id, tries - 1,
                     rest[0] ? ";" : "", rest);
            route.step = route.buf;
            route.phase = 0;
            route.until = 0;
            continue;
        }
        if (*s == 'N') {
            /* "N<name>,N": for up to N frames, a talk with the character (a neutral): through the rooms
               to it when far (as "X"), walked up to within 20, turned to it, the action key tapped every 30 frames until the conversation
               begins; done when it has ended */
            char name[64];
            int n = 0;
            const char *comma = strchr(s, ',');
            size_t len = comma ? (size_t)(comma - s - 1) : 0;
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, s + 1, len), name[len] = 0;
            if (comma) n = atoi(comma + 1);
            const Chr *t = chars_find(name);
            if (!route.phase) route.until = now + (uint32_t)n, route.phase = 1;
            if (c->talk_partner) { route.phase = 2; return; }
            if (!t || route.phase == 2 || now >= route.until) { route_next(); continue; }
            float dx = t->c.pos[0] - c->pos[0], dz = t->c.pos[2] - c->pos[2];
            /* far (past 30, on another floor or behind a wall): through the rooms to it first (as "W"),
               then the talk again */
            {
                float dy = t->c.pos[1] - c->pos[1];
                bool far = dx * dx + dz * dz > 900.0f || fabsf(dy) > 15.0f;
                if (!far && route_env) {
                    const float from[3] = { c->pos[0], c->pos[1] + 5.0f, c->pos[2] }, v[3] = { dx, dy, dz };
                    far = akev_segment_hits(route_env, from, v, 0x42080);
                }
                static char steps[16384];
                if (far && route_plan(c, t->c.pos, steps, sizeof steps)) {
                    const char *semi = strchr(s, ';');
                    static char rest[16384];
                    snprintf(rest, sizeof rest, "%s", semi ? semi + 1 : "");
                    snprintf(route.buf, sizeof route.buf, "W%.1f,%.1f,%.1f;N%s,%u%s%s", t->c.pos[0], t->c.pos[1],
                             t->c.pos[2], name, route.until - now, rest[0] ? ";" : "", rest);
                    route.step = route.buf;
                    route.phase = 0;
                    route.until = 0;
                    continue;
                }
            }
            float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            if (dx * dx + dz * dz > 400.0f) {
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 0.6f;
                in->walk = true;
                return;
            }
            in->action = fabsf(err) < 0.3f && (route.until - now) % 30 == 0;
            return;
        }
        if (*s == 'X') {
            /* "X<name>,N": hunts the character: through the rooms to it (as "W") while it is more than
               30 away or on another floor, then the fight (as "F", N frames) */
            char name[64];
            const char *comma = strchr(s, ','), *semi = strchr(s, ';');
            size_t len = comma ? (size_t)(comma - s - 1) : 0;
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, s + 1, len), name[len] = 0;
            int n = comma ? atoi(comma + 1) : 0;
            const Chr *t = chars_find(name);
            /* back from a fight it left below her (down a slope, "!"): hunted again for N frames, then
               given up */
            static const Chr *hunted;
            static uint32_t hunt_until;
            bool again = (semi ? semi[-1] : s[strlen(s) - 1]) == '!';
            if (!t || !t->c.hit_points || !chars_in_use(t) || (again && hunted == t && now >= hunt_until)) {
                hunted = NULL;
                route_next();
                continue;
            }
            float dx = t->c.pos[0] - c->pos[0], dy = t->c.pos[1] - c->pos[1], dz = t->c.pos[2] - c->pos[2];
            static char rest[16384];
            snprintf(rest, sizeof rest, "%s", semi ? semi + 1 : "");
            /* no way to it (on a walkway the rooms don't reach): tried for N frames, then given up */
            if (hunted != t) hunted = t, hunt_until = now + (uint32_t)n;
            /* near but behind a wall: the way round, through the rooms */
            bool far = dx * dx + dz * dz > 900.0f || fabsf(dy) > 15.0f;
            if (!far && route_env) {
                const float from[3] = { c->pos[0], c->pos[1] + 5.0f, c->pos[2] }, v[3] = { dx, dy, dz };
                far = akev_segment_hits(route_env, from, v, 0x42080);
            }
            if (far) {
                /* planned again from where the last plan left her (its way ends short of the target, as
                   under a walkway): once its N frames are over, given up */
                static float planned_at[3];
                float mx = c->pos[0] - planned_at[0], my = c->pos[1] - planned_at[1], mz = c->pos[2] - planned_at[2];
                if (now >= hunt_until && mx * mx + my * my + mz * mz < 25.0f) {
                    hunted = NULL;
                    route_next();
                    continue;
                }
                memcpy(planned_at, c->pos, sizeof planned_at);
                static char steps[16384];
                if (!route_plan(c, t->c.pos, steps, sizeof steps)) {
                    if (now < hunt_until) return;
                    hunted = NULL;
                    route_next();
                    continue;
                }
            }
            if (far)
                snprintf(route.buf, sizeof route.buf, "W%.1f,%.1f,%.1f;X%s,%d%s%s%s", t->c.pos[0], t->c.pos[1], t->c.pos[2],
                         name, n, again ? "!" : "", rest[0] ? ";" : "", rest);
            else if (again && dy < -10.0f) {
                /* near, but below her (down the slope the fight left by): run at it */
                float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
                in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 0.6f;
                return;
            } else {
                if (!again) hunted = NULL;
                snprintf(route.buf, sizeof route.buf, "F%s,%d,h%s%s", name, n, rest[0] ? ";" : "", rest);
            }
            route.step = route.buf;
            route.phase = 0;
            route.until = 0;
            continue;
        }
        if (*s == 'F') {
            /* "F<name>,N": a melee fight with the character for up to N frames, or until it is down
               to no hit points or fallen 10 below her (not followed over an edge, nor while in the air): turned to it, run
               at it past 9 away unless their bodies touch (pushes, the last tick's), a punch or kick
               tapped every 8 frames within (three punches, then a kick) */
            char name[64];
            int n = 0;
            const char *comma = strchr(s, ',');
            size_t len = comma ? (size_t)(comma - s - 1) : 0;
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, s + 1, len), name[len] = 0;
            if (comma) n = atoi(comma + 1);
            const Chr *t = chars_find(name);
            if (!route.phase) route.until = now + (uint32_t)n, route.phase = 1;
            if (t && t->c.hit_points && now < route.until && t->c.pos[1] < c->pos[1] - 10.0f && comma && strchr(comma + 1, ',') &&
                strchr(comma + 1, ',')[1] == 'h' && (!strchr(s, ';') || strchr(comma + 1, ',') < strchr(s, ';'))) {
                /* a hunt's fight ("X") it left below her: the hunt again (marked "!") */
                const char *semi = strchr(s, ';');
                static char rest[16384];
                snprintf(rest, sizeof rest, "%s", semi ? semi + 1 : "");
                snprintf(route.buf, sizeof route.buf, "X%s,%d!%s%s", name, n, rest[0] ? ";" : "", rest);
                route.step = route.buf;
                route.phase = 0;
                route.until = 0;
                continue;
            }
            if (!t || !t->c.hit_points || now >= route.until || t->c.pos[1] < c->pos[1] - 10.0f) { route_next(); continue; }
            float dx = t->c.pos[0] - c->pos[0], dz = t->c.pos[2] - c->pos[2];
            float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            /* it is in the air (off an edge, or a jet's jump): waited for where it lands; nor run at
               over an edge */
            if (t->c.air_ticks) return;
            if (dx * dx + dz * dz > 81.0f && !c->pushes) {
                if (!route_floor_ahead(c->pos, dx, dz)) return;
                /* standing, she turns only past 70 degrees (HandleStandingTurn): stood 60 frames short
                   of that, she runs, and turns as she runs */
                static uint32_t stood;
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 0.6f;
                stood = !in->forward && c->type == 6 ? stood + 1 : 0;
                if (stood > 60) in->forward = true;
                return;
            }
            uint32_t k = (route.until - now) / 8;
            if ((route.until - now) % 8 == 0) {
                if (k % 4 == 3) in->kick = true;
                else in->punch = true;
            }
            return;
        }
        if (*s == 'G') {
            /* "Gfrom,to,N": for up to N frames, the weapon's barrel (its AI firing mode's, ONWC+0xd0)
               turned onto each unbroken glass target of the script ids in turn, a shot when on it */
            unsigned from = 0, to = 0, n = 0;
            sscanf(s + 1, "%u,%u,%u", &from, &to, &n);
            if (!route.phase) route.until = now + n, route.phase = 1;
            float at[3];
            unsigned id = from;
            while (id <= to && !glass_centre(id, at)) id++;
            float m[12];
            if (now >= route.until || id > to || !c->weapon || !chr_weapon_matrix(c, m)) { route_next(); continue; }
            if (route.phase > 1) { route.phase--; return; }    /* after a shot */
            const uint8_t *p = c->weapon->cls + 0xd0;
            float b[3], mz[3], w[3];
            for (int j = 0; j < 3; j++) {
                float x, y, z;
                memcpy(&x, p + 0x34, 4), memcpy(&y, p + 0x38, 4), memcpy(&z, p + 0x3c, 4);
                b[j] = m[j] * x + m[3 + j] * y + m[6 + j] * z;
                memcpy(&x, p + 0x40, 4), memcpy(&y, p + 0x44, 4), memcpy(&z, p + 0x48, 4);
                mz[j] = m[j] * x + m[3 + j] * y + m[6 + j] * z + m[9 + j];
                w[j] = at[j] - mz[j];
            }
            float ey = wrap_pi(atan2f(w[0], w[2]) - atan2f(b[0], b[2]));
            float ep = atan2f(w[1], sqrtf(w[0] * w[0] + w[2] * w[2])) - atan2f(b[1], sqrtf(b[0] * b[0] + b[2] * b[2]));
            if (fabsf(ey) < 0.004f && fabsf(ep) < 0.004f) {
                in->fire = true;
                route.phase = 41;
                return;
            }
            in->turn = ey * 0.5f > 0.05f ? 0.05f : ey * 0.5f < -0.05f ? -0.05f : ey * 0.5f;
            in->pitch = ep * 0.5f > 0.05f ? 0.05f : ep * 0.5f < -0.05f ? -0.05f : ep * 0.5f;
            return;
        }
        if (*s == 'c') {
            route.crouch = true;
            route_next();
            continue;
        }
        if (*s == 'v') {
            /* "vD": aims to D degrees up (the mouse's pitch) */
            float want = (float)atof(s + 1) * 0.017453293f, err = want - c->aim_ud;
            if (fabsf(err) < 0.005f) { route_next(); continue; }
            in->pitch = err > 0.05f ? 0.05f : err < -0.05f ? -0.05f : err;
            return;
        }
        if (*s == 'h') {
            float want = (float)atof(s + 1) * 0.017453293f, err = wrap_pi(want - c->desired_facing);
            if (fabsf(err) < 0.02f) { route_next(); continue; }
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            return;
        }
        float x, z, radius = 3.0f;
        char mode = 0;
        if (sscanf(s, "%f,%f,%c,%f", &x, &z, &mode, &radius) < 2) { route_next(); continue; }
        float dx = x - c->pos[0], dz = z - c->pos[2];
        if (dx * dx + dz * dz < radius * radius) { route.crouch = false; route_next(); continue; }
        /* steered by the desired facing (what the turn input moves; the facing follows it) */
        float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
        in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
        in->crouch = route.crouch;
        /* forward only while the body itself faces the point (it moves the way it faces); "R" runs as
           "r" but starts off up to 1.3 away: standing in fight mode the body turns on the spot only past
           70 degrees (HandleStandingTurn), and turns as it goes; so does a sprint in fight mode */
        float off = fabsf(wrap_pi(atan2f(dx, dz) - c->facing));
        bool loose = mode == 'R' || mode == 'P' || (mode == 's' && (c->varient & 0x200));
        /* standing within 70 degrees of the desired facing the body doesn't turn (HandleStandingTurn): its
           facing still for 30 frames, she sets off as it is (moving, it turns) */
        static const char *still_step;
        static float still_facing;
        static int still;
        if (still_step != s || fabsf(c->facing - still_facing) > 0.001f) still_step = s, still_facing = c->facing, still = 0;
        if (off > (loose ? 1.3f : 0.6f) && ++still < 30) return;
        if (mode == 's' && route.phase < 3) {
            /* the double tap: down, up, down (within 15 ticks), then held */
            in->forward = route.phase != 1;
            route.phase++;
            return;
        }
        in->forward = 1;
        in->walk = mode == 'k' || (mode == 'P' && dx * dx + dz * dz < 100.0f);
        /* no headway for a second (something the rooms' grids don't hold, at the body's height): a
           second of stepping to the side, one way then the other, punching when a body holds her */
        static float stuck_best;
        static const char *stuck_step;
        static int stuck, side, tries;
        float dist = sqrtf(dx * dx + dz * dz);
        if (stuck_step != s || dist < stuck_best - 1.0f) {
            /* headway: nearer the point by 1 (or a new point) */
            if (stuck_step != s) tries = 0;
            stuck_step = s, stuck_best = dist;
            if (stuck < 60) stuck = 0;
        }
        if (++stuck > 60) {
            in->strafe = side & 1 ? -1 : 1;
            /* held by a character's body (pushes): blows at it, every 8 frames */
            if (c->pushes && stuck % 8 == 0) in->punch = true;
            if (stuck > 120) stuck = 0, side++, tries++;
        }
        /* ten such tries: the point is passed over (the next one may be reached around it) */
        if (tries >= 10) {
            tries = stuck = 0;
            route_next();
        }
        if (mode == 'J' || mode == 'j') {
            /* a running jump: 20 frames of run ("j": none), then jump held for 30 */
            route.phase++;
            int run = mode == 'J' ? 20 : 0;
            in->jump = route.phase > run && route.phase <= run + 30;
        }
        return;
    }
}

/* What the camera follows (the character fields CArFollow_Get_GoalPosition 0x4d7110 reads; aim angles
   come with the character engine's aiming). */
/* iComputeDeltaTicks 0x4fafb0's listener for OSrUpdate 0x514f20: in the follow camera with a player, her
   ears (0x4e33e0: her location at her head's height, 15 above it without an active part) facing her
   way (0x4e3640: the desired facing less the run's turn); else the camera's place and view. */
static void listen(const Chr *player)
{
    float pos[3], dir[3];
    CameraView v;
    if (player && camera_mode() == CAMERA_FOLLOW) {
        const Character *c = &player->c;
        float head[3];
        chr_location(c, pos);
        if (chr_part_point(c, 10, head)) pos[1] = head[1];
        else pos[1] += 15.0f;   /* 0x529d84 */
        float a = c->desired_facing - c->facing_offset;
        if (a < 0.0f) a += 6.2831855f;
        else if (a >= 6.2831855f) a -= 6.2831855f;     /* 0x529548 */
        dir[0] = sinf(a), dir[1] = 0.0f, dir[2] = cosf(a);  /* MUrPoint_RotateYAxis 0x433010 of (0, 0, 1) */
    } else if (camera_view(&v)) {
        memcpy(pos, v.location, sizeof pos);
        memcpy(dir, v.view, sizeof dir);
    } else
        return;
    sound_set_listener(pos, dir);
}

static void fill_target(CameraTarget *t, const Character *c)
{
    t->active = true;
    memcpy(t->pos, c->pos, sizeof t->pos);
    t->bottom = c->pos[1];
    t->facing = c->facing;
    t->aim[0] = sinf(c->facing);
    t->aim[1] = 0;
    t->aim[2] = cosf(c->facing);
}

/* ---- play (--param play=1 [--param level=N] [--param at=x,y,z,deg]): Konoko at the level's start, run with WASD (Caps Lock
   walks, Space jumps, Shift crouches, F punches, C kicks, Ctrl acts), turned with the mouse or Left/Right, the camera behind her. The first playable: the
   character engine is in progress (character.h). ---- */
/* P3rUpdate with the characters the particles may hit (the physics contexts that collide). */
static void particles_update(uint32_t tick, bool gameplay)
{
    Character *list[CHARS_MAX];
    int n = 0;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (c && chars_in_use(c)) list[n++] = &c->c;
    }
    p3_update(tick, gameplay, list, n);
}

static TmFile *level_zero;

/* ONrLevel_GetNextLevel 0x507000: the lowest level number above this one among the level descriptors
   (ONLD, +0 the number) whose files are there; 0 none. */
static int next_level(int level)
{
    TmFile *zero = level_zero;
    int best = 0xffff;
    for (uint32_t i = 0; zero && i < tm_file_instance_count(zero); i++) {
        const TmInstance *t = tm_file_instance(zero, i);
        if (!t->data || !t->name || strncmp(t->name, "ONLD", 4)) continue;
        int n = t->data[0] | t->data[1] << 8;
        if (n > level && tm_level_valid(n) && n <= best) best = n;
    }
    return best == 0xffff ? 0 : best;
}

/* Any key or mouse button pressed this frame (the splash's events 2, 4, 6 and 9). */
static bool any_press(void)
{
    uint8_t codes[16];
    PlatMouse mouse;
    return plat_key_presses(codes, (int)sizeof codes) > 0 || (plat_mouse(&mouse) && mouse.pressed);
}

static void splash_frame(const char *name, float blanket)
{
    if (plat_frame_shown()) {
        int w, h;
        plat_display_size(&w, &h);
        ui_draw_splash(w, h, name, blanket);
    }
    listen(chars_player());     /* the listener kept on the player (0x4f4e56, as the game's frame) */
    sound_update();     /* 0x483bd0, OSrUpdate 0x514f20 */
    plat_wait_frame();
    splash_frames++;
}

/* ONrGameState_SplashScreen 0x4f4d40, the original's blocking loop in the game loop's place for the
   frame: the TXMB of the name (none: nothing but the script-only switch turned off), in from black over
   120 ticks ((1 - t/120)^1.9; past 120 the power of a negative is no number and no blanket is drawn)
   within 600, then 600 more, either ended by a key or a button, then out to black over 120 ((t/120)^1.9).
   Quiet (the win and loss screens): the scripts' sounds held off and every sound stopped first (0x483710).
   The music (none for the chapters' and the levels' intro screens) starts from nothing, up over a second,
   down over a second as the fade out begins, and is halted at the end. Its clock is the machine's
   sixtieths: here the frames. Each frame the listener is set as the game's frame sets it. */
static void splash_screen(const char *name, const char *music, bool quiet)
{
    if (tm_instance_get(TM_TAG("TXMB"), name)) {
        if (quiet) {
            sound_set_script_only(true);
            sound_stop_all();
        }
        sound_music_play(music, 0.0f);
        sound_music_fade(1.0f, 1.0f);
        bool skip = false;
        for (int t = 0; t < 600 && !skip; t++) {
            if (any_press()) skip = true;
            else splash_frame(name, powf(1.0f - (float)t * (1.0f / 120.0f), 1.9f));
        }
        for (int t = 0; t < 600 && !skip; t++) {
            if (any_press()) skip = true;
            else splash_frame(name, 0.0f);
        }
        sound_music_fade(0.0f, 1.0f);
        for (int t = 0; t < 120; t++) splash_frame(name, powf((float)t * (1.0f / 120.0f), 1.9f));
        sound_music_halt();
    }
    sound_set_script_only(false);
}

/* ---- the movies (docs/formats/bink.md "The player") ---- */

/* The movie's volume (0x488080): BinkSetVolume of 32768 times the overall volume (clamped to 0..1,
   MUrUnsignedSmallFloat_To_Uns_Round); the DirectSound backend (0x1002ad40) caps it at 0x7fff and looks
   up its hundredths of a dB at (v × 127 / 0x7fff) in the DLL's table at 0x1003e820. */
static float movie_gain(void)
{
    float v = persist_volume();
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    uint32_t vol = (uint32_t)lrintf(v * 32768.0f);
    if (vol > 0x7fff) vol = 0x7fff;
    const uint8_t *t = dll_data(0x1003e820 + 2 * (vol * 127 / 0x7fff), 2);
    int16_t att = t ? (int16_t)(t[0] | t[1] << 8) : 0;
    return att <= -10000 ? 0.0f : powf(10.0f, (float)att / 2000.0f);
}

typedef struct {
    BinkMovie *m;
    uint8_t *rgba;
    unsigned tex;
    bool fresh;             /* rgba not yet uploaded */
    uint64_t t0;            /* ms, the first frame's end (0x10009b90) */
    uint32_t done;          /* frames decoded (+0x200) */
} Movie;

static bool movie_open(Movie *mv, const char *name)
{
    char rel[64], err[256], msg[320];
    memset(mv, 0, sizeof *mv);
    if (!dll_loaded() && !dll_init(err, sizeof err)) {
        snprintf(msg, sizeof msg, "OpenOni: %s", err);
        plat_log(msg);
        return false;
    }
    snprintf(rel, sizeof rel, "GameDataFolder/%s", name);
    mv->m = bink_open(rel, err, sizeof err);
    if (!mv->m) {
        snprintf(msg, sizeof msg, "bink movie file (%s) does not exist (%s)", name, err);   /* 0x53e7f0 */
        plat_log(msg);
        return false;
    }
    mv->rgba = malloc((size_t)bink_width(mv->m) * bink_height(mv->m) * 4);
    if (!mv->rgba) {
        bink_close(mv->m);
        return false;
    }
    sound_stream_start(bink_audio_channels(mv->m), movie_gain());
    return true;
}

static void movie_close(Movie *mv)
{
    sound_stream_stop();
    gl_dev_texture_free(&mv->tex);
    free(mv->rgba);
    bink_close(mv->m);
}

/* BinkWait 0x1000a3c0 on the frame clock: true until the next frame's time, frames done × 1000 × den / num
   ms after the first frame's end. (With its sound track the DLL waits on the sound buffer
   instead, which the frame clock keeps in step with.) */
static bool movie_wait(const Movie *mv)
{
    if (!mv->done) return false;
    uint32_t num, den;
    bink_rate(mv->m, &num, &den);
    uint64_t target = (uint64_t)mv->done * 1000u * den / (num ? num : 1);
    return plat_time_us() / 1000 - mv->t0 < target;
}

/* BinkDoFrame and BinkCopyToBuffer: the frame's sound to the stream, its picture to RGBA. */
static void movie_frame(Movie *mv)
{
    bink_do_frame(mv->m);
    uint32_t n;
    const int16_t *a = bink_audio(mv->m, &n);
    if (a && n) sound_stream_push(a, n);
    bink_copy_rgba(mv->m, mv->rgba);
    mv->fresh = true;
    if (!mv->done++) mv->t0 = plat_time_us() / 1000;     /* 0x10009b90 */
}

static void movie_show(Movie *mv)
{
    if (!plat_frame_shown()) return;
    int w, h;
    plat_display_size(&w, &h);
    if (mv->fresh) {
        gl_dev_texture_rgba(&mv->tex, (int)bink_width(mv->m), (int)bink_height(mv->m), mv->rgba);
        mv->fresh = false;
    }
    ui_draw_movie(w, h, mv->tex, (int)bink_width(mv->m), (int)bink_height(mv->m));
}

/* The movie player before the 3D display, 0x488080 (through 0x4d5580; ONiMain's intro.bik): a BinkBuffer
   centred on the screen; until the last frame, the events pumped (and dropped: no key ends it), BinkWait,
   BinkDoFrame, BinkCopyToBuffer, the blit, BinkNextFrame. Its first pass (opened for the window's size and
   closed again) changes nothing here. The last frame stays on the screen till something else draws. */
static void movie_play(const char *name)
{
    Movie mv;
    if (!movie_open(&mv, name)) return;
    for (;;) {
        while (movie_wait(&mv)) {
            movie_show(&mv);
            plat_wait_frame();
            splash_frames++;
        }
        movie_frame(&mv);
        if (bink_frame(mv.m) == bink_frames(mv.m) - 1) break;
        bink_next_frame(mv.m);
    }
    movie_show(&mv);
    plat_wait_frame();
    splash_frames++;
    movie_close(&mv);
}

/* The movie player in the 3D display, 0x488ff0 (through 0x4d56f0; the outro at the game's end, the
   renderer not 3Dfx): its OpenGL path (a driver not S3's): the frames as GL textures at scale 1, centred;
   a key or a mouse button (events 2, 9) ends it; the last frame is never shown (the loop ends on it,
   tested before BinkWait). */
static void movie_play_in_game(const char *name)
{
    Movie mv;
    if (!movie_open(&mv, name)) return;
    for (;;) {
        if (any_press()) break;
        bool last = bink_frame(mv.m) == bink_frames(mv.m) - 1;
        if (!movie_wait(&mv)) {
            movie_frame(&mv);
            bink_next_frame(mv.m);
        }
        movie_show(&mv);
        plat_wait_frame();
        splash_frames++;
        if (last) break;
    }
    movie_close(&mv);
}

/* A link of the level's ONLV (the loaded level's file). */
static const void *level_link(unsigned at)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONLV") && in->data) return tm_link(in->data + at);
    }
    return NULL;
}

/* One level, until it ends (*next: the level to load then, 0 none) or the player quits (Esc). */
/* ONiGameState_Display_Reflectable 0x4fb480: the level, its characters, particles and lasers, the HUD and
   the screen's layer; also the WM's dialogs' draw callback (WMrDialog_SetDrawCallback, set at the level's
   start), so the level stays behind them. */
static struct { const CameraTarget *ct; const Chr *player; uint32_t ticks; } display;

/* ONrGameState_Display: run every frame, as the original's loop displays each of its passes; a frame the
   platform doesn't show is walked without drawing (gl_dev_set_dry), so what the display changes (the
   laser triggers, OBJrTrigger_OnTrigger 0x4cb6f0, test their beams as they are drawn; the textures'
   random frames reseed the local random generator) doesn't depend on it. */
static void game_display(void)
{
    laser_frame();
    gl_dev_set_dry(!plat_frame_shown());
    int w, h;
    plat_display_size(&w, &h);
    float proj[16], view[16];
    const float near = 2.0f;
    perspective(proj, 0.785398f, h ? (float)w / (float)h : 1.0f, near, far_clip);
    if (!camera_view_matrix(view) && display.ct) {
        float eye[3] = { display.ct->pos[0], display.ct->pos[1] + 20, display.ct->pos[2] };
        view_matrix(view, eye, 3.14159265f - display.ct->facing, -0.3f);
    }
    if (display.ticks) fog_step(display.ticks);
    gl_dev_set_time(display.ticks);     /* the draw state's time, the game's (0x4fb81f) */
    gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
    gl_dev_matrices(proj, view);
    gl_dev_fog(fog_start, fog_end, fog_rgb[0], fog_rgb[1], fog_rgb[2]);    /* the frame's start, 0x4058a3 */
    LodCamera lc = { .fov = 0.785398f, .far = far_clip };
    CameraView cv;
    if (camera_view(&cv)) memcpy(lc.pos, cv.location, sizeof lc.pos), memcpy(lc.view, cv.view, sizeof lc.view);
    else if (display.ct) memcpy(lc.pos, display.ct->pos, sizeof lc.pos), lc.view[2] = 1.0f;
    geom_set_eye(lc.pos);
    /* 0x4430f0: the visibility rays from the camera (its location, view and up, and the frustum's corners
       as 0x41b5c0 unprojects them: the near plane's then the far's, top left, top right, bottom left,
       bottom right), then the sky if one got out, then the environment's marked quads (0x444030) */
    VisCamera vc;
    float right[3] = { view[0], view[4], view[8] }, up[3] = { view[1], view[5], view[9] };
    float fwd[3] = { -view[2], -view[6], -view[10] }, eye[3];
    for (int k = 0; k < 3; k++) eye[k] = -(view[4 * k] * view[12] + view[4 * k + 1] * view[13] + view[4 * k + 2] * view[14]);
    memcpy(vc.location, eye, sizeof eye), memcpy(vc.view, fwd, sizeof fwd), memcpy(vc.up, up, sizeof up);
    float aspect = h ? (float)w / (float)h : 1.0f, t = tanf(0.785398f * 0.5f);
    for (int p = 0; p < 2; p++) {
        float d = p ? far_clip : near, hh = d * t, hw = hh * aspect;
        for (int c = 0; c < 4; c++) {
            float sy = c < 2 ? hh : -hh, sx = (c & 1) ? hw : -hw;
            for (int k = 0; k < 3; k++) vc.corners[4 * p + c][k] = eye[k] + fwd[k] * d + up[k] * sy + right[k] * sx;
        }
    }
    bool sky = vis_update(&vc);
    p3_set_sky_seen(sky);       /* 0x460510 */
    if (sky) {
        gl_dev_fog_enable(false);   /* the draw state 0xf off (0x4fb69f) */
        sky_draw(eye, right, up);
        gl_dev_fog_enable(true);
    }
    env_draw_visible();
    draw_objects();
    char_shadows_clear();
    char_draw_corpses();
    /* 0x4e1540: the characters in the camera's view, their levels of detail (0x4e2900), then drawn */
    Character *seen[CHARS_MAX];
    int slot_of[CHARS_MAX], nseen = 0;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch) continue;
        if (!chr_lod_visible(&ch->c, &lc)) {
            ch->c.lod_frames = 0;
            ch->c.lod_shown = ch->c.lod_wanted = 6;
            continue;
        }
        seen[nseen] = &ch->c;
        slot_of[nseen++] = i;
    }
    Character *order[CHARS_MAX];
    memcpy(order, seen, sizeof(Character *) * (size_t)nseen);
    chr_lod_compute(order, nseen, display.player ? &display.player->c : NULL);
    for (int k = 0; k < nseen; k++) char_draw_character(seen[k], slot_of[k]);   /* the blended pose */
    decal_draw(env_draw_environment());     /* P3rDisplayStaticDecals, P3rDisplayDynamicDecals */
    particle_draw(view, p3_tick());     /* P3rDisplay */
    laser_draw(view);
    particle_draw_flares(view, near, p3_tick());
    if (display.player) ui_draw_hud(w, h, &display.player->c, display.ticks);
    ui_draw(w, h);
    int dw, dh;
    ui_draw_wm_begin(w, h, &dw, &dh);   /* the WM's windows over it, in its own pixels */
    gl_dev_set_dry(false);
}

/* The modal dialogs' frame (in WMrUpdate's place for the port): the sound, and for unattended runs the
   keys of --param keys=<n>[,<n>...], the LI codes (27 Escape, 32 space, 13 Return) tapped into the
   dialogs, or "@x:y" a click at the desktop's point (it is as tall as 480), one every 30 of their frames.
   The dialogs' frames are not the route's: its clock leaves them out. */
static uint32_t modal_frames;
static char modal_keys[128];
static const char *modal_key;
static void modal_frame(void)
{
    sound_update();     /* 0x483bd0, WMrUpdate's end */
    splash_frames++;
    if (++modal_frames % 30 == 0 && modal_key && *modal_key) {
        if (*modal_key == '@') {
            const char *c = strchr(modal_key, ':');
            wm_click((int16_t)atoi(modal_key + 1), (int16_t)(c ? atoi(c + 1) : 0));
        } else wm_key((uint8_t)atoi(modal_key));
        const char *c = strchr(modal_key, ',');
        modal_key = c ? c + 1 : NULL;
    }
}

/* OWrGame_Start 0x4beb40's choice: the level (the low byte); the continue cleared (0x4fda80), then with a
   slot in the high byte that persisted continue made the game's, so the level's scripts start from it. */
static int game_start(uint32_t v)
{
    int level = (int)(v & 0xff);
    save_clear();
    if (v & 0xff00) save_use_persisted(level, (int)(v >> 8 & 0xff));
    char m[64];
    snprintf(m, sizeof m, "OpenOni: load game: level %d, continue %d", level, (int)(v >> 8 & 0xff));
    plat_log(m);
    return level;
}

/* The pause key gone down this frame (the LI's action 0x81, F1). */
static bool pause_key(void)
{
    uint8_t p[64];
    int n = plat_key_presses(p, (int)sizeof p);
    for (int i = 0; i < n; i++)
        if (p[i] == DIK_F1) return true;
    return false;
}

static void restored(void) { igui_notify_restore(ui_time()); }

/* ONrLevel_Load 0x5059d0's progress (its second argument: 1 from both its callers): the dialog 155
   ("dialog_Progress", the procedure 0x5068e0) made in the game's look (the PSUI "psui_oniUI", 0x54c898;
   the one before set again once the dialog is gone) and drawn over a blank frame; at each step of the
   load its bar (the item 100) is set (0x2e) and a frame drawn (0x41ab00, WMrDisplay, 0x41ad00). The
   steps are the original's percentages, each where the port has done the parts the original has done
   by then (the port's load is in its own order). */
static void blank_display(void);
static WmWindow *load_dialog;   /* 0x5ed2f4 */
static const void *load_psui;   /* 0x5ed2f0 */

/* 0x5068e0: the dialog's init (0) and 0x2e (the percentage) passed to its bar. */
static bool load_proc(WmWindow *w, uint32_t msg, uintptr_t p1, uintptr_t p2)
{
    (void)p2;
    if (msg != WM_INITDIALOG && msg != 0x2e) return false;
    WmWindow *bar = wm_item(w, 100);
    if (bar) wm_send(bar, 0x2e, msg == 0x2e ? p1 : 0, 0);
    return true;
}

static void load_frame(void)
{
    if (plat_frame_shown()) {
        blank_display();
        wm_display();
    }
    plat_wait_frame();
}

static void load_step(uint32_t percent)
{
    if (!load_dialog) return;
    wm_send(load_dialog, 0x2e, percent, 0);
    load_frame();
}

static void load_begin(void)
{
    if (!load_dialog) {
        load_psui = wm_psui_active();
        const void *ui = wm_psui_get(exe_str(0x54c898));
        if (ui) wm_psui_set_active(ui);
        load_dialog = wm_dialog_new(155, load_proc, 0);
        load_frame();
    }
    load_step(10);
}

static void load_end(void)
{
    load_step(100);
    if (load_dialog) {
        wm_window_delete(load_dialog);      /* 0x474950 */
        load_dialog = NULL;
        wm_psui_set_active(load_psui);
    }
}

static int play(int level, int *next)
{
    *next = 0;
    char err[256], m[300], at_dummy[8];
    if (!gl_dev_init()) {
        plat_log("OpenOni: no display");
        return 1;
    }
    {
        int w, h;
        plat_display_size(&w, &h);
        wm_initialize(h ? w * 480 / h : 640, 480);
    }
    load_begin();
    trac_set_level((uint16_t)level);
    if (!tm_level_load(level, err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: level %d: %s", level, err);
        plat_log(m);
        return 1;
    }
    load_step(20);
    prepare_collections(tm_file_at(tm_file_slots() - 1));
    load_step(24);
    sound_level_load(level);
    load_step(25);
    const void *env = level_environment(level);
    if (!env || !env_draw_load(env)) {
        plat_log("OpenOni: the level's environment doesn't load");
        return 1;
    }
    load_step(28);
    chars_load();
    load_step(30);
    if (plat_param("ailog", at_dummy, sizeof at_dummy)) combat_log = plat_log;
    if (plat_param("physlog", at_dummy, sizeof at_dummy)) physics_debug_log = plat_log;
    Chr *player = chars_player();
    if (!player) {
        plat_log("OpenOni: no player character");
        return 1;
    }
    /* --param at=x,y,z[,degrees]: another start, for testing */
    char at[96];
    if (first && plat_param("at", at, sizeof at)) {
        float deg = player->c.facing / 0.017453293f;
        if (sscanf(at, "%f,%f,%f,%f", &player->c.pos[0], &player->c.pos[1], &player->c.pos[2], &deg) >= 3)
            player->c.facing = deg * 0.017453293f;
    }
    trigger_load();
    object_load();
    door_load((void *)env);
    obstacle_invalidate();      /* the characters' layers again, with the objects and doors */
    load_step(35);
    material_level_load();                  /* ONrTextureMaterials_LevelLoad */
    material_mark_breakable((void *)env);   /* AKrLevel_Begin: the glass */
    p3_load();      /* the PAR3 classes of the loaded files (the binary data's loader), set up at the start */
    p3_set_environment(env);
    impact_effects_level_begin();   /* its particle classes and impulses, the level's */
    envparticle_load();     /* ONrLevel_Load: the level's environment particles, after the particle system starts */
    load_step(40);
    weapons_reset();
    powerups_reset();
    weapons_level_begin();      /* 0x51b0f0: the level's Weapon and PowerUp objects */
    powerups_level_begin();
    load_step(50);
    chars_arm();
    load_step(55);
    console_load();
    laser_set_environment(env);
    laser_load();
    turret_load();      /* the mechanics: the Turret objects (OBJrTurret_Initialize 0x4cdf70's type) */
    load_step(62);
    action_install();
    load_step(65);
    env_set((void *)env);
    route_env = env;
    /* the camera behind the player (CArInitialize's state, reset to follow) */
    CameraTarget ct = { .active = true };
    camera_init();
    camera_set_environment((void *)env);
    sound_set_environment(env);
    fill_target(&ct, &player->c);
    camera_set_target(&ct);
    camera_reset();
    /* AKrEnvironment_LevelBegin 0x43ffe0's visibility, then ONrLevel_Load's sky (ONrSky_LevelBegin 0x5105a0,
       the level's ONLV +0x54) after the camera's reset */
    vis_level_begin(env);
    sky_level_begin(level_link(0x54), far_clip);
    load_step(70);
    uint32_t ticks = 0;     /* the game time (ONrGameState's +0x144), ticks since the level began */
    ui_reset();
    load_step(75);
    load_step(85);
    load_step(87);
    igui_level_load(level);     /* ONiInGameUI_LevelLoad 0x503eb0 */
    load_step(90);
    save_level_begin(level);
    weapon_new_hook = igui_new_weapon;
    powerup_new_hook = igui_new_item;
    save_restore_hook = restored;
    cinematic_reset();
    load_end();
    wm_set_draw_callback(game_display);     /* ONrGameState_LevelBegin 0x4f5350 */
    wm_set_frame_hook(modal_frame);
    display.ct = &ct, display.player = player, display.ticks = 0;
    /* the end of ONrLevel_Load 0x5059d0: the level's intro screen, no music */
    splash_screen(exe_str(0x555f28), NULL, false);
    start_scripts();
    memset(&route, 0, sizeof route);
    if (first && plat_param("route", route.buf, sizeof route.buf)) route.step = route.buf;
    first = false;
    plat_log("OpenOni: playing");
    for (;;) {
        uint8_t keys[KEY_COUNT] = { 0 }, presses[64];
        int n = plat_key_presses(presses, (int)sizeof presses);
        plat_keys(keys);
        /* the frame's actions from the bindings (key_config.txt; LIrUpdate 0x403220), the mouse's while
           it is captured (the first click captures it) */
        PlatMouse mouse;
        bool have_mouse = plat_mouse(&mouse) && mouse.captured;
        if (plat_mouse(&mouse) && (mouse.pressed & MOUSE_LEFT) && !mouse.captured) plat_mouse_capture(true);
        LiActions act;
        li_actions(&act, have_mouse ? &mouse : NULL, persist_invert_mouse());
        /* 0x4faec0: the player's escape (its input's bit 1, at most once in 15 ticks, not in a cutscene)
           opens the main menu over the level (0x4bec00); New Game starts again at level 1 (0x4beb40) */
        /* ... and its pause key (bit 4, F1 by default; not when dead) the pause screen, after the frame
           (ONiRunGame 0x4d4640, G+0x138) */
        if ((act.down & 4) && (ui_keys_allowed() & 4) && !ui_cutscene() && player && !(player->c.flags & 1)) igui_pause_screen(pause_key);
        if (act.down & 1) {
            uint32_t start = 0;
            int choice = outgame_main_menu(true, &start);
            if (choice == OUTGAME_LOAD) {
                *next = game_start(start);
                return 0;
            }
            if (choice == OUTGAME_QUIT) {
                *next = 0;
                return 0;
            }
            if (choice == OUTGAME_NEW_GAME) {
                *next = 1;
                save_clear();       /* 0x4fda80 */
                return 0;
            }
        }
        /* the action bits as the character's input (character.h's CHR_KEY_*: 1 << the action's index);
           punch and kick also on a tap within the frame; the mouse's axes as HandleTurnInput 0x4f7b90 takes
           them (turn_x is aim_lr, turn_y aim_ud) */
        uint64_t h = act.held, hd = act.held | act.down;
        CharacterInput in = {
            .forward = !!(h & CHR_KEY_FORWARD) - !!(h & CHR_KEY_BACK),
            .strafe = !!(h & CHR_KEY_STEP_RIGHT) - !!(h & CHR_KEY_STEP_LEFT),
            .walk = (h & CHR_KEY_WALK) != 0, .turn = act.aim_lr * (-1.0f / 64.0f), .pitch = act.aim_ud * (1.0f / 64.0f),
            .punch = (hd & CHR_KEY_PUNCH) != 0, .kick = (hd & CHR_KEY_KICK) != 0,
            .jump = (hd & CHR_KEY_JUMP) != 0, .crouch = (h & CHR_KEY_CROUCH) != 0,
            .action = (hd & CHR_KEY_ACTION) != 0, .fire = (hd & CHR_KEY_FIRE) != 0,
            .swap = (act.down & CHR_KEY_SWAP) != 0, .drop = (act.down & CHR_KEY_DROP) != 0,
            .reload = (act.down & CHR_KEY_RELOAD) != 0, .hypo = (act.down & CHR_KEY_HYPO) != 0,
            .extra = hd & (CHR_KEY_TURN_LEFT | CHR_KEY_TURN_RIGHT | (uint64_t)0x80000000u | CHR_KEY_LOOK |
                           CHR_KEY_ALT_FIRE | CHR_KEY_FIRE3),
        };
        /* the shapeshifter's key (debug key 0x29, LI 0x88): F8, with Shift back */
        for (int i = 0; i < n; i++)
            if (presses[i] == DIK_F8 && player) cheat_shapeshift(&player->c, keys[DIK_LSHIFT] || keys[DIK_RSHIFT]);
        /* "input 0" from a script (cutscenes): the player's keys do nothing (0x4bcc50's flag) */
        if (route.step && player) route_input(&player->c, &in);
        /* a debug aid: --param invincible=1, omnipotent=1 and unstoppable=1, the game's switches of
           those names (the cheats') kept on, for unattended runs of a level's scripts */
        char inv[8];
        if (plat_param("invincible", inv, sizeof inv) && atoi(inv)) chr_cheat_invincible = true;
        if (plat_param("omnipotent", inv, sizeof inv) && atoi(inv)) chr_cheat_omnipotent = true;
        if (plat_param("unstoppable", inv, sizeof inv) && atoi(inv)) chr_cheat_unstoppable = true;
        chr_input_mask(&in, ui_keys_allowed());     /* lock_keys */
        if (route_pause && (ui_keys_allowed() & 4) && !ui_cutscene() && player && !(player->c.flags & 1)) {    /* held until allowed */
            route_pause = false;
            igui_pause_screen(pause_key);
        }
        if (!ui_input_enabled()) memset(&in, 0, sizeof in);
        /* ONrGameState_UpdateServerTime 0x4fbeb0: in slow motion (G+0x104, the "carousel" cheat, or the
           slowmo timer G+0x108 running) the game's clock takes two of the machine's 60ths a tick: a tick
           every other frame */
        static bool slow_skip;
        slow_skip = (cheat_slow_motion || ui_slow_motion()) && !slow_skip;
        if (!slow_skip) {
            sl_update();
            /* ONrGameState_UpdateCharacters 0x4dccf0: the volumes first, then the characters */
            trigger_update(chars_trigger_actors(), chars_slots());
            object_update(++ticks);
            door_physics(ticks);
            ui_update();
            igui_update(chars_player() ? &chars_player()->c : NULL, ui_time());
            cinematic_update();
            chars_update(&in, env);
            weapons_update(env);    /* WPrUpdate, after the characters */
            powerups_update();      /* 0x5190f0 */
            weapons_physics(env);   /* OBiPhysics_Update 0x43af50: the weapons' and powerups' contexts */
            powerups_physics(env);
            door_update();      /* the objects' update 0x507880, after the characters */
            laser_update();     /* the triggers' (0x4cb100), the same mechanics update */
            turret_update(env); /* OBJiTurretCombat_Update 0x4cf140, the mechanics' update too */
            particles_update(ticks + 1, true);     /* P3rUpdate(time + 1) after the physics */
            ui_condition_sounds(chars_player() ? &chars_player()->c : NULL, camera_mode() == CAMERA_FOLLOW);
            /* a script may delete the player: the camera keeps the last place */
            if ((player = chars_player())) fill_target(&ct, &player->c);
            camera_set_target(&ct);
            camera_update(1, 0, NULL, ticks);   /* the tick's part (iComputeDeltaTicks 0x4fafb0) */
        }
        listen(chars_player());
        sound_update();
        char dbg[32];
        if (player && plat_param("trace", dbg, sizeof dbg) && plat_frames() % (uint32_t)(atoi(dbg) > 1 ? atoi(dbg) : 30) == 0) {
            const Character *konoko = &player->c;
            const TmInstance *ti = konoko->anim ? tm_handle_instance(tm_handle_of(konoko->anim)) : NULL;
            snprintf(m, sizeof m, "OpenOni: frame %u: %.1f %.1f %.1f hp %u facing %.2f (%.2f) type %u state %u varient %04x %s%s%u/%u ammo %d stowed %d%d particles %d glass %d clips %u pu %d talk %s cons %d pushes %u step %.24s",
                     (unsigned)plat_frames(), konoko->pos[0], konoko->pos[1], konoko->pos[2], konoko->hit_points, konoko->facing, konoko->desired_facing, konoko->type,
                     konoko->state, konoko->varient, ti && ti->name ? ti->name : "", " overlay ",
                     konoko->overlay[0].anim ? tr_type(konoko->overlay[0].anim) : 0u,
                     konoko->overlay[1].anim ? tr_type(konoko->overlay[1].anim) : 0u, konoko->weapon ? konoko->weapon->ammo : -1,
                     konoko->stowed[0] != NULL, konoko->stowed[1] != NULL, p3_live_count(), env_glass_broken(), konoko->ammo, powerup_count(),
                     konoko->talk_partner ? ((const Chr *)konoko->talk_partner)->name : "-", (int)konoko->console_marker, (unsigned)konoko->pushes, route.step ? route.step : "-");
            plat_log(m);
            for (int i = 0; i < door_count(); i++) {
                float d[3];
                door_position(i, d);
                if (fabsf(d[0] - konoko->pos[0]) + fabsf(d[2] - konoko->pos[2]) > 40.0f) continue;
                snprintf(m, sizeof m, "OpenOni:   door %u: %.1f %.1f %.1f flags %04x state %d frame %d", door_id(i), d[0], d[1], d[2],
                         door_flags(i), door_state(i), door_frame(i));
                plat_log(m);
            }
            for (int i = 0; i < weapon_slots(); i++) {
                const Weapon *w = weapon_get(i);
                if (!w || w->owner || fabsf(w->m[9] - konoko->pos[0]) + fabsf(w->m[11] - konoko->pos[2]) > 100.0f) continue;
                snprintf(m, sizeof m, "OpenOni:   weapon %d: %.1f %.1f %.1f facing %.2f fade %u/%u", i, w->m[9], w->m[10], w->m[11],
                         w->facing, w->fade_delay, w->fade);
                plat_log(m);
            }
            for (int i = 0; i < chars_slots(); i++) {
                const Chr *c = chars_get(i);
                if (!c || c->player) continue;
                const KnowEntry *e = c->ai.know;
                const TmInstance *ci = c->c.anim ? tm_handle_instance(tm_handle_of(c->c.anim)) : NULL;
                snprintf(m, sizeof m, "OpenOni:   %s: %.1f %.1f %.1f facing %.2f state %d alert %d hp %u know %d/%d anim %u/%u %s frame %u flags %x interp %u/%u q %u",
                         c->name, c->c.pos[0], c->c.pos[1], c->c.pos[2], c->c.facing, c->ai.state, c->ai.alert, c->c.hit_points,
                         e ? e->strength : 0, e ? e->type : -1, c->c.type, c->c.state, ci && ci->name ? ci->name : "",
                         (unsigned)c->c.frame, (unsigned)c->c.flags, c->c.interpolating ? c->c.interp_tick : 0u,
                         c->c.interpolating ? c->c.interp_length : 0u, (unsigned)c->c.queued);
                plat_log(m);
            }
        }
        char splash[32];
        bool splashing = ui_splash_take(splash);    /* ONiRunGame: G+0x118, the splash in the display's place */
        if (splashing) splash_screen(splash, NULL, false);
        if (!splashing) {
            camera_update(0, 0, NULL, ticks);   /* the frame's part */
            particles_update(ticks, false);    /* the decorative particles (iComputeDeltaTicks 0x4fafb0) */
            display.ct = &ct, display.player = player, display.ticks = ticks;
            game_display();
        }
        /* ONiRunGame 0x4d4640: won, the next level; lost, this one again, from the save point (its
           scripts restore it). Level 19 won is the game's end: the game won noted (0x50f620) and the
           game quits (0x630ffd), the outro movie played first (0x4d56f0; under 3Dfx, 0x4d5580 from
           ONiMain's exit 0x4d3180 instead). */
        if (ui_level_end() == 1) {
            *next = level == 19 ? 0 : next_level(level);
            save_clear();       /* 0x4fda80 */
            splash_screen(exe_str(0x54d0cc), exe_str(0x54c904), true);    /* the main menu's music */
            if (level == 19) {
                persist_set_won_game();
                plat_log("OpenOni: level 19 ends, the game won");
                movie_play_in_game("outro.bik");
            }
            return 0;
        }
        if (ui_level_end() == 2) {
            splash_screen(exe_str(0x54d0e0), exe_str(0x54d0f4), true);
            *next = level;
            return 0;
        }
        plat_wait_frame();
    }
}

/* ONiRunGame 0x4d4640's levels, from the one given: each level's end loads the next (or plays it again);
   0 when the game ends. */
static int run_game(int level)
{
    int next = 0, r;
    char m[96];
    while ((r = play(level, &next)) == 0 && next) {
        tm_level_unload(level);
        snprintf(m, sizeof m, "OpenOni: level %d ends, level %d next", level, next);
        plat_log(m);
        level = next;
    }
    return r;
}

/* Out of a game, the WM's frames over black. */
static void blank_display(void)
{
    if (!plat_frame_shown()) return;
    int w, h, dw, dh;
    plat_display_size(&w, &h);
    gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
    ui_draw_wm_begin(w, h, &dw, &dh);
}

/* ONiMain 0x4d3280 after the start-up: the intro movie (0x4d33a4), the Oni window (WMrInitialize, the
   desktop), the main menu (OWrOniWindow_Startup 0x4bebd0), then unless the player quit, the game from
   level 1. (The splash screen before the menu, 0x4cae10, is an empty function in v1.0.) */
static void front_end(void)
{
    int w, h;
    plat_display_size(&w, &h);
    if (!gl_dev_init()) return;
    movie_play("intro.bik");
    wm_initialize(h ? w * 480 / h : 640, 480);
    wm_set_draw_callback(blank_display);
    wm_set_frame_hook(modal_frame);
    plat_log("OpenOni: main menu");
    uint32_t start = 0;
    int choice = outgame_main_menu(false, &start);
    if (choice == OUTGAME_NEW_GAME) run_game(1);
    else if (choice == OUTGAME_LOAD) run_game(game_start(start));
}

int app_main(void)
{
    char err[256], m[300];
    if (!exe_init(err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: %s", err);
        plat_log(m);
        return 1;
    }
    plat_log("OpenOni: Oni.exe v1.0 found");
    persist_initialize(plat_load_user_file, plat_save_user_file);
    li_config_load(plat_load_user_file, plat_save_user_file, vfs_read_all);   /* key_config.txt (0x4d4a50, from ONiMain 0x4d34a5) */
    if (!im_initialize(err, sizeof err) || !tm_initialize(err, sizeof err) || !tm_level_load(0, err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: %s", err);
        plat_log(m);
        return 1;
    }
    TmFile *zero = level_zero = tm_file_at(tm_file_slots() - 1);
    TmLoadStats st = tm_load_stats();
    snprintf(m, sizeof m, "OpenOni: level 0 loaded: %u instances, %u links", (unsigned)tm_file_instance_count(zero),
             (unsigned)st.links);
    plat_log(m);
    prepare_collections(zero);
    sound_initialize();
    sound_level_load(0);
    impact_types_load();        /* MArImpacts_PreProcess, MArMaterials_PreProcess */
    impact_effects_load();      /* the ONIE block */
    char viewer[32], lv[16];
    if (plat_param("keys", modal_keys, sizeof modal_keys)) modal_key = modal_keys;
    char movie[64];
    if (plat_param("movie", movie, sizeof movie) && gl_dev_init()) movie_play_in_game(movie);     /* a check: the outro's player */
    if (plat_param("play", viewer, sizeof viewer)) {
        int r = run_game(plat_param("level", lv, sizeof lv) ? atoi(lv) : 1);
        trac_forget();
        sound_terminate();
        tm_terminate();
        return r;
    }
    if (plat_param("viewer", viewer, sizeof viewer) && !strcmp(viewer, "level")) {
        int r = level_viewer(plat_param("level", lv, sizeof lv) ? atoi(lv) : 1);
        sound_terminate();
        tm_terminate();
        return r;
    }
    if (plat_param("test", viewer, sizeof viewer)) test_screen();
    else front_end();
    trac_forget();
    sound_terminate();
    tm_terminate();
    plat_log("OpenOni: quit");
    return 0;
}

void app_exit(void) {}

void app_audio(float *stereo, unsigned frames) { sound_mix(stereo, frames); }
