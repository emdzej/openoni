/* The particle system: see particle.h. Format of a class: docs/formats/par3.md; the run time:
   docs/re/particles.md.

   A class is kept as the original keeps it: its definition (the PAR3 block past its tag and size)
   copied once and written by the setup (the variables' offsets, the action and emitter links), with
   the class's own record (0x98 bytes at 0x65c520 in the original) beside it. Where the definition holds
   a pointer at run time, the port holds an index: the emitter's class (+0x40) and an action's payload
   (+0x04). A particle is the original's 0x44-byte header and its data, the emitters' states, the
   blocks its class asks for and its variables, at the offsets the setup computed. */
#include "particle.h"

#include "akev.h"
#include "akev_collide.h"
#include "body.h"
#include "chr_hit.h"
#include "exe.h"
#include "m3gm.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the definition's layout (offsets from the PAR3 block past its tag and size) ---- */
enum {
    D_VERSION = 0x02, D_FLAGS1 = 0x04, D_FLAGS2 = 0x08, D_MASK = 0x0c, D_VARS = 0x10, D_ACTIONS = 0x12,
    D_EMITTERS = 0x14, D_EVENTS = 0x18, D_LIFETIME = 0x58, D_RADIUS = 0x74, D_DODGE = 0x90, D_ALERT = 0x94,
    D_FLYBY = 0x98, D_SCALE = 0xa8, D_YSCALE = 0xc4, D_ROTATION = 0xe0, D_ALPHA = 0xfc, D_INSTANCE = 0x118,
    D_XOFFSET = 0x138, D_XSHORTEN = 0x154, D_TINT = 0x170, D_EDGE_MIN = 0x18c, D_EDGE_MAX = 0x1a8,
    D_CONTRAIL = 0x1c4, D_FLARE_DIST = 0x1e0, D_FLARE_IN = 0x1fc, D_FLARE_OUT = 0x1fe, D_MAX_DECALS = 0x200,
    D_DECAL_FADE = 0x202, D_DECAL_WRAP = 0x204, D_ATTRACTOR = 0x220, D_SELECTOR = 0x224,
    D_ATTRACT_TARGET = 0x228, D_ATTRACT_NAME = 0x22c, D_ATTRACT_DIST = 0x26c, D_ATTRACT_ANGLE = 0x288,
    D_BLOCKS = 0x304,       /* the variables (0x34 each), the actions (0x1a8), the emitters (0x1bc) */
    VAR = 0x34, ACTION = 0x1a8, EMITTER = 0x1bc, VALUE = 0x1c,
    A_TYPE = 0x00, A_PAYLOAD = 0x04, A_VARS = 0x08, A_PARAMS = 0xc8,
    E_CLASS_NAME = 0x00, E_CLASS = 0x40, E_FLAGS = 0x44, E_THRESHOLD = 0x48, E_PROBABILITY = 0x4a,
    E_COUNT = 0x4c, E_LINK = 0x50, E_RATE = 0x54, E_POSITION = 0x58, E_DIRECTION = 0x5c, E_SPEED = 0x60,
    E_ORIENT_DIR = 0x64, E_ORIENT_UP = 0x68, E_PARAMS = 0x6c,
};

/* flags1 (+0x04) */
enum {
    F1_DECORATIVE = 0x01, F1_YSCALE = 0x08, F1_GEOMETRY = 0x100, F1_COLLIDE_WALLS = 0x200,
    F1_COLLIDE_CHARS = 0x400, F1_COLLIDES = 0x600, F1_SCALE_VELOCITY = 0x800,
};
/* flags2 (+0x08) */
enum {
    F2_EXPIRE_CUTSCENE = 0x08, F2_DIE_CUTSCENE = 0x10, F2_LOW_DETAIL = 0x20, F2_MEDIUM_DETAIL = 0x40,
    F2_DECAL = 0x800000, F2_HIDDEN = 0x1000000, F2_INVISIBLE = 0x2000000, F2_EDGE_FADE = 0x4000000,
    F2_VECTOR = 0x8000000, F2_LOCK_TO_LINK = 0x10000000, F2_CONTRAIL_EMITTER = 0x20000000,
    F2_LENSFLARE = 0x40000000, F2_ONE_SIDED = 0x80000000u,
};
/* the particle's flags (+0x04) */
enum {
    P_HIDDEN = 0x01, P_FADING = 0x02, P_STUCK = 0x04, P_CHOPPED = 0x08, P_DEAD = 0x10, P_ORIENT_VELOCITY = 0x20,
    P_DIRTY = 0x40, P_EXPLODE = 0x80, P_SOUND = 0x100, P_MOVED = 0x200, P_TEMPORARY = 0x400, P_ATTACHED = 0x2000,
    P_ATTRACTED = 0x4000, P_GONE = 0x8000, P_EXPIRING = 0x10000,
};
/* the class's record's flags (+0x40) */
enum {
    C_SOUNDS = 0x01, C_TEMPORARIES = 0x02, C_FAILED = 0x10, C_DETAIL_OFF = 0x40, C_SWAP = 0x200,
    C_LISTED = 0x400, C_PENDING = 0x800, C_PENDING_TAIL = 0x1000, C_REMOVE = 0x2000,
};

enum {
    CLASSES_MAX = 0x800, BUCKETS = 4, HEADER = 0x44, DATA_MAX = 256 - HEADER, EMITTERS_MAX = 8,
    CHUNK = 512, CHUNKS = 160,
};
static const uint16_t BUCKET_SIZE[BUCKETS] = { 64, 128, 192, 256 };     /* 0x536020 */
/* The blocks' sizes (0x536028, u32 each): velocity, orientation, offset, the attachment's matrix,
   the previous position, the decal, the texture's index and time, the damage's owner, the contrail,
   the lens flare, the attractor, the collision cache. */
static const uint16_t BLOCK_SIZE[P3_XS] = { 12, 36, 12, 4, 16, 20, 4, 4, 4, 32, 4, 8, 20 };

/* The action types (the table at 0x536060, 0xc0 bytes each: type, kind, name, the function, the
   variables' and parameters' counts, their names and types). */
enum {
    A_LINEAR = 0, A_ACCEL = 1, A_RANDOM = 2, A_PINGPONG = 3, A_LOOPING = 4, A_TO_VALUE = 5, A_COLOR_BLEND = 6,
    A_FADE_OUT = 8, A_ENABLE_ACTION = 9, A_DISABLE_ACTION = 10, A_DIE = 11, A_SET_LIFETIME = 12,
    A_ENABLE_EMITTER = 13, A_DISABLE_EMITTER = 14, A_EMIT = 15, A_CHANGE_CLASS = 16, A_KILL_LAST = 17,
    A_EXPLODE_LAST = 18, A_AMB_SOUND = 20, A_END_AMB_SOUND = 21, A_IMPULSE_SOUND = 23, A_DAMAGE_CHAR = 26,
    A_BLAST = 27, A_EXPLODE = 28, A_DAMAGE_ENV = 29, A_GLASS = 30, A_STOP = 31, A_ROTATE_X = 33,
    A_ROTATE_Y = 34, A_ROTATE_Z = 35, A_FIND_ATTRACTOR = 37, A_ATTRACT_GRAVITY = 38, A_ATTRACT_HOMING = 39,
    A_ATTRACT_SPRING = 40, A_VELOCITY = 47, A_GRAVITY = 48, A_SPIRAL = 49, A_AIR = 50, A_DRIFT = 51,
    A_SET_SPEED = 52, A_SPIRAL_TANGENT = 53, A_KILL_BEYOND = 54, A_CREATE_EFFECT = 55, A_STICK = 56,
    A_BOUNCE = 57, A_CHOP = 59, A_IMPACT = 60, A_UNHIDE = 62, A_HIDE = 63, A_TYPES = 64,
};
/* An action type's kind (+0x04): the update skips the kinds a particle's state rules out
   (0x45b170: a stuck particle 2, a chopped one 4, a collision this step 1). */
static uint32_t action_kind(uint32_t t)
{
    switch (t) {
    case A_SET_LIFETIME: case A_EMIT: case A_CHANGE_CLASS: case A_DAMAGE_CHAR: case A_BLAST: case A_EXPLODE:
    case A_DAMAGE_ENV: case A_GLASS: case A_STOP: case A_CREATE_EFFECT: case A_STICK: case A_BOUNCE:
    case 58: case A_CHOP: case A_IMPACT:
        return 4;
    case A_VELOCITY: case A_KILL_BEYOND:
        return 2;
    case A_GRAVITY: case A_SPIRAL: case A_AIR: case A_DRIFT: case A_SET_SPEED: case A_SPIRAL_TANGENT:
        return 3;
    default:
        return 0;
    }
}
/* The variables (+0x1c) and parameters (+0x1e) each type has, and the variables' types (the +0x20
   descriptors): the actions' setup checks a variable slot against its type. */
static void action_counts(uint32_t t, unsigned *nv, unsigned *np)
{
    static const uint8_t V[A_TYPES] = { [A_LINEAR] = 1, [A_ACCEL] = 2, [A_RANDOM] = 1, [A_PINGPONG] = 2,
        [A_LOOPING] = 1, [A_TO_VALUE] = 1, [A_COLOR_BLEND] = 1, [A_SPIRAL] = 1 };
    static const uint8_t P[A_TYPES] = { [A_LINEAR] = 1, [A_ACCEL] = 1, [A_RANDOM] = 3, [A_PINGPONG] = 3,
        [A_LOOPING] = 3, [A_TO_VALUE] = 2, [A_COLOR_BLEND] = 3, [A_FADE_OUT] = 1, [A_ENABLE_ACTION] = 2,
        [A_DISABLE_ACTION] = 2, [A_SET_LIFETIME] = 1, [A_ENABLE_EMITTER] = 1, [A_DISABLE_EMITTER] = 1,
        [A_EMIT] = 2, [A_CHANGE_CLASS] = 1, [A_KILL_LAST] = 1, [A_EXPLODE_LAST] = 1, [A_AMB_SOUND] = 1,
        [A_IMPULSE_SOUND] = 1, [A_DAMAGE_CHAR] = 6, [A_BLAST] = 8, [A_DAMAGE_ENV] = 1, [A_GLASS] = 2,
        [A_ROTATE_X] = 3, [A_ROTATE_Y] = 3, [A_ROTATE_Z] = 3, [A_FIND_ATTRACTOR] = 1, [A_ATTRACT_GRAVITY] = 3,
        [A_ATTRACT_HOMING] = 3, [A_ATTRACT_SPRING] = 3, [A_GRAVITY] = 1, [A_SPIRAL] = 2, [A_AIR] = 2,
        [A_DRIFT] = 7, [A_SET_SPEED] = 3, [A_SPIRAL_TANGENT] = 3, [A_KILL_BEYOND] = 2, [A_CREATE_EFFECT] = 4,
        [A_BOUNCE] = 2, [A_IMPACT] = 2 };
    *nv = t < A_TYPES ? V[t] : 0;
    *np = t < A_TYPES ? P[t] : 0;
}
static uint32_t action_var_type(uint32_t t, unsigned k)
{
    if (t == A_PINGPONG && k == 1) return 0x1000;
    if (t == A_COLOR_BLEND) return 8;
    return 2;
}
static bool action_known(uint32_t t)
{
    switch (t) {
    case 7: case 19: case 22: case 24: case 25: case 32: case 36: case 41: case 42: case 43: case 44: case 45:
    case 46: case 58: case 61:
        return false;
    default:
        return t < A_TYPES;
    }
}

/* The emitters' subsections (0x53a590 rate, 0x53a800 position, 0x53aba8 direction, 0x53aee8 speed): the
   parameters each kind has, from the subsection's first slot (0, 2, 5, 8). */
static unsigned emitter_params(int section, uint32_t kind)
{
    static const uint8_t RATE[] = { 1, 2, 0, 1, 2 }, POS[] = { 0, 1, 2, 2, 3, 3, 1, 1 },
                         DIR[] = { 0, 0, 2, 2, 3, 3, 0 }, SPEED[] = { 1, 2 };
    switch (section) {
    case 0: return kind < sizeof RATE ? RATE[kind] : 0;
    case 1: return kind < sizeof POS ? POS[kind] : 0;
    case 2: return kind < sizeof DIR ? DIR[kind] : 0;
    case 3: return kind < sizeof SPEED ? SPEED[kind] : 0;
    default: return 0;
    }
}
static const uint8_t EMITTER_FIRST_PARAM[4] = { 0, 2, 5, 8 };
static const uint8_t EMITTER_SECTION_FIELD[4] = { E_RATE, E_POSITION, E_DIRECTION, E_SPEED };

/* ---- little-endian access ---- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* ---- the records ---- */

struct P3Class {
    char name[64];              /* +0x00 */
    uint32_t flags;             /* +0x40: C_* */
    uint32_t size;              /* +0x44 */
    uint8_t *def;               /* +0x48 */
    uint16_t version;           /* +0x4c */
    uint16_t x[P3_XS];          /* +0x5c: the blocks' offsets in the data, 0xffff none */
    uint32_t count;             /* +0x78: the particles alive */
    uint16_t psize;             /* +0x7c: a particle's bytes, with the header */
    uint16_t bucket;            /* +0x7e */
    uint16_t emitters;          /* +0x80 */
    uint16_t index;             /* +0x82 */
    uint16_t prev, next;        /* +0x84, +0x86: the active list (0xffff the end) */
    void *flyby;                /* +0x8c: the flyby sound (not ported) */
    P3Particle *head, *tail;    /* +0x90, +0x94 */
};

struct P3Particle {
    uint16_t cls;               /* +0x00 */
    uint8_t ai_slot, flyby_slot;    /* +0x02, +0x03: 0xff none */
    uint32_t flags;             /* +0x04: P_* */
    uint32_t mask;              /* +0x08: the actions turned off, a bit each */
    float pos[3];               /* +0x0c */
    int32_t sound;              /* +0x18: the ambient sound playing, -1 none */
    uint32_t tick;              /* +0x1c: of the last update */
    float time;                 /* +0x20: seconds */
    float life;                 /* +0x24: left, 0 forever */
    float fade;                 /* +0x28: the fade's length */
    uint32_t handle;            /* +0x2c */
    uint32_t link;              /* +0x30: the particle it follows */
    P3Particle *next, *prev;    /* +0x34, +0x38: the class's list (next: the free list once dead) */
    P3Particle *dnext, *dprev;  /* +0x3c, +0x40: the bucket's list of decorative particles */
    uint8_t v[DATA_MAX];        /* +0x44 */
};

/* An emitter's state in the particle's data (12 bytes each from +0x44). */
enum { ES_COUNT = 0, ES_TIME = 4, ES_LAST = 8, EMITTER_STATE = 12 };

static P3Class classes[CLASSES_MAX];    /* 0x65c520 */
static uint16_t nclasses;               /* 0x6a8666 */
static uint16_t nfailed;                /* 0x6a8668: past the 0x800 */
static P3Class *sorted[CLASSES_MAX];    /* 0x6a8520: by name */
static uint16_t nsorted;                /* 0x6a8524 */
static uint16_t active_head = 0xffff, active_tail = 0xffff;    /* 0x6a8b46, 0x6a8b42 */
static uint16_t pending = 0xffff;       /* 0x6a85c0: activated during an update */
static bool updating;                   /* 0x6a866b */
static bool removed_during;             /* 0x659ced */
static uint32_t last_tick;              /* 0x6a8708 */
static float now;                       /* 0x5e96b4: the update's time */
static uint32_t nlive;                  /* 0x6a8718 */

