/* The laser triggers: see laser.h. */
#include "laser.h"

#include "akev_collide.h"
#include "chars.h"
#include "event.h"
#include "oban.h"
#include "objc.h"
#include "particle.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The trigger's data in the OBJC chunk (OBJiTrigger_Read 0x4cbf30). */
enum {
    OSD_CLASS = 0x00,       /* 63: the TRIG instance's name */
    OSD_ID = 0x3f,          /* u16 */
    OSD_FLAGS = 0x41,       /* u16: of it only 0x38 is kept */
    OSD_COLOUR = 0x43,      /* u32 ARGB: the beams' */
    OSD_START = 0x47,       /* float: where along the animation the emitters start, 0 to 1 */
    OSD_SPEED = 0x4b,       /* float: frames a tick, 0 to 100 */
    OSD_EMITTERS = 0x4f,    /* u16: 1 to 32 */
    OSD_TIME_ON = 0x51,     /* u16: ticks the beams are on, 0 always */
    OSD_TIME_OFF = 0x53,    /* u16: ticks they are off */
    OSD_EVENTS = 0x55,
};

/* The trigger object's flags (D+0x2e). */
enum {
    TRIG_TRIGGERED = 0x02, TRIG_ACTIVE = 0x04, TRIG_INITIAL = 0x08, TRIG_REVERSE = 0x10,
    TRIG_PINGPONG = 0x20, TRIG_HIDDEN = 0x40,
};

/* The TRIG class: the base geometry (+0x10), the emitter (+0x1c, a TRGE), its animation (+0x20, an OBAN),
   the active sound (+0x24, 32) and the hit sound (+0x44, 32). TRGE: the beam's start (+0x00) and
   direction (+0x0c) in the emitter's space, its geometry (+0x18). */
enum { TRIG_EMITTER = 0x1c, TRIG_ANIM = 0x20, TRIG_HIT_SOUND = 0x44 };
enum { TRGE_POS = 0x00, TRGE_DIR = 0x0c, TRGE_GEOMETRY = 0x18 };
enum { EMITTERS_MAX = 32 };

/* An emitter (0x4c bytes from D+0x9c): its frame, the animation's context, its matrix at the frame. */
typedef struct {
    float frame;            /* +0x00 */
    ObanPlayer anim;        /* +0x04: the frame at +0x0a, the step at +0x0c, the stop at +0x0e */
    float m[12];            /* +0x18 */
    bool beam;              /* this frame's beam (the port's, for the draw) */
    float from[3], to[3];
} Emitter;

typedef struct {
    uint16_t id, flags;     /* D+0x2c, D+0x2e */
    float pos[3], rm[9];    /* D+0x0c; D+0xc0: the rotation (0x437130 of the degrees) */
    const uint8_t *cls;     /* D+0x74 */
    const uint8_t *oban;    /* the class's animation */
    uint32_t colour;        /* D+0x78 */
    float start, speed, speed_now;      /* D+0x7c, D+0x80, D+0x84 */
    uint16_t emitters, time_off, time_on;   /* D+0x88, D+0x8a, D+0x8c */
    uint16_t countdown;     /* D+0xa0 */
    bool on;                /* D+0xa2 */
    Emitter em[EMITTERS_MAX];   /* D+0x9c */
    float *hits;            /* D+0x104: the environment's hit point by the animation's frame, 0 0 0 none yet */
    const uint8_t *events;  /* D+0x90 */
} Laser;

static Laser *lasers;
static int nlasers;

/* The Euler rotation (0x437130, order 0): R = Rz Ry Rx of the degrees, by columns (as console.c's). */
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

/* a * b, 3x4 by columns */
static void m34_mul(const float a[12], const float b[12], float out[12])
{
    float r[12];
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) {
            float v = a[k] * b[3 * c] + a[3 + k] * b[3 * c + 1] + a[6 + k] * b[3 * c + 2];
            r[3 * c + k] = c == 3 ? v + a[9 + k] : v;
        }
    memcpy(out, r, sizeof r);
}

static void m34_point(const float m[12], const float p[3], float out[3])
{
    for (int k = 0; k < 3; k++) out[k] = m[k] * p[0] + m[3 + k] * p[1] + m[6 + k] * p[2] + m[9 + k];
}

static uint16_t frames_of(const Laser *l) { return l->oban ? rd16(l->oban + OBAN_FRAMES) : 0; }

/* MSVC's __ftol (0x4264e0): toward zero */
static int ftol(float f) { return (int)f; }

/* 0x4cafc0: each emitter a step along the animation (by its direction and the speed), back to the start at
   the end (or, ping-ponging, turned round at either end), then its matrix at the frame. */
