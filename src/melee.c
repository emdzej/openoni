/* The AI's melee: see melee.h. */
#include "melee.h"
#include "gametime.h"

#include "chars.h"
#include "cheat.h"
#include "chr_anim.h"
#include "exe.h"
#include "localpath.h"
#include "chr_intersect.h"
#include "combat.h"
#include "ui.h"
#include "throw.h"
#include "weapon.h"
#include "chr_hit.h"
#include "akev_collide.h"
#include "particle.h"
#include "objc.h"
#include "persist.h"
#include "tm.h"
#include "trac.h"

#include <math.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f, HALF_PI = 1.5707963705062866f;

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }
static float wrap_pi(float a) { return a < -PI ? a + TWO_PI : a > PI ? a - TWO_PI : a; }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* ---- the tables of the exe ---- */

/* The attack moves (0x540a88, 0x38 bytes each, ended by -1 at +0x20): a name (32), the melee state it
   is done from and goes to (bytes at +0x20, +0x21), its direction (bits 16..19 of +0x20), the
   animation type (+0x24), and what AI2rMelee_Initialize fills from the attacks' table (0x551c00): the
   type it must follow (+0x26), the keys held (+0x28) and pressed (+0x30). */
typedef struct {
    uint8_t from, to, dir;
    uint16_t type, prev;
    uint64_t held, down;
} AttackMove;

enum { ATTACK_MOVES_VA = 0x540a88, ATTACK_MOVE_SIZE = 0x38, ATTACK_MOVES_MAX = 128, ATTACK_KEYS_VA = 0x551c00 };
static AttackMove attacks[ATTACK_MOVES_MAX];
static int nattacks;                                    /* DAT_005eb8ac */

/* The positions (0x5435b0, 0x40 bytes each, 20): a name (32), the direction (+0x20: 0 none, 1 forward,
   2 back, 3 left, 4 right), whether it takes distances (+0x24), its states. */
enum { POSITIONS_VA = 0x5435b0, POSITION_SIZE = 0x40, POSITIONS = 0x14 };

/* The animation states' melee states (AI2rMelee_Initialize from the pairs at 0x52ac50). */
static uint8_t melee_state_of[0x46];                    /* DAT_006377e0 */

void melee_initialize(void)
{
    nattacks = 0;
    for (uint32_t va = ATTACK_MOVES_VA; nattacks < ATTACK_MOVES_MAX; va += ATTACK_MOVE_SIZE) {
        uint32_t packed = exe_u32(va + 0x20);
        if (packed == 0xffffffffu) break;
        AttackMove *m = &attacks[nattacks++];
        memset(m, 0, sizeof *m);
        m->from = (uint8_t)packed;
        m->to = (uint8_t)(packed >> 8);
        m->dir = (uint8_t)(packed >> 16 & 0xf);
        m->type = (uint16_t)exe_u32(va + 0x24);
        m->held = (uint64_t)exe_u32(va + 0x28) | (uint64_t)exe_u32(va + 0x2c) << 32;
        m->down = (uint64_t)exe_u32(va + 0x30) | (uint64_t)exe_u32(va + 0x34) << 32;
        /* the attack of that type in the attacks' table (0x18 bytes each: keys pressed, keys held,
           the type to follow and the type), ended by no keys */
        for (uint32_t k = ATTACK_KEYS_VA;; k += 0x18) {
            uint32_t lo = exe_u32(k), hi = exe_u32(k + 4);
            if (!lo && !hi) break;
            if ((uint16_t)(exe_u32(k + 0x10) >> 16) != m->type) continue;
            m->prev = (uint16_t)exe_u32(k + 0x10);
            m->held |= (uint64_t)exe_u32(k + 8) | (uint64_t)exe_u32(k + 0xc) << 32;
            m->down |= (uint64_t)lo | (uint64_t)hi << 32;
            break;
        }
    }
    memset(melee_state_of, 0, sizeof melee_state_of);
    for (uint32_t va = 0x52ac50;; va += 8) {
        uint32_t st = exe_u32(va) & 0xffff;
        if (st == 0xffff) break;
        if (st < sizeof melee_state_of) melee_state_of[st] = (uint8_t)exe_u32(va + 4);
    }
}

/* ---- the profiles ---- */

enum { PROFILES_MAX = 64 };
static MeleeProfile profiles[PROFILES_MAX];
static int nprofiles;

/* OBJiMelee_Read 0x4c6c10 (version 39): the id, the name and the class (64 each), the dodge chances
   (+0x84..+0x90), the block chances alone and in a group (+0x94, +0x98), six factors, the throw
   danger, the dazed times, the counts of attacks, evades, maneuvers and moves (+0xbc..+0xc8), the
   techniques (0x58 bytes each), the moves (16). The profile in memory keeps the disk's offsets. */
static bool add_profile(const ObjcObject *o, void *user)
{
    (void)user;
    if (nprofiles == PROFILES_MAX || o->size < 0xcc) return true;
    const uint8_t *d = o->data;
    MeleeProfile *p = &profiles[nprofiles];
    memset(p, 0, sizeof *p);
    p->id = rd32(d);
    memcpy(p->name, d + 4, 63);
    memcpy(p->cls, d + 0x44, 63);
    for (int i = 0; i < 4; i++) p->dodge[i] = rd32(d + 0x84 + 4 * i);
    p->block_single = rd32(d + 0x94);
    p->block_group = rd32(d + 0x98);     /* from version 0x26; before, the single chance */
    for (int i = 0; i < 6; i++) p->factor[i] = rdf(d + 0x9c + 4 * i);
    p->throw_danger = rdf(d + 0xb4), p->extra = rd32(d + 0xb8);
    p->nattack = rd32(d + 0xbc);
    p->nevade = rd32(d + 0xc0);
    p->nmaneuver = rd32(d + 0xc4);
    p->nmoves = rd32(d + 0xc8);
    uint32_t nt = p->nattack + p->nevade + p->nmaneuver;
    if (nt > MELEE_TECH_MAX || p->nmoves > MELEE_MOVE_MAX || 0xcc + 0x58u * nt + 16u * p->nmoves > o->size) return true;
    const uint8_t *t = d + 0xcc;
    for (uint32_t i = 0; i < nt; i++, t += 0x58) {
        MeleeTech *k = &p->tech[i];
        memcpy(k->name, t, 63);
        k->flags = rd32(t + 0x40);
        k->weight = rd32(t + 0x44);
        k->importance = rd32(t + 0x48);
        k->delay = rd32(t + 0x4c);
        k->count = rd32(t + 0x50);
        k->first = rd32(t + 0x54);
    }
    for (uint32_t i = 0; i < p->nmoves; i++, t += 16) {
        p->move[i].type = rd32(t);
        for (int j = 0; j < 3; j++) p->move[i].p[j] = rdf(t + 4 + 4 * j);
    }
    nprofiles++;
    return true;
}

int melee_load(void)
{
    nprofiles = 0;
    objc_each("Melee Profile", add_profile, NULL);
    return nprofiles;
}

const MeleeProfile *melee_find(uint32_t id)
{
    for (int i = 0; i < nprofiles; i++)
        if (profiles[i].id == id) return &profiles[i];
    return NULL;
}

static const AttackMove *attack_of(uint32_t type)
{
    uint32_t i = type & 0xfffffffu;
    return (type >> 28) == MELEE_ATTACK && i < (uint32_t)nattacks ? &attacks[i] : NULL;
}


/* The melee states' animation states and types (0x52ab50, 0x52ab74: u16s, 18 each). */
static uint16_t mstate_anim_state(uint32_t st) { return st < 18 ? exe_u16(0x52ab50 + 2 * st) : 0; }
static uint16_t mstate_anim_type(uint32_t st) { return st < 18 ? exe_u16(0x52ab74 + 2 * st) : 0; }

static int16_t rd16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* 0x4292d0: the animation's position point at a frame (TRAM +0x28: s16 x, z, ... 8 bytes a frame), in
   hundredths; false if it has none */
static bool position_point(const void *tram, uint16_t frame, float out[2])
{
    const uint8_t *q = tr_raw_block(tram, 0x28, 8u * frame, 4);
    if (!q) return false;
    out[0] = (float)rd16s(q) * 0.01f, out[1] = (float)rd16s(q + 2) * 0.01f;
    return true;
}

/* TRrAnimation_TestAttackFlag 0x428980 with every attack (0xffff): any of the attacks' flags (+8) */
static bool any_attack_flag(const void *tram, unsigned bit)
{
    const uint8_t *k;
    for (unsigned i = 0; (k = tr_attack(tram, i)); i++)
        if (rd32(k + 8) & (1u << bit)) return true;
    return false;
}

/* 0x428900: the attacks' most damage (+0xc) */
static uint32_t attack_damage(const void *tram)
{
    uint32_t m = 0;
    const uint8_t *k;
    for (unsigned i = 0; (k = tr_attack(tram, i)); i++)
        if (rd16(k + 0xc) >= m) m = rd16(k + 0xc);
    return m;
}

/* 0x429220: an attack that stuns a blocker for 15 frames or more (+0x16); 0x429260: one that staggers
   (+0x18) */
static bool block_stuns(const void *tram)
{
    const uint8_t *k;
    for (unsigned i = 0; (k = tr_attack(tram, i)); i++)
        if (rd16(k + 0x16) >= 0xf) return true;
    return false;
}
static bool staggers(const void *tram)
{
    const uint8_t *k;
    for (unsigned i = 0; (k = tr_attack(tram, i)); i++)
        if (rd16(k + 0x18) > 0) return true;
    return false;
}

/* 0x428930: the throw's damages (TRAM +0x10, u16 every 4 bytes, the count at +0x17b), added */
static uint32_t throw_damage(const void *tram)
{
    uint32_t n = ((const uint8_t *)tram)[0x17b], sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *q = tr_raw_block(tram, 0x10, 4u * i, 2);
        if (q) sum += rd16(q);
    }
    return sum;
}

/* TRrAnimation_GetThrowDistance 0x4291c0 */
static float throw_distance_of(const void *tram)
{
    const uint8_t *t = tr_raw_block(tram, TRAM_THROW, 0, 0x18);
    return t ? rdf(t + 0x10) : 0.0f;
}

/* 0x4e36b0: a throw from the front (the animation types 0x6a, 0x6b, 0x6e, 0x70) */
static bool throw_from_front(uint16_t type) { return type == 0x6a || type == 0x6b || type == 0x6e || type == 0x70; }

/* A category's look up (its function 3 in the table at 0x52a9e8: 0x4a5430 attack, 0x4a6ec0 maneuver,
   0x4a7620 evade, 0x4a7a90 throw): the move's animation type (0 none), the melee state it is done
   from and goes to, its direction, and for a throw the victim's state and variant. */
static uint16_t category_lookup(uint32_t type, uint32_t *dir, uint32_t *from, uint32_t *to, uint16_t *tstate, uint16_t *tvar)
{
    uint32_t cat = type >> 28, i = type & 0xfffffffu;
    *tstate = *tvar = 0;
    if (cat == MELEE_ATTACK) {
        const AttackMove *m = attack_of(type);
        if (!m) return 0;
        *from = m->from, *to = m->to, *dir = m->dir;
        return m->type;
    }
    if (cat == MELEE_MANEUVER) {
        if (i >= 0xe) return 0;
        uint32_t e = 0x543ab0 + 0x40 * i;
        *dir = 5, *from = exe_u32(e + 0x30), *to = exe_u32(e + 0x34);
        return exe_u16(e + 0x38);
    }
    if (cat == MELEE_EVADE) {
        uint32_t v = exe_u32(0x543e50 + 0x38 * i);
        *from = v & 0xff, *to = v >> 8 & 0xff, *dir = 0;
        return (uint16_t)(v >> 16);
    }
    if (cat == MELEE_THROW) {
        uint32_t v = exe_u32(0x544390 + 0x24 * i);
        *from = ((v & 0xff) & 2) << 2 | 1, *to = 1, *dir = 1;
        *tstate = (uint16_t)((((~v) & 0x10) | 0xc) >> 2);
        if (v & 4) *tvar = 0x800;
        return (uint16_t)(v >> 16);
    }
    return 0;
}

/* A category's "can follow" (function 4: 0x4a5490 attack, 0x4a7670 evade, the others always): an attack
   that must follow an attack type needs a previous attack of that type; an evade only comes first. */
static bool category_follows(uint32_t type, uint32_t prev)
{
    uint32_t cat = type >> 28;
    if (cat == MELEE_ATTACK) {
        const AttackMove *m = attack_of(type);
        if (!m || m->prev == 0) return true;
        if (prev == 0xffffffffu || (prev & 0xf0000000u)) return false;
        const AttackMove *pm = attack_of(prev);
        return pm && pm->type == m->prev;
    }
    if (cat == MELEE_EVADE) return prev == 0xffffffffu;
    return true;
}

/* AI2iMelee_FindThrow 0x4a52d0: of the class's throws of the type from the state (its collections in
   turn), the first whose victim's animation (the throw's +0x14 type, from the victim's state, the
   variant's bits it asks for) the class has. */
static bool find_throw(const Character *probe, uint16_t type, uint16_t state, uint16_t tstate, uint16_t tvar,
                       const void **anim, const void **anim2, float *dist)
{
    const void *coll = chr_collection(probe), *list[64];
    if (!coll) return false;
    int n = trac_list(coll, type, state, list, 64);
    for (int i = 0; i < n; i++) {
        const uint8_t *t = tr_raw_block(list[i], TRAM_THROW, 0, 0x18);
        if (!t) continue;
        const void *v = chr_lookup_varient(probe, rd16(t + 0x14), tstate, (uint16_t)(tvar | 0x200));
        if (!v) continue;
        uint16_t vv = tr_varient(v);
        if (!((vv & 0x6000) || !(tvar & 0x6000))) continue;
        if (!((vv & 0x1800) || !(tvar & 0x1800))) continue;
        *anim = list[i], *anim2 = v, *dist = throw_distance_of(list[i]);
        return true;
    }
    return false;
}

/* AI2rMelee_MoveIsValid 0x4a50b0: the move's animation type, direction, the state it starts from (*cur,
   set when unset; false when it can't be done from it), the state it leaves, its animations and a
   throw's distance. A position's from its table (0x5435b0: the direction, "has parameters", its states,
   the last the end); the others' from their category. */