static const void *env;
static Character **chr_list;            /* 0x6a8740: the characters' contexts to collide with */
static int chr_count;                   /* 0x6a871c */

/* The storage: the original carves 50000-byte blocks (up to 100) into its four sizes and keeps each size's
   free list (0x659c9c: the count, the list, the oldest and newest decorative particle). The port's
   chunks hold particles of the biggest size; a handle names its slot. */
static P3Particle *chunks[CHUNKS];
static uint32_t nslots;
static struct { uint32_t free_count; P3Particle *free; P3Particle *dhead, *dtail; } bucket[BUCKETS];

bool (*p3_damage_hook)(Character *c, const float point[3], const float dir[3], int type, float damage,
                       float stun, float knockback, uint32_t owner, uint32_t handle, bool self_immune);
void (*p3_glass_hook)(uint32_t quad, const float point[3], const float dir[3], float force, float radius);
void (*p3_blast_hook)(const void *env, const float pos[3], int type, float damage, float stun, float knockback,
                     float radius, int falloff, uint32_t owner, Character *exclude, uint32_t handle, bool self_immune);

static uint8_t *vars_of(const P3Class *c) { return c->def + D_BLOCKS; }
static uint8_t *actions_of(const P3Class *c) { return vars_of(c) + rd16(c->def + D_VARS) * VAR; }
static uint8_t *emitters_of(const P3Class *c) { return actions_of(c) + rd16(c->def + D_ACTIONS) * ACTION; }
static uint32_t flags1(const P3Class *c) { return rd32(c->def + D_FLAGS1); }
static uint32_t flags2(const P3Class *c) { return rd32(c->def + D_FLAGS2); }
static uint16_t nemitters(const P3Class *c) { return rd16(c->def + D_EMITTERS); }
static P3Class *class_at(uint32_t index1) { return index1 && index1 <= nclasses ? &classes[index1 - 1] : NULL; }
static P3Class *emitter_class(const uint8_t *e) { return class_at(rd32(e + E_CLASS)); }

/* The particle's block of a kind: NULL when the class has none (the original's tests of the pointer it
   makes from the offset 0xffff, never NULL, are the flag's test before it). */
static void *block(const P3Class *c, P3Particle *p, int x)
{
    return (flags1(c) & (0x1000u << x)) && c->x[x] != 0xffff ? p->v + c->x[x] : NULL;
}
static float *fblock(const P3Class *c, P3Particle *p, int x) { return (float *)block(c, p, x); }

/* ---- maths ---- */
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float *a, const float *b, float *o)   /* MUrVector_CrossProduct 0x433b70 */
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x, o[1] = y, o[2] = z;
}
/* MUrVector_Normalize_ZeroTest 0x4340f0: true (and v untouched) when v is too short to normalise. */
static bool normalize_zero(float *v)
{
    float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l < 1e-6f) return true;
    l = 1.0f / sqrtf(l);
    v[0] *= l, v[1] *= l, v[2] *= l;
    return false;
}
/* MUrMatrix3x3_MultiplyPoint 0x433a40, by columns. */
static void m3_apply(const float *m, const float *in, float *out)
{
    float x = m[0] * in[0] + m[3] * in[1] + m[6] * in[2], y = m[1] * in[0] + m[4] * in[1] + m[7] * in[2],
          z = m[2] * in[0] + m[5] * in[1] + m[8] * in[2];
    out[0] = x, out[1] = y, out[2] = z;
}
/* MUrMatrix_MultiplyPoint 0x4339e0: the 3x4 matrix (by columns, then the place). */
static void m4_apply(const float *m, const float *in, float *out)
{
    m3_apply(m, in, out);
    out[0] += m[9], out[1] += m[10], out[2] += m[11];
}
static void m3_mul(const float *a, const float *b, float *o)     /* MUrMatrix3x3_Multiply 0x4343a0 */
{
    float r[9];
    for (int j = 0; j < 3; j++) m3_apply(a, b + 3 * j, r + 3 * j);
    memcpy(o, r, sizeof r);
}
/* The rounding of 0x4340a0 (MUrUnsignedSmallFloat_To_Uns_Round): 2^23 added, the mantissa's bits. */
static uint32_t round_small(float x)
{
    float t = x + 8388608.0f;
    uint32_t b;
    memcpy(&b, &t, 4);
    return b & 0x7fffff;
}
/* 0x4264e0: the x87's rounding, to the nearest (ties to even). */
static int32_t fround(float x) { return (int32_t)lrintf(x); }
static float urand(void) { return (float)trac_local_random() * (1.0f / 65535.0f); }    /* x 0x52a04c */
static float srand1(void) { return (float)trac_local_random() * (1.0f / 32767.5f) - 1.0f; }   /* x 0x52a050, - 1 */
static float wrap_2pi(float a)
{
    if (a < 0.0f) a += 6.2831855f;
    else if (a >= 6.2831855f) a -= 6.2831855f;
    return a;
}

/* ---- the attachment matrices ---- */

/* The original keeps a pointer to the matrix in the particle (P3_X_ATTACH); the port an id of a table
   of the matrices handed out. */
enum { ATTACH_MAX = 2048 };   /* the weapons', the turrets', the characters' bones (64 x 19) */
static const float *attach[ATTACH_MAX];
static unsigned nattach;
uint32_t p3_attach_id(const float *m)
{
    for (unsigned i = 0; i < nattach; i++)
        if (attach[i] == m) return i + 1;
    if (nattach == ATTACH_MAX) return 0;
    attach[nattach++] = m;
    return nattach;
}
static const float *attach_matrix(uint32_t id) { return id && id <= nattach ? attach[id - 1] : NULL; }

/* ---- the values (a type and a 24-byte union, 0x1c bytes) ---- */

typedef union { float f; uint32_t u; const char *s; } Val;

static const float *bell_table(void)        /* 0x52a05c: the normal distribution's quantiles, 11 */
{
    static float t[12];
    static bool done;
    if (!done) {
        const uint8_t *d = exe_data(0x52a05c, 11 * 4);
        for (int i = 0; i < 11; i++) t[i] = d ? rdf(d + 4 * i) : 0.0f;
        done = true;
    }
    return t;
}

/* The bell curve's draw (0x45ae92): a uniform value in (-0.999, 0.999), its quantile through the table
   (x10, rounded to the nearest and interpolated with the next), signed. */
static float bell(void)
{
    const float *t = bell_table();
    float x = srand1() * 0.999f;        /* 0x52a194 */
    float s = fabsf(x) * 10.0f;         /* 0x52a190 */
    int32_t k = fround(s) & 0xffff;
    if (k > 9) k = 9;
    float f = s - (float)k;
    float r = (1.0f - f) * t[k] + f * t[k + 1];
    return x < 0.0f ? -r : r;
}
/* 0x447c10: the blend of two shades by t (in 256ths, 0x4340c0). */
static uint32_t shade_blend(uint32_t a, uint32_t b, float t)
{
    float tt = t * 256.0f + 8388608.0f;
    uint32_t bits;
    memcpy(&bits, &tt, 4);
    uint32_t i = bits & 0x7fffff, j = 0x100 - i;
    uint32_t ah = a >> 8, bh = b >> 8;
    return (((a & 0xff) * j + (b & 0xff) * i) >> 8 & 0xff) | (((bh & 0xff0000) * i + (ah & 0xff0000) * j) & 0xff000000) |
           (((bh & 0xff) * i + (ah & 0xff) * j) & 0xff00) | (((bh & 0xff00) * i + (ah & 0xff00) * j) & 0xff0000);
}

/* P3iCalculateValue 0x45ad80: a value for the particle (its variables, a draw for the ranges and
   curves). False for a variable missing or outside the particle. The integer types leave out as it is
   (the original returns true and writes nothing). */
static bool value(const P3Class *c, P3Particle *p, const uint8_t *v, Val *out)
{
    const uint8_t *u = v + 4;
    switch (rd32(v)) {
    case 0: {
        uint16_t off = rd16(v + 0x18);
        if (off == 0xffff || (uint32_t)off + HEADER >= c->psize) return false;
        uint8_t *at = p->v + off;
        uint32_t t = rd32(v + 0x14);
        if (t == 4 || t == 0x2000 || t == 0x4000) out->s = (const char *)at;
        else out->u = rd32(at);
        return true;
    }
    case 3: out->f = rdf(u); return true;
    case 4: { float r = urand(); out->f = (rdf(u + 4) - rdf(u)) * r + rdf(u); return true; }
    case 5: { float r = bell(); out->f = r * rdf(u + 4) + rdf(u); return true; }
    case 6: out->s = (const char *)u; return true;
    case 7: case 10: out->u = rd32(u); return true;
    case 8: out->u = shade_blend(rd32(u), rd32(u + 4), urand()); return true;
    case 9: {
        float r = bell();
        uint32_t m = rd32(u), d = rd32(u + 4);
        uint32_t ch[4];
        for (int k = 0; k < 4; k++) {
            float x = (float)(d >> (8 * k) & 0xff) * r + (float)(m >> (8 * k) & 0xff);
            ch[k] = round_small(x) & 0xff;
        }
        out->u = ch[0] | ch[1] << 8 | ch[2] << 16 | ch[3] << 24;
        return true;
    }
    case 11: { float t = now / rdf(u); out->f = (t - floorf(t)) * rdf(u + 4); return true; }
    default: return true;
    }
}
static bool fvalue(const P3Class *c, P3Particle *p, const uint8_t *v, float *out)
{
    Val x;
    x.f = *out;
    if (!value(c, p, v, &x)) return false;
    *out = x.f;
    return true;
}
static const uint8_t *param(const uint8_t *a, unsigned k) { return a + A_PARAMS + VALUE * k; }
static uint32_t penum(const uint8_t *a, unsigned k) { return rd32(param(a, k) + 4); }     /* +0xcc + 0x1c k */
static const uint8_t *eparam(const uint8_t *e, unsigned k) { return e + E_PARAMS + VALUE * k; }

/* ---- the classes ---- */

static int name_cmp(const char *a, const char *b) { return strcmp(a, b); }

