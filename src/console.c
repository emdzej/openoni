/* Consoles: see console.h. */
#include "console.h"

#include "event.h"
#include "exe.h"
#include "objc.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The console's data in the OBJC chunk (OBJiConsole_Read 0x4c1710). */
enum {
    OSD_CLASS = 0x00,       /* 63: the CONS instance's name */
    OSD_ID = 0x3f,          /* u16 */
    OSD_FLAGS = 0x41,       /* u16 */
    OSD_INACTIVE = 0x43,    /* 63: the screen's texture (TXMP) while inactive */
    OSD_ACTIVE = 0x82,      /* 63: while active */
    OSD_USED = 0xc1,        /* 63: once used */
    OSD_EVENTS = 0x100,
};

/* The CONS class: the action point and its facing, in the console's space; the geometry (OFGA), the
   screen (M3GM). */
enum { CONS_POINT = 0x04, CONS_DIRECTION = 0x10, CONS_GEOMETRY = 0x1c, CONS_SCREEN = 0x20 };

/* The sounds OnActivate plays (their names are the exe's). */
enum { SOUND_LOCKED_VA = 0x54b5e4, SOUND_USED_VA = 0x54b5cc, SOUND_ACTIVATE_VA = 0x546c20 };

typedef struct {
    uint16_t id, flags;             /* D+0x2c, D+0x2e */
    float pos[3], rot[3];           /* D+0x0c, D+0x18 (degrees) */
    float rm[9];                    /* D+0x188: the rotation */
    const uint8_t *cls;             /* D+0x74 */
    const void *screen[3];          /* D+0x150 inactive, D+0x14c active, D+0x154 used (TXMP) */
    const uint8_t *events;          /* D+0x138 */
    int marker;                     /* D+0x148 */
} Console;

static Console *cons;
static int ncons;
static ActionMarker *markers;
static int nmarkers;

/* The Euler rotation (0x437130, order 0): R = Rz Ry Rx of the degrees, by columns. */
static void rotation(const float deg[3], float m[9])
{
    const float rad = 0.017453292f;
    float ci = cosf(deg[0] * rad), si = sinf(deg[0] * rad);
    float cj = cosf(deg[1] * rad), sj = sinf(deg[1] * rad);
    float ch = cosf(deg[2] * rad), sh = sinf(deg[2] * rad);
    const float r[9] = { cj * ch, cj * sh, -sj,
                         sj * si * ch - ci * sh, sj * si * sh + ci * ch, cj * si,
                         sj * ci * ch + si * sh, sj * ci * sh - si * ch, cj * ci };
    memcpy(m, r, sizeof r);
}

static const void *texture(const uint8_t *name63)
{
    char n[64];
    memcpy(n, name63, 63);
    n[63] = 0;
    return n[0] ? tm_instance_get(TM_TAG("TXMP"), n) : NULL;   /* 0x41f950; NULL: the geometry's own */
}

static bool add_console(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < OSD_EVENTS + 2) return true;
    Console *c = &cons[ncons++];
    memset(c, 0, sizeof *c);
    c->id = rd16(o->data + OSD_ID);
    c->flags = rd16(o->data + OSD_FLAGS);
    memcpy(c->pos, o->pos, sizeof c->pos);
    memcpy(c->rot, o->rot, sizeof c->rot);
    rotation(c->rot, c->rm);
    char name[64];
    memcpy(name, o->data + OSD_CLASS, 63);
    name[63] = 0;
    /* (SetOSD 0x4c1130 takes the level's first CONS when the name isn't found, "failed to find console
       class") */
    c->cls = name[0] ? tm_instance_get(TM_TAG("CONS"), name) : NULL;
    c->screen[0] = texture(o->data + OSD_INACTIVE);
    c->screen[1] = texture(o->data + OSD_ACTIVE);
    c->screen[2] = texture(o->data + OSD_USED);
    c->events = event_list_size(o->data + OSD_EVENTS, o->size - OSD_EVENTS) ? o->data + OSD_EVENTS : NULL;
    c->marker = -1;
    return true;
}

static bool count_console(const ObjcObject *o, void *user) { (void)o; ++*(int *)user; return true; }

/* OBJiConsole_LevelBegin 0x4c1c20: the marker at the class's action point, facing away from its
   direction (atan2 of the negated x and z, in [0, 2 pi)); not used; active as the OSD says. */
static void level_begin(int i)
{
    Console *c = &cons[i];
    if (!c->cls) return;    /* "issues with console class" */
    ActionMarker *m = &markers[nmarkers];
    c->marker = nmarkers++;
    m->console = i;
    float p[3], d[3];
    for (int k = 0; k < 3; k++) p[k] = rdf(c->cls + CONS_POINT + 4 * k), d[k] = rdf(c->cls + CONS_DIRECTION + 4 * k);
    for (int r = 0; r < 3; r++) {
        m->pos[r] = c->rm[r] * p[0] + c->rm[3 + r] * p[1] + c->rm[6 + r] * p[2] + c->pos[r];
    }
    float dx = c->rm[0] * d[0] + c->rm[3] * d[1] + c->rm[6] * d[2];
    float dz = c->rm[2] * d[0] + c->rm[5] * d[1] + c->rm[8] * d[2];
    float f = atan2f(-dx, -dz);
    m->facing = f < 0.0f ? f + 6.2831855f : f;
    m->floor = c->pos[1];
    c->flags &= (uint16_t)~CONSOLE_USED;
    if (c->flags & CONSOLE_ACTIVE_AT_START) c->flags |= CONSOLE_ACTIVE;
    else c->flags &= (uint16_t)~CONSOLE_ACTIVE;
}