static bool move_is_valid(const Character *probe, uint32_t type, uint16_t *atype, uint32_t *dir, uint32_t *cur,
                          uint32_t *to, const void **anim, const void **anim2, float *dist)
{
    uint32_t cat = type >> 28;
    if (cat == MELEE_POSITION) {
        uint32_t e = POSITIONS_VA + POSITION_SIZE * (type & 0xfffffffu);
        *dir = exe_u32(e + 0x20);
        *to = exe_u32(e + 0x2c + 4 * exe_u32(e + 0x2c));
        if (exe_u32(e + 0x24) & 0xff) {
            uint32_t st = exe_u32(e + 0x30);
            *atype = mstate_anim_type(st);
            *anim = chr_lookup_varient(probe, mstate_anim_type(st), mstate_anim_state(st), 0x200);
        } else {
            *atype = 0, *anim = NULL;
        }
        *dist = 0.0f;
        return true;
    }
    uint32_t d = 0, from = 0, t = 0;
    uint16_t tstate, tvar;
    uint16_t at = category_lookup(type, &d, &from, &t, &tstate, &tvar);
    if (!at) return false;
    if (from && from != *cur) {
        if (*cur) return false;
        *cur = from;
    }
    uint16_t st = (*cur > 0 && *cur < 0x12) ? mstate_anim_state(from) : 1;
    const void *a = NULL, *a2 = NULL;
    float ds = 0.0f;
    if (cat == MELEE_THROW) {
        if (!find_throw(probe, at, st, tstate, tvar, &a, &a2, &ds)) return false;
    } else {
        a = chr_lookup_varient(probe, at, st, 0x200);
        if (!a && (cat == MELEE_EVADE || cat == MELEE_ATTACK)) return false;
    }
    *atype = at, *dir = d, *to = t, *anim = a, *anim2 = a2, *dist = ds;
    return true;
}

/* AI2rMelee_PrepareForUse 0x4a4650 for the class: its jump times and steps, the transitions between
   melee states it has the animations for, and each technique's computed part: its kind and index, its
   delay slot, the state it starts from and its direction, its moves' animations and motions, its
   attacks' damage, reach and kinds (blockable high or low, unblockable, block stun, stagger), its
   throw. A technique with a move the class can't do is left without moves. */
static void prepare(MeleeProfile *p, const void *oncc)
{
    if (p->prepared) return;
    p->prepared = true;
    Character probe;
    memset(&probe, 0, sizeof probe);
    probe.oncc = oncc;
    p->oncc = oncc;
    const uint8_t *cc = oncc;
    if (cc) {
        p->jump_time = (rdf(cc + 8) + rdf(cc + 8)) / rdf(cc + 4);
        float f = (float)rd16(cc + 0x16) * rdf(cc + 0x10);
        p->long_jump_time = (f + f) / rdf(cc + 4) + p->jump_time;
    }
    memset(p->dist, 0, sizeof p->dist);
    static const struct { uint16_t type, state; int slot; float sign; bool z; } STEPS[4] = {
        { 3, 3, 3, 1.0f, true }, { 9, 0x10, 4, -1.0f, true }, { 0xa, 0x17, 5, 1.0f, false }, { 0xb, 0x19, 6, -1.0f, false },
    };
    for (int i = 0; i < 4; i++) {
        const void *t = chr_lookup_varient(&probe, STEPS[i].type, STEPS[i].state, 0x200);
        if (!t) continue;
        float v[3];
        tr_position(t, 0, v);
        p->dist[STEPS[i].slot] = STEPS[i].sign * (STEPS[i].z ? v[2] : v[0]);
    }
    for (int i = 2; i < 7; i++) {
        float h = cc ? rdf(cc + 8) * 0.5f : 0.0f;
        if (h < p->dist[i]) h = p->dist[i];
        p->dist[i] = h;
    }

    /* the transitions: groups of rows (0x38 bytes from 0x541d68, a group ended by an empty row, the
       table by an empty group); each row whose animation the class has, at most 55 */
    p->ntransitions = 0;
    bool full = false;
    uint32_t over = 0;
    for (uint32_t row = 0x541d68;; row += 0x38) {
        uint32_t rows = 0, found = 0, first = row;
        for (; exe_u32(row + 4); row += 0x38) {
            rows++;
            uint32_t st = exe_u32(row + 4);
            const void *a = chr_lookup_varient(&probe, (uint16_t)exe_u32(row + 0x20), mstate_anim_state(st), 0x200);
            if (!a) continue;
            found++;
            if (full) {
                over++;
                continue;
            }
            MeleeTransition *t = &p->transition[p->ntransitions++];
            memset(t, 0, sizeof *t);
            t->dir = (uint8_t)exe_u32(row);
            t->from = (uint8_t)st;
            t->nsteps = (uint8_t)exe_u32(row + 0xc);
            t->f08 = rdf(exe_data(row + 0x30, 4));
            t->held = exe_u32(row + 0x28), t->down = exe_u32(row + 0x2c);
            t->type = (uint16_t)exe_u32(row + 0x20);
            for (uint32_t k = 0; k < t->nsteps && k < 4; k++) t->steps[k] = (uint8_t)exe_u32(row + 0x10 + 4 * k);
            t->anim = a;
            t->duration = (uint8_t)tr_duration(a);
            t->to = tr_to(a);
            /* the motion: the position point at the end along the direction, and across it the mean
               step of the last five frames */
            float pt[2] = { 0, 0 }, sx = 0, sz = 0;
            position_point(a, (uint16_t)(t->duration - 1), pt);
            uint32_t k0 = t->duration >= 5 ? t->duration - 5u : 0, cnt = 0;
            for (uint32_t k = k0; k < t->duration; k++) {
                float v[3];
                tr_position(a, (uint16_t)k, v);
                sx += v[0], sz += v[2];
                cnt++;
            }
            if (cnt) sx *= 1.0f / (float)cnt, sz *= 1.0f / (float)cnt;
            switch (t->dir) {
            case 1: t->across = sz, t->along = pt[1]; break;
            case 2: t->along = -pt[1], t->across = -sz; break;
            case 3: t->across = sx, t->along = pt[0]; break;
            case 4: t->along = -pt[0], t->across = -sx; break;
            default: t->along = t->across = 0.0f;
            }
            if (p->ntransitions > 0x36) full = true;
        }
        if (!rows) break;
        (void)first, (void)found;   /* (a required group with none of its rows: AI2rError) */
    }
    (void)over;         /* (more than 55: AI2rError) */

    /* the techniques */
    uint32_t nt = p->nattack + p->nevade + p->nmaneuver, slots = 0;
    for (uint32_t i = 0; i < nt; i++) {
        MeleeTech *k = &p->tech[i];
        k->attack_move = -1;
        k->computed = 0, k->damage = 0, k->near = k->reach = 0.0f, k->attack_frames = 0, k->attack = NULL, k->throw_dist = 0.0f;
        k->index = (uint16_t)i;
        k->delay_slot = -1;
        if (i < p->nattack) k->computed |= 0x40000;
        else if (i < p->nattack + p->nevade) k->computed |= 0x80000, k->index = (uint16_t)(i - p->nattack);
        else k->computed |= 0x100000, k->index = (uint16_t)(i - p->nattack - p->nevade);
        if (!k->count || k->first + k->count > p->nmoves) {
            k->computed |= 0x4000;
            continue;
        }
        if (k->delay && slots < MELEE_MEMORY) k->delay_slot = (int16_t)slots++;
        bool first = true;
        uint32_t cur = 0;
        for (uint32_t j = 0; j < k->count; j++) {
            MeleeMove *mv = &p->move[k->first + j];
            uint32_t prev = j ? p->move[k->first + j - 1].type : 0xffffffffu;
            if (!first && !category_follows(mv->type, prev)) {
                mv->invalid = true;     /* (AI2rError) */
                k->computed |= 0x4000;
                break;
            }
            uint16_t atype = 0;
            uint32_t mdir = 0, to = 0;
            float dist = 0.0f;
            bool ok = move_is_valid(&probe, mv->type, &atype, &mdir, &cur, &to, &mv->anim, &mv->anim2, &dist);
            mv->state = (uint16_t)cur;
            if (!ok) {
                mv->invalid = true;
                k->computed |= 0x4000;
                break;
            }
            mv->invalid = false;
            uint32_t cat = mv->type >> 28;
            if (first) {
                if (cur == 0) {
                    k->computed |= 0x20000 | to;
                    if (cat == MELEE_MANEUVER) continue;    /* (still the first move: the original's) */
                    if (cat != MELEE_POSITION) {
                        mv->invalid = true;
                        k->computed |= 0x4000;
                        break;
                    }
                } else {
                    k->computed |= cur;
                }
            }
            /* the direction: a maneuver's (5) only if none yet; another the first, or over a 5 (or'd, as
               the original's) */
            uint32_t d = k->computed >> 8 & 0xf;
            if (mdir) {
                if (mdir == 5) {
                    if (d == 0) k->computed |= 0x500;
                } else if (d != mdir && (d == 0 || d == 5)) {
                    k->computed |= mdir << 8;
                }
            }
            float m1[2] = { 0, 0 }, m2[2] = { 0, 0 };
            if (mv->anim) position_point(mv->anim, (uint16_t)(tr_duration(mv->anim) - 1), m1);
            if (mv->anim2) position_point(mv->anim2, (uint16_t)(tr_duration(mv->anim2) - 1), m2);
            switch (mdir) {
            case 0: case 1: case 5: mv->motion = m1[1], mv->motion2 = m2[1]; break;
            case 2: mv->motion = -m1[1], mv->motion2 = -m2[1]; break;
            case 3: mv->motion = m1[0], mv->motion2 = m2[0]; break;
            case 4: mv->motion = -m1[0], mv->motion2 = -m2[0]; break;
            default: break;
            }
            if (mv->anim) {
                if (!tr_test_flag(mv->anim, TRAM_FLAG_THROW_SOURCE)) {
                    if (tr_is_attack(mv->anim)) {
                        const uint8_t *t = mv->anim;
                        k->computed |= 0x1000;
                        if (any_attack_flag(mv->anim, 1)) k->computed |= 0x8000;
                        if (any_attack_flag(mv->anim, 2)) k->computed |= 0x10000;
                        if (any_attack_flag(mv->anim, 0)) k->computed |= 0x200000;
                        if (block_stuns(mv->anim)) k->computed |= 0x400000;
                        if (staggers(mv->anim)) k->computed |= 0x800000;
                        k->damage += attack_damage(mv->anim);
                        k->reach = rdf(t + 0x50);
                        k->near = k->reach - rdf(t + 0x11c) * 0.5f;
                        k->attack_frames = rd16(t + 0xec);
                        if (!(k->computed & 0x1000000) && !k->attack) k->attack_move = (int32_t)j, k->attack = mv->anim;
                    }
                } else {
                    if (k->computed & 0x1000000) {      /* a second throw */
                        mv->invalid = true;
                        k->computed |= 0x4000;
                        break;
                    }
                    k->computed |= 0x1000000;
                    k->throw_dist = dist;
                    k->computed |= throw_from_front(atype) ? 0x2000000 : 0x4000000;
                    if (mv->anim2) {
                        uint16_t v = tr_varient(mv->anim2);
                        if (v & 0x6000) k->computed |= 0x8000000;
                        else if (v & 0x1800) k->computed |= 0x10000000;
                        k->damage += throw_damage(mv->anim2);
                    }
                }
            }
            if (cat == MELEE_MANEUVER && (mv->type & 0xfffffffu) == 0xd) k->computed |= 0x20000000;
            first = false;
            cur = to;
        }
    }
}

void melee_start(MeleeState *m, const MeleeProfile *p, const void *oncc)
{
    memset(m, 0, sizeof *m);
    m->face = 1000.0f;
    if (!p) return;
    prepare((MeleeProfile *)p, oncc);
    m->profile = p;
}

/* ---- running ---- */

/* ONrCharacter_GetPelvisPosition 0x4e34f0 (the active record's A+0x229c). */
static void pelvis_of(const Character *c, float out[3]) { memcpy(out, &c->bone_world[0][9], 3 * sizeof(float)); }

/* AI2rManeuver_CheckBlockage 0x49ea60 (localpath.c). */
static bool check_blockage(const Character *c, const float p[3]) { return local_blockage(p3_environment(), c, p); }

void melee_test_local_environment(MeleeState *m, const Character *c, const Character *target)
{
    /* the angle (AI2iMelee_AngleToTarget 0x4a21c0, without the attack's lead: the port's) */
    float dx = target->pos[0] - c->pos[0], dz = target->pos[2] - c->pos[2];
    m->angle = wrap_2pi(atan2f(dx, dz));
    float sa = sinf(m->angle), ca = cosf(m->angle), loc[3];
    chr_location(c, loc);
    float d = m->dist + 50.0f;      /* 0x52a768 */
    float to[3] = { d * sa + loc[0], loc[1], d * ca + loc[2] }, at[3];
    m->env_time = game_time();          /* (the game's time: ONrGameState_GetGameTime) */
    memcpy(m->env_pos, loc, sizeof loc);
    bool reached, crossed;
    uint8_t kind;
    if (local_try_movement(c, loc, to, 3, &reached, at, &kind, &crossed) && !crossed) {
        if (!reached) {
            float f = (at[0] - loc[0]) * sa + (at[2] - loc[2]) * ca;
            m->room_fwd = f;
            m->env_blocked = f < m->dist;
            m->danger_fwd = kind == 8;
            memcpy(m->env_fwd, at, sizeof at);
        } else {
            m->env_blocked = false;
            m->room_fwd = 0.0f;
            m->danger_fwd = false;
            memcpy(m->env_fwd, to, sizeof to);
        }
    } else {
        /* no grid, or a line that started blocked: blocked, 6 ahead (0x5297f8) */
        m->env_blocked = true;
        m->danger_fwd = false;
        m->env_fwd[0] = m->env_pos[0] + sa * 6.0f;
        m->env_fwd[1] = m->env_pos[1];
        m->env_fwd[2] = m->env_pos[2] + ca * 6.0f;
    }
    float back[3] = { loc[0] - sa * 50.0f, loc[1], loc[2] - ca * 50.0f };
    if (local_try_movement(c, loc, back, 3, &reached, at, &kind, &crossed)) {
        if (!reached) {
            m->danger_back = kind == 8;
            memcpy(m->env_back, at, sizeof at);
            m->room_back = (at[0] - loc[0]) * sa + (at[2] - loc[2]) * ca;
        } else {
            m->room_back = 0.0f;
            m->danger_back = false;
            memcpy(m->env_back, back, sizeof back);
        }
    } else {
        m->room_back = 0.0f;
        m->danger_back = false;
        m->env_back[0] = m->env_pos[0] + sa * 6.0f;
        m->env_back[1] = m->env_pos[1];
        m->env_back[2] = m->env_pos[2] + ca * 6.0f;
    }
    /* a blockage within 8 doesn't count (nor with A+0xb6 set, not kept by the port); else the way from
       the pelvis to the target's, at the lower of the two heights, decides */
    if (m->env_blocked && m->dist < 8.0f) m->env_blocked = false;
    if (m->env_blocked) {
        float mine[3], theirs[3];
        pelvis_of(c, mine);
        pelvis_of(target, theirs);
        if (theirs[1] > mine[1]) theirs[1] = mine[1];
        m->env_blocked = check_blockage(c, theirs);
    }
}