/* The name's search: the sorted array, then the classes added since (0x45a030). */
static P3Class *lookup(const char *name)
{
    int lo = 0, hi = nsorted - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1, r = name_cmp(name, sorted[mid]->name);
        if (!r) return sorted[mid];
        if (r < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    for (uint16_t i = nsorted; i < nclasses; i++)
        if (!name_cmp(name, classes[i].name)) return &classes[i];
    return NULL;
}
P3Class *p3_class(const char *name) { return name ? lookup(name) : NULL; }
const char *p3_class_name(const P3Class *c) { return c ? c->name : ""; }
int p3_class_count(void) { return nclasses; }
int p3_live_count(void) { return (int)nlive; }
uint32_t p3_class_def_flags(const P3Class *c, int which) { return which ? flags2(c) : flags1(c); }

/* P3rLoadParticleDefinition 0x459f20: a class's record (the name under 64 characters; 0x800 at most). */
static void load_definition(const char *name, const uint8_t *block, uint32_t size)
{
    if (strlen(name) >= 0x40) return;
    if (nclasses >= CLASSES_MAX) { nfailed++; return; }
    P3Class *c = &classes[nclasses];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->def = malloc(size);
    if (!c->def) return;
    memcpy(c->def, block, size);
    c->size = size;
    c->version = rd16(block + D_VERSION);
    c->index = ++nclasses;
}

/* The size of a variable of the type (0x45ab70). */
static unsigned var_size(uint32_t t)
{
    if (t == 1) return 2;
    if (t == 2 || t == 8) return 4;
    if (t == 4 || t == 0x2000 || t == 0x4000) return 16;
    return t >> 10 & 4;     /* the enums' 0x1000 */
}

/* A variable's name looked up in the class (0x45ff40): its offset and type into the reference, 0xffff
   if none; an empty name a reference to nothing. */
static void resolve_ref(const P3Class *c, uint8_t *ref)
{
    if (!ref[0]) {
        wr16(ref + 0x14, 0xffff);
        wr32(ref + 0x10, 0);
        return;
    }
    const uint8_t *v = vars_of(c);
    for (uint16_t i = 0; i < rd16(c->def + D_VARS); i++, v += VAR)
        if (!strncmp((const char *)v, (const char *)ref, 16)) {
            wr16(ref + 0x14, rd16(v + 0x14));
            wr32(ref + 0x10, rd32(v + 0x10));
            return;
        }
}

/* P3rTraverseVarRef 0x460010: every reference to a variable the class makes: the actions' variables
   and their parameters that are variables, the appearance's values (the scale to the decal's wrap),
   the emitters' parameters of their kinds. */
static void traverse_refs(P3Class *c)
{
    uint8_t *a = actions_of(c);
    for (uint16_t i = 0; i < rd16(c->def + D_ACTIONS); i++, a += ACTION) {
        unsigned nv, np;
        action_counts(rd32(a + A_TYPE), &nv, &np);
        for (unsigned k = 0; k < nv; k++) resolve_ref(c, a + A_VARS + 24 * k);
        for (unsigned k = 0; k < np; k++)
            if (rd32(param(a, k)) == 0) resolve_ref(c, (uint8_t *)param(a, k) + 4);
    }
    static const uint16_t APPEARANCE[] = { D_SCALE, D_YSCALE, D_ROTATION, D_ALPHA, D_XOFFSET, D_XSHORTEN, D_TINT,
                                           D_EDGE_MIN, D_EDGE_MAX, D_CONTRAIL, D_FLARE_DIST, D_DECAL_WRAP };
    for (unsigned k = 0; k < sizeof APPEARANCE / sizeof *APPEARANCE; k++) {
        if (APPEARANCE[k] == D_YSCALE && !(flags1(c) & F1_YSCALE)) continue;
        if (rd32(c->def + APPEARANCE[k]) == 0) resolve_ref(c, c->def + APPEARANCE[k] + 4);
    }
    uint8_t *e = emitters_of(c);
    for (uint16_t i = 0; i < nemitters(c); i++, e += EMITTER)
        for (int s = 0; s < 4; s++) {
            unsigned n = emitter_params(s, rd32(e + EMITTER_SECTION_FIELD[s]));
            for (unsigned k = 0; k < n; k++) {
                uint8_t *v = e + E_PARAMS + VALUE * (EMITTER_FIRST_PARAM[s] + k);
                if (rd32(v) == 0) resolve_ref(c, v + 4);
            }
        }
}

/* P3rPackVariables 0x45fa20 (at the setup, with no particles to move): the emitters' states, then the
   blocks the flags ask for in their order, then the variables; the particle's size with its header. */
static void pack_variables(P3Class *c)
{
    uint32_t f = flags1(c);
    unsigned off = nemitters(c) * EMITTER_STATE;
    for (int x = 0; x < P3_XS; x++) {
        if (f & (1u << (12 + x))) c->x[x] = (uint16_t)off, off += BLOCK_SIZE[x];
        else c->x[x] = 0xffff;
    }
    uint8_t *v = vars_of(c);
    for (uint16_t i = 0; i < rd16(c->def + D_VARS); i++, v += VAR) {
        wr16(v + 0x14, (uint16_t)off);
        off += var_size(rd32(v + 0x10));
    }
    c->psize = (uint16_t)(off + HEADER);
    c->emitters = nemitters(c);
    traverse_refs(c);
}

/* P3iCheckActionListIndices 0x459eb0: each event's list starts where the last ended; an empty list out
   of place is put there. */
static void check_lists(P3Class *c)
{
    uint16_t at = 0;
    for (int ev = 0; ev < 16; ev++) {
        uint8_t *l = c->def + D_EVENTS + 4 * ev;
        if (rd16(l) != at && rd16(l) == rd16(l + 2)) wr16(l, at), wr16(l + 2, at);
        at = rd16(l + 2);
    }
}

/* The default a bad parameter takes (0x45f980), by the slot's type. */
static void default_param(uint32_t type, uint8_t *v)
{
    memset(v, 0, VALUE);
    if (type == 8) { wr32(v, 7); wr32(v + 4, 0xffff0000); }
    else if (type == 0 || type == 1) wr32(v, 1);
    else if (type == 2) wr32(v, 3);
    else if (type == 4 || type == 0x2000 || type == 0x4000) wr32(v, 6);
    else if (type & 0x1000) wr32(v, 10);    /* the enum's default (0x53a400): its first value, 0 */
}

/* P3rUpdateActionData 0x457240: an action's payload: the sound (not ported), the class an effect
   creates, the impact type (not ported). */
static void update_action_data(P3Class *c, uint8_t *a)
{
    wr32(a + A_PAYLOAD, 0);
    uint32_t t = rd32(a + A_TYPE);
    if ((t == A_AMB_SOUND || t == A_IMPULSE_SOUND) && rd32(param(a, 0)) == 6) {
        c->flags |= C_SOUNDS;   /* OSrAmbient_GetByName 0x511670, OSrImpulse_GetByName 0x512440 */
    } else if (t == A_CREATE_EFFECT && rd32(param(a, 0)) == 6) {
        char n[17];
        memcpy(n, param(a, 0) + 4, 16);
        n[16] = 0;
        P3Class *k = lookup(n);
        wr32(a + A_PAYLOAD, k ? k->index : 0);
    }
}

/* The classes' setup (0x4574e0, at the level's start): each class's definition linked and packed, its
   actions checked; the classes sorted (the kept ones before the decorative ones, the failed last), the
   name index made; each emitter's class found; the actions' data. */
static void setup_classes(void)
{
    for (uint16_t i = 0; i < nclasses; i++) {
        P3Class *c = &classes[i];
        /* P3iUpdateVersion 0x458740 brings old versions up to 18: every class on the CD is 18 */
        if (rd16(c->def + D_VERSION) != 18) { c->flags |= C_FAILED; continue; }
        c->count = 0;
        c->head = c->tail = NULL;
        c->prev = c->next = 0xffff;
        if (flags1(c) & 0x200) wr32(c->def + D_FLAGS1, flags1(c) | 0x1000000);  /* walls: the collision cache */
        pack_variables(c);
        c->bucket = 0;
        while (c->bucket < BUCKETS && c->psize > BUCKET_SIZE[c->bucket]) c->bucket++;
        if (c->bucket >= BUCKETS) { c->flags |= C_FAILED; continue; }      /* "is too large" */
        check_lists(c);
        if (flags2(c) & F2_DECAL) wr32(c->def + D_FLAGS1, flags1(c) | F1_DECORATIVE);
        uint8_t *a = actions_of(c);
        for (uint16_t k = 0; k < rd16(c->def + D_ACTIONS); k++, a += ACTION) {
            uint32_t t = rd32(a + A_TYPE);
            unsigned nv, np;
            action_counts(t, &nv, &np);
            for (unsigned j = 0; j < nv; j++) {
                uint8_t *slot = a + A_VARS + 24 * j;
                uint16_t off = rd16(slot + 0x14);
                if (off == 0xffff) continue;
                unsigned sz = var_size(rd32(slot + 0x10));
                if (!sz || c->psize < off + HEADER + sz) {
                    wr16(slot + 0x14, 0xffff);
                    wr32(slot + 0x10, action_var_type(t, j));
                }
            }
            for (unsigned j = 0; j < np; j++) {
                uint8_t *v = (uint8_t *)param(a, j);
                int32_t vt = (int32_t)rd32(v);
                bool bad = vt < 0 || vt > 11;
                if (vt == 0) {
                    uint16_t off = rd16(v + 0x18);
                    unsigned sz = var_size(rd32(v + 0x14));
                    bad = !sz || c->psize < off + HEADER + sz;
                }
                if (bad) default_param(2, v);
            }
            if (!action_known(t)) wr32(a + A_TYPE, A_LINEAR);   /* never on the CD */
        }
    }
    /* the sort (0x51e8b1, qsort with 0x457a70): the port's is stable */
    P3Class tmp[CLASSES_MAX];
    int n = 0;
    for (int pass = 0; pass < 3; pass++)
        for (uint16_t i = 0; i < nclasses; i++) {
            P3Class *c = &classes[i];
            int g = (c->flags & C_FAILED) ? 2 : (flags1(c) & F1_DECORATIVE) ? 1 : 0;
            if (g == pass) tmp[n++] = *c;
        }
    memcpy(classes, tmp, sizeof(P3Class) * (size_t)n);
    nsorted = 0;
    for (uint16_t i = 0; i < nclasses; i++) {
        if (classes[i].flags & C_FAILED) break;
        classes[i].index = (uint16_t)(i + 1);
        sorted[nsorted++] = &classes[i];
    }
    for (uint16_t i = nsorted; i < nclasses; i++) free(classes[i].def), classes[i].def = NULL;
    nclasses = nsorted;
    for (int i = 1; i < nsorted; i++)   /* by name (0x457a30) */
        for (int j = i; j > 0 && name_cmp(sorted[j - 1]->name, sorted[j]->name) > 0; j--) {
            P3Class *t = sorted[j];
            sorted[j] = sorted[j - 1], sorted[j - 1] = t;
        }
    for (uint16_t i = 0; i < nclasses; i++) {
        P3Class *c = &classes[i];
        uint8_t *e = emitters_of(c);
        for (uint16_t k = 0; k < nemitters(c); k++, e += EMITTER) {
            char nm[65];
            memcpy(nm, e + E_CLASS_NAME, 64);
            nm[64] = 0;
            P3Class *t = lookup(nm);
            wr32(e + E_CLASS, t ? t->index : 0);    /* "warning: particle class %s emitter %d: no class %s" */
        }
        uint8_t *a = actions_of(c);
        for (uint16_t k = 0; k < rd16(c->def + D_ACTIONS); k++, a += ACTION) update_action_data(c, a);
    }
}

/* ---- the active list (the classes with particles) ---- */

/* 0x4584d0: a class into the active list: the kept ones at the head, the decorative at the tail; during
   an update into the pending list, added after it (0x4583b0). */
static void activate(P3Class *c, bool tail)
{
    if (c->flags & C_REMOVE) { c->flags &= ~(uint32_t)C_REMOVE; return; }
    uint16_t i = (uint16_t)(c - classes);
    if (updating) {
        c->prev = 0xffff;
        c->next = pending;
        if (pending != 0xffff) classes[pending].prev = i;
        pending = i;
        c->flags |= tail ? C_PENDING_TAIL : C_PENDING;
        return;
    }
    if (!tail) {
        c->prev = 0xffff;
        c->next = active_head;
        if (active_head != 0xffff) classes[active_head].prev = i;
        else active_tail = i;
        active_head = i;
    } else {
        c->next = 0xffff;
        c->prev = active_tail;
        if (active_tail != 0xffff) classes[active_tail].next = i;
        else active_head = i;
        active_tail = i;
    }
    c->flags |= C_LISTED;
}

/* 0x458620: a class out of the active list (its last particle gone); during an update marked, and taken
   out after it. */
static void deactivate(P3Class *c)
{
    if (!(c->flags & C_LISTED)) {
        if (c->prev == 0xffff) pending = c->next;
        else classes[c->prev].next = c->next;
        if (c->next != 0xffff) classes[c->next].prev = c->prev;
        c->prev = c->next = 0xffff;
        c->flags &= ~(uint32_t)(C_LISTED | C_PENDING | C_PENDING_TAIL);
        return;
    }
    if (updating) {
        removed_during = true;
        c->flags |= C_REMOVE;
        return;
    }
    if (c->prev == 0xffff) active_head = c->next;
    else classes[c->prev].next = c->next;
    if (c->next == 0xffff) active_tail = c->prev;
    else classes[c->next].prev = c->prev;
    c->prev = c->next = 0xffff;
    c->flags &= ~(uint32_t)(C_LISTED | C_REMOVE);
}

/* 0x4583b0: after an update, the removals marked during it, then the pending classes into the list. */
static void after_update(void)
{
    updating = false;
    if (removed_during)
        for (uint16_t i = active_head; i != 0xffff;) {
            uint16_t n = classes[i].next;
            if (classes[i].flags & C_REMOVE) {
                classes[i].flags &= ~(uint32_t)C_REMOVE;
                deactivate(&classes[i]);
            }
            i = n;
        }
    removed_during = false;
    while (pending != 0xffff) {
        uint16_t i = pending;
        P3Class *c = &classes[i];
        pending = c->next;
        if (pending != 0xffff) classes[pending].prev = 0xffff;
        uint32_t f = c->flags;
        c->prev = c->next = 0xffff;
        c->flags &= ~(uint32_t)(C_PENDING | C_PENDING_TAIL);
        if (f & (C_PENDING | C_PENDING_TAIL)) activate(c, (f & C_PENDING_TAIL) != 0);
    }
}

/* ---- the particles ---- */

static P3Particle *slot_at(uint32_t s) { return s < nslots ? &chunks[s / CHUNK][s % CHUNK] : NULL; }

uint32_t p3_handle(const P3Particle *p) { return p ? p->handle : 0; }
uint32_t p3_flags(const P3Particle *p) { return p->flags; }
void p3_set_flags(P3Particle *p, uint32_t f) { p->flags = f; }
void p3_set_life(P3Particle *p, float life) { p->life = life; }

/* A handle's particle while it is the same one (the handle's salt) and lives. */
static P3Particle *from_handle(uint32_t h)
{
    if (!(h & 0x80000000u)) return NULL;
    P3Particle *p = slot_at(h >> 14 & 0x1ffff);
    return p && p->handle == h && !(p->flags & P_GONE) ? p : NULL;
}
P3Particle *p3_from_handle(uint32_t h, P3Class **c)
{
    P3Particle *p = from_handle(h);
    if (p && (p->flags & P_DEAD)) p = NULL;
    if (c) *c = p ? &classes[p->cls] : NULL;
    return p;
}

/* P3rNewParticleStorage 0x455d00: a slot of the bucket's size; the port's chunks grow to CHUNKS
   ("particle memory reached global limit"). */
static P3Particle *new_storage(P3Class *c)
{
    if (nslots == CHUNK * CHUNKS) return NULL;
    if (nslots % CHUNK == 0) {
        P3Particle *k = calloc(CHUNK, sizeof *k);
        if (!k) return NULL;
        chunks[nslots / CHUNK] = k;
    }
    uint32_t s = nslots++;
    P3Particle *p = slot_at(s);
    p->handle = 0x80000000u | s << 14 | (uint32_t)(c - classes) << 4 | 1;
    return p;
}

static void end_particle(P3Class *c, P3Particle *p);

/* 0x455ba0: a particle's storage: the bucket's free list, new storage, or (for a kept class, when there
   is no room) the oldest decorative particle of the bucket, killed. Decorative particles are listed. */
static P3Particle *new_particle(P3Class *c)
{
    P3Particle *p = NULL;
    uint16_t b = c->bucket;
    if (bucket[b].free_count) {
        p = bucket[b].free;
        bucket[b].free_count--;
        bucket[b].free = p->next;
        p->handle = (p->handle & 0xffffc00fu) | (uint32_t)(c - classes) << 4;
    }
    if (!p) p = new_storage(c);
    if (!p) {
        if (flags1(c) & F1_DECORATIVE) return NULL;
        p = bucket[b].dhead;
        if (!p) return NULL;
        end_particle(&classes[p->cls], p);
        p->flags |= P_DEAD;
        p->handle = (p->handle & 0xffffc000u) | (uint32_t)(c - classes) << 4 | ((p->handle + 1) & 0xf);
    }
    p->dnext = p->dprev = NULL;
    if (flags1(c) & F1_DECORATIVE) {
        p->dprev = bucket[b].dtail;
        if (bucket[b].dtail) bucket[b].dtail->dnext = p;
        else bucket[b].dhead = p;
        bucket[b].dtail = p;
    }
    return p;
}

/* The free list (the end of 0x456ce0's callers): the handle's salt moves on. */
static void free_particle(P3Class *c, P3Particle *p)
{
    p->flags |= P_DEAD;
    p->handle = (p->handle & ~0xfu) | ((p->handle + 1) & 0xf);
    p->next = bucket[c->bucket].free;
    bucket[c->bucket].free = p;
    bucket[c->bucket].free_count++;
}

static bool global_tint_on;
static uint32_t global_tint;

void p3_global_tint(bool on, uint32_t tint)
{
    global_tint_on = on;
    if (on) global_tint = tint;     /* 0x456fa0 only turns it off */
}

/* 0x456fb0's write: the tint into the class's tint variable (D_TINT +0x18) when its tint is a variable's
   (its type 0) and flags2 bit 0 asks for it. False if the class takes none. */
static bool tint_variable(P3Class *c, P3Particle *p, uint32_t tint)
{
    if (!(flags2(c) & 1) || rd32(c->def + D_TINT) != 0) return false;
    uint16_t off = rd16(c->def + D_TINT + 0x18);
    if (off == 0xffff || (uint32_t)off + 4 > sizeof p->v) return false;
    wr32(p->v + off, tint);
    return true;
}

bool p3_set_tint(P3Class *c, P3Particle *p, uint32_t tint) { return c && p && tint_variable(c, p, tint); }

/* P3rCreateParticle 0x455e40: the header, the lifetime drawn, the emitters (on if the flag says so,
   never emitted), the variables' first values. */
P3Particle *p3_create(P3Class *c, uint32_t tick)
{
    if (!c || (c->flags & C_DETAIL_OFF)) return NULL;
    P3Particle *p = new_particle(c);
    if (!p) return NULL;
    if (c->count == 0) activate(c, (flags1(c) & F1_DECORATIVE) != 0);    /* decorative ones at the tail */
    p->cls = (uint16_t)(c - classes);
    p->flags = 0;
    p->mask = rd32(c->def + D_MASK);
    p->sound = -1;
    p->ai_slot = p->flyby_slot = 0xff;
    if (flags2(c) & F2_HIDDEN) p->flags = P_HIDDEN;
    p->prev = c->tail;
    if (c->tail) c->tail->next = p;
    else c->head = p;
    p->next = NULL;
    c->tail = p;
    c->count++;
    nlive++;
    p->link = 0;
    p->tick = tick;
    p->time = (float)tick * (1.0f / 60.0f);
    Val l = { .f = 0 };
    value(c, p, c->def + D_LIFETIME, &l);
    p->life = l.f;
    memset(p->v, 0, sizeof p->v);
    const uint8_t *e = emitters_of(c);
    for (uint16_t k = 0; k < nemitters(c); k++, e += EMITTER) {
        uint8_t *s = p->v + k * EMITTER_STATE;
        wr16(s + ES_COUNT, (rd32(e + E_FLAGS) & 1) ? 0 : 0xffff);
        float never = 1e9f;
        memcpy(s + ES_TIME, &never, 4);
        wr32(s + ES_LAST, 0);
    }
    const uint8_t *v = vars_of(c);
    for (uint16_t k = 0; k < rd16(c->def + D_VARS); k++, v += VAR) {
        Val x = { .u = 0 };
        uint16_t off = rd16(v + 0x14);
        uint32_t t = rd32(v + 0x10);
        if ((uint32_t)off + HEADER + var_size(t) > c->psize) continue;
        if (t == 4 || t == 0x2000 || t == 0x4000) {
            if (rd32(v + 0x18) == 6) memcpy(p->v + off, v + 0x1c, 16);
            continue;
        }
        if (value(c, p, v + 0x18, &x)) wr32(p->v + off, x.u);
    }
    /* the creation's tint (0x659ce6, 0x6a8660: P3rSetGlobalTint 0x456f90) into a class's tint
       variable that has one (flags2 bit 0) and no other tint value */
    if (global_tint_on) tint_variable(c, p, global_tint);
    return p;
}

/* 0x456ce0's list work: out of the class's and the bucket's lists, the class out of the active list with
   its last particle. */
static void unlink_particle(P3Class *c, P3Particle *p)
{
    if (p->next) p->next->prev = p->prev;
    else c->tail = p->prev;
    if (p->prev) p->prev->next = p->next;
    else c->head = p->next;
    c->count--;
    nlive--;
    if (flags1(c) & F1_DECORATIVE) {
        if (p->dprev) p->dprev->dnext = p->dnext;
        else bucket[c->bucket].dhead = p->dnext;
        if (p->dnext) p->dnext->dprev = p->dprev;
        else bucket[c->bucket].dtail = p->dprev;
    }
    if (c->count == 0) deactivate(c);
}

static bool run_event(P3Class *c, P3Particle *p, int event, const uint8_t *list, float time, float dt, bool collided);

/* 0x456ce0 and its callers' free list: the die event, the callbacks' end (the AI's projectile and the
   flyby: not ported), the decal (not ported), the ambient sound's stop, the particle gone. */
static void end_particle(P3Class *c, P3Particle *p)
{
    run_event(c, p, P3_EV_DIE, c->def + D_EVENTS + 4 * P3_EV_DIE, p->time, 0.0f, false);
    if (p->flags & P_SOUND) {           /* OSrAmbient_Stop 0x511e40 */
        p->sound = -1;
        p->flags &= ~(uint32_t)P_SOUND;
    }
    unlink_particle(c, p);
}
static void kill_now(P3Class *c, P3Particle *p)
{
    if (p->flags & P_DEAD) return;
    end_particle(c, p);
    free_particle(c, p);
}
void p3_kill(P3Class *c, P3Particle *p) { if (c && p) kill_now(c, p); }

/* ---- the collision of the step (0x6a8540, 0x38 bytes; the last step's at 0x659d00) ---- */

typedef struct {
    bool hit;                   /* +0x00 */
    bool wall;                  /* +0x01: a quad, else a character */
    bool other;                 /* +0x02: a physics object's own geometry (not ported) */
    float point[3];             /* +0x04 */
    float normal[3];            /* +0x10 */
    float move[3];              /* +0x1c: the step's move, cut at the hit */
    uint32_t quad;              /* +0x28 */
    Character *chr;             /* +0x2c: the context hit (a character's) */
    float t;                    /* +0x34: when */
} Hit;
static Hit hit, last_hit;
static bool free_move;                  /* 0x6a8720: the step's end position is the move's (no hit) */
static float move_end[3];               /* 0x6a8670 */

/* The characters' parts this update (A+0x2278, from their last frame), and the body spheres (A+0x130). */
typedef struct { const Character *c; float m[CHR_BONES][12]; float sph[CHR_BONES][4]; float body[4]; bool ok; } Parts;
static Parts parts[64];

/* ---- the events ---- */

typedef struct {
    P3Class *c;
    P3Particle *p;
    uint8_t *a;
    uint8_t *var[8];
    float time, dt;
    bool *stop;
} Act;

static bool do_action(Act *x);

/* 0x45b170: the actions of an event's list (the update's turned off by the particle's mask; the kinds
   its state rules out skipped); an action may end the list. True if the particle died. */
static bool run_event(P3Class *c, P3Particle *p, int event, const uint8_t *list, float time, float dt, bool collided)
{
    uint32_t mask = event == P3_EV_UPDATE ? p->mask : 0;
    uint32_t skip = (p->flags & P_STUCK ? 2u : 0u) | (p->flags & P_CHOPPED ? 4u : 0u) | (collided ? 1u : 0u);
    bool stop = false;
    uint16_t from = rd16(list), to = rd16(list + 2);
    uint8_t *a = actions_of(c) + (size_t)from * ACTION;
    for (uint16_t i = from; i < to && i < rd16(c->def + D_ACTIONS); i++, a += ACTION) {
        if (mask & (1u << (i & 31))) continue;
        uint32_t t = rd32(a + A_TYPE);
        if (action_kind(t) & skip) continue;
        Act x = { c, p, a, { 0 }, time, dt, &stop };
        unsigned nv, np;
        action_counts(t, &nv, &np);
        for (unsigned k = 0; k < nv; k++) {
            uint16_t off = rd16(a + A_VARS + 24 * k + 0x14);
            x.var[k] = off == 0xffff ? NULL : p->v + off;
        }
        if (do_action(&x)) return true;
        if (stop) return false;
    }
    return false;
}

/* P3rSendEvent 0x45b2c0 (the create event finds the attractor first, 0x463430, and calls the create
   callback after: not ported). */
bool p3_send_event(P3Class *c, P3Particle *p, int event, float time)
{
    if (!c || !p || event < 0 || event > 15) return false;
    return run_event(c, p, event, c->def + D_EVENTS + 4 * event, time, 0.0f, false);
}

/* ---- positions ---- */

/* P3rGetPositionPtr 0x45abc0: the particle's position. Locked to its link (flags2 0x10000000), the
   position is the link's, followed up the chain; a link gone breaks: the link cleared, the hit's point
   taken (during a collision), the broken-link event, which may kill it (*died). */
static float *position(P3Class *c, P3Particle *p, bool *died)
{
    *died = false;
    P3Class *k = c;
    P3Particle *q = p;
    while ((flags2(k) & F2_LOCK_TO_LINK) && q->link) {
        P3Particle *l = from_handle(q->link);
        if (!l) {
            q->link = 0;
            if (hit.hit) memcpy(p->pos, hit.point, sizeof hit.point);
            *died = run_event(k, q, P3_EV_BROKEN_LINK, k->def + D_EVENTS + 4 * P3_EV_BROKEN_LINK, q->time, 0.0f, false);
            break;
        }
        k = &classes[l->cls];
        q = l;
    }
    if (!*died && q != p) {
        float rp[3];
        p3_real_position(k, q, rp);
        memcpy(p->pos, rp, sizeof rp);
    }
    return p->pos;
}
float *p3_position(P3Class *c, P3Particle *p)
{
    bool died;
    float *r = position(c, p, &died);
    return died ? NULL : r;
}

/* P3rGetRealPosition 0x45acc0: the position, its offset added, through the attachment's matrix. */
bool p3_real_position(P3Class *c, P3Particle *p, float out[3])
{
    bool died;
    const float *pos = position(c, p, &died);
    if (died) return false;
    const float *off = fblock(c, p, P3_X_OFFSET);
    const uint8_t *at = block(c, p, P3_X_ATTACH);
    const float *m = at ? attach_matrix(rd32(at)) : NULL;
    float r[3] = { pos[0], pos[1], pos[2] };
    if (off) r[0] += off[0], r[1] += off[1], r[2] += off[2];
    if (m) m4_apply(m, r, r);
    memcpy(out, r, sizeof r);
    return true;
}
void *p3_block(P3Class *c, P3Particle *p, int x) { return c && p && x >= 0 && x < P3_XS ? block(c, p, x) : NULL; }

/* ---- the lifetime ---- */

/* 0x45f7f0: the lifetime over. A decal with its fade frames (+0x202) and an alpha above 0 fades over
   them; else the particle dies. True if it died. */
static bool life_over(P3Class *c, P3Particle *p)
{
    if (!(p->flags & P_FADING) && rd16(c->def + D_DECAL_FADE)) {
        float a = 0.0f;
        if (!fvalue(c, p, c->def + D_ALPHA, &a)) return false;
        if (a > 0.0f) {
            p->flags |= P_FADING;
            p->life = (float)rd16(c->def + D_DECAL_FADE) * (1.0f / 60.0f);
            p->fade = p->life / a;
            return false;
        }
    }
    kill_now(c, p);
    return true;
}

/* ---- orientations ---- */

/* MUrMatrix3x3_FormOrthonormalBasis: the columns x, y, z. */
static void basis(const float *x, const float *y, const float *z, float *m)
{
    memcpy(m, x, 12), memcpy(m + 3, y, 12), memcpy(m + 6, z, 12);
}

/* An orientation selector's vector (0-5 the parent's axes, 6-11 the world's, 12/13 the velocity and its
   reverse, 14/15 towards and away from the emitter: the reverse of the offset, and the offset). */
static void selector_vector(int s, const float *parent, const float *offset, const float *vel, float *v)
{
    static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (s < 6) {
        const float *m = parent ? parent : ID;
        for (int k = 0; k < 3; k++) v[k] = m[3 * (s / 2) + k] * (s & 1 ? -1.0f : 1.0f);
    } else if (s < 12) {
        for (int k = 0; k < 3; k++) v[k] = ID[3 * ((s - 6) / 2) + k] * (s & 1 ? -1.0f : 1.0f);
    } else if (s < 14) {
        for (int k = 0; k < 3; k++) v[k] = vel[k] * (s == 13 ? -1.0f : 1.0f);
    } else {
        for (int k = 0; k < 3; k++) v[k] = offset[k] * (s == 14 ? -1.0f : 1.0f);
    }
}

/* P3rConstructOrientation 0x45b320: the particle's frame from two selectors, the direction (its y axis)
   and the up (its z), x = y cross z. Two axes of one frame (the parent's or the world's) are taken as
   they are, the up made the next axis when it is the direction's own (+Y for an X direction, +Z for Y,
   +X for Z). Else the vectors are made orthonormal; a direction of a vector too short is the parent's +Y
   (velocity) or +Z (the emitter's). The parent may be NULL (the original reads it; the port's identity). */
static void construct_orientation(int dir, int up, const float *parent, const float *offset, const float *vel, float *out)
{
    static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const float *pm = parent ? parent : ID;
    bool par = dir < 6 && up < 6, world = dir >= 6 && dir < 12 && up >= 6 && up < 12;
    if (par || world) {
        int base = par ? 0 : 6, da = (dir - base) / 2, ua = (up - base) / 2;
        float ds = (dir - base) & 1 ? -1.0f : 1.0f, us = (up - base) & 1 ? -1.0f : 1.0f;
        if (da == ua) ua = (da + 1) % 3, us = 1.0f;
        int xa = 3 - da - ua;
        /* e_da x e_ua = +-e_xa: + when (da, ua) is in the cyclic order */
        float xs = ((ua - da + 3) % 3 == 1 ? 1.0f : -1.0f) * ds * us;
        const float *m = par ? pm : ID;
        for (int k = 0; k < 3; k++) {
            out[k] = m[3 * xa + k] * xs;
            out[3 + k] = m[3 * da + k] * ds;
            out[6 + k] = m[3 * ua + k] * us;
        }
        return;
    }
    float d[3], u[3];
    selector_vector(dir, pm, offset, vel, d);
    if (dir >= 12) {
        if (normalize_zero(d)) memcpy(d, pm + (dir < 14 ? 3 : 6), 12);   /* the parent's +Y, or its +Z */
        else if (dir == 13) { /* the reverse of the normalised velocity */ }
    }
    selector_vector(up, pm, offset, vel, u);
    float x[3];
    cross3(d, u, x);
    float l = dot3(x, x);
    if (fabsf(l) >= 9.536743e-7f) {     /* 0x529d00 */
        l = 1.0f / sqrtf(l);
        x[0] *= l, x[1] *= l, x[2] *= l;
    } else {
        x[0] = -d[2], x[1] = 0.0f, x[2] = d[0];
        l = d[0] * d[0] + x[0] * x[0];
        if (fabsf(l) >= 9.536743e-7f) {
            l = 1.0f / sqrtf(l);
            x[0] *= l, x[2] = d[0] * l;
        } else if (fabsf(d[1]) < 9.536743e-7f) {
            memcpy(out, ID, sizeof ID);
            return;
        } else {
            x[0] = 0.0f, x[1] = 0.0f, x[2] = 1.0f;
        }
    }
    cross3(x, d, u);
    l = 1.0f / sqrtf(dot3(u, u));
    u[0] *= l, u[1] *= l, u[2] *= l;
    basis(x, d, u, out);
}

/* ---- emitting ---- */

static void setup_new(P3Class *k, P3Particle *q, uint32_t tick);

/* P3iEmitParticle 0x45d610: the emitter's burst at the time: its count (the fraction a chance of one
   more), each particle placed (position kind), aimed (direction kind) at its speed, linked, owned,
   oriented, then its create event. The emitter's state keeps the last one. True if the emitting
   particle died (following its link). Not ported: the attractors' emitting (rate 4, direction 6, one per
   attractor), the bodies' surfaces and bones (positions 6 and 7: the parent's place), the weapon's
   inaccuracy (direction 5's centre bias from the owner, WPrParticleInaccuracy 0x51ced0). */
static bool emit(P3Class *c, P3Particle *p, unsigned k, float time)
{
    uint8_t *e = emitters_of(c) + (size_t)k * EMITTER;
    uint8_t *st = p->v + k * EMITTER_STATE;
    P3Class *ec = emitter_class(e);
    if (!ec) return false;
    if ((p->flags & P_EXPIRING) && (flags2(ec) & (F2_EXPIRE_CUTSCENE | F2_DIE_CUTSCENE))) return false;
    if (rd16(e + E_PROBABILITY) != 0xffff && trac_local_random() >= rd16(e + E_PROBABILITY)) return false;
    bool died;
    float *ppos = position(c, p, &died);
    if (died) return true;
    float n = rdf(e + E_COUNT);
    int32_t count = fround(n) & 0xffff;
    float frac = n - (float)count;
    if (frac > 0.001f && urand() < frac) count++;
    uint32_t tick = round_small(time * 60.0f);
    for (int32_t i = 0; i < count; i++) {
        float off[3] = { 0, 0, 0 }, dir[3] = { 0, 1, 0 }, speed = 0.0f, r = 0.0f, a, b;
        float f0 = 0, f1 = 0, f2 = 0;
        switch (rd32(e + E_POSITION)) {
        case 1:     /* line */
            if (fvalue(c, p, eparam(e, 2), &f0)) {
                r = srand1();
                off[0] = f0 * r;
                r = fabsf(r);
            }
            break;
        case 2:     /* circle */
            if (fvalue(c, p, eparam(e, 2), &f0) && fvalue(c, p, eparam(e, 3), &f1)) {
                a = urand() * 6.2831855f;
                r = urand();
                b = (f1 - f0) * r + f0;
                off[0] = cosf(a) * b;
                off[2] = sinf(a) * b;
            }
            break;
        case 3:     /* sphere */
            if (fvalue(c, p, eparam(e, 2), &f0) && fvalue(c, p, eparam(e, 3), &f1)) {
                r = urand();
                b = (f1 - f0) * r + f0;
                float l;
                do {
                    off[0] = srand1(), off[1] = srand1(), off[2] = srand1();
                    l = off[0] * off[0] + off[2] * off[2] + off[1] * off[1];
                } while (l > 1.0f);
                l = 1.0f / sqrtf(l);
                off[0] *= b * l, off[1] *= b * l, off[2] *= b * l;
            }
            break;
        case 4:     /* offset (x, y, z: the slots 2, 3, 4) */
            if (fvalue(c, p, eparam(e, 2), &f0) && fvalue(c, p, eparam(e, 3), &f1) && fvalue(c, p, eparam(e, 4), &f2))
                off[0] = f0, off[1] = f1, off[2] = f2;
            break;
        case 5:     /* cylinder */
            if (fvalue(c, p, eparam(e, 2), &f2) && fvalue(c, p, eparam(e, 3), &f0) && fvalue(c, p, eparam(e, 4), &f1)) {
                a = urand() * 6.2831855f;
                r = urand();
                b = (f1 - f0) * r + f0;
                off[0] = cosf(a) * b;
                off[1] = urand() * f2;
                off[2] = sinf(a) * b;
            }
            break;
        default:    /* point; the bodies' (6, 7): not ported */
            break;
        }
        switch (rd32(e + E_DIRECTION)) {
        case 1: {   /* random */
            float l;
            do {
                dir[0] = srand1(), dir[1] = srand1(), dir[2] = srand1();
                l = dir[2] * dir[2] + dir[1] * dir[1] + dir[0] * dir[0];
            } while (l > 1.0f);
            l = 1.0f / sqrtf(l);
            dir[0] *= l, dir[1] *= l, dir[2] *= l;
            break;
        }
        case 2:     /* cone: the angle, the centre bias (P3mSignedRandom 0x45e990) */
        case 5: {   /* inaccurate: the base angle added to the angle (the owner's inaccuracy: not ported) */
            float ang = 0, bias = 0, base = 0;
            bool ok;
            if (rd32(e + E_DIRECTION) == 2) ok = fvalue(c, p, eparam(e, 5), &ang) && fvalue(c, p, eparam(e, 6), &bias);
            else ok = fvalue(c, p, eparam(e, 5), &base) && fvalue(c, p, eparam(e, 6), &ang) && fvalue(c, p, eparam(e, 7), &bias),
                 ang += base;
            if (!ok) { dir[0] = 0, dir[1] = 1, dir[2] = 0; break; }
            float u = urand();
            if (bias > 0.0f) u = powf(u, bias + 1.0f);      /* pow 0x51f2c0 */
            else if (bias < 0.0f) u = powf(u, 1.0f / (1.0f - bias));
            r = u;      /* the stratified speed's share */
            float th = u * ang * 0.017453292f;
            if (th < 0.0f) th += 6.2831855f;
            else if (th >= 6.2831855f) th -= 6.2831855f;
            float st = sinf(th), ph = urand() * 6.2831855f;
            dir[0] = cosf(ph) * st;
            dir[1] = cosf(th);
            dir[2] = sinf(ph) * st;
            break;
        }
        case 3: {   /* ring: the angle, its offset, each particle its share of the circle */
            float ang, ro;
            if (!fvalue(c, p, eparam(e, 5), &ang) || !fvalue(c, p, eparam(e, 6), &ro)) { dir[0] = 0, dir[1] = 1, dir[2] = 0; break; }
            ang *= 0.017453292f;
            float ph = ro * 0.017453292f + ((float)(i & 0xffff) * 6.2831855f) / (float)(count & 0xffff);
            ph = wrap_2pi(ph), ang = wrap_2pi(ang);
            float s = sinf(ang);
            dir[0] = cosf(ph) * s;
            dir[1] = cosf(ang);
            dir[2] = sinf(ph) * s;
            break;
        }
        case 4:     /* offset (x, y, z) */
            if (!fvalue(c, p, eparam(e, 5), &f0) || !fvalue(c, p, eparam(e, 6), &f1) || !fvalue(c, p, eparam(e, 7), &f2)) {
                dir[0] = 0, dir[1] = 1, dir[2] = 0;
                break;
            }
            dir[0] = f0, dir[1] = f1, dir[2] = f2;
            break;
        default:    /* straight (up); the attractor's: not ported */
            break;
        }
        if (rd32(e + E_SPEED) == 0) fvalue(c, p, eparam(e, 8), &speed);
        else if (rd32(e + E_SPEED) == 1) {
            float s2;
            if (fvalue(c, p, eparam(e, 8), &speed)) {
                if (fvalue(c, p, eparam(e, 9), &s2)) speed = (s2 - speed) * r + speed;
            } else speed = 0.0f;
        }
        P3Particle *q = p3_create(ec, tick);
        if (!q) return false;
        if (rd32(e + E_FLAGS) & 0x40) q->flags |= P_ORIENT_VELOCITY;
        if ((rd32(e + E_FLAGS) & 0x80) && rd32(ec->def + D_TINT) == 0) {   /* inherit the tint */
            Val t;
            uint16_t toff = rd16(ec->def + D_TINT + 0x18);
            if (toff != 0xffff && value(c, p, c->def + D_TINT, &t)) wr32(q->v + toff, t.u);
        }
        if (p->flags & P_TEMPORARY) { q->flags |= P_TEMPORARY; ec->flags |= C_TEMPORARIES; }
        if (p->flags & P_EXPIRING) q->flags |= P_EXPIRING;
        uint32_t link = rd32(e + E_LINK);
        if (link == 1) q->link = p->handle;
        else if (link == 10) q->link = p->link;
        else if (link >= 2 && link - 2 < nemitters(c)) q->link = rd32(p->v + (link - 2) * EMITTER_STATE + ES_LAST);
        else q->link = 0;
        float *porient = fblock(c, p, P3_X_ORIENTATION);
        uint8_t *qowner = block(ec, q, P3_X_OWNER);
        if (qowner) {
            uint8_t *powner = block(c, p, P3_X_OWNER);
            wr32(qowner, powner ? rd32(powner) : 0);
        }
        uint8_t *pat = block(c, p, P3_X_ATTACH), *qat = block(ec, q, P3_X_ATTACH);
        const float *matrix = NULL;
        if (pat) {
            if (qat) {
                wr32(qat, rd32(pat));
                if (p->flags & P_ATTACHED) q->flags |= P_ATTACHED;
            } else matrix = attach_matrix(rd32(pat));
        } else if (qat) wr32(qat, 0);
        float at[3] = { ppos[0], ppos[1], ppos[2] };
        const float *poff = fblock(c, p, P3_X_OFFSET);
        if (poff) at[0] += poff[0], at[1] += poff[1], at[2] += poff[2];
        if (porient) m3_apply(porient, off, off);
        if (matrix) {
            m4_apply(matrix, at, at);
            m3_apply(matrix, off, off);
        }
        float *qpos = position(ec, q, &died);
        if (died) continue;
        float *qoff = fblock(ec, q, P3_X_OFFSET);
        if (qoff) {
            memcpy(qpos, at, 12);
            memcpy(qoff, off, 12);
        } else {
            qpos[0] = at[0] + off[0], qpos[1] = at[1] + off[1], qpos[2] = at[2] + off[2];
        }
        float *qvel = fblock(ec, q, P3_X_VELOCITY);
        if (qvel) {
            if (porient) m3_apply(porient, dir, dir);
            if (matrix) m3_apply(matrix, dir, dir);
            qvel[0] = speed * dir[0], qvel[1] = speed * dir[1], qvel[2] = speed * dir[2];
            const float *pvel = fblock(c, p, P3_X_VELOCITY);
            if ((rd32(e + E_FLAGS) & 0x10) && pvel)
                qvel[0] += pvel[0], qvel[1] += pvel[1], qvel[2] += pvel[2];
        }
        float *qorient = fblock(ec, q, P3_X_ORIENTATION);
        if (qorient) {
            const float *parent = porient;
            float pm[9];
            if (porient && matrix) {
                m3_mul(matrix, porient, pm);
                parent = pm;
            }
            construct_orientation((int)rd32(e + E_ORIENT_DIR), (int)rd32(e + E_ORIENT_UP), parent, off, dir, qorient);
        }
        setup_new(ec, q, tick);
        wr32(st + ES_LAST, q->handle);
        if (run_event(ec, q, P3_EV_CREATE, ec->def + D_EVENTS + 4 * P3_EV_CREATE, time, 0.0f, false)) continue;
    }
    return false;
}

/* The new particle's other blocks (the end of the emitter's, P3iEmitParticle, and the weapon's): a random
   texture index, the texture's start tick, the contrail at its position, the rest cleared. */
static void setup_new(P3Class *k, P3Particle *q, uint32_t tick)
{
    uint8_t *b;
    if ((b = block(k, q, P3_X_TEX_INDEX))) wr32(b, trac_local_random());
    if ((b = block(k, q, P3_X_TEX_TIME))) wr32(b, tick);
    if ((b = block(k, q, P3_X_CONTRAIL))) {
        memset(b, 0, 32);
        memcpy(b, q->pos, 12);
        wr32(b + 24, 0xffffffffu);
    }
    if ((b = block(k, q, P3_X_PREV_POS))) wr32(b, 0);
    if ((b = block(k, q, P3_X_LENSFLARE))) wr32(b, 0);
    if ((b = block(k, q, P3_X_ATTRACTOR))) {   /* the attractor (not ported): none */
        wr32(b, 0xffffffffu);
        wr32(b + 4, q->tick + (uint32_t)(trac_local_random() * 10u / 0xffffu));
    }
    if ((b = block(k, q, P3_X_CACHE))) memset(b, 0, 20);   /* AKrCollision_InitializeSpatialCache 0x441190 */
}

/* ---- collisions ---- */

/* CLrSphere_Ray 0x43c680: the segment from s along d against the sphere: the fraction of d to the
   surface, or 0 (inside) when s is in it. */
static bool sphere_ray(const float *s, const float *d, const float *sph, float *t, bool *inside)
{
    float a = dot3(d, d), w[3] = { s[0] - sph[0], s[1] - sph[1], s[2] - sph[2] };
    float b = 2.0f * dot3(d, w), cc = dot3(w, w) - sph[3] * sph[3];
    float disc = b * b - cc * a * 4.0f;
    if (disc < 0.0f) return false;
    if (cc < 0.0f) { *t = (sqrtf(disc) - b) / (a + a); *inside = true; return true; }
    float r = (-b - sqrtf(disc)) / (a + a);
    if (r < 0.0f) return false;
    *t = r;
    *inside = false;
    return true;
}

/* The character's parts now (A+0x2278) and its body sphere (A+0x130, 0x4e5ff0: the pelvis's sphere grown
   to hold every part's). */
static void make_parts(Parts *x, const Character *c)
{
    x->c = c;
    x->ok = false;
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (bones != CHR_BONES) return;
    BodyMatrix root, m[BODY_MAX_BONES];
    chr_root_matrix(c, root);
    if (!body_matrices(body, root, c->aimed, m)) return;
    for (int b = 0; b < CHR_BONES; b++) {
        memcpy(x->m[b], m[b], sizeof x->m[b]);
        float box[6];
        x->sph[b][0] = x->sph[b][1] = x->sph[b][2] = x->sph[b][3] = 0.0f;
        m3gm_bounds(body_geometry(body, b), box, x->sph[b]);
        float w[3];
        m4_apply(x->m[b], x->sph[b], w);
        if (b == 0) {
            memcpy(x->body, w, 12);
            x->body[3] = x->sph[0][3];
        } else {
            float dx = w[0] - x->body[0], dy = w[1] - x->body[1], dz = w[2] - x->body[2];
            float r = sqrtf(dx * dx + dy * dy + dz * dz) + x->sph[b][3];
            if (r > x->body[3]) x->body[3] = r;
        }
    }
    x->ok = true;
}

/* ONrCharacter_Collide_Ray 0x4f1630 (radius 0) and ONrCharacter_Collide_SphereRay 0x4f1b60: the move
   from s along d (bounded to its length) against the body sphere, then each part's sphere: the part's
   own sphere's centre taken from the part's place unturned (the original subtracts the part's place from
   s and tests the sphere's local centre), its radius by the character's scale. The nearest: its fraction,
   the point, the normal (the ray's: from the turned centre; the point's: the centre's). */
static bool char_ray(const Parts *x, const float *s, const float *d, float radius, float *t_out, float point[3], float normal[3])
{
    if (!x->ok) return false;
    float sph[4] = { x->body[0], x->body[1], x->body[2], x->body[3] + radius }, t;
    bool in;
    if (!sphere_ray(s, d, sph, &t, &in)) return false;
    if (!in && t > 1.0f) return false;
    int best = -1;
    float bt = 1e9f;
    for (int b = 0; b < CHR_BONES; b++) {
        float rel[3] = { s[0] - x->m[b][9], s[1] - x->m[b][10], s[2] - x->m[b][11] };
        float ps[4] = { x->sph[b][0], x->sph[b][1], x->sph[b][2], x->sph[b][3] * 1.0f + radius };    /* C+0x34: 1 */
        if (!sphere_ray(rel, d, ps, &t, &in)) continue;
        if (in) t = 0.0f;
        else if (t > 1.0f) continue;
        if (t < bt) bt = t, best = b;
    }
    if (best < 0) return false;
    *t_out = bt;
    float p[3] = { s[0] + d[0] * bt, s[1] + d[1] * bt, s[2] + d[2] * bt }, cen[3];
    if (radius == 0.0f) {
        cen[0] = x->m[best][9] + x->sph[best][0], cen[1] = x->m[best][10] + x->sph[best][1], cen[2] = x->m[best][11] + x->sph[best][2];
        memcpy(point, p, 12);
        normal[0] = p[0] - cen[0], normal[1] = p[1] - cen[1], normal[2] = p[2] - cen[2];
        float l = 1.0f / sqrtf(dot3(normal, normal));
        normal[0] *= l, normal[1] *= l, normal[2] *= l;
        return true;
    }
    m4_apply(x->m[best], x->sph[best], cen);
    float w[3] = { p[0] - cen[0], p[1] - cen[1], p[2] - cen[2] };
    memcpy(normal, w, 12);
    if (normalize_zero(normal)) {
        normal[0] = -d[0], normal[1] = -d[1], normal[2] = -d[2];
        if (normalize_zero(normal)) normal[0] = 0, normal[1] = 1, normal[2] = 0;
    }
    float r = x->sph[best][3];
    if (sqrtf(dot3(w, w)) < r) memcpy(point, p, 12);
    else {
        float l = r / sqrtf(dot3(w, w));
        point[0] = w[0] * l + cen[0], point[1] = w[1] * l + cen[1], point[2] = w[2] * l + cen[2];
    }
    return true;
}

bool p3_character_ray(const Character *c, const float s[3], const float d[3], float point[3])
{
    Parts x;
    make_parts(&x, c);
    float t, n[3];
    return char_ray(&x, s, d, 0.0f, &t, point, n);
}

/* WPrParticleBelongsToSelf 0x51ce20: the owner is the character itself. */
static bool owned_by(uint32_t owner, const Character *c)
{
    uint32_t k = owner & 0xff000000u;
    return (k == 0x1000000u || k == 0x2000000u) && (owner & 0xffffff) == c->id;
}

/* P3iCheckCollisions 0x45eaa0: the step from t0 to *t1 along the velocity against the walls (a point,
   AKrCollision_Point, flags1 0x200) and the characters (a point or the class's sphere, 0x400): the
   nearest hit cuts the step (*t1) and fills hit; end the position at the step's end. A hit on the same
   wall or character as the last step's waits a tick after it. True if the particle died. */
static bool check_collisions(P3Class *c, P3Particle *p, float t0, float *t1, float end[3])
{
    last_hit = hit;
    hit.hit = false;
    bool died;
    float *pos = position(c, p, &died);
    if (died) return true;
    float dt = *t1 - t0;
    if (dt <= 0.0f) { memcpy(end, pos, 12); return false; }
    const float *vel = fblock(c, p, P3_X_VELOCITY);
    bool still = vel == NULL;
    if (vel) hit.move[0] = vel[0] * dt, hit.move[1] = vel[1] * dt, hit.move[2] = vel[2] * dt;
    else hit.move[0] = hit.move[1] = hit.move[2] = 0.0f;
    if (end) end[0] = pos[0] + hit.move[0], end[1] = pos[1] + hit.move[1], end[2] = pos[2] + hit.move[2];
    bool point = rd32(c->def + D_RADIUS) != 3 || rdf(c->def + D_RADIUS + 4) <= 0.0f;
    float radius = 0.0f;
    if (!point) fvalue(c, p, c->def + D_RADIUS, &radius);
    if (!still && (flags1(c) & F1_COLLIDE_WALLS) && env) {
        static AkevHit h[AKEV_HITS_MAX];     /* (the walk fills the whole leaf: up to the list's size) */
        if (akev_collide_point(env, pos, hit.move, 0x44800, 1, h)) {
            hit.hit = hit.wall = true;
            hit.other = false;
            memcpy(hit.point, h[0].point, 12);
            hit.quad = h[0].quad;
            memcpy(hit.normal, h[0].plane, 12);
            if (dot3(hit.normal, hit.move) > 0.0f)
                hit.normal[0] = -hit.normal[0], hit.normal[1] = -hit.normal[1], hit.normal[2] = -hit.normal[2];
            float l1 = fabsf(hit.move[0]) + fabsf(hit.move[1]) + fabsf(hit.move[2]);
            hit.t = fabsf(l1) >= 0.0009765625f ? (h[0].dist / l1) * dt + t0 : *t1;
        }
    }
    if (flags1(c) & F1_COLLIDE_CHARS) {
        if (!hit.hit) hit.t = 1e9f;
        else {
            float f = (hit.t - t0) / dt;
            hit.move[0] *= f, hit.move[1] *= f, hit.move[2] *= f;
            dt = hit.t - t0;
        }
        const uint8_t *owner = block(c, p, P3_X_OWNER);
        for (int i = 0; dt > 1e-6f && i < chr_count; i++) {
            float t, pt[3], nm[3];
            if (!char_ray(&parts[i], pos, hit.move, point ? 0.0f : radius, &t, pt, nm)) continue;
            t = t * dt + t0;
            if (t < t0) t = t0;
            else if (t > *t1) t = *t1;
            if (t >= hit.t) continue;
            hit.t = t;
            /* leaving its own owner: no hit (the time kept, as the original does) */
            if (owner && owned_by(rd32(owner), chr_list[i]) && dot3(nm, hit.move) > 0.0f) continue;
            memcpy(hit.point, pt, 12);
            memcpy(hit.normal, nm, 12);
            hit.hit = true;
            hit.wall = hit.other = false;
            hit.chr = chr_list[i];
            float f = hit.t - t0;
            if (f < 0.001f) break;
            f /= dt;
            hit.move[0] *= f, hit.move[1] *= f, hit.move[2] *= f;
            dt = hit.t - t0;
        }
    }
    if (!hit.hit) return false;
    if (last_hit.hit && last_hit.wall == hit.wall &&
        (hit.wall ? last_hit.quad == hit.quad : last_hit.chr == hit.chr) && hit.t <= last_hit.t + (1.0f / 60.0f))
        hit.t = last_hit.t + (1.0f / 60.0f);
    if (hit.t <= *t1) {
        if (normalize_zero(hit.normal)) {
            const float *o = fblock(c, p, P3_X_ORIENTATION);
            if (o) memcpy(hit.normal, o + 3, 12);
            else hit.normal[0] = 0, hit.normal[1] = 1, hit.normal[2] = 0;
        }
        *t1 = hit.t;
        return false;
    }
    hit.hit = false;
    return false;
}

/* ---- the update ---- */

/* 0x45cc70: each particle of the class from its time to the update's, in steps: a step ends at the next
   emission (each emitter's rate) or at the update's time; the collisions may end it sooner. In each
   step the update event's actions, the lifetime, the orientation to the velocity, the emission, then the
   hit's event. */
static void process_class(P3Class *c, uint32_t tick)
{
    float tnow = (float)tick * (1.0f / 60.0f), until = tnow - 0.001f;
    bool orient_vel = (flags1(c) & 0x3000) == 0x3000;
    P3Particle *next;
    for (P3Particle *p = c->head; p; p = next) {
        next = p->next;
        float t = p->time, prev_t = 0.0f;
        int last_which = 0xffff;
        hit.hit = false;
        free_move = false;
        bool gone = false;
        while (t < until) {
            float tn = tnow;
            bool emitting = false, collided = false;
            int which = 0;
            free_move = false;
            const uint8_t *e = emitters_of(c);
            for (int k = 0; k < nemitters(c); k++, e += EMITTER) {
                uint8_t *st = p->v + k * EMITTER_STATE;
                if (rd16(st + ES_COUNT) == 0xffff) continue;
                float last = rdf(st + ES_TIME), f0 = 0, f1 = 0;
                bool first = false, instant = false;
                if (last == 1e9f && t < tn) {
                    uint32_t r = rd32(e + E_RATE);
                    if (r == 4 || r == 1) { last = 0.0f; wr32(st + ES_TIME, 0); }
                    else first = true;
                }
                switch (rd32(e + E_RATE)) {
                case 0:
                    if (fvalue(c, p, eparam(e, 0), &f0)) {
                        float ft = f0 + last;
                        if (ft < t) ft = t;
                        if (ft < tn) emitting = true, tn = ft, which = k;
                    }
                    break;
                case 1:
                    if (!fvalue(c, p, eparam(e, 0), &f0) || !fvalue(c, p, eparam(e, 1), &f1)) break;
                    if (f1 <= f0) {
                        float ft = f0 + last;
                        if (ft < t) ft = t;
                        if (ft < tn) emitting = true, tn = ft, which = k;
                    } else {
                        float ft = f0 + last, mt = f1 + last;
                        if (ft <= tn) {
                            if (t < mt) {
                                float base = ft < t ? t : ft;
                                ft = (mt - base) * urand() + base;
                                if (ft < t) ft = t;
                                if (ft < tn) emitting = true, tn = ft, which = k;
                            } else emitting = true, which = k, tn = t;
                        }
                    }
                    break;
                case 2:
                    instant = true;
                    break;
                case 3: {
                    if (!fvalue(c, p, eparam(e, 0), &f0)) break;
                    uint32_t lh = rd32(st + ES_LAST);
                    if (!lh) { instant = true; break; }
                    float here[3], there[3];
                    if (!p3_real_position(c, p, here)) break;
                    P3Particle *l = from_handle(lh);
                    if (l && p3_real_position(&classes[l->cls], l, there)) {
                        float dx = there[0] - here[0], dy = there[1] - here[1], dz = there[2] - here[2];
                        if (f0 * f0 < dx * dx + dy * dy + dz * dz) instant = true;
                    }
                    break;
                }
                default:    /* the attractors' (4): not ported */
                    break;
                }
                if (first) instant = true;
                if (instant && !emitting && t + 0.00016666666f < tn)
                    emitting = true, which = k, tn = t + 0.00016666666f;
            }
            if ((flags1(c) & F1_COLLIDES) && !(p->flags & (P_STUCK | P_CHOPPED))) {
                if (check_collisions(c, p, t, &tn, move_end)) { gone = true; break; }
                if (!hit.hit) free_move = true;
                else collided = true, emitting = false;
            }
            float dt = tn - t;
            if (run_event(c, p, P3_EV_UPDATE, c->def + D_EVENTS, t, dt, collided)) { gone = true; break; }
            free_move = false;
            if (p->life != 0.0f) {
                p->life -= dt;
                if (p->life < 1.0f / 60.0f) {
                    p->life = 0.0f;
                    const uint8_t *l = c->def + D_EVENTS + 4 * P3_EV_LIFETIME;
                    if ((p->flags & (P_FADING | P_CHOPPED)) || rd16(l + 2) <= rd16(l)) { kill_now(c, p); gone = true; break; }
                    if (run_event(c, p, P3_EV_LIFETIME, l, tn + p->life, 0.0f, false)) { gone = true; break; }
                }
            }
            if (orient_vel && (p->flags & (P_ORIENT_VELOCITY | P_DIRTY)) == (P_ORIENT_VELOCITY | P_DIRTY)) {
                float *o = fblock(c, p, P3_X_ORIENTATION), cur[9];
                memcpy(cur, o, sizeof cur);
                construct_orientation(12, 4, cur, NULL, fblock(c, p, P3_X_VELOCITY), o);
            }
            if (emitting) {
                if (emit(c, p, (unsigned)which, tn)) { gone = true; break; }
                if (p->flags & P_DEAD) { gone = true; break; }
                const uint8_t *em = emitters_of(c) + (size_t)which * EMITTER;
                uint8_t *st = p->v + which * EMITTER_STATE;
                if (rd32(em + E_FLAGS) & 2) {
                    uint16_t n = (uint16_t)(rd16(st + ES_COUNT) + 1);
                    if (!(rd32(em + E_FLAGS) & 4)) { if (n == 0xffff) n = 0; }
                    else if (rd16(em + E_THRESHOLD) <= n) n = 0xffff;
                    wr16(st + ES_COUNT, n);
                }
                memcpy(st + ES_TIME, &tn, 4);
                last_which = which;
            }
            if (collided && hit.hit) {
                int ev = (p->flags & P_EXPLODE) ? P3_EV_EXPLODE : hit.wall ? P3_EV_HIT_WALL : P3_EV_HIT_CHAR;
                if (run_event(c, p, ev, c->def + D_EVENTS + 4 * ev, tn, 0.0f, true)) { gone = true; break; }
            }
            t = tn;
            if (tn - prev_t < 0.0001f && which == last_which) t = tn + 1.0f / 60.0f;
            prev_t = t;
        }
        if (gone || (p->flags & P_DEAD)) continue;
        p->tick = tick;
        p->time = tnow;
        /* the update callback (none registered) and the ambient sound's place (OSrAmbient_Update: not ported) */
        p->flags &= ~(uint32_t)P_MOVED;
    }
    hit.hit = false;
}

/* P3rUpdate 0x45c830: once per new tick (or forced): the characters to collide with (the active
   physics contexts that collide), then the gameplay classes from the list's head, or the decorative
   ones from its tail; a decal class's particles age by their lifetimes only. */
void p3_update(uint32_t tick, bool gameplay, Character **chars, int nchars)
{
    if (!nclasses) return;
    if (!gameplay && tick == last_tick) return;
    last_tick = tick;
    chr_list = chars;
    chr_count = nchars > 64 ? 64 : nchars;
    for (int i = 0; i < chr_count; i++) make_parts(&parts[i], chars[i]);
    updating = true;
    pending = 0xffff;
    removed_during = false;
    now = (float)tick * (1.0f / 60.0f);
    if (gameplay) {
        for (uint16_t i = active_head; i != 0xffff;) {
            P3Class *c = &classes[i];
            uint16_t n = c->next;
            if (flags1(c) & F1_DECORATIVE) break;
            if (!(c->flags & C_REMOVE)) process_class(c, tick);
            i = n;
        }
    } else {
        for (uint16_t i = active_tail; i != 0xffff;) {
            P3Class *c = &classes[i];
            uint16_t n = c->prev;
            if (!(flags1(c) & F1_DECORATIVE)) break;
            if (!(c->flags & C_REMOVE)) {
                if (!(flags2(c) & F2_DECAL)) process_class(c, tick);
                else {
                    P3Particle *nx;
                    for (P3Particle *p = c->head; p; p = nx) {
                        nx = p->next;
                        if (p->life != 0.0f && (p->life -= now - p->time) < 1.0f / 60.0f) {
                            p->life = 0.0f;
                            if (life_over(c, p)) continue;
                        }
                        p->tick = tick;
                        p->time = now;
                    }
                }
            }
            i = n;
        }
    }
    after_update();
}

void p3_set_environment(const void *e) { env = e; }

/* The level's end (0x4581d0 and 0x455b20) and P3rInitialize 0x455a50. */
void p3_reset(void)
{
    for (uint16_t i = 0; i < nclasses; i++) free(classes[i].def);
    memset(classes, 0, sizeof classes);
    nclasses = nsorted = nfailed = 0;
    active_head = active_tail = pending = 0xffff;
    for (uint32_t k = 0; k < CHUNKS; k++) free(chunks[k]), chunks[k] = NULL;
    nslots = 0;
    memset(bucket, 0, sizeof bucket);
    nlive = 0;
    nattach = 0;
    last_tick = 0;
    hit.hit = last_hit.hit = false;
}

/* The classes of every loaded file's BINA "3RAP" instances (the binary data's loader for the PAR3 class,
   BDrRegisterClass at P3rInitialize), then their setup. */
void p3_load(void)
{
    p3_reset();
    for (int s = 0; s < tm_file_slots(); s++) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (!in->data || !in->name || strncmp(in->name, "BINA3RAP", 8)) continue;
            size_t rs;
            const uint8_t *raw = tm_raw(in->data, &rs);
            uint32_t size = rd32(in->data), off = rd32(in->data + 4);
            if (!raw || (uint64_t)off + size > rs || size < 8 + D_BLOCKS || memcmp(raw + off, "3RAP", 4)) continue;
            load_definition(in->name + 8, raw + off + 8, size - 8);
        }
    }
    setup_classes();
}

