/* The environment particles: see envparticle.h. */
#include "envparticle.h"

#include "akev_collide.h"
#include "objc.h"
#include "particle.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"
#include "ui.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* An ENVP record (0xd0 bytes from +0x18, the count at +0x16): the class's name (+0x00, 64), the tag
   (+0x40, 48), the matrix (+0x70, 3x3 and the position at +0x94), the decal's size (+0xa0, +0xa4) and
   the flags (+0xa8: 2 not made at the start). The rest is the game's at run time: here in EnvParticle. */
enum { EP_SIZE = 0xd0, EP_CLASS = 0x00, EP_TAG = 0x40, EP_MATRIX = 0x70, EP_POS = 0x94, EP_DECAL_SIZE = 0xa0, EP_FLAGS = 0xa8 };
enum { EPF_TAGGED = 1, EPF_NOT_INITIAL = 2, EPF_LISTED = 4, EPF_RUNNING = 8 };

typedef struct {
    const uint8_t *rec;
    uint16_t flags;         /* +0xa8 */
    P3Class *cls;           /* +0xb4 */
    uint32_t handle;        /* +0xb8 and +0xbc: the particle while its handle is the same */
} EnvParticle;

static EnvParticle *eps;
static int neps;
/* The records of the level's particle objects (OBJC "Particle": D+0x2c), made from their data. */
static uint8_t *obj_recs;
/* The tags (0x6a8b60's buckets): for each record, the next of the same tag in the order the original's
   chains walk them (the first of a tag, then the others newest first). */
static int *tag_next;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static const char *tag_of(const EnvParticle *e) { return (const char *)e->rec + EP_TAG; }

/* The live particle (the handle still its own), NULL none. */
static P3Particle *live(EnvParticle *e, P3Class **c)
{
    P3Particle *p = e->handle ? p3_from_handle(e->handle, c) : NULL;
    if (!p) e->handle = 0;
    return p;
}

/* The stop (0x453fe0's): not running; the live particle killed. */
static bool stop(EnvParticle *e)
{
    e->flags &= (uint16_t)~EPF_RUNNING;
    P3Class *c;
    P3Particle *p = live(e, &c);
    if (!p) return false;
    p3_kill(c, p);
    e->handle = 0;
    return true;
}

/* 0x453b60: a decal class's particle on the wall behind the record: the first quad (not of the flags
   0x44800) a segment from its place 10 along its matrix's -y meets (AKrCollision_Point 0x441c80), as an
   effect (P3rCreateEffect 0x456030) along the hit's normal, at the hit's point on that quad, its up the
   matrix's z, at the record's decal size, a static decal not turned at random. None without a wall. */
static void start_decal(EnvParticle *e, uint32_t time)
{
    const void *env = p3_environment();
    if (!env) return;
    float pos[3], v[3];
    for (int i = 0; i < 3; i++) pos[i] = rdf(e->rec + EP_POS + 4 * i), v[i] = rdf(e->rec + EP_MATRIX + 12 + 4 * i) * -10.0f;
    static AkevHit hits[AKEV_HITS_MAX];
    if (!akev_collide_point(env, pos, v, 0x44800, 1, hits)) return;
    const AkevHit hit = hits[0];
    P3EffectSpec spec = { .cls = e->cls, .orientation = P3_ORIENT_NORMAL, .location = P3_LOC_DECAL,
                          .decal_static = true, .decal_random_rotation = false };
    P3EffectLoc loc;
    memset(&loc, 0, sizeof loc);
    loc.tick = time;
    memcpy(loc.pos, hit.point, 12);
    memcpy(loc.normal, hit.plane, 12);
    for (int i = 0; i < 3; i++) loc.up[i] = rdf(e->rec + EP_MATRIX + 24 + 4 * i);
    loc.decal_scale[0] = rdf(e->rec + EP_DECAL_SIZE), loc.decal_scale[1] = rdf(e->rec + EP_DECAL_SIZE + 4);
    loc.kind = P3_HIT_WALL;
    loc.quad = hit.quad;
    P3Particle *p = p3_create_effect(&spec, &loc);
    e->handle = p ? p3_handle(p) : 0;
}

/* 0x453d50's particle: made at the record's matrix (0x453820 with initial set) and sent its create
   event; a decal class's goes on the wall (0x453b60). */