/* The time (GS+0x144) for the delays: the characters' own tick count stands in. */
static uint32_t now_of(const Character *c) { (void)c; return game_time(); }

/* AI2iMelee_TranslateTotoroAnimState: of a melee state, 13 (moving) told apart by the keys held (A+0x2168,
   the port's last tick's): 14 forward, 16 left, 17 right, 15 back. */
static uint32_t translate_state(uint32_t s, uint64_t held)
{
    if (s != 13) return s;
    if (held & CHR_KEY_FORWARD) return 14;
    if (held & CHR_KEY_STEP_LEFT) return 16;
    if (held & CHR_KEY_STEP_RIGHT) return 17;
    if (held & CHR_KEY_BACK) return 15;
    return 13;
}

static uint32_t state_of(uint16_t anim_state, uint64_t held)
{
    return translate_state(anim_state < sizeof melee_state_of ? melee_state_of[anim_state] : 0, held);
}

/* TRrAnimation_GetFirstAttackFrame 0x4292a0, GetLastAttackFrame 0x4292b0: the attack points' frames
   (TRAM +0x130 the count, +0x134 12 bytes each, the frame first). */
static uint16_t attack_point_frame(const void *tram, bool last)
{
    uint32_t n = rd32((const uint8_t *)tram + 0x130);
    if (!n) return 0;
    const uint8_t *pt = tr_raw_block(tram, 0x134, last ? 12u * (n - 1) : 0, 12);
    return pt ? rd16(pt) : 0;
}

/* TRrAnimation 0x428960: the throw's victim animation type (the throw block's +0x14), 0 if none. */
static uint16_t throw_target_type(const void *tram)
{
    const uint8_t *b = tr_raw_block(tram, TRAM_THROW, 0, 0x18);
    return b ? rd16(b + 0x14) : 0;
}

/* ONrCharacter_InAir 0x4dc810 */
static bool in_air(const Character *c) { return c->air_ticks != 0; }

/* The categories' functions (the table at 0x52a9e8, 0x48 bytes each: ten functions from +0x20), by the
   move's top four bits. */
static unsigned category(const MeleeMove *mv) { return mv->type >> 28; }

/* 8, AI2iMelee_*_FaceTarget: whether the move turns to the facing wanted; only a maneuver doesn't. */
static bool cat_face_target(const MeleeMove *mv) { return category(mv) != MELEE_MANEUVER; }

/* 9, AI2iMelee_*_CanEvade: an attack until its keys are pressed, a position on the ground, a maneuver
   always; an evasion and a throw never. */
static bool cat_can_evade(const MeleeState *m, const Character *c)
{
    switch (category(m->mv)) {
    case MELEE_ATTACK: return !m->pressed;
    case MELEE_POSITION: return !in_air(c);
    case MELEE_MANEUVER: return true;
    default: return false;
    }
}

/* AI2iMelee_AngleToTarget 0x4a21c0: toward the target (+0x8c its difference), led by its motion over the
   first attack's frames before it lands (+0x3c's first attack point). */
static float angle_to_target(MeleeState *m, const Character *c)
{
    const Character *t = m->target;
    for (int i = 0; i < 3; i++) m->vec[i] = t->pos[i] - c->pos[i];
    float x = m->vec[0], z = m->vec[2];
    if (m->lead) {
        float f = (float)attack_point_frame(m->lead, false);
        x = f * (t->pos[0] - t->prev_pos[0]) + x;       /* 0x4f1320: its motion this tick */
        z = f * (t->pos[2] - t->prev_pos[2]) + z;
    }
    return wrap_2pi(atan2f(x, z));
}

/* AI2iMelee_UpdateFacing 0x4a1f10: the facing wanted (+0x98) and how far the character is from it
   (+0x9c). Blocking or between techniques, the target; in one, its direction's side of it (none for 0
   and 5), unless the facing is held. Quirks kept: left (3) isn't wrapped below 0, right (4) not past 2 pi. */
static void update_facing(MeleeState *m, const Character *c)
{
    if (m->blocking) {
        m->angle = m->face = angle_to_target(m, c);
        m->face_target = true;
    } else if (!m->tech) {
        m->angle = m->face = angle_to_target(m, c);
        m->face_target = false;
    } else if (!m->hold_facing) {
        m->angle = angle_to_target(m, c);
        m->face_target = m->mv ? cat_face_target(m->mv) : true;
        float f;
        switch (m->tech->computed >> 8 & 0xf) {
        case 0: case 5: m->face = 1000.0f; break;
        case 1: m->face = m->angle; break;
        case 2:
            f = m->angle + PI;
            m->face = f >= TWO_PI ? f - TWO_PI : f;
            break;
        case 3:
            f = m->angle - HALF_PI;
            m->face = f >= TWO_PI ? f - TWO_PI : f;
            break;
        case 4:
            f = m->angle + HALF_PI;
            m->face = f < 0.0f ? f + TWO_PI : f;
            break;
        default: break;
        }
    }
    if (m->face == 1000.0f) {
        m->face_off = 0.0f;
        return;
    }
    m->face_off = fabsf(wrap_pi(m->face - c->facing));
}

/* AI2iMelee_ApplyFacing: a move that faces turns to the facing wanted at the full rate, its aim let go
   (0x498b90, AI2rMovement_StopAiming); else the character looks at the target
   (AI2rMovement_LookAtCharacter). */
static void apply_facing(AiState *a)
{
    MeleeState *m = &a->melee;
    if (m->face_target && m->face != 1000.0f) {
        a->full_turn = true;
        a->facing_override = m->face;
        a->aim.look = 0;
    } else {
        a->aim.look = 3;
        a->aim.look_char = m->target;
    }
}

static void new_seeds(MeleeState *m)
{
    m->seed[0] = trac_random();     /* AI2iMelee_NewRandomSeeds */
    m->seed[1] = trac_random();
}

/* The technique's state cleared (the start of a new one, AI2iMeleeState_NewTechnique, and the end). */
static void clear_technique(MeleeState *m)
{
    m->keys = 0;
    m->move = 0;
    m->leftover = 0.0f;
    m->keys_set = false;
    m->m66 = false;
    m->m84 = -1;
    m->face = 1000.0f;
    m->hold_facing = m->committed = false;
    m->m60 = 0;
}

/* AI2iMeleeState_AbortTechnique: a failed attack is remembered (1000), the technique dropped. */
static bool abort_technique(MeleeState *m)
{
    if (!m->tech) return true;
    if ((m->tech->computed & 0x40000) && m->tech->index < MELEE_MEMORY) m->memory[m->tech->index] = 1000.0f;
    m->tech = NULL;
    m->mv = NULL;
    m->lead = NULL;
    new_seeds(m);
    clear_technique(m);
    return true;
}

/* AI2iMeleeState_ApplyTechniqueDelay: the technique may not be picked again for its delay. */
static void apply_delay(MeleeState *m, const Character *c)
{
    if (m->tech && m->tech->delay && m->tech->delay_slot >= 0 && m->tech->delay_slot < MELEE_MEMORY)
        m->available[m->tech->delay_slot] = now_of(c) + m->tech->delay;
}

/* AI2rExecutor_MoveOverride 0x498b60: these keys this tick. */
static void move_override(AiState *a, uint64_t keys)
{
    a->key_override = true;
    a->override_keys = keys;
}

/* The executor's attack override (AI2rExecutor_AttackOverride 0x498bb0). */
static void attack_override(MeleeState *m, uint16_t type, uint64_t held, uint64_t down, uint16_t prev)
{
    m->override = true;
    m->override_type = type;
    m->override_prev = prev;
    m->override_held = held;
    m->override_down = down;
}

/* AI2iMelee_WeightLocalMovement 0x4a4440: a move that goes `along` (forward, or back below 0) toward a
   danger ahead of the way (+0x141, +0x142 with the room +0x144, +0x148): true (and 0) when it would
   reach it, a weight falling under 10 of it (x 0.1). Techniques with flag 4 (computed) don't care. */
static bool weight_local_movement(const MeleeState *m, const MeleeTech *k, float along, float *w)
{
    float f = 1.0f;
    bool danger = false;
    if (!(k->computed & 4)) {
        bool d;
        float room;
        if (along <= 0.0f) d = m->danger_back, along = -along, room = m->room_back;
        else d = m->danger_fwd, room = m->room_fwd;
        if (d) {
            room -= along;
            if (room >= 0.0f) {
                if (room < 10.0f) f = room * 0.1f;
            } else {
                danger = true;
                f = 0.0f;
            }
        }
    }
    if (w) *w = f;
    return danger;
}

/* ONrCharacterClass_MovementSpeed 0x4ee020 for running (mode 6): forward 0.88, back 0.8, sideways 0.84. */
static float run_speed(int kind)
{
    switch (kind) {
    case 0: return 0.88f;
    case 2: case 3: return 0.84f;
    case 1: return 0.8f;
    default: return 0.0f;
    }
}

/* 0x498520: the class's turn a tick (ONCC+0x124, a hundredth of a turn). */
static float turn_rate(const Character *c) { return rdf((const uint8_t *)c->oncc + 0x124) * 0.06283185631036758f; }

/* AI2iMelee_SetupAttackerState 0x4a2270: the attacker's half of the context as it would start the
   move: in the air as it is; else, for a moving position, a long jump begun (half the jump's and the
   long jump's air times, the motion estimate with the class's jump speed up and at least the step along
   the direction); the animation from its first frame. */
static void setup_attacker(const MeleeProfile *p, ChrIntersectChar *h, const Character *c, bool moving, float step,
                           uint32_t dir, const void *anim)
{
    h->modified = true;
    if (in_air(c)) {
        h->now.air = true;
        h->now.jumped = (c->jumped & 1) != 0;
        h->now.jump_held = false;       /* (A+0x2168's jump: the AI's held keys, last tick's) */
        h->now.air_ticks = c->air_ticks;
        memcpy(h->now.air_velocity, c->air_velocity, sizeof h->now.air_velocity);
        float f = p->long_jump_time - (float)c->air_ticks;
        h->now.air_frames = f >= 0.0f ? (uint16_t)lrintf(f) : 0;
    } else if (!moving) {
        h->now.air = false;
    } else {
        h->now.air = true;
        h->now.jumped = true;
        h->now.air_ticks = 0;
        h->now.air_frames = (uint16_t)lrintf((p->long_jump_time + p->jump_time) * 0.5f);
        chr_velocity_estimate(c, h->now.air_velocity);
        h->now.air_velocity[1] += h->oncc_08;
        float *v = h->now.air_velocity;
        switch (dir) {
        case 1: if (v[2] <= step) v[2] = step; break;
        case 2: if (-step < v[2]) v[2] = -step; break;
        case 3: if (v[0] <= step) v[0] = step; break;
        case 4: if (-step < v[0]) v[0] = -step; break;
        default: break;
        }
    }
    h->frame = 0;
    h->anim = anim;
}

static bool start_move(AiState *a, Character *c);
static bool position_start(AiState *a, Character *c);
static bool position_update(AiState *a, Character *c);

/* AI2iMelee_StartNextMove 0x4a20f0: the move finished (its category's 7), the next started (its 5);
   true when the technique is over (or the start ends it). */
static bool start_next_move(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    if (!m->mv->invalid) {
        switch (category(m->mv)) {
        case MELEE_ATTACK: case MELEE_THROW:
            /* 0x4a7fd0: an attack (or throw) that may not be interrupted commits the technique */
            if (!(m->tech->flags & 1)) m->committed = true;
            break;
        case MELEE_MANEUVER:
            apply_delay(m, c);      /* 0x4a7570 */
            ai_halt(a);
            break;
        default: break;
        }
    }
    m->move++;
    new_seeds(m);
    bool done;
    if (m->move < m->tech->count) {
        m->mv++;
        done = start_move(a, c);
    } else {
        done = true;
        m->mv = NULL;
        m->lead = NULL;
    }
    update_facing(m, c);
    apply_facing(a);
    return done;
}

/* ---- the attacks (AI2iMelee_Attack_*) ---- */

/* The wait while the character's animation can't yet give way to the move's (the move's state left
   later, or an atomic part): 4 frames of the end for an animation of the fight variant, 8 for a
   shortcut, else none. True: wait. */
static bool move_window(const Character *c, const MeleeMove *mv, uint32_t to, uint32_t from)
{
    if (!c->anim) return false;
    if (!tr_is_atomic(c->anim, (uint16_t)c->frame) && !(from != mv->state && to == mv->state)) return false;
    uint32_t window = 0;
    if (chr_lookup_varient(c, tr_type(c->anim), c->state, (uint16_t)(c->varient | 0x200))) window = 4;
    else if (tr_is_shortcut(c->anim, c->state)) window = 8;
    return window < (uint32_t)(tr_duration(c->anim) - c->frame);
}

static bool hold_keys(AiState *a)
{
    MeleeState *m = &a->melee;
    if (m->keys_set && m->keys) move_override(a, m->keys);
    return false;
}