static void advance(Laser *l)
{
    uint16_t frames = frames_of(l);
    if (!l->oban) return;
    for (int k = 0; k < l->emitters; k++) {
        Emitter *e = &l->em[k];
        float old = e->frame;
        e->anim.last = ui_time();
        e->frame = (float)e->anim.step * l->speed_now + e->frame;
        if ((float)frames <= e->frame || (e->anim.stop != 0 && (float)e->anim.stop <= old)) {
            if (!(l->flags & TRIG_PINGPONG)) e->frame = 0.0f;
            else {
                e->frame = (float)frames - 1.0f;
                e->anim.step = -1;
            }
        }
        if (e->frame < 0.0f) {
            if (!(l->flags & TRIG_PINGPONG)) e->frame = (float)frames - 1.0f;
            else {
                e->frame = 0.0f;
                e->anim.step = 1;
            }
        }
        e->anim.frame = (int16_t)ftol(e->frame);
        if (!oban_matrix(l->oban, e->anim.frame, false, e->m)) {
            static const float id[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
            memcpy(e->m, id, sizeof id);
        }
    }
}

/* The type's update (0x4cb100): (the active sound, not ported,) the emitters while active, then the on and
   off times: on for time_on ticks, off for time_off, when either is set. */
static void update(Laser *l)
{
    if (l->oban && (l->flags & TRIG_ACTIVE)) advance(l);
    if (l->countdown) {
        l->countdown--;
        return;
    }
    if (l->time_off || l->time_on) {
        if (!l->on) {
            if (l->time_on) {
                l->on = true;
                l->countdown = l->time_on;
            }
        } else if (l->time_off) {
            l->on = false;
            l->countdown = l->time_off;
        }
    }
}

static void start_timer(Laser *l)
{
    l->on = true;
    l->countdown = l->time_on || l->time_off ? l->time_on : 0;
}

/* OBJrTrigger_Reset 0x4cae40: on, the emitters spread evenly along the animation from the start
   (frames / count apart, wrapped at the last frame; all at the last with a start of 1), the direction by
   the reverse flag; not triggered, active as the initial flag says; then the update, and while active
   another step. */
static void reset(Laser *l)
{
    start_timer(l);
    uint16_t frames = frames_of(l);
    if (l->oban) {
        float spacing = (float)frames / (float)l->emitters;
        for (int k = 0; k < l->emitters; k++) {
            Emitter *e = &l->em[k];
            oban_player_init(&e->anim, l->oban);
            e->anim.frame = 0;
            e->anim.stop = 0;
            e->anim.last = 0;
            oban_start(&e->anim, ui_time());     /* OBrAnim_Start 0x439580 (its random start, if any) */
            float last = (float)frames - 1.0f;
            e->frame = l->start == 1.0f ? (float)frames - 1.0f : fmodf((float)k * spacing + last * l->start, last);
            e->anim.step = (l->flags & TRIG_REVERSE) ? -1 : 1;
        }
    }
    l->flags &= (uint16_t)~TRIG_TRIGGERED;
    if (l->flags & TRIG_INITIAL) {
        l->flags |= TRIG_ACTIVE;
        start_timer(l);
    } else l->flags &= (uint16_t)~TRIG_ACTIVE;
    update(l);
    if (l->flags & TRIG_ACTIVE) advance(l);
}

static float clampf(float v, float lo, float hi) { return v >= lo ? (v < hi ? v : hi) : lo; }

static bool add(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < OSD_EVENTS + 2) return true;
    Laser *l = &lasers[nlasers];
    memset(l, 0, sizeof *l);
    char name[64];
    memcpy(name, o->data + OSD_CLASS, 63);
    name[63] = 0;
    /* (SetOSD takes the level's first TRIG when the name isn't found, "failed to find trigger class") */
    l->cls = name[0] ? tm_instance_get(TM_TAG("TRIG"), name) : NULL;
    if (!l->cls) return true;
    nlasers++;
    l->id = rd16(o->data + OSD_ID);
    l->flags = rd16(o->data + OSD_FLAGS) & 0x38;
    l->colour = rd32(o->data + OSD_COLOUR);
    l->start = clampf(rdf(o->data + OSD_START), 0.0f, 1.0f);
    l->speed = l->speed_now = clampf(rdf(o->data + OSD_SPEED), 0.0f, 100.0f);
    uint16_t n = rd16(o->data + OSD_EMITTERS);
    l->emitters = n == 0 ? 1 : n > EMITTERS_MAX ? EMITTERS_MAX : n;
    l->time_on = rd16(o->data + OSD_TIME_ON);
    l->time_off = rd16(o->data + OSD_TIME_OFF);
    l->events = event_list_size(o->data + OSD_EVENTS, o->size - OSD_EVENTS) ? o->data + OSD_EVENTS : NULL;
    memcpy(l->pos, o->pos, sizeof l->pos);
    rotation(o->rot, l->rm);
    l->oban = tm_link(l->cls + TRIG_ANIM);
    /* SetOSD's end: the reset, a step more, the hit cache cleared */
    reset(l);
    if (l->oban) advance(l);
    uint16_t frames = frames_of(l);
    l->hits = calloc(frames ? frames : 1, 12);
    return true;
}