static void start(EnvParticle *e, uint32_t time)
{
    e->flags |= EPF_RUNNING;
    if (!e->cls) return;
    if (p3_class_def_flags(e->cls, 1) & 0x800000) {
        start_decal(e, time);
        return;
    }
    P3Particle *p = p3_create(e->cls, time);
    if (!p) { e->handle = 0; return; }
    e->handle = p3_handle(p);
    float *pos = p3_position(e->cls, p);
    if (pos) for (int i = 0; i < 3; i++) pos[i] = rdf(e->rec + EP_POS + 4 * i);
    float *v;
    uint8_t *b;
    if ((v = p3_block(e->cls, p, P3_X_ORIENTATION))) for (int i = 0; i < 9; i++) v[i] = rdf(e->rec + EP_MATRIX + 4 * i);
    if ((v = p3_block(e->cls, p, P3_X_VELOCITY))) v[0] = v[1] = v[2] = 0.0f;
    if ((v = p3_block(e->cls, p, P3_X_OFFSET))) v[0] = v[1] = v[2] = 0.0f;
    if ((b = p3_block(e->cls, p, P3_X_ATTACH))) memset(b, 0, 4);   /* the level's: no owner (+0xb0) */
    if ((b = p3_block(e->cls, p, P3_X_OWNER))) memset(b, 0, 4);
    if ((b = p3_block(e->cls, p, P3_X_TEX_INDEX))) { uint32_t r = trac_local_random(); memcpy(b, &r, 4); }
    if ((b = p3_block(e->cls, p, P3_X_TEX_TIME))) memcpy(b, &time, 4);
    if ((b = p3_block(e->cls, p, P3_X_PREV_POS))) memset(b, 0, 4);
    if ((b = p3_block(e->cls, p, P3_X_LENSFLARE))) memset(b, 0, 4);
    if ((b = p3_block(e->cls, p, P3_X_CACHE))) memset(b, 0, 20);
    if (p3_send_event(e->cls, p, P3_EV_CREATE, (float)time * (1.0f / 60.0f))) e->handle = 0;
}

void envparticle_reset(void)
{
    free(eps);
    free(tag_next);
    free(obj_recs);
    eps = NULL;
    tag_next = NULL;
    obj_recs = NULL;
    neps = 0;
}

static const uint8_t *level_envp(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONLV") && in->data) return tm_link(in->data + 0x70);
    }
    return NULL;
}

static int first_of_tag(const char *tag);

/* The particle object's data (OBJC version 39, OBJiParticle_Read 0x4c87e0 from version 0x20): the class
   (64 bytes), the tag (48, +0x40), the flags (u16 +0x70, only 2 kept), the decal's size (+0x72, +0x76). */
enum { OP_CLASS = 0x00, OP_TAG = 0x40, OP_FLAGS = 0x70, OP_DECAL_W = 0x72, OP_DECAL_H = 0x76, OP_SIZE = 0x7a };

static bool count_object(const ObjcObject *o, void *user)
{
    if (o->size >= OP_SIZE) ++*(int *)user;
    return true;
}

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

static void wrf(uint8_t *p, float f) { memcpy(p, &f, 4); }

/* OBJiParticle_Read 0x4c87e0: the object's record (D+0x2c) from its data, at the object's matrix (its
   rotation through 0x437130) and position. */
static bool add_object(const ObjcObject *o, void *user)
{
    int *k = user;
    if (o->size < OP_SIZE) return true;
    uint8_t *r = obj_recs + (size_t)EP_SIZE * (size_t)(*k)++;
    memcpy(r + EP_CLASS, o->data + OP_CLASS, 64);
    r[EP_CLASS + 63] = 0;
    memcpy(r + EP_TAG, o->data + OP_TAG, 48);
    r[EP_TAG + 47] = 0;
    float m[9];
    rotation(o->rot, m);
    for (int i = 0; i < 9; i++) wrf(r + EP_MATRIX + 4 * i, m[i]);
    for (int i = 0; i < 3; i++) wrf(r + EP_POS + 4 * i, o->pos[i]);
    memcpy(r + 0xa0, o->data + OP_DECAL_W, 4);
    memcpy(r + 0xa4, o->data + OP_DECAL_H, 4);
    uint16_t fl = (uint16_t)((o->data[OP_FLAGS] | o->data[OP_FLAGS + 1] << 8) & EPF_NOT_INITIAL);
    r[EP_FLAGS] = (uint8_t)fl, r[EP_FLAGS + 1] = (uint8_t)(fl >> 8);
    return true;
}

