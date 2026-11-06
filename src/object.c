/* The level's objects: see object.h. */
#include "object.h"
#include "env.h"

#include "body.h"
#include "oban.h"
#include "physctx.h"
#include "sl.h"
#include "tm.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The physics type of an animated object (PHrPhysics_MaintainMatrix's case 3). */
enum { PHYSICS_ANIMATED = 3 };

typedef struct {
    const uint8_t *setup;       /* the OBOA element */
    const uint8_t *geometry;    /* M3GA */
    uint32_t physics;
    float pos[3], rot[4], scale, place[12];
    ObanPlayer anim;
    bool shown;
    uint16_t flags;             /* +0x12: 0x8 always drawn (obj_force_draw), 0x8000 its own shade */
    uint32_t shade;             /* +0x20: the shade OBiDraw lights it with (the port's draw has no lights
                                   yet, so neither is applied) */
    /* its physics context (+0x18): the flags (+0x128), the sphere (centre the position) and the box */
    uint32_t ctx_flags;
    float sphere[4];
    PhysBox box;
} Object;

/* The level's array (its elements made into objects by obj_create, 0x4bc690), and the objects there are:
   none until a script creates them. */
static const uint8_t *oboa;
static uint32_t n_elements;
static Object *objs;
static int nobjs, capobjs;
static uint32_t now_ticks;
enum { OBOA_CREATABLE = 0x200 };    /* the element flag obj_create wants */

static const uint8_t *level_oboa(void)
{
    for (int s = tm_file_slots() - 1; s >= 0; s--) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag == OBOA_TAG && in->data) return in->data;
        }
    }
    return NULL;
}

int object_load(void)
{
    object_unload();
    oboa = level_oboa();
    n_elements = oboa ? (uint32_t)(oboa[OBOA_COUNT] | oboa[OBOA_COUNT + 1] << 8) : 0;
    return (int)n_elements;
}

void object_unload(void)
{
    free(objs);
    objs = NULL;
    nobjs = capobjs = 0;
    oboa = NULL;
    n_elements = 0;
}

/* The object setup 0x439910: the geometry, the placement, the animation at its first frame (or a random
   one, flag 0x04), not playing until a script or the autostart flag starts it. */
static bool create(const uint8_t *e)
{
    if (nobjs == capobjs) {
        int cap = capobjs ? 2 * capobjs : 64;
        Object *n = realloc(objs, sizeof *n * (size_t)cap);
        if (!n) return false;
        objs = n, capobjs = cap;
    }
    Object *o = &objs[nobjs++];
    memset(o, 0, sizeof *o);
    o->setup = e;
    o->shown = true;
    o->geometry = tm_link(e + OBOA_GEOMETRY);
    for (int k = 0; k < 3; k++) o->pos[k] = rdf(e + OBOA_POSITION + 4 * k);
    for (int k = 0; k < 4; k++) o->rot[k] = rdf(e + OBOA_ROTATION + 4 * k);
    o->scale = rdf(e + OBOA_SCALE);
    for (int k = 0; k < 12; k++) o->place[k] = rdf(e + OBOA_MATRIX + 4 * k);
    o->flags = (uint16_t)rd32(e + OBOA_FLAGS);
    const void *oban = tm_link(e + OBOA_ANIMATION);
    oban_player_init(&o->anim, oban);
    o->physics = oban ? PHYSICS_ANIMATED : rd32(e + OBOA_PHYSICS);
    if (oban) {
        /* as OBrAnim_Start but without the playing bit (the setup's own copy of it) */
        oban_start(&o->anim, now_ticks);
        o->anim.state &= (uint16_t)~OBAN_PLAYING;
    }
    /* the context: the box from the geometries' bounds and the sphere (physctx_box_make); flag 0x1000
       a box (0x200), else 0x400 no collisions (4); 0x800 no gravity (0x40) */
    const void *geoms[64];
    int ng = 0;
    for (uint32_t k = 0; o->geometry && k < rd32(o->geometry + 0x14) && ng < 64; k++) geoms[ng++] = tm_link(o->geometry + 0x18 + 4 * k);
    memcpy(o->sphere, o->pos, sizeof o->pos);
    physctx_box_make(&o->box, geoms, ng, o->pos, &o->sphere[3]);
    o->ctx_flags = 2u;
    if (o->flags & OBOA_FLAG_BOX_PLANES) o->ctx_flags |= PHYS_VOLUME;
    else if (o->flags & OBOA_FLAG_PHYS_4) o->ctx_flags |= 4u;
    if (o->flags & OBOA_FLAG_PHYS_40) o->ctx_flags |= 0x40u;
    float m[12];
    object_matrix(nobjs - 1, m);
    physctx_box_place(&o->box, m);
    return true;
}