/* ---- the actions (the table's functions, 0x463f60 to 0x467c50) ---- */

static float *fvar(Act *x, int k) { return (float *)x->var[k]; }
static bool pf(Act *x, unsigned k, float *out) { return fvalue(x->c, x->p, param(x->a, k), out); }

/* Rotate X, Y, Z (0x466870, 0x466b30, 0x466de0): the orientation turned about its own axis (space 0) or
   the world's, the velocity with it if asked. */
static void rotate(Act *x, int axis)
{
    Val sp;
    float rate;
    Val rv;
    if (!value(x->c, x->p, param(x->a, 0), &sp) || !pf(x, 1, &rate) || !value(x->c, x->p, param(x->a, 2), &rv)) return;
    float ang = wrap_2pi(rate * x->dt * 0.017453292f), co = cosf(ang), si = sinf(ang);
    int i = (axis + 1) % 3, j = (axis + 2) % 3;
    float *o = fblock(x->c, x->p, P3_X_ORIENTATION);
    uint32_t space = sp.u;
    if (o) {
        float r[9];
        memcpy(r, o, sizeof r);
        if (space == 0) {   /* about the particle's own axis: its other two columns turn */
            for (int k = 0; k < 3; k++) {
                r[3 * i + k] = co * o[3 * i + k] + si * o[3 * j + k];
                r[3 * j + k] = co * o[3 * j + k] - si * o[3 * i + k];
            }
        } else {            /* about the world's axis: each column's two coordinates */
            for (int col = 0; col < 3; col++) {
                r[3 * col + i] = co * o[3 * col + i] - si * o[3 * col + j];
                r[3 * col + j] = co * o[3 * col + j] + si * o[3 * col + i];
            }
        }
        memcpy(o, r, sizeof r);
    } else space = 1;
    float *v = fblock(x->c, x->p, P3_X_VELOCITY);
    if (rv.u && v) {
        float w[3];
        if (space == 0) {   /* into the particle's space and back (0x433a90, 0x433a40) */
            float t[9] = { o[0], o[3], o[6], o[1], o[4], o[7], o[2], o[5], o[8] };
            m3_apply(t, v, w);
        } else memcpy(w, v, 12);
        float a = co * w[i] - si * w[j], b = co * w[j] + si * w[i];
        w[i] = a, w[j] = b;
        if (space == 0) m3_apply(o, w, v);
        else memcpy(v, w, 12);
        x->p->flags |= P_DIRTY;
    }
}

