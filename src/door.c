/* Doors: see door.h. */
#include "door.h"

#include "akev.h"
#include "chars.h"
#include "event.h"
#include "graph.h"
#include "knowledge.h"
#include "m3gm.h"
#include "oban.h"
#include "physctx.h"
#include "objc.h"
#include "object.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* The door's data in the OBJC chunk (the read method 0x4c3890, OBJC version 39). */
enum {
    OSD_CLASS = 0x00,       /* 63: the DOOR instance's name */
    OSD_ID = 0x3f,          /* u16 */
    OSD_KEY = 0x41,         /* u16: 0 none */
    OSD_FLAGS = 0x43,       /* u16, kept & 0x1795 */
    OSD_CENTRE = 0x45,      /* 3 floats: the door frame's (AKDA) */
    OSD_RADIUS = 0x51,      /* float: the activation distance, squared */
    OSD_EVENTS = 0xd3,      /* the event list (event.h) */
    OSD_SIZE = 0xd5,
};

/* The DOOR class (TM instance). */
enum {
    DOOR_CLASS_OBAN = 0x08,
    DOOR_CLASS_AI_SOUND = 0x14,     /* i32: the AI's sound type, -1 none */
    DOOR_CLASS_AI_RADIUS = 0x18,    /* float: its radius */
    DOOR_CLASS_OPEN_SOUND = 0x1c,   /* 32: the impulse sound's name */
    DOOR_CLASS_CLOSE_SOUND = 0x3c,
};

enum { OPEN_TIME = 180, STUCK_DELAY = 180 };   /* SetOSD's +6 (0x4c3b30); the stuck logic's delay */

/* A panel: its element, its animation context (+0x130 of the physics context) and that context's flags
   0x20 (half turns about y and z after the frame) and 0x40 (the frame of the first panel's animation). */
typedef struct {
    const uint8_t *element;
    bool live;
    ObanPlayer anim;
    uint16_t anim_flags;            /* its animation context's flags (CTX_*), not the physics' */
    float sphere[4];                /* the context's sphere and box (physctx.h) */
    PhysBox box;
} Panel;
enum { CTX_HALF_TURNS = 0x20, CTX_FOLLOW = 0x40 };

typedef struct {
    uint32_t object_id;             /* D+0x04 */
    float pos[3], rot[3];           /* D+0x0c, D+0x18 (degrees) */
    /* S = D+0x2c, the OSD */
    uint16_t id, flags, key;        /* S+0, S+2, S+4 */
    uint16_t open_time;             /* S+6 */
    float radius2;                  /* S+8 */
    int16_t state;                  /* S+0xc: 1 open */
    uint16_t stuck, countdown, delay;   /* S+0xe, S+0x10, S+0x12 */
    const uint8_t *cls;             /* S+0x60: the DOOR instance */
    const uint8_t *oban;            /* its +8 */
    Panel panel[2];                 /* S+0x68/+0x6c (the contexts), S+0x70/+0x74 (the elements) */
    const uint8_t *gunk;            /* S+0xe4: the ONOA entry's IDXA of quads */
    uint32_t shade;                 /* S+0xe8 */
    float centre[3];                /* S+0xc0 */
    int16_t frame, step, stop;      /* S+0x16a..: the animation without panels */
    const uint8_t *events;          /* S+0xa0 (D+0xcc): the event list, NULL none */
} Door;

static Door *doors;
static int ndoors;
static void *door_env;

/* The level's object gunk (ONOA, ONLV +0x6c): (object id, IDXA of quads) pairs. */
static const uint8_t *level_onoa(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONOA") && in->data) return in->data;
    }
    return NULL;
}

/* 0x506fc0: the gunk of an object id. */
static const uint8_t *find_gunk(uint32_t id)
{
    const uint8_t *a = level_onoa();
    if (!a) return NULL;
    uint32_t n = rd32(a + 0x14);
    for (uint32_t i = 0; i < n; i++)
        if (rd32(a + 0x18 + 8 * i) == id) return tm_link(a + 0x18 + 8 * i + 4);
    return NULL;
}

static uint32_t gunk_count(const Door *d) { return d->gunk ? rd32(d->gunk + 0x14) : 0; }
static uint32_t gunk_quad(const Door *d, uint32_t k) { return rd32(d->gunk + 0x18 + 4 * k); }

/* 0x507080: the gunk's mean shade, of the corners brighter than 0x3f in a channel (0x3f3f3f without any;
   0xffffff without quads). */