/* AI2iMelee_Attack_Update 0x4a5500 */
static bool attack_update(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = m->mv;
    if (m->pressed) {
        if (m->override) return false;      /* AI2rExecutor_HasAttackOverride: not taken yet */
        m->hold_facing = true;
        m->face = c->facing;
        apply_delay(m, c);
        if (m->busy) {
            /* still attacking: the next move waits unless it is an attack (a combo goes on) */
            if (!m->tech || m->move + 1 >= m->tech->count) return false;
            if (category(mv + 1) != MELEE_ATTACK) return false;
        }
        return start_next_move(a, c);
    }
    const AttackMove *am = attack_of(mv->type);
    if (mv->invalid || !am) return true;    /* (AI2rError 0x4936b0) */
    uint32_t to = state_of(c->anim ? tr_to(c->anim) : 0, a->held), from = state_of(c->from_state, a->held);
    /* (committed, it isn't aimed again, unless in the ultra mode, 0x5eb849, or with C+8 bit 2) */
    if ((!m->committed || cheat_ultra_mode) && m->face != 1000.0f) {
        int r = -1;
        if (m->face_off > 1.0471975803375244f) return abort_technique(m);
        ChrIntersect x;
        if (m->face_off > 0.2617993950843811f) {
            chr_intersect_setup(c, m->target, true, &x);
            r = CHR_ISECT_MISSED;
        } else if (!m->m66) {
            chr_intersect_setup(c, m->target, true, &x);
            setup_attacker(m->profile, &x.a, c, m->moving, m->step, am->dir, mv->anim);
            if (!chr_intersect(&x)) r = x.result;
        }
        if (r >= 0) {
            /* a miss: with keys to hold, too far but facing, or passing while turning, waits; else the
               attack is given up */
            if (!m->keys_set || r == CHR_ISECT_TOO_HIGH || r == CHR_ISECT_TOO_LOW || r == CHR_ISECT_AWAY)
                return abort_technique(m);
            if (r == CHR_ISECT_TOO_FAR) {
                if (m->face_off < 0.05235987901687622f && (m->keys & 0x6600000)) return hold_keys(a);
                return abort_technique(m);
            }
            if (r != CHR_ISECT_MISSED) return abort_technique(m);
            if (!exe_data(0x52abac + from, 1)[0] || m->face_off >= 0.2617993950843811f ||
                m->face_off <= 0.05235987901687622f)
                goto press;
            if (!(m->keys & 0x6600000)) return hold_keys(a);
            /* the turn the keys make: along the attack's way and across it, the circle it needs against
               the run's speed over the turn rate */
            float f = c->facing;
            int kind = 0;
            switch (am->dir) {
            case 2: f += PI; if (f >= TWO_PI) f -= TWO_PI; kind = 1; break;
            case 3: f += HALF_PI; if (f >= TWO_PI) f -= TWO_PI; kind = 2; break;
            case 4: f -= HALF_PI; if (f < 0.0f) f += TWO_PI; kind = 3; break;
            default: break;
            }
            float s = sinf(f), co = cosf(f);
            float along = fabsf(co * m->vec[2] + s * m->vec[0]);
            float across = fabsf(co * m->vec[0] - s * m->vec[2]) - x.t_radius;
            if (across <= 0.1f) return hold_keys(a);
            float circle = (across * across + along * along) / (across + across) * 0.9f;
            if (run_speed(kind) / turn_rate(c) <= circle) return hold_keys(a);
            return abort_technique(m);
        }
    }
press:
    if (m->override) return false;
    if (c->combo_type != am->prev && am->prev != 0) return abort_technique(m);
    if (move_window(c, mv, to, from)) return hold_keys(a);
    if (weight_local_movement(m, m->tech, mv->motion, NULL)) return abort_technique(m);
    attack_override(m, am->type, am->held, am->down, am->prev);
    m->pressed = true;
    return false;
}

/* ---- the positions (AI2iMelee_Position_*) ---- */

/* ---- the maneuvers (AI2iMelee_Maneuver_*) ---- */

/* 0x4a6b30: a step of the maneuver's direction (1 forward, 2 back, 3 left, 4 right) from the facing, or,
   toward the target, around the angle to it: the way tried over the grids, 15 ahead (from the facing,
   AI2rTryLocalMovement; toward the target, AI2rFindLocalPath 0x49d220 up to 4 steps of pi/16 to either
   side, the way then the line's); a blocked advance (with +0x140 clear) goes straight at the target.
   True: moving. */
static bool maneuver_step(AiState *a, Character *c, int dir, bool toward)
{
    MeleeState *m = &a->melee;
    static const float off[4] = { 0.0f, 3.1415927410125732f, 1.5707963705062866f, 4.71238899230957f };
    static const int kinds[4] = { AI_MOVE_FORWARD, AI_MOVE_BACK, AI_MOVE_LEFT, AI_MOVE_RIGHT };
    if (dir < 1 || dir > 4) return false;
    float way = (toward ? m->angle : c->facing) + off[dir - 1];
    if (way >= TWO_PI) way -= TWO_PI;
    float loc[3], at[3];
    chr_location(c, loc);
    bool reached = false, crossed;
    uint8_t kind;
    if (!toward) {
        /* 15 ahead of the way, straight (AI2rTryLocalMovement) */
        float to[3] = { loc[0] + sinf(way) * 15.0f, loc[1], loc[2] + cosf(way) * 15.0f };
        if (!local_try_movement(c, loc, to, 3, &reached, at, &kind, &crossed)) reached = false;
    } else if (local_find_path(c, loc, 15.0f, way, 0.19634954631328583f, 4, 8, 3, &reached, at, &kind, &crossed) &&
               reached) {
        /* the way the line found (up to 4 steps of pi/16 either side) */
        way = wrap_2pi(atan2f(at[0] - loc[0], at[2] - loc[2]));
    } else {
        reached = false;
    }
    if (!reached) {
        if (dir != 1 || m->env_blocked || c->air_ticks) return false;   /* (A+0x1a8: the port's air) */
        way = m->angle;
    }
    ai_halt(a);
    a->facing_mode = 1;
    a->move_kind = kinds[dir - 1];
    ai_movement_modifier(a, way, 1.0f);
    return true;
}

/* The maneuvers' table (0x543ab0, 0x40 bytes each: the name, the parameters' counts and names, +0x1c
   "toward the target", +0x20 the direction, +0x28 and +0x2c the keys held, +0x30 the kind of move). */
static bool maneuver_toward(uint32_t idx) { return exe_data(0x543acc + 0x40 * idx, 1)[0] != 0; }
static int maneuver_dir(uint32_t idx) { return (int)exe_u32(0x543ad0 + 0x40 * idx); }
static uint64_t maneuver_keys(uint32_t idx)
{
    return (uint64_t)exe_u32(0x543ad8 + 0x40 * idx) | (uint64_t)exe_u32(0x543adc + 0x40 * idx) << 32;
}

/* AI2iMelee_Maneuver_Update 0x4a7060 */
static bool maneuver_update(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = m->mv;
    uint32_t idx = mv->type & 0xfffffffu;
    if (category(mv) != MELEE_MANEUVER || idx >= 0xe) return abort_technique(m);
    uint32_t t = now_of(c);
    if (idx == 0xd) {
        /* "max range": the keys pressed once, then the technique ends */
        if (!mv->p[1]) return abort_technique(m);
        if (m->pressed) {
            if (m->override) return false;
            apply_delay(m, c);
            if (!m->busy) return start_next_move(a, c);
            return false;
        }
        uint32_t to = state_of(c->anim ? tr_to(c->anim) : 0, a->held), from = state_of(c->from_state, a->held);
        if (move_window(c, mv, to, from)) {
            if (m->keys_set && m->keys) move_override(a, m->keys);
            return false;
        }
        uint64_t k = maneuver_keys(idx);
        attack_override(m, exe_u16(0x543ae8 + 0x40 * idx), k, k, 0);
        m->pressed = true;
        return false;
    }
    if (!m->man_started) {
        if ((c->anim && tr_is_attack(c->anim)) || m->override || in_air(c)) return false;
        m->man_started = true;
        m->man_until = (uint32_t)lrintf(mv->p[0] * 60.0f) + t;
    }
    if (m->man_until < t) return start_next_move(a, c);
    if (!maneuver_toward(idx)) {
        move_override(a, maneuver_keys(idx));
        return false;
    }
    if (idx == 0) {
        /* advance: until within the range less the threshold (at least 8) */
        float f = mv->p[1] - mv->p[2];
        if (f <= 8.0f) f = 8.0f;
        if (m->dist < f) return start_next_move(a, c);
    } else if (idx == 1) {
        /* retreat: until beyond the range and the threshold (at least 75) */
        float f = mv->p[2] + mv->p[1];
        if (f <= 75.0f) f = 75.0f;
        if (f < m->dist) return start_next_move(a, c);
    }
    if (maneuver_step(a, c, maneuver_dir(idx), true)) return false;
    if (!m->tech) return true;
    if (m->tech->computed & 0x40000) m->memory[m->tech->index] = 1000.0f;
    m->tech = NULL;
    m->mv = NULL;
    m->lead = NULL;
    new_seeds(m);
    clear_technique(m);
    return true;
}

/* AI2iMelee_Maneuver_Start 0x4a6f20: in the ultra mode (0x5eb849, or with C+8 bit 2, a debugging
   switch nothing sets) a pause (4) is skipped for the next move; "random stop" (8) ends the technique
   by its chance (the first number, %); the others start their update. */
static bool maneuver_start(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = m->mv;
    uint32_t idx = mv->type & 0xfffffffu;
    if (category(mv) != MELEE_MANEUVER || idx > 0xd) return false;
    if (cheat_ultra_mode && idx == 4) return start_next_move(a, c);
    if (idx == 8) {
        if ((float)trac_random() * 1.5259021893143654e-05f * 100.0f < mv->p[0]) return abort_technique(m);
        return start_next_move(a, c);
    }
    if (maneuver_toward(idx)) m->hold_facing = false;
    if (idx == 0xd) m->pressed = false;
    m->man_started = false;
    return maneuver_update(a, c);
}

/* ---- the evasions (AI2iMelee_Evade_*) ---- */