static bool do_action(Act *x)
{
    P3Class *c = x->c;
    P3Particle *p = x->p;
    uint8_t *a = x->a;
    float f0 = 0, f1 = 0, f2 = 0, *v;
    switch (rd32(a + A_TYPE)) {
    case A_LINEAR:
        if ((v = fvar(x, 0)) && pf(x, 0, &f0)) *v += f0 * x->dt;
        return false;
    case A_ACCEL:
        if ((v = fvar(x, 0)) && fvar(x, 1) && pf(x, 0, &f0)) {
            *fvar(x, 1) += f0 * x->dt;
            *v += *fvar(x, 1) * x->dt;
        }
        return false;
    case A_RANDOM:
        if ((v = fvar(x, 0)) && pf(x, 0, &f0) && pf(x, 1, &f1) && pf(x, 2, &f2)) {
            float t = (*v - f0) / (f1 - f0);
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
            float r = (float)trac_local_random() * (1.0f / 65535.0f), d;
            if (t < 0.5f) d = 1.0f - r * t * 4.0f;
            else d = (1.0f - t) * r * 4.0f - 1.0f;
            *v += d * f2 * x->dt;
        }
        return false;
    case A_PINGPONG:
        if ((v = fvar(x, 0)) && x->var[1] && pf(x, 0, &f0) && pf(x, 1, &f1) && pf(x, 2, &f2)) {
            if (rd32(x->var[1]) == 0) {
                *v += f2 * x->dt;
                if (*v > f1) *v = f1, wr32(x->var[1], 1);
            } else {
                *v -= f2 * x->dt;
                if (*v < f0) *v = f0, wr32(x->var[1], 0);
            }
        }
        return false;
    case A_LOOPING:
        if ((v = fvar(x, 0)) && pf(x, 0, &f0) && pf(x, 1, &f1) && pf(x, 2, &f2)) {
            *v += f2 * x->dt;
            if (f2 > 0.0f) { if (*v > f1) *v = f0; }
            else if (f2 < 0.0f && *v < f0) *v = f1;
        }
        return false;
    case A_TO_VALUE:
        if ((v = fvar(x, 0)) && pf(x, 0, &f0) && pf(x, 1, &f1)) {
            *v += f0 * x->dt;
            if (f0 > 0.0f) { if (*v > f1) *v = f1; }
            else if (f0 < 0.0f && *v < f1) *v = f1;
        }
        return false;
    case A_COLOR_BLEND: {
        Val c0, c1;
        if (x->var[0] && value(c, p, param(a, 0), &c0) && value(c, p, param(a, 1), &c1) && pf(x, 2, &f2))
            wr32(x->var[0], shade_blend(c0.u, c1.u, f2));
        return false;
    }
    case A_FADE_OUT:
        if (p->flags & P_FADING) return false;
        if (!pf(x, 0, &f0) || !fvalue(c, p, c->def + D_ALPHA, &f1)) return false;
        if (f1 <= 0.0f) { kill_now(c, p); return true; }
        p->life = f0;
        p->flags |= P_FADING;
        p->fade = f0 / f1;
        return false;
    case A_ENABLE_ACTION: case A_DISABLE_ACTION: {
        Val k;
        if (!value(c, p, param(a, 0), &k) || !pf(x, 1, &f1)) return false;
        bool on = rd32(a + A_TYPE) == A_ENABLE_ACTION ? p->life < f1 : f1 < p->life;
        if (on) p->mask &= ~(1u << (k.u & 31));
        else p->mask |= 1u << (k.u & 31);
        return false;
    }
    case A_DIE:
        kill_now(c, p);
        return true;
    case A_SET_LIFETIME:
        fvalue(c, p, param(a, 0), &p->life);
        return false;
    case A_ENABLE_EMITTER: {
        uint32_t k = penum(a, 0);
        if (k < nemitters(c)) {
            uint8_t *st = p->v + k * EMITTER_STATE;
            float never = 1e9f;
            wr16(st + ES_COUNT, 0);
            memcpy(st + ES_TIME, &never, 4);
            wr32(st + ES_LAST, 0);
        }
        return false;
    }
    case A_DISABLE_EMITTER: {
        uint32_t k = penum(a, 0);
        if (k < nemitters(c)) wr16(p->v + k * EMITTER_STATE + ES_COUNT, 0xffff);
        return false;
    }
    case A_EMIT: {
        uint32_t k = penum(a, 0);
        if (k >= nemitters(c)) return false;
        const uint8_t *em = emitters_of(c) + (size_t)k * EMITTER;
        uint8_t *st = p->v + k * EMITTER_STATE;
        /* the count is the parameter's float as stored (not a value); the fraction is taken from the
           emitter's number read as a float, the original's slip: it never adds one */
        float n = rdf(param(a, 1) + 4), en = rdf(param(a, 0) + 4);
        int32_t cnt = fround(n);
        float frac = en - (float)cnt;
        if (frac > 0.999f) cnt++;
        else if (frac > 0.001f && urand() < frac) cnt++;
        if (!cnt) return false;
        for (int32_t i = 0; i < cnt; i++)
            if (emit(c, p, k, x->time)) return false;
        uint16_t s = rd16(st + ES_COUNT);
        if (s != 0xffff && (rd32(em + E_FLAGS) & 2)) {
            s = (uint16_t)(s + cnt);
            wr16(st + ES_COUNT, s);
            if (!(rd32(em + E_FLAGS) & 4)) { if (s == 0xffff) wr16(st + ES_COUNT, 0); }
            else if (rd16(em + E_THRESHOLD) <= s) wr16(st + ES_COUNT, 0xffff);
        }
        memcpy(st + ES_TIME, &x->time, 4);
        return false;
    }
    case A_CHANGE_CLASS: {
        uint32_t k = penum(a, 0);
        if (k >= nemitters(c)) return false;
        P3Class *nc = emitter_class(emitters_of(c) + (size_t)k * EMITTER);
        if (!nc) return false;
        uint32_t tick = round_small(x->time * 60.0f);
        P3Particle *q = p3_create(nc, tick);
        if (!q) return false;
        q->flags = p->flags & ~(uint32_t)P_FADING;
        q->link = p->link;
        uint8_t *b, *pb;
        if ((b = block(nc, q, P3_X_OWNER))) wr32(b, (pb = block(c, p, P3_X_OWNER)) ? rd32(pb) : 0);
        if ((b = block(nc, q, P3_X_ATTACH))) {
            if ((pb = block(c, p, P3_X_ATTACH))) {
                wr32(b, rd32(pb));
                if (p->flags & P_ATTACHED) q->flags |= P_ATTACHED;
            } else wr32(b, 0);
        }
        bool died;
        float *qp = position(nc, q, &died);
        if (died) return false;
        float *qo = fblock(nc, q, P3_X_OFFSET);
        float *pp = position(c, p, &died);
        if (died) return true;
        const float *po = fblock(c, p, P3_X_OFFSET);
        memcpy(qp, pp, 12);
        if (qo) {
            if (po) memcpy(qo, po, 12);
            else qo[0] = qo[1] = qo[2] = 0.0f;
        } else if (po) qp[0] += po[0], qp[1] += po[1], qp[2] += po[2];
        float *qv = fblock(nc, q, P3_X_VELOCITY), *pv = fblock(c, p, P3_X_VELOCITY);
        if (qv) {
            if (pv) memcpy(qv, pv, 12);
            else qv[0] = qv[1] = qv[2] = 0.0f;
        }
        float *qr = fblock(nc, q, P3_X_ORIENTATION), *pr = fblock(c, p, P3_X_ORIENTATION);
        if (qr) {
            static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            if (pr) memcpy(qr, pr, 36);
            else if (qv) construct_orientation(12, 8, NULL, NULL, qv, qr);
            else memcpy(qr, ID, 36);
        }
        if ((b = block(nc, q, P3_X_TEX_INDEX))) wr32(b, (pb = block(c, p, P3_X_TEX_INDEX)) ? rd32(pb) : trac_local_random());
        if ((b = block(nc, q, P3_X_TEX_TIME))) wr32(b, (pb = block(c, p, P3_X_TEX_TIME)) ? rd32(pb) : tick);
        if ((b = block(nc, q, P3_X_PREV_POS))) wr32(b, 0);
        if ((b = block(nc, q, P3_X_LENSFLARE))) wr32(b, 0);
        if ((b = block(nc, q, P3_X_CACHE))) memset(b, 0, 20);
        run_event(nc, q, P3_EV_CREATE, nc->def + D_EVENTS + 4 * P3_EV_CREATE, x->time, 0.0f, false);
        return false;
    }
    case A_KILL_LAST: case A_EXPLODE_LAST: {
        uint32_t k = penum(a, 0);
        if (k >= nemitters(c)) return false;
        P3Particle *l = from_handle(rd32(p->v + k * EMITTER_STATE + ES_LAST));
        if (!l) return false;
        P3Class *lc = &classes[l->cls];
        if (rd32(a + A_TYPE) == A_KILL_LAST) { kill_now(lc, l); return false; }
        /* the original returns the last one's death as the particle's own */
        return run_event(lc, l, P3_EV_EXPLODE, lc->def + D_EVENTS + 4 * P3_EV_EXPLODE, x->time, 0.0f, false);
    }
    case A_AMB_SOUND: case A_IMPULSE_SOUND:     /* OSrAmbient_Start 0x511730, OSrImpulse_Play 0x512480: not ported */
        return false;
    case A_END_AMB_SOUND:
        p->sound = -1;
        p->flags &= ~(uint32_t)P_SOUND;
        return false;
    case A_DAMAGE_CHAR: {
        if ((p->flags & P_EXPIRING) || !hit.hit || hit.wall || hit.other || !hit.chr) return false;
        float dmg, stun, knock;
        if (!pf(x, 0, &dmg) || !pf(x, 1, &stun) || !pf(x, 2, &knock)) return false;
        int type = 0;
        if (rd32(param(a, 3)) == 10) { int32_t t = (int32_t)rd32(param(a, 3) + 4); if (t >= 0 && t < 7) type = t; }
        bool self_immune = param(a, 4)[4] != 0;
        bool multi = rd32(param(a, 5)) == 10 ? param(a, 5)[4] != 0 : true;
        const uint8_t *ob = block(c, p, P3_X_OWNER);
        uint32_t owner = ob ? rd32(ob) : 0;
        if ((owner & 0xff000000u) == 0x6000000u || (owner & 0xff000000u) == 0x5000000u) owner = 0;
        if (p3_damage_hook && p3_damage_hook(hit.chr, hit.point, hit.move, type, dmg, stun, knock, owner,
                                             multi ? 0 : p->handle, self_immune))
            *x->stop = true;
        return false;
    }
    case A_BLAST: {     /* P3iAction_DamageBlast 0x465f30 */
        float dmg, stun, knock, r;
        if (!pf(x, 0, &dmg) || !pf(x, 1, &stun) || !pf(x, 2, &knock) || !pf(x, 3, &r)) return false;
        int falloff = (int)penum(a, 4), type = 0;
        if (rd32(param(a, 5)) == 10) { int32_t t = (int32_t)penum(a, 5); if (t >= 0 && t < 7) type = t; }
        bool self_immune = param(a, 6)[4] != 0;
        bool damage_env = rd32(param(a, 7)) == 10 ? param(a, 7)[4] != 0 : true;
        const uint8_t *ob = block(c, p, P3_X_OWNER);
        uint32_t owner = ob ? rd32(ob) : 0;
        Character *exclude = hit.hit && !hit.wall ? hit.chr : NULL;     /* the character hit */
        float at[3];
        if (hit.hit) {      /* half a unit out from the hit */
            for (int i = 0; i < 3; i++) at[i] = hit.normal[i] * 0.5f + hit.point[i];
        } else if (!p3_real_position(c, p, at)) return true;
        if (damage_env && env && p3_glass_hook) {
            /* the glass in the radius, pushed along the particle's move when it moves faster than 4 */
            const float *v = fblock(c, p, P3_X_VELOCITY);
            float d[3], *dir = NULL;
            if (v) {
                float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (l > 4.0f) { d[0] = v[0] / l, d[1] = v[1] / l, d[2] = v[2] / l; dir = d; }
            }
            static AkevHit hs[AKEV_HITS_MAX];
            float sph[4] = { at[0], at[1], at[2], r }, mv[3] = { 0, 0, 0 };
            unsigned n = akev_collide_sphere(env, sph, mv, 0x44800, 0xff, hs);
            for (unsigned i = 0; i < n; i++) p3_glass_hook(hs[i].quad, at, dir, dmg, r);
        }
        if (!(p->flags & P_EXPIRING) && p3_blast_hook)
            p3_blast_hook(env, at, type, dmg, stun, knock, r, falloff, owner, exclude, p->handle, self_immune);
        return false;
    }
    case A_EXPLODE:
        return run_event(c, p, P3_EV_EXPLODE, c->def + D_EVENTS + 4 * P3_EV_EXPLODE, x->time, 0.0f, false);
    case A_STOP:
        return run_event(c, p, P3_EV_STOP, c->def + D_EVENTS + 4 * P3_EV_STOP, x->time, 0.0f, false);
    case A_DAMAGE_ENV:
        if (hit.hit && hit.wall && pf(x, 0, &f0) && p3_glass_hook) p3_glass_hook(hit.quad, NULL, NULL, f0, 0.0f);
        return false;
    case A_GLASS: {     /* the glass in the radius (AKrCollision_Sphere) */
        float r;
        if (!pf(x, 0, &f0) || !pf(x, 1, &r)) return false;
        float at[3];
        if (!p3_real_position(c, p, at)) return true;
        if (!env || !p3_glass_hook || f0 == 0.0f) return false;
        static AkevHit hs[AKEV_HITS_MAX];
        float sph[4] = { at[0], at[1], at[2], r }, mv[3] = { 0, 0, 0 };
        unsigned n = akev_collide_sphere(env, sph, mv, 0x44800, 0xff, hs);
        const float *o = fblock(c, p, P3_X_ORIENTATION);
        for (unsigned i = 0; i < n; i++) p3_glass_hook(hs[i].quad, at, o ? o + 3 : NULL, f0, r);
        return false;
    }
    case A_ROTATE_X: rotate(x, 0); return false;
    case A_ROTATE_Y: rotate(x, 1); return false;
    case A_ROTATE_Z: rotate(x, 2); return false;
    case A_FIND_ATTRACTOR: case A_ATTRACT_GRAVITY: case A_ATTRACT_HOMING: case A_ATTRACT_SPRING:
        return false;   /* the attractors: not ported */
    case A_VELOCITY: {
        uint32_t f = flags1(c);
        if (free_move && (f & F1_COLLIDES) && !(f & 0x1c000) && !hit.hit) {
            memcpy(p->pos, move_end, 12);
            p->flags |= P_MOVED;
            return false;
        }
        if ((v = fblock(c, p, P3_X_VELOCITY))) {
            p->pos[0] += x->dt * v[0], p->pos[1] += x->dt * v[1], p->pos[2] += x->dt * v[2];
            p->flags |= P_MOVED;
        }
        return false;
    }
    case A_GRAVITY:
        if ((v = fblock(c, p, P3_X_VELOCITY)) && pf(x, 0, &f0)) {
            v[1] -= 98.0f * f0 * x->dt;     /* 0x52a054 */
            p->flags |= P_DIRTY;
        }
        return false;
    case A_AIR:
        if ((v = fblock(c, p, P3_X_VELOCITY)) && pf(x, 0, &f0) && pf(x, 1, &f1)) {
            float s = sqrtf(dot3(v, v));
            if (s > f1) {
                float k = ((s - f1) * expf(-(f0 * x->dt)) + f1) / s;
                v[0] *= k, v[1] *= k, v[2] *= k;
                p->flags |= P_DIRTY;
            }
        }
        return false;
    case A_DRIFT: {
        float acc, maxs, decay, d[3];
        Val sp;
        if (!(v = fblock(c, p, P3_X_VELOCITY)) || !pf(x, 0, &acc) || !pf(x, 1, &maxs) || !pf(x, 2, &decay) ||
            !pf(x, 3, &d[0]) || !pf(x, 4, &d[1]) || !pf(x, 5, &d[2]) || !value(c, p, param(a, 6), &sp))
            return false;
        const float *o = fblock(c, p, P3_X_ORIENTATION);
        if (sp.u == 0 && o) m3_apply(o, d, d);
        if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) return false;
        float l = 1.0f / sqrtf(dot3(d, d));
        d[0] *= l, d[1] *= l, d[2] *= l;
        float along = dot3(v, d);
        float side[3] = { v[0] - along * d[0], v[1] - along * d[1], v[2] - along * d[2] };
        if (maxs == 0.0f) along = acc * x->dt + along;
        else along = (along - maxs) * expf(-(acc * x->dt)) + maxs;
        if (decay != 0.0f) {
            float k = expf(-(decay * x->dt));
            side[0] *= k, side[1] *= k, side[2] *= k;
        }
        v[0] = along * d[0] + side[0], v[1] = along * d[1] + side[1], v[2] = along * d[2] + side[2];
        p->flags |= P_DIRTY;
        return false;
    }
    case A_SET_SPEED: {
        Val sp, ns;
        if (!(v = fblock(c, p, P3_X_VELOCITY)) || !pf(x, 0, &f0) || !value(c, p, param(a, 1), &sp) ||
            !value(c, p, param(a, 2), &ns))
            return false;
        const float *o = fblock(c, p, P3_X_ORIENTATION);
        if (o && sp.u == 0) {   /* along the particle's y */
            float along = -dot3(v, o + 3);
            float old[3] = { v[0], v[1], v[2] };
            v[0] = o[3] * f0, v[1] = o[4] * f0, v[2] = o[5] * f0;
            if (ns.u == 0) v[0] += along * o[3] + old[0], v[1] += along * o[4] + old[1], v[2] += along * o[5] + old[2];
        } else {                /* MUrVector_SetLength 0x433d60 */
            float s = sqrtf(dot3(v, v));
            if (s > 0.0f) v[0] *= f0 / s, v[1] *= f0 / s, v[2] *= f0 / s;
        }
        p->flags |= P_DIRTY;
        return false;
    }
    case A_SPIRAL: case A_SPIRAL_TANGENT:   /* with the offset (+0x60): the classes of level 1 have none */
        return false;
    case A_KILL_BEYOND: {
        if (!pf(x, 1, &f1)) return false;
        float at[3];
        if (!p3_real_position(c, p, at)) return true;
        float d;
        switch (penum(a, 0)) {
        case 0: d = f1 - at[0]; break;
        case 1: d = at[0] - f1; break;
        case 2: d = f1 - at[1]; break;
        case 3: d = at[1] - f1; break;
        case 4: d = f1 - at[2]; break;
        case 5: d = at[2] - f1; break;
        default: return false;
        }
        if (d < 0.0f) { kill_now(c, p); return true; }
        return false;
    }
    case A_CREATE_EFFECT: {     /* P3rCreateEffect 0x456030 (its decals and wall offsets: in part) */
        P3Class *ec = class_at(rd32(a + A_PAYLOAD));
        if (!ec) return false;
        float at[3], wall_off = 0.0f;
        const float *n = NULL;
        if (hit.hit) {
            memcpy(at, hit.point, 12);
            n = hit.normal;
            if (pf(x, 1, &wall_off)) at[0] += n[0] * wall_off, at[1] += n[1] * wall_off, at[2] += n[2] * wall_off;
        } else if (!p3_real_position(c, p, at)) return true;
        uint32_t tick = round_small(x->time * 60.0f);
        P3Particle *q = p3_create(ec, tick);
        if (!q) return false;
        memcpy(q->pos, at, 12);
        uint8_t *b, *pb;
        if ((b = block(ec, q, P3_X_OWNER))) wr32(b, (pb = block(c, p, P3_X_OWNER)) ? rd32(pb) : 0);
        if ((b = block(ec, q, P3_X_ATTACH))) wr32(b, 0);
        if ((v = fblock(ec, q, P3_X_VELOCITY))) v[0] = v[1] = v[2] = 0.0f;
        if ((v = fblock(ec, q, P3_X_OFFSET))) v[0] = v[1] = v[2] = 0.0f;
        float *qo = fblock(ec, q, P3_X_ORIENTATION);
        if (qo) {
            static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            const float *po = fblock(c, p, P3_X_ORIENTATION), *pv = fblock(c, p, P3_X_VELOCITY);
            if (n) {
                float up[3] = { 0, 1, 0 };
                construct_orientation(12, 8, NULL, NULL, n, qo);    /* the normal its y */
                (void)up;
            } else if (po) memcpy(qo, po, 36);
            else if (pv) construct_orientation(12, 8, NULL, NULL, pv, qo);
            else memcpy(qo, ID, 36);
        }
        setup_new(ec, q, tick);
        run_event(ec, q, P3_EV_CREATE, ec->def + D_EVENTS + 4 * P3_EV_CREATE, x->time, 0.0f, false);
        return false;
    }
    case A_STICK:
        if (!hit.hit) return false;
        {
            bool died;
            float *pp = position(c, p, &died);
            if (died) return true;
            memcpy(pp, hit.point, 12);
        }
        if ((v = fblock(c, p, P3_X_VELOCITY))) {
            v[0] = v[1] = v[2] = 0.0f;
            p->flags = (p->flags & ~(uint32_t)P_ORIENT_VELOCITY) | P_STUCK | P_DIRTY;
        }
        return false;
    case A_BOUNCE: {
        if (!hit.hit) return false;
        bool died;
        float *pp = position(c, p, &died);
        if (died) return true;
        pp[0] = hit.normal[0] * 0.01f + hit.point[0];
        pp[1] = hit.normal[1] * 0.01f + hit.point[1];
        pp[2] = hit.normal[2] * 0.01f + hit.point[2];
        if (!(v = fblock(c, p, P3_X_VELOCITY))) return false;
        float d = dot3(v, hit.normal);
        if (d > 0.0f) return false;
        float k = d * -2.0f / dot3(hit.normal, hit.normal);
        v[0] += k * hit.normal[0], v[1] += k * hit.normal[1], v[2] += k * hit.normal[2];
        if (pf(x, 0, &f0) && pf(x, 1, &f1)) {
            v[0] *= f0, v[1] *= f0, v[2] *= f0;
            if (hit.wall && dot3(v, v) < 1.0f) {
                v[0] = v[1] = v[2] = 0.0f;
                p->flags = (p->flags & ~(uint32_t)P_ORIENT_VELOCITY) | P_STUCK;
            }
            p->tick = round_small(hit.t * 60.0f);
            p->time = hit.t;
            p->flags |= P_DIRTY;
        }
        return false;
    }
    case A_CHOP:
        if ((p->flags & (P_FADING | P_CHOPPED)) || !(v = fblock(c, p, P3_X_VELOCITY))) return false;
        {
            float s = sqrtf(dot3(v, v));
            if (s < 0.01f) return false;
            fvalue(c, p, c->def + D_SCALE, &f0);
            p->flags |= P_CHOPPED;
            p->life = p->fade = (f0 + f0) / s;
        }
        return false;
    case A_IMPACT:      /* ONrImpactEffect 0x4fe5b0: the sounds and particles of the material hit: not ported */
        return false;
    case A_UNHIDE: p->flags &= ~(uint32_t)P_HIDDEN; return false;
    case A_HIDE: p->flags |= P_HIDDEN; return false;
    default:
        return false;
    }
}