static uint32_t gunk_shade(const Door *d)
{
    uint32_t n = gunk_count(d), r = 0, g = 0, b = 0, count = 0;
    if (!n) return 0xffffff;
    for (uint32_t k = 0; k < n; k++) {
        AkevQuad q;
        if (!akev_quad(door_env, gunk_quad(d, k), &q)) continue;
        unsigned corners = q.flags & AKEV_QUAD_TRIANGLE ? 3 : 4;
        for (unsigned c = 0; c < corners; c++) {
            uint32_t s = q.shade[c];
            if ((s & 0xff) > 0x3f || (s >> 8 & 0xff) > 0x3f || (s >> 16 & 0xff) > 0x3f)
                r += s >> 16 & 0xff, g += s >> 8 & 0xff, b += s & 0xff, count++;
        }
    }
    if (!count) r = g = b = 0x3f;
    else {
        uint32_t h = count >> 1;
        r = (r + h) / count, g = (g + h) / count, b = (b + h) / count;
    }
    return 0xff000000u | r << 16 | g << 8 | b;
}

/* OBJiDoor_HideShowGunk 0x4c2940: hidden is flags 0x2800 (not drawn, not collided with). */
static void show_gunk(const Door *d, bool show)
{
    for (uint32_t k = 0; k < gunk_count(d); k++)
        akev_quad_set_flags(door_env, gunk_quad(d, k), show ? 0 : 0x2800, show ? 0x2800 : 0);
}

/* The OBOA element of a door's panel (LevelBegin's search by +0x14, the first match). */
static const uint8_t *find_element(uint32_t id)
{
    for (int s = tm_file_slots() - 1; s >= 0; s--) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag != OBOA_TAG || !in->data) continue;
            const uint8_t *a = in->data;
            uint32_t n = rd16(a + OBOA_COUNT);
            for (uint32_t k = 0; k < n; k++) {
                const uint8_t *e = a + OBOA_ELEMENTS + (size_t)OBOA_SIZE * k;
                if (rd32(e + OBOA_DOOR) == id) return e;
            }
            return NULL;
        }
    }
    return NULL;
}

/* OBJrDoor_LevelBegin 0x4c48a0 */
static void level_begin(Door *d)
{
    if (d->flags & DOOR_HIDE_FRAME) {
        /* the door frame (AKDA, 0x34 bytes: the quad, ..., the centre at +0x1c) at the door's centre */
        const uint8_t *a = tm_link((const uint8_t *)door_env + AKEV_DOOR_FRAMES);
        uint32_t n = a ? rd32(a + 0x14) : 0;
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *e = a + 0x18 + 0x34 * k;
            if (rdf(e + 0x1c) == d->centre[0] && rdf(e + 0x20) == d->centre[1] && rdf(e + 0x24) == d->centre[2]) {
                akev_quad_set_flags(door_env, rd32(e), 0x2000, 0);
                break;
            }
        }
    }
    d->panel[0].element = find_element(d->id);
    d->panel[1].element = find_element(d->id | 0x1000u);
    d->panel[0].live = d->panel[1].live = false;
    d->gunk = find_gunk((d->object_id & 0xffffff) | 0x3000000);
    if (d->gunk) {
        d->shade = gunk_shade(d);
        for (uint32_t k = 0; k < gunk_count(d); k++) akev_quad_set_shade(door_env, gunk_quad(d, k), d->shade);
    } else {
        d->shade = 0xffffffff;
    }
    d->flags |= DOOR_STARTED;
    if (d->flags & DOOR_LOCKED_AT_START) d->flags |= DOOR_LOCKED;
    else d->flags &= (uint16_t)~DOOR_LOCKED;
    d->state = 0;
    d->delay = 0;
    show_gunk(d, true);
    d->frame = 0;
    d->step = 1;
}

static bool add_door(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < OSD_SIZE) return true;
    Door *d = &doors[ndoors++];
    memset(d, 0, sizeof *d);
    d->object_id = o->id;
    memcpy(d->pos, o->pos, sizeof d->pos);
    memcpy(d->rot, o->rot, sizeof d->rot);
    d->id = rd16(o->data + OSD_ID);
    d->key = rd16(o->data + OSD_KEY);
    d->flags = rd16(o->data + OSD_FLAGS) & 0x1795;
    for (int k = 0; k < 3; k++) d->centre[k] = rdf(o->data + OSD_CENTRE + 4 * k);
    d->radius2 = rdf(o->data + OSD_RADIUS);
    d->events = event_list_size(o->data + OSD_EVENTS, o->size - OSD_EVENTS) ? o->data + OSD_EVENTS : NULL;
    d->open_time = OPEN_TIME;
    char name[64];
    memcpy(name, o->data + OSD_CLASS, 63);
    name[63] = 0;
    d->cls = name[0] ? tm_instance_get(TM_TAG("DOOR"), name) : NULL;
    d->oban = d->cls ? tm_link(d->cls + DOOR_CLASS_OBAN) : NULL;
    level_begin(d);
    return true;
}

static bool count_door(const ObjcObject *o, void *user) { (void)o; ++*(int *)user; return true; }

