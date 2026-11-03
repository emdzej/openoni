/* Trigger volumes: see trigger.h. */
#include "trigger.h"

#include "objc.h"
#include "sl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The TRGV data (the read method 0x4cced0, OBJC version 0x16 and later). */
enum {
    TRGV_NAME = 0x00, TRGV_ENTRY = 0x3f, TRGV_INSIDE = 0x5f, TRGV_EXIT = 0x7f, TRGV_TEAMS = 0x9f,
    TRGV_SCALE = 0xa3, TRGV_ID = 0xaf, TRGV_PARENT = 0xb3, TRGV_NOTE = 0xb7, TRGV_FLAGS = 0x137,
    TRGV_SIZE = 0x13b,
};

enum { SCRIPT_LEN = 32, QUEUE_MAX = 128 };

typedef struct {
    char name[64];
    /* the scripts and flags as stored, and as they are now (trigvolume_setscript, the one-time flags and
       trigvolume_enable change these; trigvolume_reset, 0x4cc760, copies the stored ones back) */
    char script0[3][SCRIPT_LEN], script[3][SCRIPT_LEN];
    uint32_t flags0, flags;
    uint32_t teams, id, parent;
    float origin[3], axis[3][3], scale[3];     /* the box: origin + axis * (0..scale) */
} Volume;

static Volume *vols;
static int nvols;
static TriggerActor *last_actors;               /* for trigvolume_count */
static int last_count;

/* The rotation of the box (0x4d0ad0): the object's angles in degrees times pi/180, through the Euler
   conversion 0x437130 with order 0 (static X, Y, Z: R = Rz Ry Rx). axis[k] is R's column k. */
static void rotation(const float deg[3], float axis[3][3])
{
    const float rad = 0.017453292f;
    float ci = cosf(deg[0] * rad), si = sinf(deg[0] * rad);
    float cj = cosf(deg[1] * rad), sj = sinf(deg[1] * rad);
    float ch = cosf(deg[2] * rad), sh = sinf(deg[2] * rad);
    float cc = ci * ch, cs = ci * sh, sc = si * ch, ss = si * sh;
    float m[3][3] = {   /* m[row][col] */
        { cj * ch, sj * sc - cs, sj * cc + ss },
        { cj * sh, sj * ss + cc, sj * cs - sc },
        { -sj, cj * si, cj * ci },
    };
    for (int k = 0; k < 3; k++)
        for (int r = 0; r < 3; r++) axis[k][r] = m[r][k];
}

static void copy_str(char *d, const uint8_t *s, size_t n)
{
    memcpy(d, s, n - 1);
    d[n - 1] = 0;
}

static bool add_volume(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < TRGV_SIZE) return true;
    Volume *v = &vols[nvols++];
    memset(v, 0, sizeof *v);
    copy_str(v->name, o->data + TRGV_NAME, 0x3f + 1 > sizeof v->name ? sizeof v->name : 0x3f + 1);
    for (int k = 0; k < 3; k++) {
        copy_str(v->script0[k], o->data + TRGV_ENTRY + SCRIPT_LEN * k, SCRIPT_LEN);
        memcpy(v->script[k], v->script0[k], SCRIPT_LEN);
        v->scale[k] = rdf(o->data + TRGV_SCALE + 4 * k);
        v->origin[k] = o->pos[k];
    }
    v->teams = rd32(o->data + TRGV_TEAMS);
    v->id = rd32(o->data + TRGV_ID);
    v->parent = rd32(o->data + TRGV_PARENT);
    v->flags0 = v->flags = rd32(o->data + TRGV_FLAGS);
    rotation(o->rot, v->axis);
    return true;
}

static bool count_volume(const ObjcObject *o, void *user) { (void)o; ++*(int *)user; return true; }

int trigger_load(void)
{
    trigger_unload();
    int n = 0;
    objc_each("Trigger Volume", count_volume, &n);
    vols = n ? calloc((size_t)n, sizeof *vols) : NULL;
    if (vols) objc_each("Trigger Volume", add_volume, NULL);
    return nvols;
}

void trigger_unload(void)
{
    free(vols);
    vols = NULL;
    nvols = 0;
    last_actors = NULL;
    last_count = 0;
}