static bool count(const ObjcObject *o, void *user) { (void)o; ++*(int *)user; return true; }

void laser_reset(void)
{
    for (int i = 0; i < nlasers; i++) free(lasers[i].hits);
    free(lasers);
    lasers = NULL;
    nlasers = 0;
}

int laser_load(void)
{
    laser_reset();
    int n = 0;
    objc_each("Trigger", count, &n);
    if (!n) return 0;
    lasers = calloc((size_t)n, sizeof *lasers);
    if (!lasers) return 0;
    objc_each("Trigger", add, NULL);
    return nlasers;
}

void laser_update(void)
{
    for (int i = 0; i < nlasers; i++) update(&lasers[i]);
}

static const void *env;
void laser_set_environment(const void *e) { env = e; }

/* OBJrTrigger_OnTrigger 0x4cb6f0, without the drawing: unless hidden, each emitter's beam (when active
   and on) from its start along its direction set to 500 long, to where it first meets the level (found
   once for each frame of the animation and kept: 0x104's cache); the player crossing it nearer sets the
   trigger off (the hit sound, the event list) unless it already is, and an emitter missing clears that,
   so with more than one emitter a beam held crossed can set it off again. The beam drawn stops 2 short of
   its end. */
void laser_frame(void)
{
    Chr *player = chars_player();
    for (int i = 0; i < nlasers; i++) {
        Laser *l = &lasers[i];
        for (int k = 0; k < l->emitters; k++) l->em[k].beam = false;
        if (l->flags & TRIG_HIDDEN) continue;
        const uint8_t *trge = tm_link(l->cls + TRIG_EMITTER);
        if (!trge) continue;
        float obj[12];
        memcpy(obj, l->rm, sizeof l->rm);
        memcpy(obj + 9, l->pos, sizeof l->pos);
        for (int k = 0; k < l->emitters; k++) {
            Emitter *e = &l->em[k];
            if (!(l->flags & TRIG_ACTIVE) || !l->on) continue;
            float m[12], p[3], d[3], ends[2][3];
            m34_mul(obj, e->m, m);
            for (int c = 0; c < 3; c++) p[c] = rdf(trge + TRGE_POS + 4 * c), d[c] = rdf(trge + TRGE_DIR + 4 * c);
            float inv = 1.0f / sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float q[3] = { p[0] + d[0] * inv, p[1] + d[1] * inv, p[2] + d[2] * inv };
            m34_point(m, p, ends[0]);
            m34_point(m, q, ends[1]);
            float *s = ends[0], dir[3] = { ends[1][0] - s[0], ends[1][1] - s[1], ends[1][2] - s[2] };
            float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (len > 0.0f) for (int c = 0; c < 3; c++) dir[c] *= 500.0f / len;    /* 0x433d60 */
            int f = ftol(e->frame);
            uint16_t frames = frames_of(l);
            float *hit = l->hits && f >= 0 && f < (frames ? frames : 1) ? l->hits + 3 * f : NULL;
            float end[3] = { 0, 0, 0 };
            if (hit) {
                if (hit[0] == 0.0f && hit[1] == 0.0f && hit[2] == 0.0f && env) {
                    AkevHit h[AKEV_HITS_MAX];
                    if (akev_collide_point(env, s, dir, 0x44800, 1, h)) memcpy(hit, h[0].point, 12);
                }
                memcpy(end, hit, 12);   /* none found: the origin, as the original's cleared cache */
            }
            float to[3] = { end[0] - s[0], end[1] - s[1], end[2] - s[2] }, at[3];
            bool crossed = false;
            float stop[3];
            memcpy(stop, end, 12);
            if (player && p3_character_ray(&player->c, s, to, at)) {
                float da = (s[0] - at[0]) * (s[0] - at[0]) + (s[1] - at[1]) * (s[1] - at[1]) + (s[2] - at[2]) * (s[2] - at[2]);
                float de = (s[0] - end[0]) * (s[0] - end[0]) + (s[1] - end[1]) * (s[1] - end[1]) + (s[2] - end[2]) * (s[2] - end[2]);
                if (da <= de) {
                    crossed = true;
                    memcpy(stop, at, 12);
                }
            }
            if (crossed) {
                if (!(l->flags & TRIG_TRIGGERED)) {
                    l->flags |= TRIG_TRIGGERED;     /* "Trigger Triggered (%d)" */
                    char snd[33];
                    memcpy(snd, l->cls + TRIG_HIT_SOUND, 32);
                    snd[32] = 0;
                    if (snd[0] && sound_has_impulse(snd)) sound_impulse_play(snd, 1.0f);
                    event_list_run(l->events, player->name);
                }
            } else l->flags &= (uint16_t)~TRIG_TRIGGERED;
            float bl = sqrtf((stop[0] - s[0]) * (stop[0] - s[0]) + (stop[1] - s[1]) * (stop[1] - s[1]) + (stop[2] - s[2]) * (stop[2] - s[2]));
            float t = 1.0f;
            if (bl > 0.0009765625f) t = 1.0f - clampf(2.0f / bl, 0.0f, 1.0f);
            e->beam = true;
            memcpy(e->from, s, 12);
            for (int c = 0; c < 3; c++) e->to[c] = (stop[c] - s[c]) * t + s[c];
        }
    }
}