int door_load(void *env)
{
    door_unload();
    door_env = env;
    if (!env) return 0;
    int n = 0;
    objc_each("Door", count_door, &n);
    doors = n ? calloc((size_t)n, sizeof *doors) : NULL;
    if (doors) objc_each("Door", add_door, NULL);
    graph_link_doors();     /* PHrBuildGraph's, if the graph came first */
    return ndoors;
}

void door_unload(void)
{
    free(doors);
    doors = NULL;
    ndoors = 0;
    door_env = NULL;
    graph_link_doors();
}

int door_count(void) { return ndoors; }
uint16_t door_id(int i) { return i >= 0 && i < ndoors ? doors[i].id : 0; }
uint16_t door_flags(int i) { return i >= 0 && i < ndoors ? doors[i].flags : 0; }
int door_state(int i) { return i >= 0 && i < ndoors ? doors[i].state : 0; }
uint32_t door_object_id(int i) { return i >= 0 && i < ndoors ? doors[i].object_id : 0; }
float door_half_width(int i)
{
    if (i < 0 || i >= ndoors || !door_env) return 0.0f;
    const Door *d = &doors[i];
    const uint8_t *a = tm_link((const uint8_t *)door_env + AKEV_DOOR_FRAMES);
    uint32_t n = a ? rd32(a + 0x14) : 0;
    for (uint32_t k = 0; k < n; k++) {
        const uint8_t *e = a + 0x18 + 0x34 * k;
        if (rdf(e + 0x1c) != d->centre[0] || rdf(e + 0x20) != d->centre[1] || rdf(e + 0x24) != d->centre[2]) continue;
        float w = 0.0f, p[3];
        for (unsigned c = 0; c < 4; c++) {
            if (!akev_quad_corner(door_env, rd32(e), c, p)) continue;
            float dx = p[0] - d->centre[0], dz = p[2] - d->centre[2], r = sqrtf(dx * dx + dz * dz);
            if (r > w) w = r;
        }
        return w;
    }
    return 0.0f;
}
float door_facing(int i) { return i >= 0 && i < ndoors ? doors[i].rot[1] * 0.017453293f : 0.0f; }
void door_centre(int i, float pos[3]) { if (i >= 0 && i < ndoors) memcpy(pos, doors[i].centre, sizeof doors[i].centre); }
void door_position(int i, float pos[3]) { if (i >= 0 && i < ndoors) memcpy(pos, doors[i].pos, sizeof doors[i].pos); }

int door_frame(int i)
{
    if (i < 0 || i >= ndoors) return -1;
    const Door *d = &doors[i];
    if (d->panel[0].live) return d->panel[0].anim.frame;
    return d->panel[0].element ? -1 : d->frame;
}

static uint16_t open_frame(const Door *d) { return rd16(d->oban + OBAN_DOOR_OPEN); }
static uint16_t frame_count(const Door *d) { return rd16(d->oban + OBAN_FRAMES); }

/* 0x438b90: the object plays the animation from a frame to a stop. */
static void play(Panel *p, const uint8_t *oban, int16_t from, int16_t stop)
{
    p->anim.oban = oban;
    p->anim.state |= OBAN_PLAYING;
    p->anim.last = 0;
    p->anim.step = 1;
    p->anim.state &= (uint16_t)~OBAN_PAUSED;   /* 0x43a600 (it would take the time if it were paused) */
    p->anim.frame = from;
    p->anim.stop = stop;
}

/* OBJiDoor_PointOnSide 0x4c2370: the door's plane through its position, made of its rotation's x and y
   axes (Rz Ry Rx of the degrees, the points (0,0,0), (1,0,0), (0,1,0)); 1 if p is more than 1/1024
   behind it, else 0 (never -1, which the callers also test). */
static int point_on_side(const Door *d, const float p[3])
{
    const float rad = 0.017453292f;
    float ci = cosf(d->rot[0] * rad), si = sinf(d->rot[0] * rad);
    float cj = cosf(d->rot[1] * rad), sj = sinf(d->rot[1] * rad);
    float ch = cosf(d->rot[2] * rad), sh = sinf(d->rot[2] * rad);
    /* R = Rz Ry Rx: its columns are the axes */
    float x[3] = { cj * ch, cj * sh, -sj };
    float y[3] = { sj * si * ch - ci * sh, sj * si * sh + ci * ch, cj * si };
    float p0[3], p1[3], p2[3];
    for (int k = 0; k < 3; k++) p0[k] = d->pos[k], p1[k] = x[k] + d->pos[k], p2[k] = y[k] + d->pos[k];
    /* MUrVector_PlaneFromPoints 0x433ca0: n = (p0 - p1) x (p2 - p0), normalised; d = -n.p0 */
    float a[3] = { p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2] }, b[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    float n[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0) n[0] /= len, n[1] /= len, n[2] /= len;
    float dd = -(n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2]);
    return n[0] * p[0] + n[1] * p[1] + n[2] * p[2] + dd < -0.0009765625f ? 1 : 0;
}