int trigger_count(void) { return nvols; }
const char *trigger_name(int i) { return i >= 0 && i < nvols ? vols[i].name : ""; }
uint32_t trigger_id(int i) { return i >= 0 && i < nvols ? vols[i].id : 0; }
uint32_t trigger_flags(int i) { return i >= 0 && i < nvols ? vols[i].flags : 0; }

const char *trigger_script(int i, int kind) { return i >= 0 && i < nvols && kind >= 0 && kind < 3 ? vols[i].script[kind] : ""; }

void trigger_box(int i, float origin[3], float axis[3][3], float size[3])
{
    if (i < 0 || i >= nvols) return;
    memcpy(origin, vols[i].origin, sizeof vols[i].origin);
    memcpy(axis, vols[i].axis, sizeof vols[i].axis);
    memcpy(size, vols[i].scale, sizeof vols[i].scale);
}

void trigger_centre(int i, float c[3])
{
    if (i < 0 || i >= nvols) return;
    const Volume *v = &vols[i];
    for (int r = 0; r < 3; r++)
        c[r] = v->origin[r] + 0.5f * (v->axis[0][r] * v->scale[0] + v->axis[1][r] * v->scale[1] + v->axis[2][r] * v->scale[2]);
}

/* The segment a..b against the box, in the box's own space (the slabs). */
static bool segment_hits(const Volume *v, const float a[3], const float b[3])
{
    float t0 = 0.0f, t1 = 1.0f;
    for (int k = 0; k < 3; k++) {
        const float *ax = v->axis[k];
        float pa = (a[0] - v->origin[0]) * ax[0] + (a[1] - v->origin[1]) * ax[1] + (a[2] - v->origin[2]) * ax[2];
        float pb = (b[0] - v->origin[0]) * ax[0] + (b[1] - v->origin[1]) * ax[1] + (b[2] - v->origin[2]) * ax[2];
        float lo = fminf(0.0f, v->scale[k]), hi = fmaxf(0.0f, v->scale[k]), d = pb - pa;
        if (d == 0.0f) {
            if (pa < lo || pa > hi) return false;
            continue;
        }
        float ta = (lo - pa) / d, tb = (hi - pa) / d;
        if (ta > tb) { float t = ta; ta = tb; tb = t; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}

/* OBJrTriggerVolume_IntersectsCharacter 0x4cc680: the team in the mask; then the character's segment
   crossing one of the box's six faces (0x43f750), or its first point inside all six planes (0x43f910),
   which together are the segment touching the box. The original first compares the character's and the
   volume's bounding spheres, a shortcut that changes nothing here. The segment is two points of the active
   character (+0x140, +0x14c) the port doesn't have yet; a stand-in: from the feet up a character's height. */
enum { BODY_HEIGHT = 18 };
static bool intersects(const Volume *v, const TriggerActor *a)
{
    if (!a->alive || a->team >= 32 || !(v->teams & 1u << a->team)) return false;
    float top[3] = { a->pos[0], a->pos[1] + BODY_HEIGHT, a->pos[2] };
    return segment_hits(v, a->pos, top);
}

bool trigger_contains(int i, const float pos[3])
{
    if (i < 0 || i >= nvols) return false;
    float top[3] = { pos[0], pos[1] + BODY_HEIGHT, pos[2] };
    return segment_hits(&vols[i], pos, top);
}

/* The deferred scripts (0x4f2070 queues one: the function, the character, the cause; 128 at most), run
   at the end of the volume's update (SLrScript_ExecuteOnce through 0x5100b0, the character's name as the
   argument). The port's sl_call starts the function's thread, which runs on the next sl_update. */
typedef struct { char func[SCRIPT_LEN], who[SCRIPT_LEN]; } Deferred;
static Deferred queue[QUEUE_MAX];
static int nqueue;
static bool flushing;

void trigger_defer(const char *func, const char *who)
{
    if (nqueue >= QUEUE_MAX) return;       /* "failed to defer script" */
    strncpy(queue[nqueue].func, func, SCRIPT_LEN - 1);
    queue[nqueue].func[SCRIPT_LEN - 1] = 0;
    strncpy(queue[nqueue].who, who ? who : "", SCRIPT_LEN - 1);
    queue[nqueue].who[SCRIPT_LEN - 1] = 0;
    nqueue++;
}

/* A script of a volume, if it has one and it isn't turned off: the one-time flag turns it off as it
   fires. kind: 0 entry, 1 inside, 2 exit. */
static void fire(Volume *v, int kind, const char *who)
{
    static const uint32_t once[3] = { TRIGGER_ONCE_ENTRY, TRIGGER_ONCE_INSIDE, TRIGGER_ONCE_EXIT };
    static const uint32_t off[3] = { TRIGGER_NO_ENTRY, TRIGGER_NO_INSIDE, TRIGGER_NO_EXIT };
    if (!v->script[kind][0] || (v->flags & off[kind])) return;
    if (v->flags & once[kind]) v->flags |= off[kind];
    trigger_defer(v->script[kind], who);
}

static Volume *find_id(uint32_t id)
{
    for (int i = 0; i < nvols; i++) if (vols[i].id == id) return &vols[i];
    return NULL;
}

/* ONrGameState_UpdateTrigger 0x4e0cf0, for one volume. */
static void update_volume(Volume *v, TriggerActor *actors, int n)
{
    Volume *parent = v->parent ? find_id(v->parent) : NULL;     /* "failed to locate parent trigger" */
    for (int c = 0; c < n; c++) {
        TriggerActor *a = &actors[c];
        if (v->flags & TRIGGER_DISABLED) continue;
        if ((v->flags & TRIGGER_PLAYER_ONLY) && !a->player) continue;
        int slot = -1;
        for (int k = 0; k < 4; k++) if (a->inside[k] == v->id) { slot = k; break; }
        if (!intersects(v, a)) {
            if (slot < 0) continue;
            /* leaving: its slot goes and the ones after move down */
            for (int k = slot; k < 3; k++) a->inside[k] = a->inside[k + 1];
            a->inside[3] = 0;
            fire(v, 2, a->name);
            if (parent) fire(parent, 2, a->name);
            continue;
        }
        if (slot < 0) {
            /* entering: the first free slot; with none, the character is never in it (and enters again
               next tick) */
            for (int k = 0; k < 4; k++) {
                if (a->inside[k]) continue;
                a->inside[k] = v->id;
                fire(v, 0, a->name);
                if (parent) fire(parent, 0, a->name);
                break;
            }
        }
        fire(v, 1, a->name);
        if (parent) fire(parent, 1, a->name);
    }
    trigger_flush();
}

/* 0x4f2120: the queued scripts, run now (not while they are already being run). */
void trigger_flush(void)
{
    if (flushing) return;
    flushing = true;
    Deferred run[QUEUE_MAX];
    int m = nqueue;
    memcpy(run, queue, sizeof run[0] * (size_t)m);
    nqueue = 0;
    for (int i = 0; i < m; i++) {
        const char *arg = run[i].who;
        sl_call(run[i].func, 1, &arg);
    }
    flushing = false;
}

void trigger_update(TriggerActor *actors, int n)
{
    last_actors = actors;
    last_count = n;
    for (int i = 0; i < nvols; i++) update_volume(&vols[i], actors, n);
}

/* ---- the commands (AI2iScript_TriggerVolume_*) ---- */

static void report(SlThread *t, const char *msg, const char *name)
{
    (void)t, (void)msg, (void)name;     /* SLrScript_ReportError: to the console, which isn't there yet */
}

/* The script type word: entry, inside, exit (and "all" for trigvolume_enable). */
static int kind_of(const char *s)
{
    if (!strcmp(s, "entry")) return 0;
    if (!strcmp(s, "inside")) return 1;
    if (!strcmp(s, "exit")) return 2;
    if (!strcmp(s, "all")) return 3;
    return -1;
}

/* The disable of a whole volume (case 0 of 0x4b8000) also takes it out of every character's slots
   (0x4f28a0); the slot is cleared where it is, without the move down. */
static void forget(uint32_t id)
{
    for (int c = 0; c < last_count; c++)
        for (int k = 0; k < 4; k++) if (last_actors[c].inside[k] == id) last_actors[c].inside[k] = 0;
}

/* trigvolume_enable name:string on:int [type:string] (AI2iScript_TriggerVolume_Enable 0x4b7e80, every
   volume of the name through 0x4b8000). */
static bool cmd_enable(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)r, (void)u;
    if (argc < 2 || argv[0].type != SL_STRING) return true;
    int kind = 3;
    if (argc >= 3 && argv[2].type == SL_STRING && (kind = kind_of(argv[2].s)) < 0) {
        report(t, "trigvolume_enable: enable type '%s' must be all, entry, exit or inside", argv[2].s);
        return true;
    }
    bool on = (argv[1].type == SL_FLOAT ? argv[1].f : (float)argv[1].i) > 0;
    static const uint32_t bit[4] = { TRIGGER_NO_ENTRY, TRIGGER_NO_INSIDE, TRIGGER_NO_EXIT, TRIGGER_DISABLED };
    int found = 0;
    for (int i = 0; i < nvols; i++) {
        Volume *v = &vols[i];
        if (strcmp(v->name, argv[0].s)) continue;
        found++;
        if (on) v->flags &= ~bit[kind];
        else {
            if (kind == 3) forget(v->id);
            v->flags |= bit[kind];
        }
    }
    if (!found) report(t, "trigvolume_enable: no trigger volume '%s' found", argv[0].s);
    return true;
}

/* trigvolume_reset name (AI2iScript_TriggerVolume_Reset 0x4b84e0, OBJ's reset 0x4cc760): the stored flags
   and scripts back. */
static bool cmd_reset(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (argc < 1 || argv[0].type != SL_STRING) return true;
    for (int i = 0; i < nvols; i++) {
        Volume *v = &vols[i];
        if (strcmp(v->name, argv[0].s)) continue;
        v->flags = v->flags0;
        memcpy(v->script, v->script0, sizeof v->script);
    }
    return true;
}

/* trigvolume_setscript name script type (AI2iScript_TriggerVolume_SetScript 0x4b81d0) */
static bool cmd_setscript(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)r, (void)u;
    if (argc < 3 || argv[0].type != SL_STRING || argv[1].type != SL_STRING || argv[2].type != SL_STRING) return true;
    int kind = kind_of(argv[2].s);
    if (kind < 0 || kind > 2) {
        report(t, "trigvolume_setscript: type '%s' must be entry, exit or inside", argv[2].s);
        return true;
    }
    for (int i = 0; i < nvols; i++) {
        if (strcmp(vols[i].name, argv[0].s)) continue;
        strncpy(vols[i].script[kind], argv[1].s, SCRIPT_LEN - 1);
        vols[i].script[kind][SCRIPT_LEN - 1] = 0;
    }
    return true;
}