/* ---- drawing ---- */

void p3_each_drawn(P3DrawFn fn, void *user)
{
    for (uint16_t i = active_head; i != 0xffff; i = classes[i].next) {
        P3Class *c = &classes[i];
        /* P3iDisplayParticleClass 0x460520: not the decals nor the invisible (flags2 0x2800000); those of
           0x100000 only with a debug switch (0x6a8b40, off) */
        if (flags2(c) & (F2_INVISIBLE | F2_DECAL | 0x100000)) continue;
        for (P3Particle *p = c->head; p; p = p->next) {
            if (p->flags & (P_HIDDEN | P_DEAD)) continue;
            P3Draw d;
            memset(&d, 0, sizeof d);
            d.cls = c, d.p = p;
            if (!p3_real_position(c, p, d.pos)) continue;
            d.orient = fblock(c, p, P3_X_ORIENTATION);
            d.velocity = fblock(c, p, P3_X_VELOCITY);
            d.scale = 1, d.yscale = 1, d.alpha = 1;
            fvalue(c, p, c->def + D_SCALE, &d.scale);
            /* the scale grows with the speed (flags1 0x800: |v| + 1 times) */
            if ((flags1(c) & F1_SCALE_VELOCITY) && d.velocity)
                d.scale *= sqrtf(dot3(d.velocity, d.velocity)) + 1.0f;
            d.yscale = d.scale;
            if (flags1(c) & F1_YSCALE) fvalue(c, p, c->def + D_YSCALE, &d.yscale);
            fvalue(c, p, c->def + D_ALPHA, &d.alpha);
            if ((p->flags & P_FADING) && p->fade > 0.0f) d.alpha = p->life / p->fade;
            if ((p->flags & P_CHOPPED) && p->fade > 0.0f) d.chop = 1.0f - p->life / p->fade;
            fvalue(c, p, c->def + D_ROTATION, &d.rotation);
            fvalue(c, p, c->def + D_XOFFSET, &d.x_offset);
            fvalue(c, p, c->def + D_XSHORTEN, &d.x_shorten);
            Val t = { .u = 0xffffffffu };
            value(c, p, c->def + D_TINT, &t);
            d.tint = t.u;
            char nm[33];
            memcpy(nm, c->def + D_INSTANCE, 32);
            nm[32] = 0;
            d.geometry = (flags1(c) & F1_GEOMETRY) != 0;
            d.vector = (flags2(c) & F2_VECTOR) != 0;
            d.texture = nm[0] ? tm_instance_get(d.geometry ? TM_TAG("M3GM") : TM_TAG("TXMP"), nm) : NULL;
            d.mode = (int)(flags1(c) >> 5 & 7);
            uint8_t *b;
            if ((b = block(c, p, P3_X_TEX_INDEX))) d.tex_index = rd32(b);
            if ((b = block(c, p, P3_X_TEX_TIME))) d.tex_time = rd32(b);
            fvalue(c, p, c->def + D_EDGE_MIN, &d.edge_min);
            fvalue(c, p, c->def + D_EDGE_MAX, &d.edge_max);
            d.edge_fade = (flags2(c) & F2_EDGE_FADE) != 0;
            d.one_sided = (flags2(c) & F2_ONE_SIDED) != 0;
            fn(&d, user);
        }
    }
}