/* WPrInventory_HasKey 0x424370 (through 0x51cd00): keys aren't ported. */
static bool has_key(const Chr *c, uint16_t key) { (void)c, (void)key; return false; }

/* The test the update makes of a character (twice, as the original has it): may it pass? */
static bool may_pass(const Door *d, const Chr *c)
{
    int side = point_on_side(d, c->c.pos);
    /* (the cheat byte 0x5ec5f5, "unlock all doors", passes everyone) */
    if (d->flags & DOOR_JAMMED) return false;
    if (!(d->flags & DOOR_LOCKED)) return true;
    if (d->flags & DOOR_ONE_WAY) {
        if (side == -1) return true;
        if (d->flags & DOOR_ONE_WAY_REVERSED) side = !side;
        if (side) return true;
    }
    return d->key != 0 && has_key(c, d->key);
}

bool door_lock_passes(int i, const float p[3])
{
    const Door *d = &doors[i];
    if (d->flags & DOOR_JAMMED) return false;
    if (!(d->flags & DOOR_LOCKED)) return true;
    if (!(d->flags & DOOR_ONE_WAY)) return false;
    int side = point_on_side(d, p);
    if (d->flags & DOOR_ONE_WAY_REVERSED) side = !side;
    return side != 0;
}

int door_point_side(int i, const float p[3]) { return i >= 0 && i < ndoors ? point_on_side(&doors[i], p) : 0; }

/* OBJrDoor_CharacterCanOpen 0x4c24b0: the path search's test of a link's door, by the side of the link's
   room (the link's +0x24): not jammed; and unlocked, or one way with the side it lets through (-1, never
   made, passes too), or a key (keys aren't ported). */
bool door_side_passes(int i, int side)
{
    if (i < 0 || i >= ndoors) return true;
    const Door *d = &doors[i];
    if (d->flags & DOOR_JAMMED) return false;
    if (!(d->flags & DOOR_LOCKED)) return true;
    if (d->flags & DOOR_ONE_WAY) {
        if (side == -1) return true;
        if (d->flags & DOOR_ONE_WAY_REVERSED) side = !side;
        if (side) return true;
    }
    return false;
}

/* OBJrDoor_CharacterProximity 0x4c2a20: the character (C+0x148) within the door's activation distance. */
bool door_near(int i, const float p[3])
{
    if (i < 0 || i >= ndoors) return false;
    const Door *d = &doors[i];
    float dx = p[0] - d->pos[0], dy = p[1] - d->pos[1], dz = p[2] - d->pos[2];
    return dx * dx + dy * dy + dz * dz < d->radius2;
}

static void play_sound(const Door *d, unsigned name_at, const Chr *who)
{
    /* the class's sound (UpdateSoundPointers 0x4c4ce0 looks it up), played at the door (OSrImpulse_Play
       0x512480, full volume) */
    char name[33];
    memcpy(name, d->cls + name_at, 32);
    name[32] = 0;
    const void *imp = name[0] ? sound_impulse_find(name) : NULL;
    if (imp) sound_impulse_play_at(imp, d->pos, NULL);
    /* then the class's AI sound (+0x14, -1 none; its radius +0x18), of who opens it */
    int32_t type;
    memcpy(&type, d->cls + DOOR_CLASS_AI_SOUND, 4);
    float radius;
    memcpy(&radius, d->cls + DOOR_CLASS_AI_RADIUS, 4);
    if (type != -1) knowledge_sound(type, d->pos, radius, (Chr *)who, NULL);
}