/* trigvolume_trigger name type [character] (AI2iScript_TriggerVolume_Trigger 0x4b8300): the script, as if
   the character had set it off. */
static bool cmd_trigger(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)r, (void)u;
    if (argc < 2 || argv[0].type != SL_STRING || argv[1].type != SL_STRING) return true;
    int kind = kind_of(argv[1].s);
    if (kind < 0 || kind > 2) {
        report(t, "trigvolume_trigger: type '%s' must be entry, exit or inside", argv[1].s);
        return true;
    }
    const char *who = argc >= 3 && argv[2].type == SL_STRING ? argv[2].s : "";
    for (int i = 0; i < nvols; i++) {
        if (strcmp(vols[i].name, argv[0].s) || !vols[i].script[kind][0]) continue;
        const char *arg = who;
        sl_call(vols[i].script[kind], 1, &arg);
    }
    return true;
}

/* trigvolume_count id:int -> int (AI2iScript_TriggerVolume_Count, OBJrTriggerVolumeID_CountInside): the
   living characters in the volume of that id. */
static bool cmd_count(SlThread *t, int argc, const SlValue *argv, SlValue *r, void *u)
{
    (void)t, (void)u;
    r->type = SL_INT;
    r->i = 0;
    if (argc < 1) return true;
    uint32_t id = (uint32_t)(argv[0].type == SL_FLOAT ? (int32_t)argv[0].f : argv[0].i);
    Volume *v = find_id(id);
    if (!v) return true;
    for (int c = 0; c < last_count; c++) {
        const TriggerActor *a = &last_actors[c];
        float top[3] = { a->pos[0], a->pos[1] + BODY_HEIGHT, a->pos[2] };
        if (a->alive && segment_hits(v, a->pos, top)) r->i++;
    }
    return true;
}

void trigger_register_commands(void)
{
    sl_register("trigvolume_enable", cmd_enable, NULL);
    sl_register("trigvolume_reset", cmd_reset, NULL);
    sl_register("trigvolume_setscript", cmd_setscript, NULL);
    sl_register("trigvolume_trigger", cmd_trigger, NULL);
    sl_register("trigvolume_count", cmd_count, NULL);
}