/* The evasions' table (0x543e30, 0x38 bytes each: +0x20 the states, +0x28 the keys held, +0x30 down). */
static uint64_t evade_keys(uint32_t idx, unsigned at)
{
    uint32_t va = 0x543e30 + 0x38 * idx + at;
    return (uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
}

/* AI2iMelee_Evade_Update 0x4a76a0: its keys pressed once (in the move's window), then it lasts the
   animation's duration; a technique not a maneuver gives up the evasion once the target's attack is
   past (or another began). */
static bool evade_update(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = m->mv;
    uint32_t t = now_of(c);
    if (m->ev_until) {
        if (t < m->ev_until) return false;
        return start_next_move(a, c);
    }
    if (!(m->tech->computed & 0x100000)) {
        const Character *tg = m->target;
        if (!(tg->sound_serial == m->block_serial && tg->anim && tg->frame <= attack_point_frame(tg->anim, true))) {
            if (m->ev_pressed) m->override = false;     /* AI2rExecutor_ClearAttackOverride */
            return start_next_move(a, c);
        }
    }
    if (m->ev_pressed) {
        if (m->override) return false;
        m->hold_facing = true;
        m->face = c->facing;
        m->ev_until = (c->anim ? tr_duration(c->anim) : 0) + t;
        return false;
    }
    if (mv->invalid) return true;
    if (m->override) return false;
    uint32_t to = state_of(c->anim ? tr_to(c->anim) : 0, a->held), from = state_of(c->from_state, a->held);
    if (move_window(c, mv, to, from)) return false;
    uint32_t idx = mv->type & 0xfffffffu;
    attack_override(m, (uint16_t)(exe_u32(0x543e50 + 0x38 * idx) >> 16), evade_keys(idx, 0x28), evade_keys(idx, 0x30), 0);
    apply_delay(m, c);
    m->ev_pressed = true;
    return false;
}

static bool evade_start(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    m->ev_pressed = false;      /* 0x4a7680 */
    m->ev_until = 0;
    return evade_update(a, c);
}

/* ---- the throws (AI2iMelee_Throw_*) ---- */

/* The throws' table (0x544370, 0x24 bytes each: the name, +0x20 flags (4 the victim in its 0x18
   variant, 8 in its 0x60) and the throw's type in the high half). */
static uint32_t throw_entry(uint32_t idx) { return exe_u32(0x544390 + 0x24 * idx); }

/* AI2rExecutor_ThrowOverride 0x498c10: the throw to remap the punch into, and the punch (0x16). */
static void throw_override(MeleeState *m, const void *anim, const void *victim, const Character *t)
{
    m->throwing = true;
    m->throw_anim = anim;
    m->throw_victim = victim;
    m->throw_target = t;
    attack_override(m, 0x16, CHR_KEY_PUNCH, CHR_KEY_PUNCH, 0);
}

/* The throw given up (the technique aborted, its move state cleared: 0x4a7f60). */
static bool throw_abort(MeleeState *m)
{
    if (!m->tech) return true;
    if ((m->tech->computed & 0x40000) && m->tech->index < MELEE_MEMORY) m->memory[m->tech->index] = 1000.0f;
    m->tech = NULL;
    m->mv = NULL;
    m->lead = NULL;
    new_seeds(m);
    m->m84 = -1;
    m->keys = 0;
    m->move = 0;
    m->leftover = 0.0f;
    m->keys_set = false;
    m->m66 = false;
    m->face = 1000.0f;
    m->hold_facing = m->committed = false;
    m->m60 = 0;
    return true;
}

/* AI2iMelee_Throw_Update 0x4a7b40: once pressed, the override taken, the next move. Else within 45
   degrees of the facing and the throw's reach, a target that can be thrown now (its class throwable, not
   unstoppable, the cheat's player or flag 0x100, no throw flag on its animation, not invulnerable), in the variant the throw
   asks: the class's throw for the two states (AI2iMelee_FindThrow); in the window, from the front
   facing each other (beyond 135 degrees, 0x52adec) or from behind facing the same way (within 45),
   the way clear of dangers, the throw override. Anything else gives the technique up. */
static bool throw_move_update(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = m->mv;
    if (m->pressed) {
        if (m->override) return false;
        return start_next_move(a, c);
    }
    if (mv->invalid) return true;
    if (c->anim && tr_test_flag(c->anim, 15)) return false;
    const Character *t = m->target;
    if (m->face_off > 0.7853981852531433f || !mv->anim || throw_distance_of(mv->anim) < m->dist) return abort_technique(m);
    if (!throw_target_ok(t)) return abort_technique(m);
    uint32_t e = throw_entry(mv->type & 0xfffffffu);
    uint16_t var = 0;
    if (e & 4) {
        var = 0x800;
        if (!(t->varient >> 8 & 0x18)) return abort_technique(m);
    } else if (e & 8) {
        var = 0x2000;
        if (!(t->varient >> 8 & 0x60)) return abort_technique(m);
    }
    const void *anim, *victim;
    float dist;
    if (!find_throw(c, (uint16_t)(e >> 16), c->state, t->state, var, &anim, &victim, &dist)) return throw_abort(m);
    if (c->anim && tr_is_atomic(c->anim, (uint16_t)c->frame) && c->frame < tr_duration(c->anim)) {
        if (m->keys_set && m->keys) move_override(a, m->keys);
        return false;
    }
    float d = fabsf(wrap_pi(t->facing - c->facing));
    bool ok = d < 0.7853981852531433f;
    if (throw_from_front(tr_type(anim))) ok = 2.356194496154785f < d;     /* ONrAnimType_ThrowIsFromFront */
    if (!ok || weight_local_movement(m, m->tech, mv->motion, NULL)) return throw_abort(m);
    apply_delay(m, c);
    throw_override(m, anim, victim, t);
    m->pressed = true;
    return false;
}

/* 5, AI2iMelee_*_Start. */
static bool start_move(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    switch (category(m->mv)) {
    case MELEE_ATTACK:
        m->pressed = false;     /* 0x4a54e0 */
        return attack_update(a, c);
    case MELEE_POSITION: return position_start(a, c);
    case MELEE_MANEUVER: return maneuver_start(a, c);
    case MELEE_EVADE: return evade_start(a, c);
    default:
        m->pressed = false;     /* 0x4a7b20 */
        return throw_move_update(a, c);
    }
}

/* 6, AI2iMelee_*_Update */
static bool update_move(AiState *a, Character *c)
{
    switch (category(a->melee.mv)) {
    case MELEE_ATTACK: return attack_update(a, c);
    case MELEE_POSITION: return position_update(a, c);
    case MELEE_MANEUVER: return maneuver_update(a, c);
    case MELEE_EVADE: return evade_update(a, c);
    default: return throw_move_update(a, c);
    }
}

void melee_stop(MeleeState *m)
{
    /* AI2rStopMeleeTechnique 0x48b6d0 */
    m->override = m->throwing = false;
    abort_technique(m);
}

void melee_notify_animation(MeleeState *m, const void *anim, uint16_t type, uint16_t left, bool victim)
{
    /* hit (0x4e37b0): the technique stops */
    if (victim) {
        melee_stop(m);
        return;
    }
    /* the attack's keys pressed (AI2rExecutor_HasAttackOverride): its animation, the keys taken; still
       the one they follow, on waiting; any other, "wrong animation" (AI2rError 0x4936b0) and the
       override and the technique stop */
    if (m->throwing) {
        /* AI2rExecutor_HasThrowOverride: an attack or a throw takes it */
        if (anim && (tr_is_attack(anim) || tr_test_flag(anim, 15))) m->override = m->throwing = false;
        return;
    }
    if (!m->override) return;
    if (type == m->override_type) {
        m->override = false;    /* AI2rExecutor_ClearAttackOverride */
        return;
    }
    if (left == m->override_type) return;
    melee_stop(m);
}

/* AI2rFight_GetAttackCookie 0x499820: of the AIs fighting one target, at most two (0x545774) attack
   at a time, each for 180 ticks, then giving way if others wait. The fight (0x499670) is the target's
   attackers: the AIs in combat against it. */
static uint32_t fight_count(const Character *target, uint32_t *attacking)
{
    uint32_t fighting = 0;
    if (attacking) *attacking = 0;
    for (int i = 0; i < chars_slots(); i++) {
        const Chr *o = chars_get(i);
        if (!o || o->player || o->ai.state != AI_STATE_COMBAT || !o->ai.target || &o->ai.target->c != target) continue;
        fighting++;
        if (attacking && o->ai.melee.cookie) (*attacking)++;
    }
    return fighting;
}

static bool get_cookie(AiState *a, const Character *c, const Character *target)
{
    MeleeState *m = &a->melee;
    uint32_t t = now_of(c), attacking, fighting = fight_count(target, &attacking);
    if (fighting <= 1) {
        m->cookie_until = t + 180;
        m->cookie = true;
        return true;
    }
    if (!m->cookie) {
        if (attacking < exe_u32(0x545774)) {
            m->cookie = true;
            m->cookie_until = t + 180;
            return true;
        }
        return false;
    }
    if (t < m->cookie_until) return true;
    if (fighting <= attacking) {
        m->cookie_until = t + 180;
        return true;
    }
    m->cookie = false;
    return false;
}

/* 0x4998a0: the cookie given back. */
static void release_cookie(AiState *a) { a->melee.cookie = false; }

/* AI2iMelee_CalculateDirectionWeights: per direction, 1 within 45 degrees falling to 0 at 60 (narrow) or
   1 within 70 falling to 0 at 110 (wide); 0 and 5 always 1; direction 1 at least 0.35. */
static void direction_weights(float angle, float facing, float narrow[6], float wide[6])
{
    for (int d = 0; d < 6; d++) {
        if (d == 0 || d == 5) { narrow[d] = wide[d] = 1.0f; continue; }
        float x = angle - facing;
        if (d == 2) x -= PI;
        else if (d == 3) x -= HALF_PI;
        else if (d == 4) x += HALF_PI;
        float a = fabsf(wrap_pi(x));
        narrow[d] = a < 0.7853981852531433f ? 1.0f : a <= 1.0471975803375244f ? (a - 1.0471975803375244f) * -3.819718599319458f : 0.0f;
        wide[d] = a < 1.2217304706573486f ? 1.0f : a <= 1.919862151145935f ? (a - 1.919862151145935f) * -1.4323945045471191f : 0.0f;
    }
    if (narrow[1] <= 0.35f) narrow[1] = 0.35f;
    if (wide[1] <= 0.35f) wide[1] = 0.35f;
}

/* AI2iMelee_HandleNoTechnique 0x4a1dd0: the cookie back, the facing free; beyond 8, toward the target
   (to 50 at most) as far as the grids let it. True: moving. */
static bool no_technique(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    release_cookie(a);
    a->facing_mode = 0;
    if (!m->target || m->dist <= 8.0f) return false;
    ai_halt(a);
    float d = m->dist < 50.0f ? m->dist : 50.0f, loc[3], at[3];
    chr_location(c, loc);
    float to[3] = { d * sinf(m->angle) + loc[0], loc[1], d * cosf(m->angle) + loc[2] };
    bool reached, crossed;
    uint8_t kind;
    bool ok = local_try_movement(c, loc, to, 3, &reached, at, &kind, &crossed);
    if (!ok || !reached) return false;
    ai_movement_modifier(a, wrap_2pi(atan2f(at[0] - loc[0], at[2] - loc[2])), a->combat.weight[COMBAT_MANEUVER_MELEE]);
    return true;
}

/* The weighing of a technique (0x2c bytes each in the table at 0x637260). */
typedef struct {
    const MeleeTech *tech;      /* +0x00 */
    float weight;               /* +0x04 */
    int32_t attack_at;          /* +0x08: its first attack's move, -1 none */
    const MeleeMove *attack;    /* +0x0c */
    bool moving;                /* +0x10: its position moves (a jump) */
    bool late;                  /* +0x11: its attack would land after the target's */
    float step;                 /* +0x14: the jump's step (the profile's dist[] of the position) */
    uint16_t air_ticks;         /* +0x18: the attacker's air ticks when it weighed */
    int16_t stop_move;          /* +0x1a: the move at which a delaying position stops, -1 none */
    float rot_sin, rot_cos;     /* +0x1c, +0x20: the attack turned toward the target */
    float offset;               /* +0x24: how far it goes before the attack */
    uint32_t rot_dir;           /* +0x28: the attack's direction it was turned for, 0 none */
} Weighing;

/* AI2iMelee_CheckStateTransition 0x4a4590: the first of the profile's transitions from the melee state
   `from` with `to` among its steps whose animation (of the row's type from anim_state) the character
   has, or, with no character, the profile's class (variant 0x200). */
static const MeleeTransition *check_state_transition(const Character *c, const MeleeProfile *p, uint32_t from,
                                                     uint32_t to, uint16_t anim_state)
{
    Character probe;
    const Character *who = c;
    if (!who) {
        memset(&probe, 0, sizeof probe);
        probe.oncc = p->oncc;
        who = &probe;
    }
    uint16_t var = (uint16_t)((c ? c->varient : 0) | 0x200);
    for (uint32_t i = 0; i < p->ntransitions; i++) {
        const MeleeTransition *t = &p->transition[i];
        if (t->from != from) continue;
        unsigned k = 0;
        while (k < t->nsteps && t->steps[k] != to) k++;
        if (k < t->nsteps && chr_lookup_varient(who, t->type, anim_state, var)) return t;
    }
    return NULL;
}

/* AI2iMelee_GetTransitionLength 0x4a4300: the transitions from the melee state to `to` (through the
   idle state 1 when there's no direct way), each from the last one's steps[0]: the factors multiplied
   into *factor, their motion added to *along, their ticks to *ticks; *count of them. False: no way.
   The state and the animation state are the caller's own: not written back. */
static bool transition_length(const MeleeProfile *p, uint32_t state, uint16_t anim_state, uint32_t to, float *factor,
                              float *along, float *ticks, uint32_t *count)
{
    float f = factor ? *factor : 0.0f, al = along ? *along : 0.0f, tk = ticks ? *ticks : 0.0f;
    uint32_t n = 0, aim = to;
    bool via_idle = false;
    for (;;) {
        bool ok = true;
        while (state != aim) {
            const MeleeTransition *t = check_state_transition(NULL, p, state, aim, anim_state);
            if (!t) {
                ok = false;
                break;
            }
            al += t->along;
            tk += (float)t->duration;
            anim_state = t->to;
            f *= t->f08;
            n++;
            state = t->steps[0];
        }
        if (ok) {
            if (aim == to) break;
            aim = to;
            continue;
        }
        if (via_idle || state == 1 || aim == 1) return false;
        aim = 1;
        via_idle = true;
    }
    if (factor) *factor = f;
    if (along) *along = al;
    if (ticks) *ticks = tk;
    if (count) *count = n;
    return true;
}

/* AI2iMelee_RotateAnimationContext 0x4a24b0: the context's target space turned by the attack's turn
   (from its saved matrix) and moved back along the attack's direction by its offset. */
static void rotate_context(ChrIntersect *x, const Weighing *w)
{
    if (w->rot_dir) {
        for (int i = 0; i < 4; i++) {
            const float *s = &x->m0[i * 3];
            float *d = &x->m[i * 3];
            d[0] = w->rot_cos * s[0] - w->rot_sin * s[1];
            d[1] = w->rot_sin * s[0] + w->rot_cos * s[1];
            d[2] = s[2];
        }
    }
    switch (w->rot_dir) {
    case 0: case 1: x->m[10] -= w->offset; break;
    case 2: x->m[10] += w->offset; break;
    case 3: x->m[9] -= w->offset; break;
    case 4: x->m[9] += w->offset; break;
    default: break;
    }
    x->m_modified = true;
}

/* The position table's row (0x40 bytes at 0x5435b0): +0x20 the direction, +0x24 a delay, +0x28 the
   jump's dist[] slot (0 none; 1 down to a lower target), +0x2c the melee states it goes through, from
   +0x30. */
static uint32_t prow(uint32_t idx, unsigned off) { return exe_u32(POSITIONS_VA + POSITION_SIZE * idx + off); }

/* AI2iMelee_WeightTechnique 0x4a2580: w->weight (the caller's base) scaled for the technique against
   the target: the attack's blockability (the profile's factors), its facing requirements (from the
   front or behind, falling off within 30 degrees of the side), then its moves in turn: positions add
   their transitions' motion and time (a delay position waits for the target to come in range, at its
   animation's speed plus the target's closing), maneuvers and evasions by their own measures, and the
   first attack is timed and turned toward where the target will be and checked by the prediction's
   bounds (TRrCheckAnimationBounds); throws need a throwable target at their reach. x is the context of
   the character against the target (for an evasion, of the target's attack against the character);
   attacking: weighing an attack (else an evasion); dir the technique's direction; state the melee state
   and anim_state the animation state it is in; blockable/high/low ONrCharacter_CouldBlock's. */
static void weigh_technique(const AiState *a, const Character *c, ChrIntersect *x, bool attacking, uint32_t dir,
                            uint32_t state, uint16_t anim_state, bool blockable, bool high, bool low, Weighing *w)
{
    const MeleeState *m = &a->melee;
    const MeleeProfile *p = m->profile;
    const MeleeTech *k = w->tech;
    const Character *t = m->target;
    if (!k->count) {
        w->weight = 0.0f;
        return;
    }
    uint32_t fl = k->computed;
    if (fl & 0x1000) {
        /* an attack: what the target's block would do to it */
        if (!attacking) {
            w->weight = 0.0f;
            return;
        }
        if (!blockable) w->weight *= p->factor[0];
        else if ((!low && (fl & 0x8000)) || (!high && (fl & 0x10000))) w->weight *= p->factor[1];
        else if (fl & 0x200000) w->weight *= p->factor[2];
        else if (fl & 0x800000) w->weight *= p->factor[3];
        else if (fl & 0x400000) w->weight *= p->factor[4];
        else w->weight *= p->factor[5];
    } else if (fl & 0x1000000) {
        /* a disarm: with flag 0x8000000 or 0x10000000 a target holding a pistol (not a rifle) and the
           character empty-handed */
        if (!attacking) {
            w->weight = 0.0f;
            return;
        }
        bool target_gun = t->weapon && !(weapon_options(t->weapon) & ONWC_RIFLE), own = c->weapon != NULL;
        if (fl & 0x8000000) {
            if (!target_gun || own) {
                w->weight = 0.0f;
                return;
            }
        } else if ((fl & 0x10000000) && (!target_gun || own)) {
            w->weight = 0.0f;
            return;
        }
    }
    /* the facings: within 90 degrees of each other the character is behind the target */
    float df = wrap_pi(t->facing - c->facing), adf = fabsf(df);
    bool behind = adf < 1.5707963705062866f;
    if (fl & 0x6000000) {
        float f;
        if (behind) {
            if (fl & 0x2000000) {
                w->weight = 0.0f;
                return;
            }
            f = (1.5707963705062866f - adf) * 1.909859299659729f;
        } else {
            if (fl & 0x4000000) {
                w->weight = 0.0f;
                return;
            }
            f = (adf - 1.5707963705062866f) * 1.909859299659729f;
        }
        if (f <= 0.0f) f = 0.0f;
        if (f < 1.0f) w->weight *= f;
    }
    chr_intersect_restore(x);
    float loc[3], tloc[3];
    chr_location(c, loc);
    chr_location(t, tloc);
    float pos[3] = { 0 }, vel[3] = { 0 }, left = 0.0f, closing = 0.0f;
    if (attacking) {
        /* the target's place and motion in the character's space, the motion along the direction */
        pos[0] = x->m[9], pos[1] = x->m[10], pos[2] = x->m[11];
        left = m->dist - x->t_radius;
        chr_matrix3_apply(x->m, x->t_velocity, vel);
        switch (dir) {
        case 0: case 1: closing = -vel[1]; break;
        case 2: closing = vel[1]; break;
        case 3: closing = -vel[0]; break;
        case 4: closing = vel[0]; break;
        case 5: closing = 0.0f; break;
        default: break;
        }
    }
    w->offset = 0.0f;
    w->air_ticks = x->a.now.air ? x->a.now.air_ticks : 0;
    w->moving = false;
    w->step = 0.0f;
    w->rot_cos = 1.0f;
    w->rot_sin = 0.0f;
    w->rot_dir = 0;
    float pre_along = 0.0f, pre_ticks = 0.0f, post_along = 0.0f, post_ticks = 0.0f;
    float d_min = 0.0f, d_max = 0.0f, d_ramp = 0.0f, d_speed = 0.0f;
    bool delay = false, in_air = false, after_position = false;
    uint32_t cur = state;
    uint16_t cur_anim = anim_state;
    for (uint32_t j = 0; j < k->count; j++) {
        const MeleeMove *mv = &p->move[k->first + j];
        uint32_t idx = mv->type & 0xfffffffu;
        switch (category(mv)) {
        case MELEE_ATTACK: {
            if (!attacking || !mv->anim || !tr_is_attack(mv->anim)) {
                w->weight = 0.0f;   /* ("BROKEN") */
                return;
            }
            if (tr_test_flag(mv->anim, 0x16) && combat_waiting_for_shield_charge((AiState *)a, c)) {
                w->weight = 0.0f;
                return;
            }
            if (!w->attack) {
                w->attack_at = (int32_t)j;
                w->attack = mv;
            }
            float ticks = pre_ticks + post_ticks, along = pre_along + post_along, f;
            if (!delay) {
                if (w->moving) {
                    /* a jump in: the target beyond the run and half the jump */
                    float need = along + 0.5f * w->step * p->jump_time + x->t_radius;
                    if (!in_air) need += ticks * closing;
                    if (left < need) {
                        w->weight = 0.0f;
                        return;
                    }
                    bool danger = weight_local_movement(m, k, along + w->step * p->jump_time + mv->motion, &f);
                    w->weight *= f;
                    if (danger) return;
                }
            } else {
                float ac = left - along;
                if (w->moving) ac -= 0.5f * w->step * p->jump_time;
                if (w->stop_move == -1 && d_min >= 1.0f) {
                    if (ac < d_min) {
                        w->weight = 0.0f;
                        return;
                    }
                    if (left < d_min + d_ramp) w->weight *= (ac - d_min) / d_ramp;
                }
                bool stopping = false;
                float a4 = left - x->t_radius;
                if (!w->moving) {
                    a4 -= k->near;
                    if (w->stop_move != -1 && a4 < -3.0f) stopping = true;
                } else {
                    a4 -= 0.5f * w->step * (p->jump_time + p->long_jump_time);
                }
                if (!stopping) {
                    w->stop_move = -1;
                    a4 -= along;
                    if (!in_air) a4 -= ticks * closing;
                    if (a4 <= 0.0f) a4 = 0.0f;
                    if (d_max < a4) {
                        w->weight = 0.0f;
                        return;
                    }
                    if (d_max - d_ramp < a4) w->weight *= (d_max - a4) / d_ramp;
                    float time = a4 / (in_air ? d_speed : d_speed + closing);
                    if (time < 0.0f || time > 120.0f) {
                        w->weight = 0.0f;
                        return;
                    }
                    along += a4;
                    ticks += time;
                } else {
                    along = 0.0f;
                    ticks = 0.0f;
                }
                bool danger = weight_local_movement(m, k, along + mv->motion, &f);
                w->weight *= f;
                if (danger) return;
            }
            /* against the target's own attack under way */
            if (m->hit_frames != -1) {
                if ((float)m->hit_frames <= ticks + (float)attack_point_frame(mv->anim, false)) w->weight *= 0.3f;
                else w->late = true;
            }
            /* where the target will be, the attack turned toward it */
            if (ticks > 0.0f && !in_air) pos[0] += vel[0] * ticks, pos[1] += vel[1] * ticks;
            if (!x->a.now.air) {
                w->rot_dir = dir;
                float len = sqrtf(pos[0] * pos[0] + pos[1] * pos[1]);
                switch (dir) {
                case 1: case 5: w->rot_sin = pos[0] / len, w->rot_cos = pos[1] / len; break;
                case 2: w->rot_sin = -pos[0] / len, w->rot_cos = -pos[1] / len; break;
                case 3: w->rot_sin = -pos[1] / len, w->rot_cos = pos[0] / len; break;
                case 4: w->rot_sin = pos[1] / len, w->rot_cos = -pos[0] / len; break;
                default: break;
                }
                w->weight *= fabsf(w->rot_cos);
            } else {
                w->rot_dir = 0;
                w->rot_sin = 0.0f;
                w->rot_cos = 1.0f;
            }
            w->offset = in_air ? along : ticks * closing + along;
            rotate_context(x, w);
            setup_attacker(p, &x->a, c, w->moving, w->step, dir, mv->anim);
            if (!chr_intersect_bounds(x)) w->weight = 0.0f;
            return;
        }
        case MELEE_POSITION: {
            if (idx >= POSITIONS) {
                w->weight = 0.0f;
                return;
            }
            bool row_delay = (prow(idx, 0x24) & 0xff) != 0;
            uint32_t slot = prow(idx, 0x28), n = prow(idx, 0x2c);
            if ((!attacking && row_delay) || (after_position && row_delay)) {
                w->weight = 0.0f;
                return;
            }
            if (!(fl & 0x20000000) && !k->attack && k->throw_dist == 0.0f) {
                w->weight = 0.0f;
                return;
            }
            if (slot == 1 || slot == 2) {
                in_air = true;
                if (slot == 1 && loc[1] + 7.0f > tloc[1]) {
                    w->weight = 0.0f;
                    return;
                }
            }
            /* the current state among the row's: from the next; at its end, a delay stops there */
            uint32_t s = 0;
            while (s < n && cur != prow(idx, 0x30 + 4 * s)) s++;
            if (s < n) {
                s++;
                if (s == n) {
                    if ((!row_delay || mv->p[0] < 1.0f) && w->stop_move == -1) w->stop_move = (int16_t)j;
                    s = 0;
                }
            } else {
                s = 0;
            }
            if (s == 0) {
                if (!transition_length(p, cur, cur_anim, prow(idx, 0x30), &w->weight, &pre_along, &pre_ticks, NULL)) {
                    w->weight = 0.0f;   /* ("NOTRANS-PRE") */
                    return;
                }
                s = 1;
            }
            if (s == 1 && (delay = row_delay)) {
                if (!mv->anim) {
                    w->weight = 0.0f;
                    return;
                }
                d_min = mv->p[0], d_max = mv->p[1], d_ramp = mv->p[2];
                float v[3];
                tr_position(mv->anim, 0, v);
                switch (prow(idx, 0x20)) {
                case 1: d_speed = v[2]; break;
                case 2: d_speed = -v[2]; break;
                case 3: d_speed = v[0]; break;
                case 4: d_speed = -v[0]; break;
                default: break;
                }
            }
            for (; s < n; s++) {
                if (!transition_length(p, cur, cur_anim, prow(idx, 0x30 + 4 * s), &w->weight, &post_along, &post_ticks,
                                       NULL)) {
                    w->weight = 0.0f;   /* ("NOTRANS-POST") */
                    return;
                }
            }
            after_position = true;
            if (slot) {
                w->moving = true;
                w->step = p->dist[slot];
            }
            break;
        }
        case MELEE_MANEUVER:
            if (!attacking) {
                w->weight = 0.0f;
                return;
            }
            switch (idx) {
            case 0: {
                /* advance: the target beyond its range and threshold, ramping over the threshold */
                float f = m->dist - mv->p[1] - mv->p[2];
                if (f < 0.0f) {
                    w->weight = 0.0f;
                    return;
                }
                if (f < mv->p[2]) w->weight *= f / mv->p[2];
                break;
            }
            case 1: {
                float f = mv->p[1] + mv->p[2] - m->dist;
                if (f < 0.0f) {
                    w->weight = 0.0f;
                    return;
                }
                if (f < mv->p[2]) w->weight *= f / mv->p[2];
                break;
            }
            case 2: case 3: {
                /* circling: the target's heading from the way to it, turned back (degrees), inside the
                   range of the first two numbers, by how deep (within 10) */
                float h = wrap_2pi(t->desired_facing - t->facing_offset);
                float d = h - (m->angle - 3.1415927410125732f);
                if (d < -3.1415927410125732f) d += TWO_PI;
                else if (d > 3.1415927410125732f) d -= TWO_PI;
                d *= 57.295780181884766f;
                float v;
                if (mv->p[0] < mv->p[1]) {
                    float u = d - mv->p[0], o = mv->p[1] - d;
                    v = u <= o ? u : o;
                } else {
                    float u = d - mv->p[1], o = mv->p[0] - d;
                    v = u <= o ? u : o;
                }
                if (v < 0.0f) {
                    w->weight = 0.0f;
                    return;
                }
                if (v < 10.0f) w->weight *= v * 0.1f;
                break;
            }
            case 8:
                return;     /* random stop: the weighing ends there */
            case 0xd:
                /* max range */
                if (!mv->anim) {
                    w->weight = 0.0f;
                    return;
                }
                if (tr_test_flag(mv->anim, 0x16) && combat_waiting_for_shield_charge((AiState *)a, c)) {
                    w->weight = 0.0f;
                    return;
                }
                if (m->dist > mv->p[0]) w->weight = 0.0f;
                break;
            default:
                break;
            }
            break;
        case MELEE_EVADE: {
            if (attacking || !mv->anim) {
                w->weight = 0.0f;
                return;
            }
            /* where the evasion ends, in the world (from the character's facing), then seen from the
               target: ahead of it it weighs less (1 + 2 ahead / across) */
            float cf = cosf(c->facing), sf = sinf(c->facing);
            float pp[2] = { 0.0f, 0.0f };
            position_point(mv->anim, (uint16_t)(tr_duration(mv->anim) - 1), pp);
            float ex = pp[1] * sf + pp[0] * cf, ez = pp[1] * cf - pp[0] * sf;
            float tc = cosf(t->facing), ts = sinf(t->facing);
            float ahead = ts * ex + tc * ez, across = tc * ex - ts * ez;
            float f = 1.0f + (ahead + ahead) / fabsf(across);
            if (f < 0.0f) {
                w->weight = 0.0f;
                return;
            }
            if (f < 1.0f) w->weight *= f;
            float to[3] = { ex + loc[0], loc[1], ez + loc[2] }, at[3];
            bool reached = false, crossed;
            uint8_t kind;
            if (local_try_movement(c, loc, to, 3, &reached, at, &kind, &crossed)) {
                if ((fl & 4) && !reached && kind == 8) reached = true;
            } else {
                reached = false;
            }
            if (!reached) {
                w->weight = 0.0f;
                return;
            }
            /* the target's attack against the character evading */
            x->t.frame = 0;
            x->t.anim = mv->anim;
            if (chr_intersect(x)) w->weight *= 0.3f;
            break;
        }
        case MELEE_THROW: {
            if (!attacking || !mv->anim || !tr_test_flag(mv->anim, 0xf) || !mv->anim2 || !tr_test_flag(mv->anim2, 0x10)) {
                w->weight = 0.0f;
                return;
            }
            /* a throw needs the side its technique asks for (else the original reports it) */
            bool front = throw_from_front(tr_type(mv->anim));
            if (behind ? front : !front) {
                w->weight = 0.0f;
                return;
            }
            if (!throw_target_ok(t)) {
                w->weight = 0.0f;
                return;
            }
            float a4 = 0.0f, f;
            if (delay) {
                a4 = left - 0.9f * throw_distance_of(mv->anim);
                if (w->stop_move == -1 || a4 >= -3.0f) {
                    w->stop_move = -1;
                    a4 -= pre_along + post_along;
                    if (a4 <= 1.0f) a4 = 1.0f;
                    if (a4 < d_min) {
                        w->weight = 0.0f;
                        return;
                    }
                    if (a4 < d_min + d_ramp) w->weight *= (a4 - d_min) / d_ramp;
                    if (d_max < a4) {
                        w->weight = 0.0f;
                        return;
                    }
                    if (d_max - d_ramp < a4) w->weight *= (d_max - a4) / d_ramp;
                    float time = a4 / (in_air ? d_speed : d_speed + closing);
                    if (time < 0.0f || time > 120.0f) {
                        w->weight = 0.0f;
                        return;
                    }
                } else {
                    pre_along = post_along = 0.0f;
                }
            }
            bool danger = weight_local_movement(m, k, a4 + mv->motion, &f);
            w->weight *= f;
            if (danger) return;
            /* where the victim lands: toward a danger, the profile's factor */
            if (weight_local_movement(m, k, left + mv->motion2, NULL)) w->weight *= p->throw_danger;
            return;
        }
        default:
            w->weight = 0.0f;
            return;
        }
    }
}

/* AI2iMelee_Position_GetCurrentAnimState 0x4a5ef0: the animation state the character is in (the
   interpolation's start, else its animation's from state, but within 3 frames of its end, or with
   none, the state it goes to; 0 past the table), and its melee state (0x6377e0 with the keys held). */
static uint16_t current_anim_state(const AiState *a, const Character *c, uint32_t *mstate)
{
    uint16_t s = 0;
    if (c->anim) {
        if ((uint16_t)(tr_duration(c->anim) - c->frame) > 3)
            s = c->interpolating ? c->interp_state : tr_from(c->anim);
        if (!s) s = tr_to(c->anim);
    }
    if (s >= sizeof melee_state_of) s = 0;     /* ("has an illegal anim state") */
    if (mstate) *mstate = state_of(s, a->held);
    return s;
}

static bool row_flag(uint32_t idx, unsigned off) { return exe_data(POSITIONS_VA + POSITION_SIZE * idx + off, 1)[0] != 0; }

/* The target's place in the character's space along a direction: 1 and 5 forward, 2 back, 3 and 4
   the sides (the second table also counts 0 as forward). */
static float along_dir(const ChrIntersect *x, uint32_t dir, bool zero_forward)
{
    switch (dir) {
    case 0: return zero_forward ? x->m[10] : 0.0f;
    case 1: case 5: return x->m[10];
    case 2: return -x->m[10];
    case 3: return x->m[9];
    case 4: return -x->m[9];
    default: return 0.0f;
    }
}

/* AI2iMelee_Position_Update 0x4a6040: the position's transitions through its melee states, the keys
   of each (or the state's own, 0x52abc0) held. In the state it waits for: with an attack still ahead,
   the attack predicted from where the position's remaining transitions take the character and the
   target (TRrCheckAnimationBounds); on the ground a position that can't reach goes back to its first
   step (started part way) or, a delay, waits within the move's range (beyond it, or too close, the
   technique fails); a jump in waits until the attack would land in the first two thirds of its
   attack frames (dropping the jump key once if it would go over, then four ticks); a throw ahead waits
   for the target within its reach. Turned more than 120 degrees from the facing wanted, nothing is
   pressed (a minute of it fails the technique); a stop with only a direction held steps that way
   (0x4a6b30). */
static bool position_update(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeProfile *p = m->profile;
    const MeleeMove *mv = m->mv;
    const MeleeTech *k = m->tech;
    const Character *t = m->target;
    uint32_t idx = mv->type & 0xfffffffu;
    if (idx >= POSITIONS || !k || !t) return abort_technique(m);
    uint32_t dir = prow(idx, 0x20), slot = prow(idx, 0x28), n = prow(idx, 0x2c);
    bool delay = row_flag(idx, 0x24);
    uint32_t mstate;
    uint16_t astate = current_anim_state(a, c, &mstate);
    bool stop = false;
    float ticks = 0.0f;
    if (m->pos_state == mstate) {
        /* facing off more than 15 degrees: a minute of it fails; the row's last step stops */
        if (m->face_off > 0.2617993950843811f) {
            if (m->m60) {
                if (--m->m60 == 0) return abort_technique(m);
            } else {
                m->m60 = 60;
            }
            if (row_flag(idx, 0x25) && m->pos_step == n - 1) stop = true;
        }
        if ((delay && m->pos_step == 0) || slot) {
            float along = 0.0f;
            if (!row_flag(idx, 0x26)) {
                for (uint32_t i = m->pos_step; i < n; i++)
                    if (!transition_length(p, mstate, astate, prow(idx, 0x30 + 4 * i), NULL, &along, &ticks, NULL))
                        return abort_technique(m);
            }
            if (k->attack && m->move < (uint32_t)k->attack_move) {
                ChrIntersect x;
                chr_intersect_setup(c, t, true, &x);
                if (along > 0.0f) {
                    /* the attacker moved on by the transitions' motion along the direction */
                    float f = x.a.now.facing, s = sinf(f), co = cosf(f);
                    float *ap = x.a.now.pos;
                    switch (dir) {
                    case 1: x.m[10] -= along, ap[0] += s * along, ap[2] += co * along; break;
                    case 2: x.m[10] += along, ap[0] -= s * along, ap[2] -= co * along; break;
                    case 3: x.m[9] -= along, ap[0] += co * along, ap[2] -= s * along; break;
                    case 4: x.m[9] += along, ap[0] -= co * along, ap[2] += s * along; break;
                    default: break;
                    }
                }
                if (slot != 1 && slot != 2 && ticks > 0.0f) {
                    /* and the target by its motion over the transitions' ticks */
                    float v[3];
                    chr_matrix3_apply(x.m, x.t_velocity, v);
                    v[0] *= ticks, v[1] *= ticks, v[2] *= ticks;
                    x.m[9] += v[0], x.m[10] += v[1], x.m[11] += v[2];
                    float s = sinf(x.a.now.facing), co = cosf(x.a.now.facing);
                    x.t.now.pos[0] += co * v[0] + s * v[1];
                    x.t.now.pos[1] += v[2];
                    x.t.now.pos[2] += co * v[1] - s * v[0];
                }
                setup_attacker(p, &x.a, c, m->moving, m->step, dir, k->attack);
                if (!chr_intersect_bounds(&x)) stop = true;
                uint32_t raw = c->anim ? melee_state_of[tr_to(c->anim) < sizeof melee_state_of ? tr_to(c->anim) : 0] : 0;
                bool jumping = raw >= 13 && raw <= 17;     /* AI2rMelee_StateIsJumping */
                if (!jumping && !in_air(c)) {
                    if (stop) {
                        if (m->pos_mid) {
                            m->pos_step = 0;
                            m->pos_state = prow(idx, 0x30);
                            m->pos_trans = NULL;
                            m->pos_mid = false;
                            return false;
                        }
                        if (!delay || m->pos_step != 0) return abort_technique(m);
                        if (m->face_off < 0.2617993950843811f) {
                            /* a delay: the target within the move's range (and 5) of the attack */
                            float d = along_dir(&x, dir, false) - x.t_radius;
                            d -= m->moving ? 0.5f * m->step * (p->long_jump_time + p->jump_time) : k->near;
                            if (d < 0.0f || d > mv->p[2] + mv->p[1] + 5.0f) return abort_technique(m);
                            if (c->blocked_by_char) return abort_technique(m);
                        }
                    } else if (!c->blocked_by_char && ((uint32_t)m->keys & 0x6600000) && !chr_intersect(&x) &&
                               x.result == CHR_ISECT_TOO_FAR) {
                        stop = true;
                        goto keys;
                    } else {
                        goto next;
                    }
                } else {
                    /* in a jump: on when the attack would land in the first two thirds of its frames */
                    float d = along_dir(&x, dir, true);
                    if (d < x.t_radius + k->near) {
                        m->m66 = true;
                    } else {
                        bool ok = false;
                        if (chr_intersect(&x)) {
                            if (m->pos_68) {
                                m->pos_68--;
                            } else {
                                uint16_t last = attack_point_frame(x.a.anim, true), first = attack_point_frame(x.a.anim, false);
                                if (first < x.a.frame) first = x.a.frame;
                                int lim = (int)last - ((int)last - (int)first) / 3;
                                ok = x.frames <= (uint32_t)lim;
                            }
                        }
                        if (!ok) {
                            if (m->pos_65 && x.result == CHR_ISECT_TOO_HIGH) {
                                m->pos_65 = false;
                                m->keys &= ~(uint64_t)0x8000000;
                                m->pos_68 = 4;
                            }
                            stop = true;
                            goto keys;
                        }
                    }
                }
            } else if ((k->computed & 0x1000000) && k->throw_dist > 0.0f) {
                /* a throw ahead: on until the target is within its reach (its closing over the ticks) */
                float r = m->dist - 0.9f * k->throw_dist;
                if (ticks > 0.0f) {
                    float v = 0.0f;
                    if (dir == 0 || dir == 1) {
                        float dx = t->pos[0] - t->prev_pos[0], dz = t->pos[2] - t->prev_pos[2];
                        v = -(sinf(c->facing) * dx + cosf(c->facing) * dz);
                    }   /* (else the original reports the profile's direction) */
                    r -= v * ticks;
                }
                if (r > 0.0f) stop = true;
                if (stop) {
                    if (m->pos_mid) {
                        m->pos_step = 0;
                        m->pos_state = prow(idx, 0x30);
                        m->pos_trans = NULL;
                        m->pos_mid = false;
                        return false;
                    }
                    if (!delay || m->pos_step != 0) return abort_technique(m);
                    if (c->blocked_by_char) return abort_technique(m);
                } else {
                    goto next;
                }
            } else {
                stop = false;
                goto next;
            }
        }
        if (stop) goto keys;
    next:
        m->pos_step++;
        if (m->pos_step >= n) {
            m->keys_set = true;
            return start_next_move(a, c);
        }
        m->pos_state = prow(idx, 0x30 + 4 * m->pos_step);
    }
keys:
    if (m->pos_state != mstate && !(m->pos_trans && m->pos_trans->from == mstate)) {
        /* the transition toward the state waited for, its keys */
        const MeleeTransition *tr = check_state_transition(c, p, mstate, m->pos_state, astate);
        m->pos_trans = tr;
        if (!tr) return abort_technique(m);
        m->keys = (uint64_t)tr->held | (uint64_t)tr->down << 32;
        m->pos_65 = (m->keys & 0x8000000) != 0;
        if (m->pos_65 && weight_local_movement(m, k, 0.5f * m->step * (p->long_jump_time + p->jump_time), NULL))
            return abort_technique(m);
    }
    if (!m->pos_trans) {
        /* in the state: its own keys */
        m->keys = (uint64_t)exe_u32(0x52abc0 + 8 * mstate) | (uint64_t)exe_u32(0x52abc4 + 8 * mstate) << 32;
        m->pos_65 = (m->keys & 0x8000000) != 0;
    }
    m->hold_facing = false;
    if (m->face_off > 2.094395160675049f) {
        if (m->pos_step) {
            if (m->m60) {
                if (--m->m60 == 0) return abort_technique(m);
            } else {
                m->m60 = 60;
            }
        }
        move_override(a, 0);
        return false;
    }
    uint32_t mvk = (uint32_t)m->keys & 0x6600000;
    if (stop && !in_air(c) && mvk && mvk == (uint32_t)m->keys && !(m->keys >> 32)) {
        /* only a direction held: a step that way */
        uint32_t lo = (uint32_t)m->keys;
        int sd = (lo & 0x200000) ? 1 : (lo & 0x2000000) ? 3 : (lo & 0x400000) ? 2 : 4;
        if (maneuver_step(a, c, sd, false)) return false;
        return abort_technique(m);
    }
    move_override(a, m->keys);
    if (m->keys == 0x10000000 && mstate == 1) {
        a->full_turn = true;        /* 0x498b90 */
        a->facing_override = 1000.0f;
    }
    return false;
}

/* AI2iMelee_Position_Start 0x4a5e40: past the weighing's last needed move (+0x44) the move is done;
   a position without a delay starts at the step of its states the character is in (part way). */
static bool position_start(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    if ((uint32_t)(uint16_t)m->stop_move == m->move) return start_next_move(a, c);
    uint32_t idx = m->mv->type & 0xfffffffu;
    if (idx >= POSITIONS) return abort_technique(m);
    m->pos_step = 0;
    m->pos_mid = false;
    if (!row_flag(idx, 0x24)) {
        uint32_t mstate, n = prow(idx, 0x2c), i = 0;
        current_anim_state(a, c, &mstate);
        while (i < n && mstate != prow(idx, 0x30 + 4 * i)) i++;
        if (i > 0 && i < n) {
            m->pos_step = i;
            m->pos_mid = true;
        }
    }
    m->pos_state = prow(idx, 0x30 + 4 * m->pos_step);
    m->pos_trans = NULL;
    m->pos_65 = false;
    m->pos_68 = 0;
    return position_update(a, c);
}

/* The maneuvers' weighing when the AI may not attack (AI2iMelee_WeightSpacingBehavior, in the update
   with 0x545778): advancing wants the target beyond its range, retreating within, by how far over the
   threshold (at least 5); circling by the other attackers around the target (toward the side they
   leave, away from those near it); an evasion needs the way its animation goes clear; attacks,
   positions, throws and "max range" rule it out. False: none. */
static bool weigh_spacing(const AiState *a, const Character *c, Weighing *w)
{
    const MeleeState *m = &a->melee;
    const MeleeTech *k = w->tech;
    for (uint32_t j = 0; j < k->count; j++) {
        const MeleeMove *mv = &m->profile->move[k->first + j];
        uint32_t cat = category(mv), idx = mv->type & 0xfffffffu;
        if (cat == MELEE_MANEUVER) {
            float th = mv->p[2] < 5.0f ? 5.0f : mv->p[2], f;
            switch (idx) {
            case 0:
                f = (m->dist - mv->p[1]) / th;
                if (f < 0.0f) return false;
                w->weight *= f;
                break;
            case 1:
                f = (mv->p[1] - m->dist) / th;
                if (f < 0.0f) return false;
                w->weight *= f;
                break;
            case 2: case 3: {
                /* the others fighting the target: those within half the range's degrees of the angle
                   (the move's first number), nearer than 10 or on the ground, push the circling */
                f = 0.0f;
                const Character *self = c;
                for (int i = 0; i < chars_slots(); i++) {
                    const Chr *o = chars_get(i);
                    if (!o || &o->c == self || o->player || o->ai.state != AI_STATE_COMBAT || !o->ai.target ||
                        &o->ai.target->c != m->target)
                        continue;
                    const MeleeState *om = &o->ai.melee;
                    float dd = om->dist - m->dist;
                    if (!(dd <= 10.0f || !om->cookie)) continue;
                    float range = mv->p[0] * 0.01745329238474369f;
                    float da = wrap_pi(om->angle - m->angle);
                    if (fabsf(da) > range) continue;
                    if (idx == 2) da = -da;
                    float lim = fabsf(dd) < 10.0f ? 0.0f : 0.3490658402442932f;
                    if (da > lim) {
                        if (da < 0.5235987901687622f) f -= 1.0f;
                        else f -= 1.0f - (da - 0.5235987901687622f) / (range - 0.5235987901687622f);
                    } else if (da < 0.0f) {
                        if (da > -0.5235987901687622f) f += 1.0f;
                        else f += 1.0f - (-da - 0.5235987901687622f) / (range - 0.5235987901687622f);
                    }
                }
                f += 1.0f;
                if (f <= 0.0f) return false;
                w->weight *= f;
                break;
            }
            case 0xd:
                return false;
            default:
                break;
            }
        } else if (cat == MELEE_EVADE) {
            /* the way the evasion's animation goes (its last position point, turned to the facing) */
            if (!mv->anim) return false;
            float s = sinf(c->facing), co = cosf(c->facing), pt[2], loc[3], at[3];
            if (!position_point(mv->anim, (uint16_t)(tr_duration(mv->anim) - 1), pt)) return false;
            chr_location(c, loc);
            float to[3] = { pt[0] * co + pt[1] * s + loc[0], loc[1], pt[1] * co - pt[0] * s + loc[2] };
            bool reached, crossed;
            uint8_t kind;
            if (!local_try_movement(c, loc, to, 3, &reached, at, &kind, &crossed)) return false;
            if (k->computed & 4) {
                if (!reached && kind != 8) return false;
            } else if (!reached) {
                return false;
            }
        } else {
            return false;   /* attacks, positions, throws */
        }
    }
    return true;
}

/* The pick by weight (seed +0xa0 or +0xa2): the first whose running total reaches the random part of the
   total, less 0.01. */
static int pick(const Weighing *w, int n, float total, uint16_t seed, float *leftover)
{
    float r = (float)seed * total * 1.5259021893143654e-05f - 0.009999999776482582f, sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += w[i].weight;
        if (r <= sum) {
            *leftover = sum - r;
            return i;
        }
    }
    return -1;
}