/* OBJrDoor_Open with the character that opens it (NULL for a script) */
static void open_door(int i, const Chr *who)
{
    if (i < 0 || i >= ndoors) return;
    Door *d = &doors[i];
    if (!d->cls || !d->oban || d->state == 1) return;
    bool done = false;
    if (!(d->flags & DOOR_ANIMATING)) {
        /* the panels from their elements (the setup 0x439910 with flags 0x1200, the array's 0x439470) */
        for (int k = 0; k < 2; k++) {
            Panel *p = &d->panel[k];
            if (!p->element) continue;
            p->live = true;
            oban_player_init(&p->anim, tm_link(p->element + OBOA_ANIMATION));
            p->anim.frame = 0;
            p->anim_flags = 0;
            const uint8_t *ga = tm_link(p->element + OBOA_GEOMETRY);
            const void *geoms[16];
            int ng = 0;
            for (uint32_t g = 0; ga && g < rd32(ga + 0x14) && ng < 16; g++) geoms[ng++] = tm_link(ga + 0x18 + 4 * g);
            for (int j = 0; j < 3; j++) p->sphere[j] = rdf(p->element + OBOA_POSITION + 4 * j);
            physctx_box_make(&p->box, geoms, ng, p->sphere, &p->sphere[3]);
        }
        if (d->panel[0].live && (d->flags & DOOR_MIRROR)) d->panel[0].anim_flags |= CTX_HALF_TURNS;
        if (d->panel[1].live) {
            d->panel[1].anim_flags |= CTX_FOLLOW;
            if (!(d->flags & DOOR_MIRROR)) d->panel[1].anim_flags |= CTX_HALF_TURNS;
        }
        for (int k = 0; k < 2; k++) {
            float m[12];
            if (d->panel[k].live && door_panel(i, k, m)) physctx_box_place(&d->panel[k].box, m);
        }
    } else if (d->panel[0].live) {
        /* closing: the same place of the opening (the original reads the first panel without a test) */
        int16_t f = (int16_t)(frame_count(d) - d->panel[0].anim.frame);
        d->panel[0].anim.frame = f;
        if (d->panel[1].live) d->panel[1].anim.frame = f;
        done = f == open_frame(d);
    }
    d->state = 1;
    d->countdown = 0;
    if (!done) {
        d->flags |= DOOR_ANIMATING;
        if (!d->panel[0].live) d->stop = (int16_t)open_frame(d);
        else
            for (int k = 0; k < 2; k++)
                if (d->panel[k].live) play(&d->panel[k], d->oban, d->panel[k].anim.frame, (int16_t)open_frame(d));
    }
    show_gunk(d, false);
    event_list_run(d->events, who ? who->name : NULL);     /* 0x4f3cc0 */
    play_sound(d, DOOR_CLASS_OPEN_SOUND, who);
}

void door_open(int i) { open_door(i, NULL); }

void door_close(int i)
{
    if (i < 0 || i >= ndoors) return;
    Door *d = &doors[i];
    if (!d->cls || !d->oban || d->state == 0) return;
    d->state = 0;
    bool done = false;
    if ((d->flags & DOOR_ANIMATING) && d->panel[0].live) {
        /* opening: the same place of the closing */
        int16_t f = (int16_t)(2 * open_frame(d) - d->panel[0].anim.frame);
        d->panel[0].anim.frame = f;
        if (d->panel[1].live) d->panel[1].anim.frame = f;
        done = f == frame_count(d);
    }
    if (!done) {
        d->flags |= DOOR_ANIMATING;
        if (!d->panel[0].live) d->stop = (int16_t)frame_count(d);
        else
            for (int k = 0; k < 2; k++)
                if (d->panel[k].live) play(&d->panel[k], d->oban, d->panel[k].anim.frame, (int16_t)frame_count(d));
    }
    play_sound(d, DOOR_CLASS_CLOSE_SOUND, NULL);
}

void door_physics(uint32_t now)
{
    for (int i = 0; i < ndoors; i++)
        for (int k = 0; k < 2; k++)
            if (doors[i].panel[k].live) oban_update(&doors[i].panel[k].anim, now);
    /* PHrPhysics_MaintainMatrix 0x43b410: the panels' boxes where their frames put them */
    for (int i = 0; i < ndoors; i++)
        for (int k = 0; k < 2; k++) {
            float m[12];
            if (doors[i].panel[k].live && door_panel(i, k, m)) physctx_box_place(&doors[i].panel[k].box, m);
        }
}

/* The panels' contexts while they move (kind 2, type 3, a box: the setup's flags 0x1200; 0x4c26c0 sets the
   physics flags 0x82). */
static int ctx_count(void) { return 2 * ndoors; }
static bool ctx_get(int i, PhysCtx *out)
{
    Panel *p = &doors[i / 2].panel[i % 2];
    if (!p->live) return false;
    memset(out, 0, sizeof *out);
    out->kind = PHYS_KIND_OBJECT;
    out->type = 3;
    out->flags = 0x82u | PHYS_VOLUME;
    out->tree.s = (const float (*)[4])p->sphere;
    out->box = &p->box;
    out->owner = p;
    return true;
}