int object_create(uint32_t from, uint32_t to)
{
    if (from > to) { uint32_t t = from; from = to; to = t; }
    int made = 0;
    for (uint32_t i = 0; i < n_elements; i++) {
        const uint8_t *e = oboa + OBOA_ELEMENTS + (size_t)OBOA_SIZE * i;
        uint32_t id = rd32(e + OBOA_ID);
        if (id >= from && id <= to && (rd32(e + OBOA_FLAGS) & OBOA_CREATABLE) && create(e)) made++;
    }
    return made;
}

int object_count(void) { return nobjs; }
bool object_shown(int i) { return i >= 0 && i < nobjs && objs[i].shown; }
uint32_t object_id(int i) { return i >= 0 && i < nobjs ? rd32(objs[i].setup + OBOA_ID) : 0; }
const char *object_name(int i) { return i >= 0 && i < nobjs ? (const char *)objs[i].setup + OBOA_NAME : ""; }

uint32_t object_geometry_count(int i)
{
    return i >= 0 && i < nobjs && objs[i].geometry ? rd32(objs[i].geometry + 0x14) : 0;
}

const void *object_geometry(int i, uint32_t k)
{
    if (k >= object_geometry_count(i)) return NULL;
    return tm_link(objs[i].geometry + 0x18 + 4 * k);
}

static void mul(const float a[12], const float b[12], float o[12])
{
    float t[12];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 3; r++)
            t[3 * c + r] = a[r] * b[3 * c] + a[3 + r] * b[3 * c + 1] + a[6 + r] * b[3 * c + 2] + (c == 3 ? a[9 + r] : 0.0f);
    memcpy(o, t, sizeof t);
}

/* PHrPhysics_MaintainMatrix 0x43b410: an animated object is its animation's frame, under its placement
   for the doors' animations (flag 0x10); the others T(position) Q(rotation) S(scale). The context's
   flags 0x40 (another object's matrix) and 0x20 (half turns about y and z) aren't set by anything
   ported yet. */
void object_matrix(int i, float m[12])
{
    static const float ident[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    memcpy(m, ident, sizeof ident);
    if (i < 0 || i >= nobjs) return;
    Object *o = &objs[i];
    if (o->physics == PHYSICS_ANIMATED && o->anim.oban) {
        oban_matrix(o->anim.oban, o->anim.frame, false, m);
        if (rd32((const uint8_t *)o->anim.oban + OBAN_FLAGS) & OBAN_Z_UP) mul(o->place, m, m);
        return;
    }
    float r[12] = { 0 };
    body_quat_to_matrix(o->rot, r);
    for (int k = 0; k < 9; k++) m[k] = r[k] * o->scale;
    memcpy(m + 9, o->pos, sizeof o->pos);
}

void object_update(uint32_t now)
{
    now_ticks = now;
    for (int i = 0; i < nobjs; i++)
        if (objs[i].physics == PHYSICS_ANIMATED) {
            oban_update(&objs[i].anim, now);
            float m[12];
            object_matrix(i, m);    /* PHrPhysics_MaintainMatrix 0x43b410's box */
            physctx_box_place(&objs[i].box, m);
        }
}

/* The contexts (the callbacks 0x5cb4e0, kind 2; 0x43b820, the push, moves only types 2 and 4, which no
   object the port makes is). */
static int ctx_count(void) { return nobjs; }
static bool ctx_get(int i, PhysCtx *out)
{
    Object *o = &objs[i];
    memset(out, 0, sizeof *out);
    out->kind = PHYS_KIND_OBJECT;
    out->type = (int)o->physics;
    out->flags = o->ctx_flags;
    out->tree.s = (const float (*)[4])o->sphere;
    out->box = &o->box;
    out->owner = o;
    return true;
}

/* ---- env_setanim id name, env_anim from [to], and their _block forms (AIiScript_EnvSetAnim_Internal
   0x4bc780, AIiScript_EnvAnim_Internal 0x4bc920; the objects of the ids, from AIiObjectsFromScript
   0x4bc320, either order) ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

static int objects_of(int argc, const SlValue *argv, int *out, int cap)
{
    int32_t a = argc >= 1 ? num(&argv[0]) : 0, b = argc >= 2 && argv[1].type != SL_STRING ? num(&argv[1]) : a;
    if (a > b) { int32_t t = a; a = b; b = t; }
    int n = 0;
    for (int i = 0; i < nobjs && n < cap; i++) {
        uint32_t id = object_id(i);
        if ((int64_t)id >= a && (int64_t)id <= b) out[n++] = i;
    }
    return n;
}

static uint16_t frames_of(const void *oban) { const uint8_t *d = oban; return (uint16_t)(d[OBAN_FRAMES] | d[OBAN_FRAMES + 1] << 8); }

/* Playing and short of its last frame: what the block forms wait on. */
static bool busy(const Object *o)
{
    return o->anim.oban && (o->anim.state & OBAN_PLAYING) && o->anim.frame + 1 < (int)frames_of(o->anim.oban);
}

/* env_setanim: the block form waits while one of the objects still plays; then each gets the animation
   (0x438c50: animated, stopping at the animation's end) and starts it (OBrAnim_Start). */
static bool setanim(int argc, const SlValue *argv, bool block)
{
    if (argc < 2 || argv[1].type != SL_STRING) return true;
    int ids[128], n = objects_of(1, argv, ids, 128);
    if (block)
        for (int k = 0; k < n; k++) if (busy(&objs[ids[k]])) return false;
    const void *oban = tm_instance_get(OBAN_TAG, argv[1].s);
    if (!oban) return true;     /* "Couldn't load animation for script" */
    for (int k = 0; k < n; k++) {
        Object *o = &objs[ids[k]];
        oban_player_init(&o->anim, oban);
        o->physics = PHYSICS_ANIMATED;
        o->anim.stop = (int16_t)frames_of(oban);
        oban_start(&o->anim, now_ticks);
    }
    return true;
}

/* env_anim: the block form waits while one of the objects still plays, when the second id isn't 0 (the
   original tests the second argument there); then it starts them. Neither form waits for the animation
   it starts. */
static bool anim(int argc, const SlValue *argv, bool block)
{
    int ids[128], n = objects_of(argc, argv, ids, 128);
    if (block && argc >= 2 && num(&argv[1]) != 0)
        for (int k = 0; k < n; k++) if (busy(&objs[ids[k]])) return false;
    for (int k = 0; k < n; k++) if (objs[ids[k]].anim.oban) oban_start(&objs[ids[k]].anim, now_ticks);
    return true;
}

static bool cmd_setanim(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return setanim(c, v, false); }
static bool cmd_setanim_block(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return setanim(c, v, true); }
static bool cmd_anim(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return anim(c, v, false); }
static bool cmd_anim_block(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return anim(c, v, true); }

/* obj_create from [to] (0x4bc690): every element of the range the obj_create flag allows becomes an
   object, again if it already is one. */
static bool cmd_create(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1) return true;
    int32_t a = num(&v[0]), b = c >= 2 ? num(&v[1]) : a;
    object_create((uint32_t)a, (uint32_t)b);
    return true;
}