/* The evasion's chance (AI2iMelee_CheckEvasionChance): rolled once for each animation of the target's,
   the profile's first dodge chance (+0x84) times the difficulty's (game settings +0xab0); passed, its
   attack's damage makes the second: +0x88 plus +0x8c scaled by the damage over +0x90 (all of it
   beyond), times +0xac8's. */
static void check_evasion_chance(MeleeState *m, const Character *tg)
{
    const MeleeProfile *p = m->profile;
    if (tg->sound_serial == m->evade_serial) return;
    m->hit_frames = -1;
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    int d = persist_difficulty();
    float f1 = gs ? rdf(gs + 0xab0 + 4 * d) : 1.0f, f2 = gs ? rdf(gs + 0xac8 + 4 * d) : 1.0f;
    float chance = (float)p->dodge[0] * f1;
    if ((float)(((trac_random() & 0xffffu) * 100u) / 0xffffu) < chance) {
        m->evade_check = true;
        uint32_t damage = tg->anim ? attack_damage(tg->anim) : 0;
        uint32_t extra = damage < p->dodge[3] ? (p->dodge[2] * damage) / p->dodge[3] : p->dodge[2];
        float second = ((float)p->dodge[1] + (float)(int32_t)extra) * f2;
        m->evade_ok = (float)(((trac_random() & 0xffffu) * 100u) / 0xffffu) < second;
    } else {
        m->evade_check = false;
    }
    m->evade_serial = tg->sound_serial;
}