/* OBJrDoor_Update 0x4c44f0 */
static void update(int i)
{
    Door *d = &doors[i];
    int16_t frame = 0;
    if (d->panel[0].live) {
        frame = d->panel[0].anim.frame;
        /* the panels' blocked counter (+0x140) is 0 without their physics: the stuck logic (0x1a4 ticks
           open: DOOR_STUCK; 90: closes, or opens again for the character in the way) has nothing to do */
        d->stuck = 0;
    } else {
        if (!d->cls) return;
        if (!(d->flags & DOOR_STARTED)) return;
        if (d->flags & DOOR_ANIMATING) {
            if (d->oban) {
                d->frame = (int16_t)(d->frame + d->step);
                if (d->frame >= (int)frame_count(d)) d->frame = (int16_t)(frame_count(d) - 1);
                else if (d->stop && d->frame >= d->stop) d->frame = d->stop;
            }
            frame = d->frame;
        }
    }
    if (d->flags & DOOR_ANIMATING) {
        if (d->state == 1) {
            if ((uint16_t)frame < open_frame(d)) return;
            d->flags &= (uint16_t)~DOOR_STUCK;
            if (d->countdown) {
                if (--d->countdown == 0) d->flags &= (uint16_t)~DOOR_ANIMATING;
            } else {
                d->countdown = d->open_time;
            }
        } else {
            if ((int)(uint16_t)frame < (int)frame_count(d) - 1) return;
            d->flags &= (uint16_t)~DOOR_STUCK;
            d->panel[0].live = d->panel[1].live = false;
            show_gunk(d, true);
            d->flags &= (uint16_t)~DOOR_ANIMATING;
        }
    }
    if (d->delay) {
        d->delay--;
        return;
    }
    if (d->flags & DOOR_JAMMED) return;
    bool look = true;
    if ((d->flags & DOOR_MANUAL) && d->state == 0) look = false;
    else if ((d->flags & DOOR_LOCKED) && !(d->flags & DOOR_ONE_WAY) && d->key == 0) look = false;
    if (look) {
        /* the living characters (ONrGameState's living list) */
        for (int s = 0; s < chars_slots(); s++) {
            const Chr *c = chars_get(s);
            if (!c || (c->c.flags & 1)) continue;
            float dx = c->c.pos[0] - d->pos[0], dy = c->c.pos[1] - d->pos[1], dz = c->c.pos[2] - d->pos[2];
            if (!(dx * dx + dy * dy + dz * dz < d->radius2) || !may_pass(d, c)) continue;
            if (d->state == 0) open_door(i, c);
            return;
        }
    }
    if (!(d->flags & DOOR_ANIMATING) && d->state == 1) door_close(i);
}

/* OBJrDoor_TryDoorAction 0x4c2a50 with its enumeration's callback 0x4c2aa0: of the closed doors whose
   activation distance the character is within and that it faces (the angle from its facing, C+0x178,
   to the door from its pelvis at most 1.4), the one at the smallest angle; a manual door it may pass
   first, an automatic one it may pass never (it opens by itself). */
int door_try_action(const Chr *c, const float pelvis[3], bool *can)
{
    int best = -1;
    float best_angle = 1e9f;
    bool best_can = false;
    float dir[3] = { sinf(c->c.facing), 0.0f, cosf(c->c.facing) };
    for (int i = 0; i < ndoors; i++) {
        const Door *d = &doors[i];
        if (d->state == 1) continue;
        float dx = c->c.pos[0] - d->pos[0], dy = c->c.pos[1] - d->pos[1], dz = c->c.pos[2] - d->pos[2];
        if (!(dx * dx + dy * dy + dz * dz < d->radius2)) continue;
        float v[3] = { d->pos[0] - pelvis[0], d->pos[1] - pelvis[1], d->pos[2] - pelvis[2] };
        float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), angle = 0.0f;
        if (len > 0.0f) {
            float dot = (v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]) / len;
            angle = acosf(dot < -1.0f ? -1.0f : dot > 1.0f ? 1.0f : dot);    /* 0x433e00 */
            if (angle > 1.4f) continue;     /* 0x529d80 */
        }
        bool may = may_pass(d, c);
        if (!(d->flags & DOOR_MANUAL)) {
            if (may) continue;
        } else if (may && !best_can) {
            best = i, best_angle = angle, best_can = true;
            continue;
        }
        if (may != best_can && best_can) continue;
        if (angle <= best_angle) best = i, best_angle = angle, best_can = may;
    }
    if (can) *can = best_can;
    return best;
}

/* OBJrDoor_CharacterOpen 0x4c2520: the door opens if the character may pass (the action key, the AI's
   path). */
bool door_action_open(int i, const Chr *c)
{
    if (i < 0 || i >= ndoors || !may_pass(&doors[i], c)) return false;
    open_door(i, c);
    return true;
}

void door_update(void)
{
    for (int i = 0; i < ndoors; i++) update(i);
}

static void mul(const float a[12], const float b[12], float o[12])
{
    float t[12];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 3; r++)
            t[3 * c + r] = a[r] * b[3 * c] + a[3 + r] * b[3 * c + 1] + a[6 + r] * b[3 * c + 2] + (c == 3 ? a[9 + r] : 0.0f);
    memcpy(o, t, sizeof t);
}