void envparticle_load(void)
{
    envparticle_reset();
    const uint8_t *a = level_envp();
    int nenvp = a ? rd16(a + 0x16) : 0, nobj = 0;
    /* the level's particle objects too (OBJiParticle_New 0x4c8760 lists each with EPrNew 0x454a60):
       after the ENVP's (inferred) */
    objc_each("Particle", count_object, &nobj);
    if (nobj) {
        obj_recs = calloc((size_t)nobj, EP_SIZE);
        int k = 0;
        if (obj_recs) objc_each("Particle", add_object, &k);
        else nobj = 0;
    }
    int n = nenvp + nobj;
    if (!n) return;
    eps = calloc((size_t)n, sizeof *eps);
    tag_next = malloc(sizeof *tag_next * (size_t)n);
    if (!eps || !tag_next) { envparticle_reset(); return; }
    for (int i = 0; i < n; i++) {
        EnvParticle *e = &eps[neps];
        e->rec = i < nenvp ? a + 0x18 + (size_t)EP_SIZE * (size_t)i : obj_recs + (size_t)EP_SIZE * (size_t)(i - nenvp);
        e->flags = (uint16_t)(rd16(e->rec + EP_FLAGS) | EPF_LISTED);
        tag_next[neps] = -1;
        if (tag_of(e)[0]) {
            /* a new one goes second in its tag's chain (0x454f90) */
            int f = first_of_tag(tag_of(e));
            if (f >= 0) { tag_next[neps] = tag_next[f]; tag_next[f] = neps; }
            e->flags |= EPF_TAGGED;
        }
        neps++;
        /* the level has begun (0x5e95f5, set by 0x455500 before): made now */
        char name[65];
        memcpy(name, e->rec + EP_CLASS, 64);
        name[64] = 0;
        e->cls = p3_class(name);
        if (!(e->flags & EPF_NOT_INITIAL) && e->cls) start(e, 0);
    }
}

static int first_of_tag(const char *tag)
{
    for (int i = 0; i < neps; i++)
        if ((eps[i].flags & EPF_TAGGED) && !strcmp(tag_of(&eps[i]), tag)) return i;
    return -1;
}

/* The command's callback (0x50a9c0) on one particle. */
static void apply(EnvParticle *e, int cmd, int event, uint32_t time)
{
    switch (cmd) {
    case 0: start(e, time); break;      /* create: a new one, the old left to live on (0x453d50) */
    case 1:                             /* reset (0x454040) */
        stop(e);
        if (!(e->flags & EPF_NOT_INITIAL)) start(e, time);
        break;
    case 2: stop(e); break;             /* kill (0x453fe0) */
    case 3: {                           /* do <event> */
        P3Class *c;
        P3Particle *p = live(e, &c);
        if (p && p3_send_event(c, p, event, (float)time * (1.0f / 60.0f))) {
            p3_kill(c, p);
            e->handle = 0;
        }
        break;
    }
    }
}

/* The event names (0x52a088, 16 bytes each). */
static const char *const events[] = {
    "update", "pulse", "start", "stop", "bgfx_start", "bgfx_stop", "hit_wall", "hit_char", "lifetime",
    "explode", "brokenlink", "create", "die", "newattractor", "delay_start", "delay_stop",
};

static int event_index(const char *s)
{
    for (int i = 0; i < 16; i++)
        if (!strcmp(s, events[i])) return i;
    return -1;
}

/* particle <tag> <command> (ONiParticle3_Command 0x50a6f0): create, reset, kill, do <event> or an
   event's name, to every environment particle of the tag (0x454420). */
static bool cmd_particle(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[1].type != SL_STRING || v[0].type != SL_STRING || !v[0].s || !v[1].s) return true;
    const char *op = v[1].s;
    int cmd, event = 0;
    if (!strcmp(op, "create")) cmd = 0;
    else if (!strcmp(op, "reset")) cmd = 1;
    else if (!strcmp(op, "kill")) cmd = 2;
    else if (!strcmp(op, "do")) {
        if (c < 3 || v[2].type != SL_STRING || !v[2].s || (event = event_index(v[2].s)) < 0) return true;
        cmd = 3;
    } else {
        if ((event = event_index(op)) < 0) return true;     /* "unknown command" */
        cmd = 3;
    }
    if (!v[0].s[0]) return true;
    uint32_t time = ui_time();
    for (int i = first_of_tag(v[0].s); i >= 0; i = tag_next[i]) apply(&eps[i], cmd, event, time);
    return true;
}

void envparticle_register_commands(void)
{
    sl_register("particle", cmd_particle, NULL);
}
