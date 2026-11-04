/* The AI's melee: see melee.h. */
#include "melee.h"

#include "chars.h"
#include "chr_anim.h"
#include "exe.h"
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

static uint32_t position_dir(uint32_t i) { return i < POSITIONS ? exe_u32(POSITIONS_VA + POSITION_SIZE * i + 0x20) : 0; }
static bool position_has_params(uint32_t i) { return i < POSITIONS && (exe_u32(POSITIONS_VA + POSITION_SIZE * i + 0x24) & 0xff); }

/* AI2rMelee_PrepareForUse 0x4a4650 for the class: each technique's kind and index, its delay slot,
   the state it starts from and its direction, its first attack and that attack's reach (the TRAM's
   +0x50, and that less half of +0x11c). Not ported: the moves' validity by the class's animations,
   the throws' flags. */
static void prepare(MeleeProfile *p, const void *oncc)
{
    if (p->prepared) return;
    p->prepared = true;
    Character probe;
    memset(&probe, 0, sizeof probe);
    probe.oncc = oncc;
    uint32_t nt = p->nattack + p->nevade + p->nmaneuver, slots = 0;
    for (uint32_t i = 0; i < nt; i++) {
        MeleeTech *k = &p->tech[i];
        k->computed = 0;
        k->attack = NULL;
        k->reach = k->near = 0.0f;
        k->delay_slot = -1;
        if (i < p->nattack) k->computed |= 0x40000, k->index = (uint16_t)i;
        else if (i < p->nattack + p->nevade) k->computed |= 0x80000, k->index = (uint16_t)(i - p->nattack);
        else k->computed |= 0x100000, k->index = (uint16_t)(i - p->nattack - p->nevade);
        if (!k->count || k->first + k->count > p->nmoves) {
            k->computed |= 0x4000;
            continue;
        }
        if (k->delay && slots < MELEE_MEMORY) k->delay_slot = (int16_t)slots++;
        uint32_t dir = 0;
        for (uint32_t j = 0; j < k->count; j++) {
            MeleeMove *mv = &p->move[k->first + j];
            uint32_t cat = mv->type >> 28, idx = mv->type & 0xfffffffu;
            mv->invalid = false;
            uint32_t d = 0;
            if (cat == MELEE_ATTACK) {
                const AttackMove *am = attack_of(mv->type);
                if (!am) { mv->invalid = true; continue; }
                d = am->dir;
                if (j == 0) k->computed |= am->from;
                k->computed |= 0x1000;
                if (!k->attack) {
                    k->attack = chr_lookup_varient(&probe, am->type, 7, 0x200);
                    if (k->attack) {
                        const uint8_t *t = k->attack;
                        k->reach = rdf(t + 0x50);
                        k->near = k->reach - rdf(t + 0x11c) * 0.5f;
                    }
                }
            } else if (cat == MELEE_POSITION) {
                if (idx >= POSITIONS) { mv->invalid = true; continue; }
                d = position_dir(idx);
                if (j == 0) k->computed |= 0x20000;
            } else if (cat == MELEE_MANEUVER) {
                if (j == 0) k->computed |= 0x20000;
                if (idx == 0xd) k->computed |= 0x20000000;
            } else {
                if (j == 0) k->computed |= 0x20000;     /* evades and throws: not ported */
            }
            if (d && (!dir || dir == 5)) dir = d;
        }
        k->computed |= dir << 8;
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

/* The time (GS+0x144) for the delays: the characters' own tick count stands in. */
static uint32_t now_of(const Character *c) { return c->time; }

/* The character's melee state: of the state its animation goes to (TRrAnimation_GetTo 0x428730), 13
   (moving) told apart by the keys held: 14 forward, 16 left, 17 right, 15 back. */
static uint32_t melee_state(const Character *c, uint64_t held)
{
    uint16_t to = c->anim ? tr_to(c->anim) : 0;
    uint32_t s = to < sizeof melee_state_of ? melee_state_of[to] : 0;
    if (s == 13) {
        if (held & CHR_KEY_FORWARD) return 14;
        if (held & CHR_KEY_STEP_LEFT) return 16;
        if (held & CHR_KEY_STEP_RIGHT) return 17;
        if (held & CHR_KEY_BACK) return 15;
    }
    return s;
}

/* The facing that puts the target on the direction's side: 1 ahead, 2 behind, 3 left, 4 right. */
static float facing_for(float angle, uint32_t dir)
{
    switch (dir) {
    case 2: return wrap_2pi(angle + PI);
    case 3: return wrap_2pi(angle - HALF_PI);
    case 4: return wrap_2pi(angle + HALF_PI);
    default: return angle;
    }
}

/* The end of a technique (+0x0c cleared): the attacks' memory fades (failed ones come back). */
static void end_technique(MeleeState *m)
{
    m->tech = NULL;
    m->move = 0;
    m->move_started = m->pressed = false;
    m->move_ticks = 0;
    m->face = 1000.0f;
    if (m->profile)
        for (uint32_t i = 0; i < m->profile->nattack && i < MELEE_MEMORY; i++)
            m->memory[i] = m->memory[i] == 1000.0f ? 0.0f : m->memory[i] * 0.6f;
}

void melee_stop(MeleeState *m)
{
    m->override = false;
    if (m->tech) end_technique(m);
}

void melee_notify_animation(MeleeState *m, uint16_t type, bool victim)
{
    if (victim) {
        melee_stop(m);
        return;
    }
    if (m->override && type == m->override_type) m->override = false;   /* the keys were taken */
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

/* The facing factors (AI2rMelee_Update 0x4a01f0): per direction, 1 within 45 degrees falling to 0 at
   60 (narrow) or 1 within 70 falling to 0 at 110 (wide); direction 1 at least 0.35. */
static void facing_factors(float angle, float facing, float narrow[6], float wide[6])
{
    for (int d = 0; d < 6; d++) {
        if (d == 0 || d == 5) { narrow[d] = wide[d] = 1.0f; continue; }
        float x = angle - facing;
        if (d == 2) x -= PI;
        else if (d == 3) x -= HALF_PI;
        else if (d == 4) x += HALF_PI;
        float a = fabsf(wrap_pi(x));
        narrow[d] = a < 0.78539819f ? 1.0f : a <= 1.0471976f ? (a - 1.0471976f) * -3.8197186f : 0.0f;
        wide[d] = a < 1.2217305f ? 1.0f : a <= 1.9198622f ? (a - 1.9198622f) * -1.4323945f : 0.0f;
    }
    if (narrow[1] <= 0.35f) narrow[1] = 0.35f;
    if (wide[1] <= 0.35f) wide[1] = 0.35f;
}

/* The weighting of one maneuver's moves when the AI may not attack: advancing wants the target
   beyond its range, retreating within, by how far over the threshold (at least 5); attacks,
   positions, throws and "max range" rule it out. */
static float maneuver_factor(const MeleeProfile *p, const MeleeTech *k, float dist)
{
    float f = 1.0f;
    for (uint32_t j = 0; j < k->count; j++) {
        const MeleeMove *mv = &p->move[k->first + j];
        uint32_t cat = mv->type >> 28, idx = mv->type & 0xfffffffu;
        if (cat == MELEE_ATTACK || cat == MELEE_POSITION || cat == MELEE_THROW) return 0.0f;
        if (cat != MELEE_MANEUVER) continue;
        float th = mv->p[2] < 5.0f ? 5.0f : mv->p[2];
        if (idx == 0) {
            float x = (dist - mv->p[1]) / th;
            if (x < 0.0f) return 0.0f;
            f *= x;
        } else if (idx == 1) {
            float x = (mv->p[1] - dist) / th;
            if (x < 0.0f) return 0.0f;
            f *= x;
        } else if (idx == 0xd) {
            return 0.0f;
        }
    }
    return f;
}

/* ---- the moves ---- */

/* Sets the executor's move for the tick (AI2rExecutor_Move): the move's kind toward the target. */
static void exec_move(AiState *a, float facing, int mode, int kind)
{
    ai_halt(a);
    ai_executor_move(a, facing, mode, kind, false);
}

/* Starts the next move (AI2iMelee_StartNextMove 0x4a20f0); false when the technique is over. */
static bool next_move(MeleeState *m)
{
    m->move++;
    m->move_started = m->pressed = false;
    m->move_ticks = 0;
    if (!m->tech || (uint32_t)m->move >= m->tech->count) {
        end_technique(m);
        return false;
    }
    return true;
}

/* An attack move (its update 0x4a5500): when the character may take it (a combo's keys go in the last
   frames of the attack before, 4), the keys are pressed through the override; the move is done when
   the animation starts (the override taken, AI2rMelee_NotifyAnimation). A combo whose attack before
   didn't play is given up. */
static bool attack_move(AiState *a, Character *c, const MeleeMove *mv)
{
    MeleeState *m = &a->melee;
    const AttackMove *am = attack_of(mv->type);
    if (!am) return false;
    if (m->pressed) {
        if (m->override) return true;       /* not taken yet */
        return next_move(m);
    }
    if (am->prev && c->combo_type != am->prev) {
        if (m->tech && (m->tech->computed & 0x40000) && m->tech->index < MELEE_MEMORY) m->memory[m->tech->index] = 1000.0f;
        end_technique(m);
        return true;
    }
    if (c->anim && tr_is_attack(c->anim) && tr_duration(c->anim) - c->frame > 4) return true;   /* wait for the window */
    m->override = true;
    m->override_type = am->type;
    m->override_prev = am->prev;
    m->override_held = am->held;
    m->override_down = am->down;
    m->pressed = true;
    if (m->tech && m->tech->delay_slot >= 0) m->available[m->tech->delay_slot] = now_of(c) + m->tech->delay;
    return true;
}

/* A position (AI2iMelee_Position_Update 0x4a6040, a stand-in for its prediction): toward the target by
   the position's direction until within the technique's attack's reach, or the profile's distance
   (the move's second number) when it takes distances; running for the run-ins. Two seconds at most. */
static bool position_move(AiState *a, Character *c, const MeleeMove *mv)
{
    MeleeState *m = &a->melee;
    uint32_t idx = mv->type & 0xfffffffu, dir = position_dir(idx);
    if (idx > 3 && idx < 12) return next_move(m);                   /* jumps, crouches, standing: not ported */
    float want = m->tech && m->tech->reach > 0.0f ? m->tech->reach * 0.9f : 12.0f;
    if (position_has_params(idx) && mv->p[1] > 0.0f) want = mv->p[1];
    if (m->dist <= want || ++m->move_ticks > 120) return next_move(m);
    int kind = dir == 2 ? AI_MOVE_BACK : dir == 3 ? AI_MOVE_LEFT : dir == 4 ? AI_MOVE_RIGHT : AI_MOVE_FORWARD;
    bool run = idx <= 3 || m->dist > want + 20.0f;
    exec_move(a, facing_for(m->angle, dir), run ? AI_MODE_RUN : AI_MODE_DEFAULT, kind);
    return true;
}

/* A maneuver (AI2iMelee_Maneuver_Update 0x4a7060): advance or retreat for its duration (seconds) or
   until past its range, circle (step aside) or pause for the duration, a chance of stopping the
   technique (8), stopping it beyond a range (13). */
static bool maneuver_move(AiState *a, Character *c, const MeleeMove *mv)
{
    (void)c;
    MeleeState *m = &a->melee;
    uint32_t idx = mv->type & 0xfffffffu;
    uint32_t ticks = (uint32_t)(mv->p[0] * 60.0f);
    if (!m->move_started) m->move_started = true;
    switch (idx) {
    case 0:     /* advance */
        if (m->dist < mv->p[1] || m->move_ticks++ >= ticks) return next_move(m);
        exec_move(a, m->angle, AI_MODE_DEFAULT, AI_MOVE_FORWARD);
        return true;
    case 1:     /* retreat */
        if (m->dist > mv->p[1] || m->move_ticks++ >= ticks) return next_move(m);
        exec_move(a, m->angle, AI_MODE_DEFAULT, AI_MOVE_BACK);
        return true;
    case 2: case 3:     /* circle left, right */
        if (m->move_ticks++ >= ticks) return next_move(m);
        exec_move(a, m->angle, AI_MODE_DEFAULT, idx == 2 ? AI_MOVE_LEFT : AI_MOVE_RIGHT);
        return true;
    case 8:     /* random stop: the chance in percent */
        if ((float)(trac_random() % 100) < mv->p[0]) {
            end_technique(m);
            return true;
        }
        return next_move(m);
    case 0xd:   /* max range */
        if (m->dist > mv->p[0]) {
            end_technique(m);
            return true;
        }
        return next_move(m);
    default:    /* pause, taunt and the others: standing for the duration */
        if (m->move_ticks++ >= ticks) return next_move(m);
        exec_move(a, m->angle, AI_MODE_DEFAULT, AI_MOVE_NONE);
        return true;
    }
}

static bool run_move(AiState *a, Character *c)
{
    MeleeState *m = &a->melee;
    const MeleeMove *mv = &m->profile->move[m->tech->first + m->move];
    if (mv->invalid) return next_move(m);
    switch (mv->type >> 28) {
    case MELEE_ATTACK: return attack_move(a, c, mv);
    case MELEE_POSITION: return position_move(a, c, mv);
    case MELEE_MANEUVER: return maneuver_move(a, c, mv);
    default: return next_move(m);       /* evades, throws: not ported */
    }
}

/* ---- the update ---- */

typedef struct { const MeleeTech *k; float w; } Weighed;

bool melee_update(AiState *a, Character *c, const Character *target)
{
    MeleeState *m = &a->melee;
    if (!m->profile || !target || (target->flags & 1)) {
        melee_stop(m);
        return false;
    }
    chr_enter_fight_mode(c);
    float dx = target->pos[0] - c->pos[0], dy = target->pos[1] - c->pos[1], dz = target->pos[2] - c->pos[2];
    m->dist = sqrtf(dx * dx + dy * dy + dz * dz);
    m->angle = wrap_2pi(atan2f(dx, dz));     /* AI2iMelee_AngleToTarget 0x4a21c0 */
    /* the default: look at the target, stand */
    exec_move(a, m->angle, AI_MODE_DEFAULT, AI_MOVE_NONE);
    /* a move under way runs; while an attack plays, the AI waits for it */
    if (m->tech) {
        run_move(a, c);
        if (m->tech) return true;
    }
    if (c->anim && tr_is_attack(c->anim)) return true;      /* "waiting for moves to finish" */
    if (m->override) return true;
    /* choose: with the cookie the attacks, else the maneuvers */
    const MeleeProfile *p = m->profile;
    uint32_t state = melee_state(c, a->held), t = now_of(c);
    float narrow[6], wide[6];
    facing_factors(m->angle, c->facing, narrow, wide);
    bool cookie = get_cookie(a, c, target);
    Weighed w[MELEE_TECH_MAX];
    int n = 0;
    float total = 0.0f, max_attack = 0.0f;
    uint32_t from = cookie ? 0 : p->nattack + p->nevade, count = cookie ? p->nattack : p->nmaneuver;
    for (uint32_t i = 0; i < count; i++) {
        const MeleeTech *k = &p->tech[from + i];
        float x = (float)k->weight;
        if (k->computed & 0x4000) continue;
        if (k->delay_slot >= 0 && m->available[k->delay_slot] > t) continue;
        if (cookie) {
            float mem = i < MELEE_MEMORY ? m->memory[i] : 0.0f;
            if (mem == 1000.0f) continue;
            x *= 1.0f - (mem < 0.8f ? mem : 0.8f);
        }
        uint32_t dir = k->computed >> 8 & 0xf;
        x *= (k->flags & 2) ? wide[dir] : narrow[dir];
        if (x == 0.0f) continue;
        if ((k->computed & 0xff) != state && !(k->computed & 0x20000)) continue;
        if (cookie) {
            /* AI2iMelee_WeightTechnique 0x4a2580, a stand-in: a run-in needs room for its run */
            const MeleeMove *mv = &p->move[k->first];
            uint32_t idx = mv->type & 0xfffffffu;
            if ((mv->type >> 28) == MELEE_POSITION && position_has_params(idx) && idx <= 3 && m->dist < mv->p[0] - mv->p[2])
                continue;
        } else {
            x *= maneuver_factor(p, k, m->dist);
            if (x <= 0.0f) continue;
        }
        w[n].k = k;
        w[n].w = x;
        total += x;
        if ((k->computed & 0x1001000) && max_attack <= x) max_attack = x;
        n++;
    }
    /* with the cookie (0x545771), what isn't an attack weighs no more than the heaviest attack */
    if (cookie && max_attack > 0.0f)
        for (int i = 0; i < n; i++)
            if (!(w[i].k->computed & 0x21001000) && w[i].w > max_attack) total -= w[i].w - max_attack, w[i].w = max_attack;
    if (!n || total <= 0.0f) {
        /* AI2iMelee_HandleNoTechnique 0x4a1dd0: the cookie goes back; beyond 8, toward the target */
        m->cookie = false;
        if (m->dist > 8.0f) exec_move(a, m->angle, AI_MODE_DEFAULT, AI_MOVE_FORWARD);
        return false;
    }
    float r = (float)trac_random() * total * 1.5259022e-05f - 0.01f, sum = 0.0f;
    const MeleeTech *pick = w[n - 1].k;
    for (int i = 0; i < n; i++) {
        sum += w[i].w;
        if (r <= sum) {
            pick = w[i].k;
            m->leftover = sum - r;
            break;
        }
    }
    m->tech = pick;
    m->move = 0;
    m->move_started = m->pressed = false;
    m->move_ticks = 0;
    if ((pick->computed & 0x40000) && pick->index < MELEE_MEMORY) m->memory[pick->index] += 0.4f;
    if (!(pick->computed & 0x21001000)) m->cookie = false;     /* 0x4998a0: not an attack, the cookie goes */
    run_move(a, c);
    return true;
}

/* AI2rBlockFunction 0x48b870 (the AI's C+0x15ec, asked by HandleAttackMask once the character could
   block): none without a profile, passive or non-aggressive (or with every AI passive, 0x5ec0c4, not yet
   ported) or panicking. The chance is the profile's single one; in combat with others on the same
   target it goes toward the group one by a third per other attacker, reaching it at four. Times the
   difficulty's factor (game settings +0xabc), against a random 0..100. */
bool melee_block(const AiState *a)
{
    const MeleeProfile *p = a->melee.profile;
    if (!p || (a->flags & (AI_FLAG_PASSIVE | AI_FLAG_NON_AGGRESSIVE)) || a->state == AI_STATE_PANIC) return false;
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