const void *door_panel(int i, int k, float m[12])
{
    if (i < 0 || i >= ndoors || k < 0 || k > 1) return NULL;
    const Door *d = &doors[i];
    const Panel *p = &d->panel[k];
    if (!p->live || !p->anim.oban) return NULL;
    /* PHrPhysics_MaintainMatrix's animated case: the frame (of the first panel's animation for
       CTX_FOLLOW), under the element's placement for the doors' animations, then the half turns */
    const ObanPlayer *a = (p->anim_flags & CTX_FOLLOW) ? &d->panel[0].anim : &p->anim;
    if (!oban_matrix(a->oban, a->frame, false, m)) return NULL;
    if (rd32((const uint8_t *)a->oban + OBAN_FLAGS) & OBAN_Z_UP) {
        float place[12];
        for (int j = 0; j < 12; j++) place[j] = rdf(p->element + OBOA_MATRIX + 4 * j);
        mul(place, m, m);
    }
    if (p->anim_flags & CTX_HALF_TURNS) {
        const float pi = 3.14159274f;   /* 0x40490fdb */
        float c = cosf(pi), s = sinf(pi);
        const float ry[12] = { c, 0, -s, 0, 1, 0, s, 0, c, 0, 0, 0 };    /* 0x434a80 */
        const float rz[12] = { c, s, 0, -s, c, 0, 0, 0, 1, 0, 0, 0 };    /* 0x434b00 */
        mul(m, ry, m);
        mul(m, rz, m);
    }
    return tm_link(p->element + OBOA_GEOMETRY);
}

/* OBJiDoor_GetGeometryWidth 0x4c2e60: the class's geometry (its OFGA's first, +0x1c, read once for
   each of its entries) across x, at least 0. */
static float geometry_width(const Door *d)
{
    const uint8_t *ofga = d->cls ? tm_link(d->cls) : NULL;
    float hi = -100000.0f, lo = 100000.0f, box[6], sphere[4];     /* 0xc7c35000, 0x52b0f0 */
    if (ofga && rd32(ofga + 0x14) && m3gm_bounds(tm_link(ofga + 0x1c), box, sphere)) {
        if (box[3] > hi) hi = box[3];
        if (box[0] <= lo) lo = box[0];
    }
    float w = hi - lo;
    return w < 0.0f ? 0.0f : w;
}

static void transform(const float m[12], float p[3])
{
    float x = p[0], y = p[1], z = p[2];
    for (int k = 0; k < 3; k++) p[k] = m[k] * x + m[3 + k] * y + m[6 + k] * z + m[9 + k];    /* 0x4339e0 */
}

static void rotate_y_left(float m[12])
{
    const float pi = 3.14159274f;   /* 0x40490fdb */
    float c = cosf(pi), s = sinf(pi);
    const float ry[12] = { c, 0, -s, 0, 1, 0, s, 0, c, 0, 0, 0 };    /* MUrMatrix_RotateY 0x434d70 */
    mul(ry, m, m);
}

int door_obstruction(int i, uint16_t *id, float out[4][3])
{
    if (i < 0 || i >= ndoors) return 0;
    const Door *d = &doors[i];
    if (id) *id = d->id;
    if (!d->cls || !d->oban) return 0;
    /* OBrAnim_Matrix 0x4395f0 of the door's own animation at the open frame, or the last */
    int frame = d->state == 1 ? (int)open_frame(d) : (int16_t)(frame_count(d) - 1);
    float m[12];
    if (!oban_matrix(d->oban, frame, false, m)) return 0;
    int n = (d->flags & DOOR_DOUBLE) ? 4 : 2;
    float w = geometry_width(d);
    /* the panels' placements (OBJiDoor_SetOSD 0x4c3b30, S+0x104 and S+0x134): moved up by the
       geometry's lowest z (S+0x18) and to its middle (a double door's panels to half its width), then a
       half turn about y for the mirrored one */
    float sb[6] = { 0 };
    {
        const uint8_t *ofga = tm_link(d->cls);
        float sphere[4];
        if (ofga) m3gm_bounds(tm_link(ofga + 0x1c), sb, sphere);
    }
    float lift = -sb[2], mid_x = (sb[3] + sb[0]) * 0.5f, mid_y = (sb[4] + sb[1]) * 0.5f;
    float place[2][12];
    for (int k = 0; k < 2; k++) {
        const float id12[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };     /* 0x434c30 */
        memcpy(place[k], id12, sizeof id12);
    }
    if (n == 4) {
        for (int k = 0; k < 2; k++) {
            place[k][9] = w * 0.5f, place[k][10] = lift, place[k][11] = mid_y;  /* 0x434c90 */
            if (((d->flags & DOOR_MIRROR) != 0) == (k == 0)) rotate_y_left(place[k]);
        }
    } else {
        place[0][9] = mid_x, place[0][10] = lift, place[0][11] = mid_y;
        if (d->flags & DOOR_MIRROR) rotate_y_left(place[0]);
    }
    /* the line across the width; the second panel's mirrored about x (MUrMatrix_BuildRotateX 0x434530) around the frame */
    out[0][0] = w * -0.5f, out[0][1] = 0.0f, out[0][2] = 0.0f;
    out[1][0] = w * 0.5f, out[1][1] = 0.0f, out[1][2] = 0.0f;
    if (n == 4) memcpy(out[2], out[0], sizeof out[0]), memcpy(out[3], out[1], sizeof out[1]);
    transform(m, out[0]), transform(m, out[1]);
    if (n == 4) {
        const float pi = 3.14159274f;
        float c = cosf(pi), s = sinf(pi);
        const float rx[12] = { 1, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0 };
        for (int k = 2; k < 4; k++) transform(rx, out[k]), transform(m, out[k]), transform(rx, out[k]);
    }
    for (int k = 0; k < n; k++) {
        float y = out[k][1];
        out[k][1] = out[k][2], out[k][2] = -y;      /* z up to y up */
    }
    transform(place[0], out[0]), transform(place[0], out[1]);
    if (n == 4) transform(place[1], out[2]), transform(place[1], out[3]);
    for (int k = 0; k < n; k++) out[k][1] -= lift;
    /* the door's rotation (S+0x1d8: 0x437130, order 0, R = Rz Ry Rx of the degrees) */
    const float rad = 0.017453292f;     /* 0x529550 */
    float ci = cosf(d->rot[0] * rad), si = sinf(d->rot[0] * rad);
    float cj = cosf(d->rot[1] * rad), sj = sinf(d->rot[1] * rad);
    float ch = cosf(d->rot[2] * rad), sh = sinf(d->rot[2] * rad);
    const float r[12] = { cj * ch, cj * sh, -sj,
                          sj * si * ch - ci * sh, sj * si * sh + ci * ch, cj * si,
                          sj * ci * ch + si * sh, sj * ci * sh - si * ch, cj * ci, 0, 0, 0 };
    for (int k = 0; k < n; k++) transform(r, out[k]);
    /* a line both of whose ends are 15 or more up (0x529d84) is no obstacle */
    bool high0 = out[0][1] >= 15.0f && out[1][1] >= 15.0f;
    bool high1 = n < 4 || (out[2][1] >= 15.0f && out[3][1] >= 15.0f);
    if (high0) {
        if (high1) return 0;
        memcpy(out[0], out[2], sizeof out[0]), memcpy(out[1], out[3], sizeof out[1]);
        n = 2;
    } else if (high1) {
        n = 2;
    }
    for (int k = 0; k < n; k++)
        for (int j = 0; j < 3; j++) out[k][j] += d->pos[j];
    return n;
}