/* obj_kill from [to] (0x4bc4d0, each through 0x4398c0): the objects of the range go. */
static bool cmd_kill(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    int ids[128], n = objects_of(c, v, ids, 128);
    for (int k = n - 1; k >= 0; k--) objs[ids[k]] = objs[--nobjs];
    return true;
}

/* obj_show and obj_hide from [to] (0x4bc3f0, 0x4bc460, OBrShow 0x438d20) */
static bool show(int c, const SlValue *v, bool on)
{
    int ids[128], n = objects_of(c, v, ids, 128);
    for (int k = 0; k < n; k++) {
        Object *o = &objs[ids[k]];
        o->shown = on;
        /* OBrShow 0x438d20: hidden, no collisions (4); shown, back unless the setup's 0x400 */
        if (!on) o->ctx_flags |= 4u;
        else if (!(o->flags & OBOA_FLAG_PHYS_4)) o->ctx_flags &= ~4u;
    }
    return true;
}
/* obj_force_draw from [to] (AIiScript_ForceDraw 0x4bc2c0): the objects are drawn whatever their visibility (+0x12 bit 8). */
static bool cmd_force_draw(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    int ids[128], n = objects_of(c, v, ids, 128);
    for (int k = 0; k < n; k++) objs[ids[k]].flags |= 0x8;
    return true;
}

/* obj_shade from to r g b (AIiScript_ObjShade 0x4bc540): the objects get their own shade (opaque). */
static bool cmd_shade(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 5) return true;
    uint32_t shade = env_script_shade(&v[2]);
    int ids[128], n = objects_of(2, v, ids, 128);
    for (int k = 0; k < n; k++) objs[ids[k]].flags |= 0x8000, objs[ids[k]].shade = shade;
    return true;
}

static bool cmd_show(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return show(c, v, true); }
static bool cmd_hide(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return show(c, v, false); }

void object_register_commands(void)
{
    sl_register("obj_create", cmd_create, NULL);
    physctx_add_source((PhysSource){ ctx_count, ctx_get });
    sl_register("obj_kill", cmd_kill, NULL);
    sl_register("obj_show", cmd_show, NULL);
    sl_register("obj_hide", cmd_hide, NULL);
    sl_register("obj_force_draw", cmd_force_draw, NULL);
    sl_register("obj_shade", cmd_shade, NULL);
    sl_register("env_setanim", cmd_setanim, NULL);
    sl_register("env_setanim_block", cmd_setanim_block, NULL);
    sl_register("env_anim", cmd_anim, NULL);
    sl_register("env_anim_block", cmd_anim_block, NULL);
}