int console_load(void)
{
    console_unload();
    int n = 0;
    objc_each("Console", count_console, &n);
    if (!n) return 0;
    cons = calloc((size_t)n, sizeof *cons);
    markers = calloc((size_t)n, sizeof *markers);
    if (!cons || !markers) {
        console_unload();
        return 0;
    }
    objc_each("Console", add_console, NULL);
    for (int i = 0; i < ncons; i++) level_begin(i);
    return ncons;
}

void console_unload(void)
{
    free(cons);
    free(markers);
    cons = NULL;
    markers = NULL;
    ncons = nmarkers = 0;
}

int console_count(void) { return ncons; }
uint16_t console_id(int i) { return i >= 0 && i < ncons ? cons[i].id : 0; }
uint16_t console_flags(int i) { return i >= 0 && i < ncons ? cons[i].flags : 0; }

void console_activate(uint16_t id)
{
    for (int i = 0; i < ncons; i++)     /* 0x4c09d0 */
        if (id == 0xffff || cons[i].id == id) cons[i].flags |= CONSOLE_ACTIVE;
}

void console_deactivate(uint16_t id)
{
    for (int i = 0; i < ncons; i++)     /* 0x4c0a20 */
        if (id == 0xffff || cons[i].id == id) cons[i].flags &= (uint16_t)~CONSOLE_ACTIVE;
}

void console_reset(uint16_t id)
{
    for (int i = 0; i < ncons; i++) {   /* 0x4c0a70 */
        Console *c = &cons[i];
        if (id != 0xffff && c->id != id) continue;
        c->flags &= (uint16_t)~CONSOLE_USED;
        if (c->flags & CONSOLE_ACTIVE_AT_START) c->flags |= CONSOLE_ACTIVE;
        else c->flags &= (uint16_t)~CONSOLE_ACTIVE;
    }
}

int console_marker_count(void) { return nmarkers; }

int console_marker_nearest(const float p[3])
{
    int best = -1;
    float bd = 3.4028235e38f;
    for (int i = 0; i < nmarkers; i++) {
        float dx = markers[i].pos[0] - p[0], dy = markers[i].pos[1] - p[1], dz = markers[i].pos[2] - p[2];
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bd) bd = d, best = i;
    }
    return best;
}

const ActionMarker *console_marker(int i) { return i >= 0 && i < nmarkers ? &markers[i] : NULL; }

/* SSrImpulse_Play_Simple 0x480a10 (the sound system's, by name) at the middle of the console's marker
   and the character's location (0x4c0880). */
static void play(uint32_t va, const Console *c, const float who[3])
{
    const ActionMarker *m = console_marker(c->marker);
    float at[3];
    for (int k = 0; k < 3; k++) at[k] = ((m ? m->pos[k] : 0.0f) + (who ? who[k] : 0.0f)) * 0.5f;
    sound_impulse_play_ss_at(exe_str(va), at);
}

void console_on_activate(int i, const char *who, const float who_location[3])
{
    if (i < 0 || i >= ncons) return;
    Console *c = &cons[i];
    if (!(c->flags & CONSOLE_ACTIVE)) {
        play(SOUND_LOCKED_VA, c, who_location);
        return;
    }
    if (c->flags & CONSOLE_USED) {
        play(SOUND_USED_VA, c, who_location);
        return;
    }
    play(SOUND_ACTIVATE_VA, c, who_location);
    c->flags |= CONSOLE_USED;
    event_list_run(c->events, who);
}

const void *console_screen(int i, const void **txmp, float m[12])
{
    if (i < 0 || i >= ncons) return NULL;
    const Console *c = &cons[i];
    if (!c->cls || !tm_link(c->cls + CONS_GEOMETRY)) return NULL;
    const void *g = tm_link(c->cls + CONS_SCREEN);
    if (!g) return NULL;
    *txmp = !(c->flags & CONSOLE_ACTIVE) ? c->screen[0] : !(c->flags & CONSOLE_USED) ? c->screen[1] : c->screen[2];
    memcpy(m, c->rm, sizeof c->rm);
    memcpy(m + 9, c->pos, sizeof c->pos);
    return g;
}

/* ---- console_activate id, console_deactivate [id], console_reset [id] (0x4b62b0, 0x4b62d0, 0x4b6300:
   without an id every console) ---- */

static uint16_t arg_id(int argc, const SlValue *argv)
{
    if (argc < 1) return 0xffff;
    return (uint16_t)(argv[0].type == SL_FLOAT ? (int32_t)argv[0].f : argv[0].i);
}

static bool cmd_activate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    console_activate(c >= 1 ? arg_id(c, v) : 0);
    return true;
}
static bool cmd_deactivate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; console_deactivate(arg_id(c, v)); return true; }
static bool cmd_reset(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; console_reset(arg_id(c, v)); return true; }

void console_register_commands(void)
{
    sl_register("console_activate", cmd_activate, NULL);
    sl_register("console_deactivate", cmd_deactivate, NULL);
    sl_register("console_reset", cmd_reset, NULL);
}