/* ---- the commands: each takes a door id (the u16 of its value); door_lock and door_unlock take 0xffff
   for every door (OBJiDoor_LockDoors_Enum 0x4c1dd0), the others the first door of the id
   (OBJrDoor_FindDoorWithID, 0x4c1d30) ---- */

static uint16_t arg_id(int argc, const SlValue *argv)
{
    if (argc < 1) return 0;
    return (uint16_t)(argv[0].type == SL_FLOAT ? (int32_t)argv[0].f : argv[0].i);
}

static int find_id(uint16_t id)
{
    for (int i = 0; i < ndoors; i++)
        if (doors[i].id == id) return i;
    return -1;
}

int door_find(uint16_t id) { return find_id(id); }

static bool cmd_open(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; door_open(find_id(arg_id(c, v))); return true; }
static bool cmd_close(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; door_close(find_id(arg_id(c, v))); return true; }

/* OBJrDoor_Jam 0x4c29d0 */
static void jam(uint16_t id, bool on)
{
    int i = find_id(id);
    if (i < 0) return;
    if (on) doors[i].flags |= DOOR_JAMMED;
    else doors[i].flags &= (uint16_t)~DOOR_JAMMED;
}
static bool cmd_jam(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; jam(arg_id(c, v), true); return true; }
static bool cmd_unjam(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; jam(arg_id(c, v), false); return true; }

/* OBJrDoor_Lock_ID 0x4c1d80 and OBJrDoor_Unlock_ID 0x4c1e30 (each also tells 0x4fdac0 at the door's
   place, not ported) */
void door_lock_id(uint16_t id, bool on)
{
    for (int i = 0; i < ndoors; i++) {
        if (doors[i].id != id && id != 0xffff) continue;
        if (on) doors[i].flags |= DOOR_LOCKED;
        else doors[i].flags &= (uint16_t)~DOOR_LOCKED;
    }
}
static bool cmd_lock(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; door_lock_id(arg_id(c, v), true); return true; }
static bool cmd_unlock(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; door_lock_id(arg_id(c, v), false); return true; }

void door_register_commands(void)
{
    physctx_add_source((PhysSource){ ctx_count, ctx_get });
    sl_register("door_open", cmd_open, NULL);
    sl_register("door_close", cmd_close, NULL);
    sl_register("door_lock", cmd_lock, NULL);
    sl_register("door_unlock", cmd_unlock, NULL);
    sl_register("door_jam", cmd_jam, NULL);
    sl_register("door_unjam", cmd_unjam, NULL);
}