/* AI2rMelee_Update 0x4a01f0: one tick of the melee against the target. */
bool melee_update(AiState *a, Character *c, const Character *target)
{
    MeleeState *m = &a->melee;
    m->target = target;
    if (!target || !m->profile) return no_technique(a, c);
    chr_enter_fight_mode(c);    /* 0x4eb0b0 */
    update_facing(m, c);
    float dx = c->pos[0] - target->pos[0], dy = c->pos[1] - target->pos[1], dz = c->pos[2] - target->pos[2];
    m->dist = sqrtf(dx * dx + dy * dy + dz * dz);
    const Chr *tc = (const Chr *)target;
    if (tc->player && !ui_input_enabled()) {      /* AI2rAllowedToAttack 0x48bc30 */
        m->override = false;    /* AI2rExecutor_ClearAttackOverride */
        return no_technique(a, c);
    }
    const MeleeProfile *p = m->profile;
    if (m->target != m->last_target) {
        m->blocking = false;
        m->evade_serial = (uint32_t)-1;
        m->last_target = m->target;
    }
    /* holding a block against the target's attack while it lasts */
    if (m->blocking) {
        if (target->sound_serial == m->block_serial && target->anim && tr_is_attack(target->anim) &&
            !throw_target_type(target->anim) && target->frame < attack_point_frame(target->anim, true)) {
            apply_facing(a);
            move_override(a, m->block_keys);
            return true;
        }
        m->blocking = false;
    }
    bool going = false;     /* a move under way goes on */
    m->busy = (c->anim && (tr_is_attack(c->anim) || tr_test_flag(c->anim, 15))) || m->override || in_air(c);
    if (m->mv) {
        ai_halt(a);
        apply_facing(a);
        a->facing_mode = 0;
        going = !update_move(a, c);
    }
    if (m->busy) {
        /* "waiting for moves to finish" */
        apply_facing(a);
        return true;
    }
    if (!m->mv && m->tech) {
        /* the technique is over: its memory fades */
        m->tech = NULL;
        clear_technique(m);
        for (uint32_t i = 0; i < p->nattack && i < MELEE_MEMORY; i++)
            m->memory[i] = m->memory[i] == 1000.0f ? 0.0f : m->memory[i] * 0.6f;
    }
    bool may_change = going ? cat_can_evade(m, c) : true;
    bool hit_coming = false;
    ChrIntersect evx, atx;     /* the target's attack on the character, and the character's on it */
    if (may_change && target->anim && tr_is_attack(target->anim)) {
        check_evasion_chance(m, target);
        if (m->evade_check) {
            /* the target's attack predicted against the character (TRrCheckAnimationBounds, then
               TRrIntersectAnimations) */
            chr_intersect_setup(target, c, false, &evx);
            if (chr_intersect_bounds(&evx) && chr_intersect(&evx)) {
                hit_coming = true;
                m->hit_frames = (int32_t)evx.frames;
            }
        }
    }
    if (going && !hit_coming) return true;
    uint16_t to = c->anim ? tr_to(c->anim) : 0;
    if (to > 0x45) to = 0;      /* ("has an illegal anim state") */
    bool fallen = to == 0x12 || to == 0x32;     /* ONrAnimState_IsFallen 0x4ecdf0 */
    uint32_t state = state_of(to, a->held);
    bool cookie = exe_data(0x545772, 1)[0] ? get_cookie(a, c, target) : true;
    const MeleeTech *chosen = NULL;
    Weighing w[MELEE_TECH_MAX];
    int picked = -1;
    memset(w, 0, sizeof w);
    /* chosen again unless committed to one, or always in the ultra mode (0x5eb849, "killmequick"; also
       with C+8 bit 2, a debugging switch nothing sets) */
    if (!m->tech || !m->committed || cheat_ultra_mode) {
        m->angle = angle_to_target(m, c);
        float narrow[6], wide[6];
        direction_weights(m->angle, c->facing, narrow, wide);
        uint32_t t = now_of(c);
        int n = 0;
        float total = 0.0f, best_attack = 0.0f;
        bool blockable = false, high = false, low = false;
        if (cookie) {
            chr_intersect_setup(c, target, true, &atx);
            blockable = chr_could_block(target, c, &high, &low);
        }
        uint32_t from, count;
        if (!cookie) {
            from = p->nattack + p->nevade;
            count = p->nmaneuver;
        } else {
            from = 0;
            count = p->nattack;
        }
        if (count > 32) count = 32;     /* ("exceeds max") */
        for (uint32_t i = 0; i < count; i++) {
            const MeleeTech *k = &p->tech[from + i];
            Weighing *e = &w[i];
            e->tech = k;
            e->weight = (float)k->weight;
            e->attack_at = -1;
            e->attack = NULL;
            e->moving = false;
            e->stop_move = -1;
            if (cookie && m->memory[i] == 1000.0f) {
                e->weight = 0.0f;
                continue;
            }
            if (k != m->tech && k->delay_slot >= 0 && k->delay_slot < MELEE_MEMORY && t < m->available[k->delay_slot]) {
                e->weight = 0.0f;
                continue;
            }
            if (cookie) {
                float mem = m->memory[i] < 0.8f ? m->memory[i] : 0.8f;
                e->weight = (1.0f - mem) * e->weight;
            }
            uint32_t dir = k->computed >> 8 & 0xf;
            /* (a direction or'd over a 5 can be 7: the original then reads past its six factors, into its
               other locals; none of the game's profiles makes one, so the port skips it) */
            if (dir >= 6) {
                e->weight = 0.0f;
                continue;
            }
            e->weight *= (k->flags & 2) ? wide[dir] : narrow[dir];
            if (e->weight == 0.0f) continue;
            if ((k->computed & 0xff) != state) {
                /* another state: only "any state" techniques, and with the cookie none of state 0 (an
                   error report) */
                if ((cookie && (k->computed & 0xff) == 0) || !(k->computed & 0x20000)) {
                    e->weight = 0.0f;
                    continue;
                }
            }
            if (cookie) weigh_technique(a, c, &atx, true, dir, state, to, blockable, high, low, e);
            else if (!(k->computed & 0x21001000)) {
                if (exe_data(0x545778, 1)[0] && k->count && !weigh_spacing(a, c, e)) e->weight = 0.0f;
            } else {
                e->weight = 0.0f;
            }
            if (e->weight == 0.0f) continue;
            if (k == m->tech) e->weight += e->weight;      /* the one under way, twice */
            total += e->weight;
            n++;
            if ((k->computed & 0x1001000) && best_attack <= e->weight) best_attack = e->weight;
        }
        /* the one under way, an attack (with the cookie) or a maneuver, kept while its weight holds */
        const MeleeTech *keep = NULL;
        if (m->tech && !(m->tech->computed & 0x80000) && (m->tech->computed & (cookie ? 0x40000 : 0x100000)) &&
            m->tech->index < count && m->leftover <= w[m->tech->index].weight) {
            keep = m->tech;
            if (cookie && hit_coming && w[m->tech->index].late) hit_coming = false;
        }
        bool clamp = exe_data(0x545771, 1)[0] && !fallen && cookie;
        if (n && clamp && best_attack != 0.0f) {
            /* with the cookie, what isn't an attack weighs no more than the heaviest attack */
            for (uint32_t i = 0; i < count; i++)
                if (!(w[i].tech->computed & 0x21001000) && best_attack < w[i].weight)
                    total -= w[i].weight - best_attack, w[i].weight = best_attack;
        }
        if (n && !keep) {
            for (;;) {
                if (!n) break;
                float left;
                int i = pick(w, (int)count, total, m->seed[0], &left);
                if (i < 0) break;
                /* an attack that the prediction says won't land is struck off */
                const Weighing *e = &w[i];
                bool ok = true;
                if (cookie && !e->moving && e->attack && category(e->attack) == MELEE_ATTACK) {
                    /* AI2iCheckTechniqueViability (inlined here): the weighing's context, turned as it was */
                    chr_intersect_restore(&atx);
                    rotate_context(&atx, e);
                    setup_attacker(p, &atx.a, c, e->moving, e->step, e->rot_dir, e->attack->anim);
                    ok = chr_intersect(&atx);
                }
                if (ok) {
                    m->leftover = left;
                    picked = i;
                    break;
                }
                total -= w[i].weight;
                n--;
                w[i].weight = 0.0f;
            }
            if (picked >= 0) {
                chosen = w[picked].tech;
                m->moving = w[picked].moving;
                m->step = w[picked].step;
                m->stop_move = w[picked].stop_move;
                if (hit_coming && w[picked].late) hit_coming = false;
            }
        } else if (keep) {
            chosen = keep;
        }
    }
    if (hit_coming && m->evade_ok) {
        /* an evasion: weighed by the state alone, through the weighing's second call */
        uint32_t t = now_of(c), count = p->nevade > 32 ? 32 : p->nevade;
        Weighing ev[MELEE_TECH_MAX];
        int n = 0;
        float total = 0.0f;
        for (uint32_t i = 0; i < count; i++) {
            const MeleeTech *k = &p->tech[p->nattack + i];
            Weighing *e = &ev[i];
            memset(e, 0, sizeof *e);
            e->tech = k;
            e->weight = (float)k->weight;
            e->stop_move = -1;
            if ((k->computed & 0xff) != state ||
                (k->delay_slot >= 0 && k->delay_slot < MELEE_MEMORY && t < m->available[k->delay_slot])) {
                e->weight = 0.0f;
                continue;
            }
            weigh_technique(a, c, &evx, false, 0, state, to, false, false, false, e);
            if (e->weight != 0.0f) total += e->weight, n++;
        }
        if (n) {
            float left;
            int i = pick(ev, (int)count, total, m->seed[1], &left);
            if (i >= 0) {
                m->leftover = left;
                m->moving = ev[i].moving;
                m->step = ev[i].step;
                m->stop_move = ev[i].stop_move;
                m->block_serial = target->sound_serial;
                chosen = ev[i].tech;
                w[0] = ev[i];
                picked = 0;
                hit_coming = false;
            }
        }
        if (hit_coming) {
            /* no evasion: block, low against a low attack or with the state's crouch (0x52ab98) */
            m->blocking = true;
            m->block_serial = target->sound_serial;
            if (any_attack_flag(target->anim, 1)) m->block_keys = 0;
            else if (any_attack_flag(target->anim, 2) || exe_data(0x52ab98 + state, 1)[0]) m->block_keys = CHR_KEY_CROUCH;
            else m->block_keys = 0;
            m->keys = 0;
            m->tech = NULL;
            m->mv = NULL;
            m->lead = NULL;
            m->move = 0;
            m->leftover = 0.0f;
            m->m60 = 0;
            clear_technique(m);
            update_facing(m, c);
            apply_facing(a);
            move_override(a, m->block_keys);
            return true;
        }
    }
    if (!chosen) {
        if (fallen) {
            move_override(a, CHR_KEY_FORWARD);      /* up from the floor */
            return true;
        }
        m->tech = NULL;
        m->mv = NULL;
        m->lead = NULL;
        clear_technique(m);
        memset(m->memory, 0, sizeof m->memory);
        return no_technique(a, c);
    }
    if (chosen != m->tech) {
        m->tech = chosen;
        m->mv = &p->move[chosen->first];
        m->lead = (picked >= 0 && w[picked].tech == chosen && w[picked].attack) ? w[picked].attack->anim : NULL;
        m->move = 0;
        m->leftover = picked >= 0 ? m->leftover : 0.0f;
        clear_technique(m);
        update_facing(m, c);
        apply_facing(a);
        if ((chosen->computed & 0x40000) && chosen->index < MELEE_MEMORY) m->memory[chosen->index] += 0.4f;
        if (!(chosen->computed & 0x21001000)) release_cookie(a);
    }
    if (m->mv->invalid) return !start_next_move(a, c) || m->tech != NULL;
    start_move(a, c);
    return m->tech != NULL;
}