int laser_count(void) { return nlasers; }
int laser_emitters(int i) { return i >= 0 && i < nlasers ? lasers[i].emitters : 0; }

const void *laser_emitter(int i, int k, float m[12])
{
    if (i < 0 || i >= nlasers || k < 0 || k >= lasers[i].emitters) return NULL;
    const Laser *l = &lasers[i];
    if (l->flags & TRIG_HIDDEN) return NULL;
    const uint8_t *trge = tm_link(l->cls + TRIG_EMITTER);
    if (!trge) return NULL;
    float obj[12];
    memcpy(obj, l->rm, sizeof l->rm);
    memcpy(obj + 9, l->pos, sizeof l->pos);
    m34_mul(obj, l->em[k].m, m);
    return tm_link(trge + TRGE_GEOMETRY);
}

bool laser_beam(int i, int k, LaserBeam *b)
{
    if (i < 0 || i >= nlasers || k < 0 || k >= lasers[i].emitters || !lasers[i].em[k].beam) return false;
    memcpy(b->from, lasers[i].em[k].from, 12);
    memcpy(b->to, lasers[i].em[k].to, 12);
    b->colour = lasers[i].colour;
    return true;
}

/* ---- the commands (0x4b61f0..0x4b6290, each through OBJrObjectType_EnumerateObjects): activate and
   deactivate take the id itself, the others also 0xffff (-1) for every trigger ---- */

void laser_activate(uint16_t id)
{
    /* 0x4cb230 */
    for (int i = 0; i < nlasers; i++)
        if (lasers[i].id == id) {
            lasers[i].flags |= TRIG_ACTIVE;
            start_timer(&lasers[i]);
        }
}

void laser_deactivate(uint16_t id)
{
    for (int i = 0; i < nlasers; i++)     /* 0x4cb2b0 */
        if (lasers[i].id == id) lasers[i].flags &= (uint16_t)~TRIG_ACTIVE;
}

static uint16_t arg_id(int argc, const SlValue *argv)
{
    if (argc < 1) return 0xffff;
    return (uint16_t)(argv[0].type == SL_FLOAT ? (int32_t)argv[0].f : argv[0].i);
}

static bool any(const Laser *l, uint16_t id) { return id == 0xffff || l->id == id; }

static bool cmd_activate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; laser_activate(arg_id(c, v)); return true; }
static bool cmd_deactivate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; laser_deactivate(arg_id(c, v)); return true; }

static bool cmd_reset(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    uint16_t id = arg_id(c, v);
    for (int i = 0; i < nlasers; i++)     /* 0x4cb300 */
        if (any(&lasers[i], id)) reset(&lasers[i]);
    return true;
}

static bool cmd_hide(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    uint16_t id = arg_id(c, v);
    for (int i = 0; i < nlasers; i++)     /* 0x4cb350 */
        if (any(&lasers[i], id)) lasers[i].flags |= TRIG_HIDDEN;
    return true;
}

static bool cmd_show(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    uint16_t id = arg_id(c, v);
    for (int i = 0; i < nlasers; i++)     /* 0x4cb3a0 */
        if (any(&lasers[i], id)) lasers[i].flags &= (uint16_t)~TRIG_HIDDEN;
    return true;
}

/* trig_speed id factor: the speed the factor times the object's own (0x4cb3f0) */
static bool cmd_speed(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    uint16_t id = arg_id(c, v);
    float f = c >= 2 ? (v[1].type == SL_FLOAT ? v[1].f : (float)v[1].i) : 0.0f;
    for (int i = 0; i < nlasers; i++)
        if (any(&lasers[i], id)) lasers[i].speed_now = f * lasers[i].speed;
    return true;
}

void laser_register_commands(void)
{
    sl_register("trig_activate", cmd_activate, NULL);
    sl_register("trig_deactivate", cmd_deactivate, NULL);
    sl_register("trig_reset", cmd_reset, NULL);
    sl_register("trig_hide", cmd_hide, NULL);
    sl_register("trig_show", cmd_show, NULL);
    sl_register("trig_speed", cmd_speed, NULL);
}
