/* The application (app.h). The start of ONiMain 0x4d3280 as far as the port has come (docs/re/port-plan.md):
   the exe check, the instance manager (TMrInitialize, from ONiInitializeAll) and level 0 (ONrLevel_LoadZero
   loads the shared data with TMrLevel_Load), then a test screen that runs its own loop the way the original's loops
   do: draw, plat_wait_frame, until Esc. The bars are the level files' sizes, read through the file layer. */
#include "app.h"
#include "akev.h"
#include "body.h"
#include "camera.h"
#include "cinematic.h"
#include "character.h"
#include "chars.h"
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
#include "laser.h"
#include "turret.h"
#include "persist.h"
#include "save.h"
#include "laser_draw.h"
#include "env_draw.h"
#include "exe.h"
#include "geom_draw.h"
#include "graph.h"
#include "gl_dev.h"
#include "im.h"
#include "localpath.h"
#include "objc.h"
#include "material.h"
#include "particle.h"
#include "particle_draw.h"
#include "powerup.h"
#include "object.h"
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
#include "outgame.h"
#include "wm.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DIK_ESCAPE = 0x01, DIK_TAB = 0x0f, DIK_W = 0x11, DIK_S = 0x1f, DIK_A = 0x1e, DIK_D = 0x20, DIK_Q = 0x10, DIK_E = 0x12,
    DIK_F = 0x21, DIK_C = 0x2e, DIK_R = 0x13, DIK_CAPITAL = 0x3a, DIK_LSHIFT = 0x2a, DIK_RSHIFT = 0x36, DIK_LCONTROL = 0x1d, DIK_RCONTROL = 0x9d, DIK_SPACE = 0x39, DIK_UP = 0xc8, DIK_DOWN = 0xd0, DIK_LEFT = 0xcb, DIK_RIGHT = 0xcd, DIK_F1 = 0x3b,
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
static void draw_objects(void)
{
    for (int i = 0; i < object_count(); i++) {
        if (!object_shown(i)) continue;
        float m[12];
        object_matrix(i, m);
        for (uint32_t k = 0; k < object_geometry_count(i); k++) geom_draw(object_geometry(i, k), m, NULL);
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
    /* the weapons (WPrDisplay 0x519710: those in the world; the hands' with their characters; the
       stowed ones not drawn) */
    for (int i = 0; i < weapon_slots(); i++) {
        const Weapon *w = weapon_get(i);
        if (w && (!w->owner || w->owner->weapon == w)) geom_draw(weapon_geometry(w), w->m, NULL);
    }
    /* the powerups (0x51c2b0: their geometry turned by their facing; the glow sprite with the sprites) */
    for (int i = 0; i < powerup_count(); i++) {
        const Powerup *p = powerup_get(i);
        float m[12];
        powerup_matrix(p, m);
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

static float fog_rgb[3], fog_start = 0.99f, fog_end = 1.0f, far_clip = 10000.0f;

/* The fog's smooth changes (gl_fog_start_changeto 0x40cb20, gl_fog_end_changeto 0x40cb90): to the value
   over the frames (none, or not above 0: at once), by a step a tick. */
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

/* 0x40cc00, from the draw's frame start (0x405898) with the ticks since the last: each change steps,
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
    sl_register("gl_fog_start_changeto", cmd_fog_start_to, NULL);
    sl_register("gl_fog_end_changeto", cmd_fog_end_to, NULL);
    memset(fog_change, 0, sizeof fog_change);
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
   b back, l/r step left/right, c crouch, j jump, p punch, k kick, w walk, a action, q swap, g fire).
   "R" as a mode runs from up to 1.3 rad off. "T<name>,N[,L]" aims at the character for N frames (where it
   will be in L frames) and taps fire every 20 frames; the shots leave at about 1.13 times the aim's angle
   from the facing (measured), which the aim allows for. "M<message>,F" waits for a script's message and
   shifts the later "@" frames by when it came against F (when the route was made). "P" presses the pause
   key (the pause screen). "C" waits for the end of a cutscene (and of a script's "input 0"). "Gfrom,to,N" shoots
   the unbroken glass targets of those script ids, aiming the weapon's barrel at each. "F<name>,N" fights
   the character hand to hand for up to N frames, until it is down; "A<N>" the nearest one in combat
   with her, while there is one. "N<name>,N" talks with a neutral (the action key near it) until the
   conversation is over. "Wx,y,z" runs to the point through the rooms, the way found from where she is (while there is none,
   tried again each second). Knocked down, any step first gets
   up (forward). ---- */

/* The level's environment, for the route's glass targets. */
static const void *route_env;

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

/* "Wx,y,z": the steps to the point through the rooms' graph, planned from where the player is (the
   doors locked now kept out, the manual ones opened with the action key on the way), into out. */
static bool route_plan(const Character *c, const float b[3], char *out, size_t size)
{
    const void *env = route_env;
    if (!env || !graph_for(env)) return false;
    /* the locked doors' links blocked while the path is found (the graph's own door links aren't kept) */
    static uint32_t saved[4096];
    int nl = graph_link_count() < 4096 ? graph_link_count() : 4096;
    for (int i = 0; i < nl; i++) saved[i] = graph_link(i)->flags;
    for (int d = 0; d < door_count(); d++) {
        if (!(door_flags(d) & DOOR_LOCKED)) continue;
        float dp[3];
        door_position(d, dp);
        for (int i = 0; i < nl; i++) {
            GraphLink *l = (GraphLink *)graph_link(i);
            if (fabsf(l->mid[0] - dp[0]) < 12 && fabsf(l->mid[2] - dp[2]) < 12 && fabsf(l->mid[1] - dp[1]) < 10) l->flags |= 1;
        }
    }
    float a[3] = { c->pos[0], c->pos[1], c->pos[2] };
    int ra = graph_room_at(a), rb = graph_room_at(b);
    static int path[GRAPH_PATH_MAX];
    int n = ra < 0 || rb < 0 ? 0 : graph_find_path(ra, a, rb, b, true, path);
    size_t len = 0;
    out[0] = 0;
#define EMIT(...) (len += (size_t)snprintf(out + len, len < size ? size - len : 0, __VA_ARGS__))
    float seg[3] = { a[0], a[1], a[2] };
    int room = ra;
    for (int k = 0; k < n; k++) {
        float in[3], o[3];
        const GraphLink *l = k + 1 < n ? graph_link(path[k]) : NULL;
        if (l) {
            float w[3] = { l->mid[0], l->mid[1], l->mid[2] }, pl[4];
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
            for (int j = 0; j < 3; j++) in[j] = o[j] = w[j];
            in[0] += pl[0] * 12 * sign, in[2] += pl[2] * 12 * sign;
            o[0] -= pl[0] * 8 * sign, o[2] -= pl[2] * 8 * sign;
        } else {
            for (int j = 0; j < 3; j++) o[j] = b[j];
        }
        static LocalPoint lp[LOCAL_PATH_MAX];
        int m = local_path(env, room, seg, o, NULL, false, true, lp);
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
    for (int i = 0; i < nl; i++) ((GraphLink *)graph_link(i))->flags = saved[i];
    if (!n) return false;
    if (len < size) len += (size_t)snprintf(out + len, size - len, "%.0f,%.0f,R,%s", b[0], b[2], "2");
    return len < size;
}

/* The input the route asks for this frame (in is all zero on entry). */
static uint32_t splash_frames;      /* the frames the splash screens and the dialogs took: the route's clock leaves them out */

static bool route_pause;     /* the route's "P": the pause key */

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
            for (const char *k = s + 1; *k && *k != ',' && *k != ';'; k++) {
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
            /* "A<N>": for up to N frames, fights (as "F") the nearest character within 80 that is in
               combat (alert 4) and has hit points; done when there is none */
            if (!route.phase) route.until = now + (uint32_t)atoi(s + 1), route.phase = 1;
            const Chr *t = NULL;
            float best = 80.0f * 80.0f;
            for (int i = 0; i < chars_slots(); i++) {
                const Chr *o = chars_get(i);
                if (!o || o->player || !chars_in_use(o) || !o->c.hit_points || o->ai.alert < 4) continue;
                float ox = o->c.pos[0] - c->pos[0], oy = o->c.pos[1] - c->pos[1], oz = o->c.pos[2] - c->pos[2];
                float d2 = ox * ox + oz * oz;
                if (fabsf(oy) < 20.0f && d2 < best) best = d2, t = o;
            }
            if (!t || now >= route.until) { route_next(); continue; }
            float dx = t->c.pos[0] - c->pos[0], dz = t->c.pos[2] - c->pos[2];
            float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            if (dx * dx + dz * dz > 81.0f) {
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 0.6f;
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
                    char m[96];
                    snprintf(m, sizeof m, "OpenOni: route: no way yet to %.40s", s);
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
        if (*s == 'N') {
            /* "N<name>,N": for up to N frames, a talk with the character (a neutral): walked up to
               within 20, turned to it, the action key tapped every 30 frames until the conversation
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
        if (*s == 'F') {
            /* "F<name>,N": a melee fight with the character for up to N frames, or until it is down
               to no hit points: turned to it, run at it past 9 away, a punch or kick tapped every
               8 frames within (three punches, then a kick) */
            char name[64];
            int n = 0;
            const char *comma = strchr(s, ',');
            size_t len = comma ? (size_t)(comma - s - 1) : 0;
            if (len >= sizeof name) len = sizeof name - 1;
            memcpy(name, s + 1, len), name[len] = 0;
            if (comma) n = atoi(comma + 1);
            const Chr *t = chars_find(name);
            if (!route.phase) route.until = now + (uint32_t)n, route.phase = 1;
            if (!t || !t->c.hit_points || now >= route.until) { route_next(); continue; }
            float dx = t->c.pos[0] - c->pos[0], dz = t->c.pos[2] - c->pos[2];
            float err = wrap_pi(atan2f(dx, dz) - c->desired_facing);
            in->turn = err > 0.1f ? 0.1f : err < -0.1f ? -0.1f : err;
            if (dx * dx + dz * dz > 81.0f) {
                in->forward = fabsf(wrap_pi(atan2f(dx, dz) - c->facing)) < 0.6f;
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
           70 degrees (HandleStandingTurn), and turns as it goes */
        float off = fabsf(wrap_pi(atan2f(dx, dz) - c->facing));
        if (off > (mode == 'R' ? 1.3f : 0.6f)) return;
        if (mode == 's' && route.phase < 3) {
            /* the double tap: down, up, down (within 15 ticks), then held */
            in->forward = route.phase != 1;
            route.phase++;
            return;
        }
        in->forward = 1;
        in->walk = mode == 'k';
        /* no headway for a second (something the rooms' grids don't hold, at the body's height): a
           second of stepping to the side, one way then the other */
        static float stuck_at[2];
        static int stuck, side;
        if (fabsf(c->pos[0] - stuck_at[0]) + fabsf(c->pos[2] - stuck_at[1]) > 2.0f) {
            stuck_at[0] = c->pos[0], stuck_at[1] = c->pos[2];
            if (stuck < 60) stuck = 0;
        }
        if (++stuck > 60) {
            in->strafe = side & 1 ? -1 : 1;
            if (stuck > 120) stuck = 0, side++;
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
    sound_update();     /* 0x483bd0 */
    plat_wait_frame();
    splash_frames++;
}

/* ONrGameState_SplashScreen 0x4f4d40, the original's blocking loop in the game loop's place for the
   frame: in from black over 120 ticks ((1 - t/120)^1.9; past 120 the power of a negative is no number and
   no blanket is drawn) within 600, then 600 more, either ended by a key or a button, then out to black
   over 120 ((t/120)^1.9). Its clock is the machine's sixtieths: here the frames. Not ported: the music's
   volume (up at the start, down over the fade) and the listener kept on the player. */
static void splash_screen(const char *name)
{
    bool skip = false;
    for (int t = 0; t < 600 && !skip; t++) {
        if (any_press()) skip = true;
        else splash_frame(name, powf(1.0f - (float)t * (1.0f / 120.0f), 1.9f));
    }
    for (int t = 0; t < 600 && !skip; t++) {
        if (any_press()) skip = true;
        else splash_frame(name, 0.0f);
    }
    for (int t = 0; t < 120; t++) splash_frame(name, powf((float)t * (1.0f / 120.0f), 1.9f));
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
    perspective(proj, 0.785398f, h ? (float)w / (float)h : 1.0f, 2.0f, far_clip);
    if (!camera_view_matrix(view) && display.ct) {
        float eye[3] = { display.ct->pos[0], display.ct->pos[1] + 20, display.ct->pos[2] };
        view_matrix(view, eye, 3.14159265f - display.ct->facing, -0.3f);
    }
    if (display.ticks) fog_step(display.ticks);
    gl_dev_begin(w, h, 0.0f, 0.0f, 0.0f);
    gl_dev_matrices(proj, view);
    env_draw();
    draw_objects();
    for (int i = 0; i < chars_slots(); i++)
        if (chars_get(i)) char_draw_character(&chars_get(i)->c);   /* the blended pose */
    particle_draw(view, p3_tick());     /* P3rDisplay */
    laser_draw(view);
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

static int play(int level, int *next)
{
    *next = 0;
    char err[256], m[300], at_dummy[8];
    trac_set_level((uint16_t)level);
    if (!tm_level_load(level, err, sizeof err)) {
        snprintf(m, sizeof m, "OpenOni: level %d: %s", level, err);
        plat_log(m);
        return 1;
    }
    prepare_collections(tm_file_at(tm_file_slots() - 1));
    sound_level_load(level);
    const void *env = level_environment(level);
    if (!env || !gl_dev_init() || !env_draw_load(env)) {
        plat_log("OpenOni: the level's environment doesn't load");
        return 1;
    }
    chars_load();
    if (plat_param("ailog", at_dummy, sizeof at_dummy)) combat_log = plat_log;
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
    material_level_load();                  /* ONrTextureMaterials_LevelLoad */
    material_mark_breakable((void *)env);   /* AKrLevel_Begin: the glass */
    p3_load();      /* the PAR3 classes of the loaded files (the binary data's loader), set up at the start */
    p3_set_environment(env);
    envparticle_load();     /* ONrLevel_Load: the level's environment particles, after the particle system starts */
    weapons_reset();
    powerups_reset();
    weapons_level_begin();      /* 0x51b0f0: the level's Weapon and PowerUp objects */
    powerups_level_begin();
    chars_arm();
    console_load();
    laser_set_environment(env);
    laser_load();
    turret_load();      /* the mechanics: the Turret objects (OBJrTurret_Initialize 0x4cdf70's type) */
    action_install();
    env_set((void *)env);
    route_env = env;
    /* the camera behind the player (CArInitialize's state, reset to follow) */
    CameraTarget ct = { .active = true };
    camera_init();
    camera_set_environment((void *)env);
    fill_target(&ct, &player->c);
    camera_set_target(&ct);
    camera_reset();
    uint32_t ticks = 0;     /* the game time (ONrGameState's +0x144), ticks since the level began */
    ui_reset();
    igui_level_load(level);     /* ONiInGameUI_LevelLoad 0x503eb0 */
    save_level_begin(level);
    weapon_new_hook = igui_new_weapon;
    powerup_new_hook = igui_new_item;
    save_restore_hook = restored;
    cinematic_reset();
    {
        int w, h, dw, dh;
        plat_display_size(&w, &h);
        dw = h ? w * 480 / h : 640, dh = 480;
        wm_initialize(dw, dh);
        wm_set_draw_callback(game_display);     /* ONrGameState_LevelBegin 0x4f5350 */
        wm_set_frame_hook(modal_frame);
    }
    display.ct = &ct, display.player = player, display.ticks = 0;
    start_scripts();
    memset(&route, 0, sizeof route);
    if (first && plat_param("route", route.buf, sizeof route.buf)) route.step = route.buf;
    first = false;
    plat_log("OpenOni: playing");
    for (;;) {
        uint8_t keys[KEY_COUNT] = { 0 }, presses[64];
        int n = plat_key_presses(presses, (int)sizeof presses);
        /* 0x4faec0: the player's escape (its input's bit 1, at most once in 15 ticks, not in a cutscene)
           opens the main menu over the level (0x4bec00); New Game starts again at level 1 (0x4beb40) */
        /* ... and its pause key (bit 4, F1 by default; not when dead) the pause screen, after the frame
           (ONiRunGame 0x4d4640, G+0x138) */
        for (int i = 0; i < n; i++)
            if (presses[i] == DIK_F1 && (ui_keys_allowed() & 4) && !ui_cutscene() && player && !(player->c.flags & 1)) igui_pause_screen(pause_key);
        for (int i = 0; i < n; i++)
            if (presses[i] == DIK_ESCAPE) {
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
        plat_keys(keys);
        PlatMouse mouse;
        CharacterInput in = {
            .forward = keys[DIK_W] - keys[DIK_S], .strafe = keys[DIK_D] - keys[DIK_A], .walk = keys[DIK_CAPITAL] != 0,
            .turn = (keys[DIK_LEFT] - keys[DIK_RIGHT]) * 0.05f, .pitch = (keys[DIK_UP] - keys[DIK_DOWN]) * 0.02f,
            .jump = keys[DIK_SPACE] != 0, .crouch = keys[DIK_LSHIFT] || keys[DIK_RSHIFT],
            .action = keys[DIK_LCONTROL] || keys[DIK_RCONTROL],
        };
        for (int i = 0; i < n; i++) {
            /* the exe's default bindings (its "bind f to punch" lines): F punch, C kick; mouse 1 and 2 fire */
            if (presses[i] == DIK_F) in.punch = true;
            if (presses[i] == DIK_C) in.kick = true;
            if (presses[i] == DIK_Q) in.swap = true;
            if (presses[i] == DIK_E) in.drop = true;
            if (presses[i] == DIK_R) in.reload = true;
            if (presses[i] == DIK_TAB) in.hypo = true;
        }
        if (plat_mouse(&mouse) && mouse.captured) {
            in.turn -= mouse.dx * 0.004f;
            /* LIrUpdate 0x403220: the mouse's y axis is its move by -0.125 with the persisted "invert mouse"
               (the default), else by 0.125 */
            in.pitch -= mouse.dy * (persist_invert_mouse() ? 0.004f : -0.004f);
            in.punch |= (mouse.pressed & MOUSE_LEFT) != 0;
            in.fire = (mouse.buttons & MOUSE_LEFT) != 0;     /* "bind mousebutton1 to fire1", held */
            in.kick |= (mouse.pressed & MOUSE_RIGHT) != 0;
        }
        if (plat_mouse(&mouse) && (mouse.pressed & MOUSE_LEFT) && !mouse.captured) plat_mouse_capture(true);
        /* "input 0" from a script (cutscenes): the player's keys do nothing (0x4bcc50's flag) */
        if (route.step && player) route_input(&player->c, &in);
        /* a debug aid: --param invincible=1, the player as with chr_invincible (the "liveforever" cheat's
           flag), for unattended runs of a level's scripts */
        char inv[8];
        if (player && plat_param("invincible", inv, sizeof inv) && atoi(inv)) player->c.flags |= 0x2000000u;
        chr_input_mask(&in, ui_keys_allowed());     /* lock_keys */
        if (route_pause && (ui_keys_allowed() & 4) && !ui_cutscene() && player && !(player->c.flags & 1)) {    /* held until allowed */
            route_pause = false;
            igui_pause_screen(pause_key);
        }
        if (!ui_input_enabled()) memset(&in, 0, sizeof in);
        sl_update();
        /* ONrGameState_UpdateCharacters 0x4dccf0: the volumes first, then the characters */
        trigger_update(chars_trigger_actors(), chars_slots());
        object_update(++ticks);
        door_physics(ticks);
        ui_update();
        igui_update(ui_time());
        cinematic_update();
        chars_update(&in, env);
        weapons_update(env);    /* WPrUpdate, after the characters */
        door_update();      /* the objects' update 0x507880, after the characters */
        laser_update();     /* the triggers' (0x4cb100), the same mechanics update */
        turret_update(env); /* OBJiTurretCombat_Update 0x4cf140, the mechanics' update too */
        particles_update(ticks + 1, true);     /* P3rUpdate(time + 1) after the physics */
        /* a script may delete the player: the camera keeps the last place */
        if ((player = chars_player())) fill_target(&ct, &player->c);
        camera_set_target(&ct);
        camera_update(1, 0, NULL, ticks);   /* the tick's part (iComputeDeltaTicks 0x4fafb0) */
        sound_update();
        char dbg[32];
        if (player && plat_param("trace", dbg, sizeof dbg) && plat_frames() % (uint32_t)(atoi(dbg) > 1 ? atoi(dbg) : 30) == 0) {
            const Character *konoko = &player->c;
            const TmInstance *ti = konoko->anim ? tm_handle_instance(tm_handle_of(konoko->anim)) : NULL;
            snprintf(m, sizeof m, "OpenOni: frame %u: %.1f %.1f %.1f hp %u facing %.2f (%.2f) type %u state %u varient %04x %s%s%u/%u ammo %d stowed %d%d particles %d glass %d clips %u pu %d talk %s cons %d step %.24s",
                     (unsigned)plat_frames(), konoko->pos[0], konoko->pos[1], konoko->pos[2], konoko->hit_points, konoko->facing, konoko->desired_facing, konoko->type,
                     konoko->state, konoko->varient, ti && ti->name ? ti->name : "", " overlay ",
                     konoko->overlay[0].anim ? tr_type(konoko->overlay[0].anim) : 0u,
                     konoko->overlay[1].anim ? tr_type(konoko->overlay[1].anim) : 0u, konoko->weapon ? konoko->weapon->ammo : -1,
                     konoko->stowed[0] != NULL, konoko->stowed[1] != NULL, p3_live_count(), env_glass_broken(), konoko->ammo, powerup_count(),
                     konoko->talk_partner ? ((const Chr *)konoko->talk_partner)->name : "-", (int)konoko->console_marker, route.step ? route.step : "-");
            plat_log(m);
            for (int i = 0; i < door_count(); i++) {
                float d[3];
                door_position(i, d);
                if (fabsf(d[0] - konoko->pos[0]) + fabsf(d[2] - konoko->pos[2]) > 40.0f) continue;
                snprintf(m, sizeof m, "OpenOni:   door %u: %.1f %.1f %.1f flags %04x state %d frame %d", door_id(i), d[0], d[1], d[2],
                         door_flags(i), door_state(i), door_frame(i));
                plat_log(m);
            }
            for (int i = 0; i < chars_slots(); i++) {
                const Chr *c = chars_get(i);
                if (!c || c->player) continue;
                const KnowEntry *e = c->ai.know;
                const TmInstance *ci = c->c.anim ? tm_handle_instance(tm_handle_of(c->c.anim)) : NULL;
                snprintf(m, sizeof m, "OpenOni:   %s: %.1f %.1f %.1f facing %.2f state %d alert %d hp %u know %d/%d anim %u/%u %s frame %u",
                         c->name, c->c.pos[0], c->c.pos[1], c->c.pos[2], c->c.facing, c->ai.state, c->ai.alert, c->c.hit_points,
                         e ? e->strength : 0, e ? e->type : -1, c->c.type, c->c.state, ci && ci->name ? ci->name : "",
                         (unsigned)c->c.frame);
                plat_log(m);
            }
        }
        char splash[32];
        bool splashing = ui_splash_take(splash);    /* ONiRunGame: G+0x118, the splash in the display's place */
        if (splashing) splash_screen(splash);
        if (!splashing) {
            camera_update(0, 0, NULL, ticks);   /* the frame's part */
            particles_update(ticks, false);    /* the decorative particles (iComputeDeltaTicks 0x4fafb0) */
            display.ct = &ct, display.player = player, display.ticks = ticks;
            game_display();
        }
        /* ONiRunGame 0x4d4640: won, the next level (after level 19, the outro: not yet); lost, this
           one again, from the save point (its scripts restore it) */
        if (ui_level_end() == 1) {
            *next = next_level(level);
            save_clear();       /* 0x4fda80 */
            return 0;
        }
        if (ui_level_end() == 2) {
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

/* ONiMain 0x4d3280 after the start-up: the Oni window (WMrInitialize, the desktop), the main menu
   (OWrOniWindow_Startup 0x4bebd0), then unless the player quit, the game from level 1. Not ported yet:
   the intro's movies and splash screens before the menu. */
static void front_end(void)
{
    int w, h;
    plat_display_size(&w, &h);
    if (!gl_dev_init()) return;
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
    char viewer[32], lv[16];
    if (plat_param("keys", modal_keys, sizeof modal_keys)) modal_key = modal_keys;
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