/* AI2rBlockFunction 0x48b870 (the AI's C+0x15ec, asked by HandleAttackMask once the character could
   block): none without a profile, passive or non-aggressive (or with every AI passive, 0x5ec0c4) or
   panicking. The chance is the profile's single one; in combat with others on the same
   target it goes toward the group one by a third per other attacker, reaching it at four. Times the
   difficulty's factor (game settings +0xabc), against a random 0..100. */
bool melee_block(const AiState *a)
{
    const MeleeProfile *p = a->melee.profile;
    if (!p || ai_all_passive || (a->flags & (AI_FLAG_PASSIVE | AI_FLAG_NON_AGGRESSIVE)) || a->state == AI_STATE_PANIC) return false;
    float chance = (float)p->block_single;
    if (a->state == AI_STATE_COMBAT && a->target) {
        uint32_t n = fight_count(&a->target->c, NULL);
        if (n >= 4) chance = (float)p->block_group;
        else if (n)
            chance += (float)((int)p->block_group - (int)p->block_single) * (float)(int)(n - 1) * rdf(exe_data(0x52a5ac, 4));
    }
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    float factor = gs ? rdf(gs + 0xabc + 4 * persist_difficulty()) : 1.0f;
    return chance * factor > (float)(((trac_random() & 0xffff) * 100) / 0xffff);
}