/* ---- the script commands ---- */

void p3_register_commands(void) {}

/* P3rCreateEffect 0x456030, its common part: a particle of the class at pos, moving at vel, turned by
   orient (3x3, NULL: along the velocity), at the tick, its create event. Not ported: the decals, the
   attachment to a character's part, the wall's offset kinds. */
P3Particle *p3_effect(P3Class *c, const float pos[3], const float vel[3], const float *orient, uint32_t tick)
{
    return p3_effect_owned(c, pos, vel, orient, NULL, 0, tick);
}

P3Particle *p3_effect_owned(P3Class *c, const float pos[3], const float vel[3], const float *orient,
                            const float *attach_m, uint32_t owner, uint32_t tick)
{
    P3Particle *q = c ? p3_create(c, tick) : NULL;
    if (!q) return NULL;
    memcpy(q->pos, pos, 12);
    uint8_t *b;
    float *v;
    if ((b = block(c, q, P3_X_OWNER))) wr32(b, owner);
    if ((b = block(c, q, P3_X_ATTACH))) wr32(b, attach_m ? p3_attach_id(attach_m) : 0);
    if ((v = fblock(c, q, P3_X_OFFSET))) v[0] = v[1] = v[2] = 0.0f;
    if ((v = fblock(c, q, P3_X_VELOCITY))) {
        if (vel) memcpy(v, vel, 12);
        else v[0] = v[1] = v[2] = 0.0f;
    }
    if ((v = fblock(c, q, P3_X_ORIENTATION))) {
        static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        if (orient) memcpy(v, orient, 36);
        else if (vel) construct_orientation(12, 8, NULL, NULL, vel, v);
        else memcpy(v, ID, 36);
    }
    setup_new(c, q, tick);
    run_event(c, q, P3_EV_CREATE, c->def + D_EVENTS + 4 * P3_EV_CREATE, (float)tick * (1.0f / 60.0f), 0.0f, false);
    return (q->flags & P_DEAD) ? NULL : q;
}
uint32_t p3_tick(void) { return last_tick; }
