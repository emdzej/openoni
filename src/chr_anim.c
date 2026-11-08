/* The character's animation engine: see chr_anim.h. */
#include "chr_anim.h"
#include "gametime.h"

#include "body.h"
#include "exe.h"
#include "graph.h"
#include "impact.h"
#include "m3gm.h"
#include "particle.h"
#include "ui.h"
#include "tm.h"
#include "trac.h"
#include "tram.h"
#include "im.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static const float TWO_PI = 6.2831854820251465f;   /* 0x529548 */

/* A .raw block of the animation (the field's offset, then at), len bytes inside the file; NULL if none. */
static const uint8_t *raw_block(const void *tram, unsigned field, size_t at, size_t len)
{
    return tr_raw_block(tram, field, at, len);
}

const uint8_t *tr_raw_block(const void *tram, unsigned field, size_t at, size_t len)
{
    size_t size;
    const uint8_t *raw = tm_raw(tram, &size);
    uint32_t off = rd32((const uint8_t *)tram + field);
    if (!raw || !off || off + at + len > size) return NULL;
    return raw + off + at;
}

static const uint8_t *collection(const Character *c) { return tm_link((const uint8_t *)c->oncc + ONCC_ANIMATIONS); }

static const void *lookup(const Character *c, uint16_t type, uint16_t state)
{
    const uint8_t *col = collection(c);
    return col ? trac_lookup(col, type, state, c->varient) : NULL;
}

/* ---- TR queries ---- */

bool tr_test_flag(const void *t, unsigned bit) { return (rd32((const uint8_t *)t + TRAM_FLAGS) >> bit & 1u) != 0; }
uint16_t tr_type(const void *t) { return rd16((const uint8_t *)t + TRAM_TYPE); }
uint16_t tr_from(const void *t) { return rd16((const uint8_t *)t + TRAM_FROM_STATE); }
uint16_t tr_to(const void *t) { return rd16((const uint8_t *)t + TRAM_TO_STATE); }
uint16_t tr_duration(const void *t) { return rd16((const uint8_t *)t + TRAM_DURATION); }
uint16_t tr_varient(const void *t) { return rd16((const uint8_t *)t + TRAM_VARIENT); }

/* Whether next is one of the animation's two links (TRAM+0x38): they follow it without a pause. */
static bool is_link(const void *t, const void *next)
{
    for (int i = 0; i < 2; i++)
        if (tm_link((const uint8_t *)t + TRAM_LINKS + 4 * i) == next) return true;
    return false;
}

uint16_t tr_soft_pause(const void *t, const void *next) { return is_link(t, next) ? 0 : rd16((const uint8_t *)t + TRAM_SOFT_PAUSE); }
uint16_t tr_hard_pause(const void *t, const void *next) { return is_link(t, next) ? 0 : rd16((const uint8_t *)t + TRAM_HARD_PAUSE); }

/* An attack with frames, parts and damage, or a throw. */
const uint8_t *tr_attack(const void *tram, unsigned i)
{
    return i < ((const uint8_t *)tram)[TRAM_ATTACK_COUNT] ? raw_block(tram, TRAM_ATTACK_LIST, 0x20u * i, 0x20) : NULL;
}

const void *chr_lookup(const Character *c, uint16_t type, uint16_t state) { return lookup(c, type, state); }
const void *chr_collection(const Character *c) { return collection(c); }

const void *chr_lookup_varient(const Character *c, uint16_t type, uint16_t state, uint16_t varient)
{
    const uint8_t *col = collection(c);
    return col ? trac_lookup(col, type, state, varient) : NULL;
}

bool tr_is_attack(const void *tram)
{
    const uint8_t *t = tram;
    for (unsigned i = 0; i < t[TRAM_ATTACK_COUNT]; i++) {
        const uint8_t *a = raw_block(tram, TRAM_ATTACK_LIST, 0x20u * i, 0x20);
        if (a && rd16(a + 0xe) <= rd16(a + 0x10) && rd32(a) && rd16(a + 0xc)) return true;
    }
    return rd32(t + TRAM_THROW) != 0;
}

/* The shortcut from a state into the animation (TRAM+0x18, 8 bytes each: the state u16, the
   interpolation length u8, a byte, flags u32), or the one from any state (1); -1 if none, or if the
   state is the animation's own start state. The search the three queries below share. */
static int shortcut(const void *tram, uint16_t state)
{
    const uint8_t *t = tram;
    if (state == tr_from(t)) return -1;
    unsigned n = t[TRAM_SHORTCUT_COUNT];
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *s = raw_block(t, TRAM_SHORTCUTS, 8u * i, 8);
        if (s && rd16(s) == state) return (int)i;
    }
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *s = raw_block(t, TRAM_SHORTCUTS, 8u * i, 8);
        if (s && rd16(s) == TRAC_ANY_STATE) return (int)i;
    }
    return -1;
}

bool tr_is_shortcut(const void *t, uint16_t state) { return shortcut(t, state) >= 0; }

uint8_t tr_shortcut_length(const void *t, uint16_t state)
{
    int i = shortcut(t, state);
    const uint8_t *s = i >= 0 ? raw_block(t, TRAM_SHORTCUTS, 8u * (unsigned)i, 8) : NULL;
    return s ? s[2] : 0;
}

bool tr_shortcut_flag(const void *t, uint16_t state, unsigned bit)
{
    int i = shortcut(t, state);
    const uint8_t *s = i >= 0 ? raw_block(t, TRAM_SHORTCUTS, 8u * (unsigned)i, 8) : NULL;
    return s && (rd32(s + 4) >> bit & 1u);
}

bool tr_is_atomic(const void *tram, uint16_t frame)
{
    const uint8_t *t = tram;
    return frame >= rd16(t + TRAM_ATOMIC_START) && frame <= rd16(t + TRAM_ATOMIC_END);
}

/* From the animation's end into next: the shortcut's length when next starts from another state (8, and
   a console complaint, when there's none), at least 8 between variants, and at least the animation's own
   end interpolation (TRAM+0x170) unless next is one of its links. */
uint16_t tr_end_interpolation(const void *tram, const void *next)
{
    const uint8_t *t = tram;
    uint16_t to = tr_to(t), r = 0;
    if (to != tr_from(next)) {
        int i = shortcut(next, to);
        const uint8_t *s = i >= 0 ? raw_block(next, TRAM_SHORTCUTS, 8u * (unsigned)i, 8) : NULL;
        r = s ? s[2] : 8;   /* none: the original prints a warning (0x533904) and takes 8 */
    }
    if (tr_varient(t) != tr_varient(next) && r <= 8) r = 8;
    if (!is_link(t, next) && r <= rd16(t + TRAM_END_INTERPOLATION)) r = rd16(t + TRAM_END_INTERPOLATION);
    return r;
}

/* The root's height at the frame. The frame is scaled to the animation's rate (fps / 60, rounded to a
   float as the exe stores it) and clamped to the last frame; a fraction lerps toward the next height. At
   60 fps the float rounding leaves a fraction of about 2e-7 on most frames: kept. No heights: 6. */
static double height(const void *tram, uint16_t frame)
{
    const uint8_t *t = tram;
    float last = (float)(rd16(t + TRAM_FRAMES) - 1);
    float x = (float)((double)(rd16(t + TRAM_FPS) * (uint32_t)frame) * (double)0.016666668f);
    if (!(last > x)) x = last;
    if (!rd32(t + TRAM_HEIGHTS)) return 6.0f;
    const uint8_t *h = raw_block(t, TRAM_HEIGHTS, 4u * frame, 4);
    if (x == (float)frame) return h ? rdf(h) : 0.0f;
    /* The original reads heights[frame] and [frame + 1] even when x was clamped below the frame; out
       of the block the port takes the last height instead. */
    const uint8_t *h1 = raw_block(t, TRAM_HEIGHTS, 4u * frame + 4, 4);
    if (!h || !h1) {
        const uint8_t *l = raw_block(t, TRAM_HEIGHTS, 4u * (uint32_t)last, 4);
        return l ? rdf(l) : 0.0f;
    }
    double f = (double)x - frame;
    return rdf(h) * (1.0 - f) + rdf(h1) * f;
}

/* The x87 returns the height unrounded; DoCharacterFrame blends it so before storing a float. */
float tr_height(const void *tram, uint16_t frame) { return (float)height(tram, frame); }

void tr_position(const void *tram, uint16_t frame, float v[3])
{
    float xz[2] = { 0, 0 };
    tram_velocity(tram, frame, xz);
    v[0] = xz[0], v[1] = 0, v[2] = xz[1];
}

/* ---- the character ---- */

bool chr_is_victim_type(uint16_t t)
{
    return (t >= 0x4c && t <= 0x5c) || (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) ||
           (t >= 0x7e && t <= 0x84) || t == 0x8d || t == 0xc6 || (t >= 0xd4 && t <= 0xd7);
}

bool chr_in_bad_varient(const Character *c) { return (~(uint32_t)c->varient & tr_varient(c->anim)) != 0; }

uint16_t chr_class_fight_time(const void *oncc) { return rd16((const uint8_t *)oncc + 0xc44); }

/* The idle delay: the class's (ONCC+0xc48) plus a random half of it at most. */
void chr_recalculate_idle_delay(Character *c)
{
    uint32_t d = rd32((const uint8_t *)c->oncc + 0xc48);
    c->idle_delay = (uint32_t)(((uint64_t)trac_random() * d) >> 17) + d;
}

void chr_enter_fight_mode(Character *c)
{
    if (c->inactive && chr_activate_hook) chr_activate_hook(c, true);   /* 0x4eb0b0: 0x4db830 first */
    c->varient |= 0x200;
    c->fight_timer = chr_class_fight_time(c->oncc);
}

/* ONrCharacter_GetVelocityEstimate 0x4eafc0: the root's mean motion over the current frame and up to
   three before it. */
void chr_velocity_estimate(const Character *c, float v[3])
{
    uint16_t f = (uint16_t)c->frame, last = (uint16_t)(tr_duration(c->anim) - 1u);
    if (last < f) f = last;
    float s[3] = { 0, 0, 0 };
    unsigned n = 0;
    for (unsigned k = 0; k < 4; k++) {
        float p[3];
        tr_position(c->anim, f, p);
        s[0] += p[0], s[1] += p[1], s[2] += p[2];
        n++;
        if (!f) break;
        f--;
    }
    float k = 1.0f / (float)n;
    v[0] = k * s[0], v[1] = k * s[1], v[2] = k * s[2];
}

/* MUrQuat_Mul 0x433820: a b, quaternions (x, y, z, w). */
static void quat_mul(const float a[4], const float b[4], float o[4])
{
    float r[4];
    r[2] = a[2] * b[3] + a[0] * b[1] + a[3] * b[2] - b[0] * a[1];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    r[0] = a[3] * b[0] + b[2] * a[1] + a[0] * b[3] - a[2] * b[1];
    r[1] = a[2] * b[0] + b[3] * a[1] + a[3] * b[1] - b[2] * a[0];
    memcpy(o, r, sizeof r);
}

static float wrap_2pi(float a)
{
    if (a < 0.0f) return a + TWO_PI;
    if (a >= TWO_PI) return a - TWO_PI;
    return a;
}

/* RemapAnimationHook 0x4ea550: for the player, the throw types (0x1f, 0x1a, 0x43, 0x42) become a throw
   of the character in front (AttemptThrow 0x4eaa10); for AI, AI2rExecutor_TryingToThrow's. The other
   characters are throw.c's: through chr_remap_hook. */
const void *(*chr_remap_hook)(Character *c, const void *next);
void (*chr_throw_hook)(Character *c);

static const void *remap_hook(Character *c, const void *next)
{
    return chr_remap_hook && next ? chr_remap_hook(c, next) : next;
}

/* ---- the animation's particles ---- */

static float rdf32(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* ONrGameSettings_HealthColour 0x4ef450 */
uint32_t chr_health_colour(float f)
{
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!gs) return 0xff00ff00u;
    if (f <= rdf32(gs + 0x18)) return rd32(gs + 0x58);
    uint32_t n = rd32(gs + 0x14);
    if (n > 16) n = 16;
    if (rdf32(gs + 0x14 + n * 4) <= f) return rd32(gs + 0x54 + n * 4);
    uint32_t i = 1;
    while (i < n && !(f < rdf32(gs + 0x18 + (i - 1) * 4 + 4))) i++;
    float lo = rdf32(gs + 0x14 + i * 4), hi = rdf32(gs + 0x18 + i * 4);
    float t = (f - lo) / (hi - lo);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return im_shade_interpolate(rd32(gs + 0x54 + i * 4), rd32(gs + 0x58 + i * 4), t);
}

static uint32_t health_colour_of(const Character *c)
{
    return chr_health_colour(c->max_hit_points ? (float)c->hit_points / (float)c->max_hit_points : 0.0f);
}

void chr_bone_matrices(Character *c)
{
    BodyMatrix m[BODY_MAX_BONES];
    if (!chr_pose_matrices(c, m)) return;
    for (int b = 0; b < CHR_BONES; b++) memcpy(c->bone_world[b], m[b], sizeof c->bone_world[b]);
    /* the weapon's matrix (the one past the bones: the fist's, the right's or with ONCC+0xc8c the left's) */
    memcpy(c->weapon_at, m[((const uint8_t *)c->oncc)[0xc8c] ? 14 : 18] + 9, sizeof c->weapon_at);
    c->placed = false;
}

/* The class's particle list (ONCC +0x430, ONCP: the count at +0x14, entries of 0x58 from +0x18: the
   name, 16 bytes; the particle class's, 64 at +0x10; flags (u16 +0x50: 0x8000 an impact effect)). */
enum { ONCC_PARTICLES = 0x430, ONCP_ENTRY = 0x58 };

/* 0x4e96a0: a particle of the class hanging from the bone, at its geometry's middle (the box's), owned by
   the character's melee (WPrOwner_MakeMeleeDamageFromCharacter 0x51ce00: 0x2000000 | its index), its
   dynamic matrix flag (0x2000) set. */
static P3Particle *bone_particle(Character *c, P3Class *pc, uint16_t bone)
{
    const void *body = body_of_class(c->oncc, 4);
    if (!body || bone >= CHR_BONES) return NULL;
    float box[6] = { 0 }, sph[4];
    m3gm_bounds(body_geometry(body, bone), box, sph);
    const float pos[3] = { (box[3] + box[0]) * 0.5f, (box[4] + box[1]) * 0.5f, (box[5] + box[2]) * 0.5f };
    static const float ID[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };     /* 0x529d10 */
    P3Particle *p = p3_effect_owned(pc, pos, NULL, ID, c->bone_world[bone], 0x2000000u | c->id, ui_time());
    if (p) p3_set_flags(p, p3_flags(p) | 0x2000);
    return p;
}

/* NewAnimationHook's last part: the animation's particles (TRAM +0x24, the count at +0x17f), each by its
   name in the class's list, made at once (tinted the character's health colour, 0x4ef450) and started
   and stopped by their frames in DoCharacterFrame. A name not in the list: "no particle found". */
static void anim_particles_start(Character *c)
{
    const void *t = c->anim;
    c->anim_particles_on = 0;
    c->anim_particles = ((const uint8_t *)t)[0x17f];
    if (c->anim_particles > 16) c->anim_particles = 16;
    if (!c->anim_particles) return;
    const uint8_t *oncp = tm_link((const uint8_t *)c->oncc + ONCC_PARTICLES);
    if (!oncp) {
        c->anim_particles = 0;
        return;
    }
    chr_bone_matrices(c);
    p3_global_tint(true, health_colour_of(c));
    uint32_t n = rd32(oncp + 0x14);
    for (unsigned i = 0; i < c->anim_particles; i++) {
        memset(&c->anim_particle[i], 0, sizeof c->anim_particle[i]);
        const uint8_t *ap = raw_block(t, TRAM_PARTICLES, 0x18u * i, 0x18);
        if (!ap) continue;
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *e = oncp + 0x18 + (size_t)k * ONCP_ENTRY;
            if (strncmp((const char *)e, (const char *)ap + 8, 16)) continue;
            c->anim_particle[i].entry = e;
            c->anim_particle[i].bone = rd16(ap + 4);
            char name[65];
            memcpy(name, e + 0x10, 64);
            name[64] = 0;
            P3Class *pc = p3_class(name);
            P3Particle *p = pc ? bone_particle(c, pc, c->anim_particle[i].bone) : NULL;
            if (p) {
                c->anim_particle[i].cls = pc;
                c->anim_particle[i].handle = p3_handle(p);
            }
            break;
        }
    }
    p3_global_tint(false, 0);
}

/* Step 14's end: above 0, the super entries of the class's list (the name's first 5 letters "super",
   0x550da0, a class there and a bone of the 19) made once on their bones (those that make a particle,
   16 at most), then their start event (2) once; at 0, once made and started, their stop event (3). The
   particles stay. */
void chr_super_particles(Character *c)
{
    float t = (float)ui_time() * (1.0f / 60.0f);
    if (c->super_level > 0.0f) {
        if (!c->super_made) {
            const uint8_t *oncp = tm_link((const uint8_t *)c->oncc + ONCC_PARTICLES);
            c->super_count = 0;
            uint32_t n = oncp ? rd32(oncp + 0x14) : 0;
            for (uint32_t k = 0; k < n && c->super_count < 16; k++) {
                const uint8_t *e = oncp + 0x18 + (size_t)k * ONCP_ENTRY;
                if (strncmp((const char *)e, exe_str(0x550da0), 5)) continue;
                char name[65];
                memcpy(name, e + 0x10, 64);
                name[64] = 0;
                P3Class *pc = p3_class(name);      /* the entry's class (+0x54) */
                if (!pc) continue;
                uint16_t bone = rd16(e + 0x50);
                if (bone >= CHR_BONES) continue;
                P3Particle *p = bone_particle(c, pc, bone);
                if (!p) continue;
                c->super_particle[c->super_count].entry = e;
                c->super_particle[c->super_count].cls = pc;
                c->super_particle[c->super_count].bone = bone;
                c->super_particle[c->super_count].handle = p3_handle(p);
                c->super_count++;
            }
            c->super_made = true;
            c->super_started = false;
        }
        if (!c->super_started) {
            for (unsigned i = 0; i < c->super_count; i++) {
                P3Class *pc;
                P3Particle *p = p3_from_handle(c->super_particle[i].handle, &pc);
                if (p) p3_send_event(c->super_particle[i].cls, p, P3_EV_START, t);
            }
            c->super_started = true;
        }
    } else if (c->super_made && c->super_started) {
        for (unsigned i = 0; i < c->super_count; i++) {
            P3Class *pc;
            P3Particle *p = p3_from_handle(c->super_particle[i].handle, &pc);
            if (p) p3_send_event(c->super_particle[i].cls, p, P3_EV_STOP, t);
        }
        c->super_started = false;
    }
}

static P3Particle *anim_particle_live(Character *c, unsigned i)
{
    P3Class *pc;
    P3Particle *p = c->anim_particle[i].handle ? p3_from_handle(c->anim_particle[i].handle, &pc) : NULL;
    return p && pc == c->anim_particle[i].cls ? p : NULL;
}

void chr_anim_particles_delete(Character *c)
{
    for (unsigned i = 0; i < c->super_count; i++) {
        P3Class *pc;
        P3Particle *p = p3_from_handle(c->super_particle[i].handle, &pc);
        if (p && pc == c->super_particle[i].cls) p3_kill(pc, p);    /* 0x456c70 */
    }
    c->super_count = 0;
    c->super_made = c->super_started = false;
    for (unsigned i = 0; i < c->anim_particles; i++) {
        P3Particle *p = anim_particle_live(c, i);
        if (p) p3_kill(c->anim_particle[i].cls, p);
    }
    c->anim_particles = 0;
}

uint32_t chr_health_tint(const Character *c) { return health_colour_of(c); }

void chr_anim_particles_pulse(Character *c, uint32_t parts)
{
    float time = (float)ui_time() * (1.0f / 60.0f);
    for (unsigned i = 0; i < c->anim_particles; i++) {
        if (!(c->anim_particles_on >> i & 1) || !(parts >> c->anim_particle[i].bone & 1)) continue;
        P3Particle *p = anim_particle_live(c, i);
        if (p) p3_send_event(c->anim_particle[i].cls, p, P3_EV_PULSE, time);
    }
}

void chr_anim_particles_stop(Character *c)
{
    float time = (float)ui_time() * (1.0f / 60.0f);
    for (unsigned i = 0; i < c->anim_particles; i++) {
        P3Particle *p = anim_particle_live(c, i);
        if (p) {
            if (c->anim_particles_on >> i & 1) p3_send_event(c->anim_particle[i].cls, p, P3_EV_STOP, time);
            if ((p = anim_particle_live(c, i))) p3_kill(c->anim_particle[i].cls, p);
        }
        c->anim_particle[i].handle = 0;
    }
    c->anim_particles = 0;
    c->anim_particles_on = 0;
}

/* DoCharacterFrame's step after the action frame (0x4df576): the animation's particles in their frames
   (0x4288c0: first <= frame <= last), when that changes: each live one tinted the health colour, started
   (event 2) as it comes in, stopped (3) as it goes out. An impact one (0x8000) coming in on a throw whose
   victim is dead (this one's target, or this one thrown) goes instead: its particle killed and, once a
   victim (C+4 0x20000000), the impact effect (impact_throw_killed). */
void chr_anim_particles_frame(Character *c)
{
    if (!c->anim_particles || !c->anim) return;
    uint16_t on = 0;
    for (unsigned i = 0; i < c->anim_particles; i++) {
        const uint8_t *ap = raw_block(c->anim, TRAM_PARTICLES, 0x18u * i, 0x18);
        if (ap && rd16(ap) <= c->frame && c->frame <= rd16(ap + 2)) on |= (uint16_t)(1u << i);
    }
    if (on == c->anim_particles_on) return;
    float time = (float)ui_time() * (1.0f / 60.0f);
    uint32_t tint = health_colour_of(c);
    for (unsigned i = 0; i < c->anim_particles; i++) {
        const uint8_t *e = c->anim_particle[i].entry;
        if (!e) continue;
        uint16_t bit = (uint16_t)(1u << i);
        if (rd16(e + 0x50) == 0x8000 && (on & bit) && !(c->anim_particles_on & bit)) {
            Character *victim = c->throw_target ? c->throw_target : c->thrown_by ? c : NULL;
            if (victim && (victim->flags & 1)) {
                P3Particle *p = anim_particle_live(c, i);
                if (p) p3_kill(c->anim_particle[i].cls, p);
                c->anim_particle[i].handle = 0;
                c->anim_particle[i].cls = NULL;
                if (!(victim->flags & 0x20000000u))
                    impact_throw_killed(c, c->anim_particle[i].bone, victim, c->throw_target ? c : c->thrown_by);
                continue;
            }
        }
        P3Particle *p = anim_particle_live(c, i);
        if (!p) continue;
        p3_set_tint(c->anim_particle[i].cls, p, tint);
        if ((on & bit) && !(c->anim_particles_on & bit)) p3_send_event(c->anim_particle[i].cls, p, P3_EV_START, time);
        else if (!(on & bit) && (c->anim_particles_on & bit)) p3_send_event(c->anim_particle[i].cls, p, P3_EV_STOP, time);
    }
    c->anim_particles_on = on;
}

/* ONiCharacter_OldAnimationHook 0x4ead30, as the animation is left for next. */
static void old_animation_hook(Character *c, const void *next)
{
    (void)next;
    c->left_type = c->type;
    if ((c->flags & 0x200000) && !c->hold) c->flags &= ~0x200000u;
    if (c->type == c->combo_type && c->combo_time == 0xffffffffu) c->combo_time = game_time();
    /* The animation's own turn (TRAM+0x48): the facings take it, and the pose turns back by it at the
       root (the root of the aimed copy at A+0x2020 too, which the port doesn't keep), so the change
       doesn't jump. */
    float r = wrap_2pi(rdf((const uint8_t *)c->anim + TRAM_FINAL_ROTATION));
    if (r != 0.0f) {
        float q[4] = { 0, sinf(r * 0.5f), 0, cosf(r * 0.5f) };
        quat_mul(c->quats, q, c->quats);
        c->facing = wrap_2pi(r + c->facing);
        c->desired_facing = wrap_2pi(r + c->desired_facing);
    }
    /* the animation's particles: those in their frames stopped, all killed */
    chr_anim_particles_stop(c);
}

/* ONrCharacter_NewAnimationHook 0x4e97a0, the parts that touch the animation state, and the teleport: a
   type 0xdf with one set puts the character there (its position, the last one, the physics', the
   ground's; the location, C+0x160, isn't kept apart in the port; the room, 0x4e91c0, is found from the
   position), facing as given; an AI's path is made anew (chars.c). The sound serial (C+0x1dc)
   counts up; the animation's cry (TRAM+0x4e, 0x428970: below 10) is vocalized (chr_vocalize_hook). An
   animation of flag 5 throws the hand's weapon (chr_drop_hook). An attack turns the player to its
   target (chr_attack_turn_hook). The dead whose new animation isn't a reaction lie as they are (flag 2;
   the original's 0x4eb550 there can't run: it needs the flag it is under clear); the drift they keep
   (A+0x2824) comes with the physics, and the physics context's pending motion isn't kept (0x4ed080). The
   animation's particles are made last. A throw (TRAM flag 15) starts its target's part through chr_throw_hook
   (throw.c); any other animation forgets the target. A hit's type (0x4c..0x5c, 0x60..0x65, 0x72..0x77,
   0x7e..0x84, 0x8d, 0xc6, 0xd4..0xd7, 0x17) ends the console action (C+0x1680). */
void (*chr_animation_hook)(Character *c);
void (*chr_drop_hook)(Character *c);
void (*chr_attack_turn_hook)(Character *c);
void (*chr_vocalize_hook)(Character *c, uint32_t type);

static void new_animation_hook(Character *c)
{
    const void *t = c->anim;
    c->sound_serial++;
    uint16_t cry = rd16((const uint8_t *)t + 0x4e);
    if (cry < 10 && chr_vocalize_hook) chr_vocalize_hook(c, cry);
    bool fight = (tr_varient(t) & 0x200) != 0;
    if (c->teleport && tr_type(t) == 0xdf) {
        c->facing = c->desired_facing = c->teleport_facing;
        memcpy(c->prev_pos, c->teleport_to, sizeof c->prev_pos);
        memcpy(c->pos, c->teleport_to, sizeof c->pos);
        if (c->has_physics) {
            memcpy(c->phys_pos, c->teleport_to, sizeof c->phys_pos);
            c->ground_height = c->teleport_to[1];
        }
        c->room = graph_room_at(c->teleport_to);    /* 0x4e91c0 */
    }
    if (!tr_test_flag(t, TRAM_FLAG_THROW_SOURCE)) c->throw_target = NULL, c->throw_victim_anim = NULL;
    /* a hit, a knockdown or a stun ends the console action under way (action.c's finish) */
    switch (tr_type(t)) {
    case 0x17: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c:
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x72: case 0x73: case 0x74:
    case 0x75: case 0x76: case 0x77: case 0x7e: case 0x7f: case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x8d: case 0xc6: case 0xd4: case 0xd5: case 0xd6: case 0xd7:
        c->console_marker = -1;
        break;
    default:
        break;
    }
    if (tr_test_flag(t, 5) && c->weapon && chr_drop_hook) chr_drop_hook(c);    /* 0x51b590 (slot 0, thrown) */
    c->threw = NULL;                /* A+0x1ed8 = 0xffff: set again by a throw */
    if (tr_is_attack(t) && !tr_test_flag(t, TRAM_FLAG_THROW_SOURCE)) {
        if (chr_attack_turn_hook) chr_attack_turn_hook(c);
        c->combo_type = tr_type(t);
        c->combo_time = 0xffffffffu;
    }
    if (c->type == CHR_TYPE_STAND || c->type == CHR_TYPE_IDLE) {
        if (c->type == CHR_TYPE_IDLE) {
            /* after an idle the next one waits the delay again, from a random part of +0xc4c on */
            chr_recalculate_idle_delay(c);
            c->last_action = game_time() - c->idle_delay;
            uint32_t d = rd32((const uint8_t *)c->oncc + 0xc4c);
            c->last_action += (uint32_t)(((uint64_t)trac_random() * d) >> 17) + d;
        }
    } else
        c->last_action = game_time();
    /* dead, and the reactions are over: the body stays as it lies (stage 2, flag 2). The corpse's drift
       (A+0x2824, from the knockback or the air velocity) comes with the physics. */
    if ((c->flags & 1) && !chr_is_victim_type(c->type)) c->flags |= 2;
    memset(c->hit_list, 0, sizeof c->hit_list);    /* A+0x1eb8: a new animation hits anew */
    /* an animation from the airborne states (0x25..0x2e) takes the character into the air */
    uint16_t from = tr_from(t);
    if (from > 0x24 && from < 0x2f) {
        c->start_falling = true;
        if (!c->air_ticks) c->fall_height = c->pos[1];
    }
    if (tr_test_flag(t, TRAM_FLAG_THROW_SOURCE) && chr_throw_hook) chr_throw_hook(c);
    if (!c->fight_timer && !fight) c->varient &= (uint16_t)~0x200u;
    anim_particles_start(c);
    if (c->ai && chr_animation_hook) chr_animation_hook(c);
}

void chr_set_animation(Character *c, const void *t)
{
    uint16_t old = c->state;
    c->anim = t;
    c->frame = 0;
    c->queued = 0;
    c->from_state = old;
    c->type = tr_type(t);
    c->state = tr_to(t);
}

void chr_set_animation_interpolated(Character *c, uint16_t from_state, const void *t, uint16_t length)
{
    memcpy(c->interp_quats, c->quats, sizeof c->quats);
    c->interpolating = true;
    chr_velocity_estimate(c, c->interp_velocity);   /* of the animation being left */
    c->interp_height = c->pelvis_height;
    /* The rest of an interpolation under way carries over when longer. The original then tests the
       flag it has just set, so the second clamp always runs (and changes nothing). */
    int rest = (int)c->interp_length - (int)c->interp_tick;
    if (rest < (int)length) rest = length;
    c->interp_length = (uint16_t)rest;
    c->interp_state = from_state;
    c->interp_tick = 0;
    if (c->interpolating && (int)c->interp_length < (int)length) c->interp_length = length;
    c->from_state = from_state;
    c->anim = t;
    c->frame = 0;
    c->type = tr_type(t);
    c->queued = 0;
    c->state = tr_to(t);
    if (tr_test_flag(t, TRAM_FLAG_NO_VELOCITY_LERP)) c->interp_velocity[0] = c->interp_velocity[1] = c->interp_velocity[2] = 0;
}

/* 0x4eb340, what scripts use (chr_animate): tram (through the remap hook) after the old animation's hook,
   interpolated over interp ticks when interp > 0, else at once; either way from_state is what the caller
   gives (chr_animate passes the character's own from state, A+0x1afe, not its state). */
void chr_set_animation_external(Character *c, uint16_t from_state, const void *tram, int32_t interp)
{
    if (c->inactive && chr_activate_hook) chr_activate_hook(c, true);   /* 0x4db830 first */
    const void *t = remap_hook(c, tram);
    old_animation_hook(c, t);
    if (interp > 0) {
        chr_set_animation_interpolated(c, from_state, t, (uint16_t)interp);
    } else {
        c->frame = 0;
        c->queued = 0;
        c->anim = t;
        c->from_state = from_state;
        c->type = tr_type(t);
        c->state = tr_to(t);
    }
    new_animation_hook(c);
}

void chr_next_animation(Character *c)
{
    c->flags &= ~0x10u;
    /* 0xde (a teleport's first half) is always followed by 0xdf */
    uint16_t type = c->type == 0xde ? 0xdf : c->queued;
    const void *next;
    if (c->hold) next = c->anim;
    else {
        /* With nothing queued (type 0) the lookup takes any animation from the state. */
        next = lookup(c, type, c->state);
        if (!next) next = lookup(c, CHR_TYPE_STAND, CHR_STATE_STANDING);   /* "invalid next animation" */
    }
    next = remap_hook(c, next);
    if (!next) return;      /* the original has no guard: a class without a stand */
    uint16_t hp = tr_hard_pause(c->anim, next), sp = tr_soft_pause(c->anim, next);
    if (c->hard_pause > hp) hp = c->hard_pause;
    c->hard_pause = hp;
    if (c->soft_pause > sp) sp = c->soft_pause;
    c->soft_pause = sp;
    uint16_t interp = tr_end_interpolation(c->anim, next);
    old_animation_hook(c, next);
    if (interp) chr_set_animation_interpolated(c, c->state, next, interp);
    else chr_set_animation(c, next);
    new_animation_hook(c);
}

/* What a pause lets through: non-attacks of the types stand (6), crouch (0x11), 0x17, 0x18 and
   0x28..0x2b; a soft pause not 0x17 either. */
static bool passes_pause(const void *t, bool soft)
{
    if (tr_is_attack(t)) return false;
    uint16_t ty = tr_type(t);
    if (ty == 0x17) return !soft;
    return ty == 6 || ty == 0x11 || ty == 0x18 || (ty >= 0x28 && ty <= 0x2b);
}

/* The types "when it can" never cuts (the switch at 0x4e46b0). */
static bool uncuttable_type(uint16_t t)
{
    switch (t) {
    case 0x4f: case 0x50: case 0x51: case 0x53: case 0x58: case 0x59: case 0x5a: case 0x5c: case 0x8d:
    case 0xd6: case 0xd7:
        return true;
    default:
        return false;
    }
}

const void *chr_do_animation(Character *c, unsigned flags, uint16_t type)
{
    if (c->flags & 1) return NULL;
    bool stand_to_stand = type == CHR_TYPE_STAND && c->state == CHR_STATE_STANDING;
    const void *next = lookup(c, type, c->state);
    if (!next && c->frame < 8 && (flags & CHR_ANIM_FROM_PREVIOUS)) {
        next = lookup(c, type, c->from_state);
        flags = (flags & ~3u) | CHR_ANIM_NOW;
    }
    if ((c->flags & 0x1000) && c->anim != next) c->flags &= ~0x1000u;
    if (!next) return NULL;

    /* the pauses */
    bool hard = c->block_pause || c->stun_pause || c->hard_pause || tr_hard_pause(c->anim, next) > 0;
    if (c->soft_pause || tr_soft_pause(c->anim, next) > 0) {
        if (!passes_pause(next, true)) return NULL;
    } else if (hard && !passes_pause(next, false))
        return NULL;

    if (flags & CHR_ANIM_WHEN_CAN) {
        const void *cur = c->anim;
        uint16_t cur_to = tr_to(cur);
        bool short_cut = tr_is_shortcut(next, cur_to);
        bool same = tr_type(next) == c->type && tr_varient(cur) == tr_varient(next) && tr_to(cur) == tr_to(next);
        bool atomic = tr_is_atomic(cur, (uint16_t)c->frame);
        /* 0x4e36f0 on the character's type (6 for one without an active record) */
        if (same || chr_is_victim_type(c->type) || tr_test_flag(cur, TRAM_FLAG_THROW_TARGET) ||
            tr_test_flag(cur, TRAM_FLAG_THROW_SOURCE) || uncuttable_type(tr_type(cur)))
            flags |= CHR_ANIM_QUEUE;
        else if (!atomic || (short_cut && tr_shortcut_flag(next, cur_to, 0)))
            flags |= CHR_ANIM_NOW;
        else
            flags |= CHR_ANIM_QUEUE;
    }
    if (!stand_to_stand && !c->fight_timer) c->varient &= (uint16_t)~0x200u;
    c->flags &= ~0x10u;

    /* Asking for what plays (or for what a start type leads into) queues it, unless the current one
       has variant bits the character has lost. */
    bool queue = (flags & CHR_ANIM_QUEUE) != 0;
    if (!chr_in_bad_varient(c)) {
        if (c->type == type) queue = true;
        switch (c->type) {
        case CHR_TYPE_RUN_START: queue |= type == CHR_TYPE_RUN; break;
        case CHR_TYPE_WALK_START: queue |= type == CHR_TYPE_WALK; break;
        case CHR_TYPE_RUN_BACKWARDS_START: queue |= type == CHR_TYPE_RUN_BACKWARDS; break;
        case CHR_TYPE_WALK_BACKWARDS_START: queue |= type == CHR_TYPE_WALK_BACKWARDS; break;
        case CHR_TYPE_SIDESTEP_LEFT_START: queue |= type == CHR_TYPE_SIDESTEP_LEFT; break;
        case CHR_TYPE_SIDESTEP_RIGHT_START: queue |= type == CHR_TYPE_SIDESTEP_RIGHT; break;
        default: break;
        }
    }
    if (queue) {
        c->queued = type;
        return next;
    }

    /* the change: the interpolation asked for (at least 8; the original keeps it in a byte), a
       shortcut's own, at most the new animation's maximum */
    uint8_t interp = c->next_interpolation < 8 ? 8 : (uint8_t)c->next_interpolation;
    uint16_t from = c->state;
    next = remap_hook(c, next);
    if (c->anim) old_animation_hook(c, next);
    if (tr_is_shortcut(next, from)) interp = tr_shortcut_length(next, from);
    uint16_t max = rd16((const uint8_t *)next + TRAM_MAX_INTERPOLATION);    /* 0x428e00 */
    if (interp > max) interp = (uint8_t)max;
    if (interp) chr_set_animation_interpolated(c, c->state, next, interp);
    else chr_set_animation(c, next);
    new_animation_hook(c);
    c->next_interpolation = 0;
    return next;
}

void chr_update_animation_for_varient(Character *c)
{
    if (c->type >= 0x6a && c->type <= 0x71) return;
    const void *t = lookup(c, c->type, c->from_state);
    if (t && tr_varient(t) > tr_varient(c->anim)) chr_set_animation_interpolated(c, c->from_state, t, 8);
}

void chr_force_stand(Character *c)
{
    const void *t = lookup(c, CHR_TYPE_STAND, CHR_STATE_STANDING);
    uint16_t from = c->state;
    t = remap_hook(c, t);
    if (!t) return;     /* "failed to find stand animation for %s" */
    old_animation_hook(c, t);
    chr_set_animation_interpolated(c, from, t, 12);
    new_animation_hook(c);
}

/* 0x428e10: an overlay over the pose q (bones of it): its weight rises over its first 6 frames and
   falls over its last 6 (0x529800, 1/6). The bones of TRAM +0x44 go toward the overlay's rotation;
   those of +0x40 take it on top (from no turn toward it, then MUrQuat_Mul 0x433820); the rest stay. */
static void apply_overlay(const void *t, uint16_t frame, float *q, int bones)
{
    const uint8_t *p = t;
    uint16_t d = rd16(p + TRAM_DURATION);
    float w = frame < 6 ? (float)frame * (1.0f / 6.0f) : (int)frame < (int)d - 6 ? 1.0f : ((float)d - (float)frame) * (1.0f / 6.0f);
    float o[4 * CHR_BONES];
    if (!tram_rotations(t, frame, o)) return;
    uint32_t replace = rd32(p + 0x44), add = rd32(p + 0x40);
    static const float none[4] = { 0, 0, 0, 1 };    /* 0x529898 */
    for (int b = 0; b < bones; b++) {
        float *r = q + 4 * b;
        if (replace & 1u << b) {
            if (w == 1.0f) memcpy(r, o + 4 * b, 16);
            else tram_quat_lerp(r, o + 4 * b, w, r);
        } else if (add & 1u << b) {
            float k[4];
            if (w == 1.0f) memcpy(k, o + 4 * b, 16);
            else tram_quat_lerp(none, o + 4 * b, w, k);
            quat_mul(k, r, r);
        }
    }
}

/* ---- the aiming screens (TRAS, in the class's TRSC at ONCC +0xc84): an animation whose frames are a
   grid of poses, yaw by pitch; the aim's place in the grid is lerped from its four nearest and turns the
   pose on top ---- */

enum {
    TRAS_ANIM = 0x00, TRAS_YAW_NEG = 0x04, TRAS_YAW_POS = 0x08, TRAS_YAW_NEG_N = 0x0c, TRAS_YAW_POS_N = 0x0e,
    TRAS_PITCH_NEG = 0x10, TRAS_PITCH_POS = 0x14, TRAS_PITCH_NEG_N = 0x18, TRAS_PITCH_POS_N = 0x1a,
    TRSC_COUNT = 0x16, TRSC_SCREENS = 0x18,
};

/* MUrUnsignedSmallFloat_To_Uns_Round 0x4340a0: 2^23 added, the mantissa's bits. */
static uint32_t round_unsigned(float x)
{
    float t = x + 8388608.0f;
    uint32_t b;
    memcpy(&b, &t, 4);
    return b & 0x7fffff;
}

static const void *screen_anim(const void *s) { return tm_link((const uint8_t *)s + TRAS_ANIM); }   /* 0x4293c0 */

/* 0x429040: the collection's screen for an animation type whose animation's varient is within the
   wanted one, the highest varient (the first of equals). */
static const void *find_screen(const uint8_t *trsc, uint16_t type, uint16_t varient)
{
    const void *best = NULL;
    for (unsigned i = 0, n = rd16(trsc + TRSC_COUNT); i < n; i++) {
        const void *s = tm_link(trsc + TRSC_SCREENS + 4 * i), *t = s ? screen_anim(s) : NULL;
        if (!t) continue;
        uint16_t v = tr_varient(t);
        if ((v & (uint16_t)~varient) || tr_type(t) != type) continue;
        if (!best) best = s;
        if (tr_varient(screen_anim(best)) < v) best = s;
    }
    return best;
}

/* One axis's cell and the part of the way to the next (0x429460's halves). The value is negated first
   (0x529804, -1); modf, then MUrUnsignedSmallFloat_To_Uns_Round 0x4340a0 on the whole part. */
static int screen_cell(float a, float neg_delta, float pos_delta, int neg_n, int pos_n, float *frac)
{
    int i;
    if (a < 0.0f) {
        if (neg_n == 0) { *frac = 0.0f; return 0; }
        double whole, f = modf((double)(a / neg_delta), &whole);
        *frac = (float)f + 1.0f;
        i = -1 - (int)(round_unsigned((float)-whole));
        if (i < -neg_n) i = -neg_n, *frac = 0.0f;
    } else {
        if (pos_n == 0) { *frac = 0.0f; return 0; }
        double whole, f = modf((double)(a / pos_delta), &whole);
        *frac = (float)f;
        i = (int)round_unsigned((float)whole);
        if (i >= pos_n) i = pos_n - 1, *frac = 1.0f;
    }
    return i;
}

/* TRrAimingScreen_Prepare 0x429460: the screen's turn of every bone of its animation's +0x40 at the
   aim (yaw, pitch), into out. False ("aiming screen is not valid") if the aim falls outside the grid:
   out is left as it was. */
static bool screen_prepare(const uint8_t *s, int bones, float yaw, float pitch, float *out)
{
    const uint8_t *t = screen_anim(s);
    if (!t) return false;
    yaw *= -1.0f, pitch *= -1.0f;
    int yn = (int16_t)rd16(s + TRAS_YAW_NEG_N), yp = (int16_t)rd16(s + TRAS_YAW_POS_N);
    int pn = (int16_t)rd16(s + TRAS_PITCH_NEG_N), pp = (int16_t)rd16(s + TRAS_PITCH_POS_N);
    int w = (int16_t)(yn + yp + 1), h = (int16_t)(pn + pp + 1);
    if (rd16(t + TRAM_BONES) != bones || w * h > (int)rd16(t + TRAM_FRAMES)) return false;
    float fy, fp;
    int col = (int16_t)(screen_cell(yaw, rdf(s + TRAS_YAW_NEG), rdf(s + TRAS_YAW_POS), yn, yp, &fy) + yn);
    int row = (int16_t)(screen_cell(pitch, rdf(s + TRAS_PITCH_NEG), rdf(s + TRAS_PITCH_POS), pn, pp, &fp) + pn);
    if (row < 0 || row + 1 >= h || col < 0 || col + 1 >= w) return false;
    float q00[4 * CHR_BONES], q01[4 * CHR_BONES], q10[4 * CHR_BONES], q11[4 * CHR_BONES];
    if (!tram_rotations(t, (uint32_t)(row * w + col), q00) || !tram_rotations(t, (uint32_t)(row * w + col + 1), q01) ||
        !tram_rotations(t, (uint32_t)((row + 1) * w + col), q10) || !tram_rotations(t, (uint32_t)((row + 1) * w + col + 1), q11))
        return false;
    uint32_t mask = rd32(t + 0x40);
    for (int b = 0; b < bones; b++) {
        if (!(mask & 1u << b)) continue;
        float a[4], c[4];
        tram_quat_lerp(q00 + 4 * b, q01 + 4 * b, fy, a);
        tram_quat_lerp(q10 + 4 * b, q11 + 4 * b, fy, c);
        tram_quat_lerp(a, c, fp, out + 4 * b);
    }
    return true;
}

static const float no_turn[4] = { 0, 0, 0, 1 };    /* 0x529898 */

/* TRrAimingScreen_Apply 0x4293d0: the screen's turn on top of each of its bones of the pose. The
   original turns the bones by its buffer even when the aim was outside the grid (then whatever the
   stack held); the port leaves the pose as it is then. */
static void screen_apply(const uint8_t *s, float yaw, float pitch, float *q, int bones)
{
    float k[4 * CHR_BONES];
    if (!screen_prepare(s, bones, yaw, pitch, k)) return;
    uint32_t mask = rd32((const uint8_t *)screen_anim(s) + 0x40);
    for (int b = 0; b < bones; b++)
        if (mask & 1u << b) quat_mul(k + 4 * b, q + 4 * b, q + 4 * b);
}

/* TRrAimingScreen_Apply_Lerp 0x4297e0: from screen a's turn toward b's by t (a bone only one of them
   turns lerps against no turn), on top of the pose. */
static void screen_apply_lerp(const uint8_t *a, const uint8_t *b, float t, float yaw, float pitch, float *q, int bones)
{
    float ka[4 * CHR_BONES], kb[4 * CHR_BONES];
    uint32_t ma = a ? rd32((const uint8_t *)screen_anim(a) + 0x40) : 0, mb = b ? rd32((const uint8_t *)screen_anim(b) + 0x40) : 0;
    if ((a && !screen_prepare(a, bones, yaw, pitch, ka)) || (b && !screen_prepare(b, bones, yaw, pitch, kb))) return;
    for (int i = 0; i < bones; i++) {
        uint32_t bit = 1u << i;
        if (!((ma | mb) & bit)) continue;
        float k[4];
        tram_quat_lerp((ma & bit) ? ka + 4 * i : no_turn, (mb & bit) ? kb + 4 * i : no_turn, t, k);
        quat_mul(k, q + 4 * i, q + 4 * i);
    }
}

/* No aiming screen in the throw target's animation (0x17) nor the victims' (0x4e36f0's list), whatever
   their flag (ONrCharacter_BuildQuaternions' switch). */
static bool never_aims(uint16_t type) { return type == 0x17 || chr_is_victim_type(type); }

/* The aim type a screen falls back to when the class has none for this one. */
static uint16_t screen_fallback(uint16_t type)
{
    switch (type) {
    case 1: return 0;
    case 2: case 3: case 7: case 8: return 6;
    case 9: case 10: case 11: case 0xd0: return 3;
    case 0xd: case 0xe: case 0xf: return 2;
    case 0x3a: case 0x3b: case 0x8e: case 0x8f: return 0x11;
    case 0x3c: case 0x3e: case 0x3f: return 0x3a;
    case 0x3d: case 0x40: case 0x41: return 0x3b;
    case 0xd2: return 9;
    case 0xd9: return 10;
    case 0xda: return 11;
    default: return 1;
    }
}

static float wrap_pi_once(float a)
{
    if (a > 3.1415927f) return a - 6.2831855f;
    if (a < -3.1415927f) return a + 6.2831855f;
    return a;
}

void chr_build_quaternions(Character *c)
{
    TramInfo in;
    tram_info(c->anim, &in);
    if (in.bones != CHR_BONES) return;
    if (!c->interpolating) {
        tram_rotations(c->anim, c->frame, c->quats);
    } else {
        /* TRrQuatArray_Lerp 0x427d60: from the animation's pose toward the start pose, by what is left */
        float t = (float)(1.0 - (double)c->interp_tick / (double)c->interp_length), q[4 * CHR_BONES];
        if (!tram_rotations(c->anim, c->frame, q)) return;
        for (int b = 0; b < CHR_BONES; b++) tram_quat_lerp(q + 4 * b, c->interp_quats + 4 * b, t, c->quats + 4 * b);
    }
    /* ONrCharacter_BuildQuaternions 0x4e5360: the aimed copy (A+0x2020) takes the overlays playing,
       then the aiming screen */
    memcpy(c->aimed, c->quats, sizeof c->aimed);
    bool overlay_stops = false;
    for (int k = 0; k < CHR_OVERLAYS; k++) {
        const ChrOverlay *o = &c->overlay[k];
        if (!o->anim) continue;
        if (o->frame < tr_duration(o->anim)) {
            apply_overlay(o->anim, o->frame, c->aimed, CHR_BONES);
            if (overlay_stops || tr_test_flag(o->anim, TRAM_FLAG_OVERLAY_NO_AIM)) overlay_stops = true;
        }
    }
    const void *screen = NULL;
    if (tr_test_flag(c->anim, TRAM_FLAG_AIMING) && !overlay_stops && !never_aims(in.type)) {
        const uint8_t *trsc = tm_link((const uint8_t *)c->oncc + ONCC_AIMING);
        uint16_t type = in.aim_type, varient = in.varient;
        if (trsc && !(c->flags & 1))
            while (type) {
                const void *s = find_screen(trsc, type, varient);
                if (s && (!screen || tr_varient(screen_anim(screen)) < tr_varient(screen_anim(s)))) screen = s;
                type = screen_fallback(type);
            }
    }
    if (screen != c->aim_screen) {
        c->aim_screen_prev = c->aim_screen;
        c->aim_screen = screen;
        c->aim_screen_time = game_time();
    }
    /* the aim against the body: the desired facing past the facing, on the side the run's offset turns,
       with the sideways aim, less the offset; the pitch with the recoil */
    float d = c->desired_facing - c->facing_offset;
    float side = wrap_pi_once(c->desired_facing - d), ahead = wrap_pi_once(d - c->facing);
    float yaw = c->desired_facing - c->facing;
    if (side > 0.0f && ahead > 0.0f) { if (yaw < 0.0f) yaw += 6.2831855f; }
    else if (side < 0.0f && ahead < 0.0f) { if (yaw > 0.0f) yaw -= 6.2831855f; }
    else yaw = wrap_pi_once(yaw);
    yaw = yaw + c->aim_lr - c->facing_offset + c->aim_lr_add;
    float pitch = c->aim_ud + c->recoil;
    pitch = pitch < -1.5707964f ? -1.5707964f : pitch > 1.5707964f ? 1.5707964f : pitch;
    pitch += c->aim_ud_add;
    if (yaw == 0.0f && pitch == 0.0f) return;
    uint32_t since = game_time() - c->aim_screen_time;
    if (since > 8) c->aim_screen_prev = screen;
    if (!screen && !c->aim_screen_prev) return;
    if (screen == c->aim_screen_prev) screen_apply(screen, yaw, pitch, c->aimed, CHR_BONES);
    else screen_apply_lerp(c->aim_screen_prev, screen, (float)since * 0.125f, yaw, pitch, c->aimed, CHR_BONES);
}

void chr_overlay_set(Character *c, int slot, const void *anim, uint32_t flags)
{
    c->overlay[slot].anim = anim;
    c->overlay[slot].frame = 0;
    c->overlay[slot].flags = flags;
}

void (*chr_overlay_hook)(Character *c, const void *anim);

void chr_overlays_advance(Character *c)
{
    for (int k = 0; k < CHR_OVERLAYS; k++) {
        ChrOverlay *o = &c->overlay[k];
        if (!o->anim) continue;
        /* slot 1 (the weapon's: holster, draw, reload) acts at its action frame (TRAM +0x174) */
        if (k == CHR_OVERLAY_WEAPON && (int)o->frame == (int16_t)rd16((const uint8_t *)o->anim + 0x174) && chr_overlay_hook)
            chr_overlay_hook(c, o->anim);
        if (!o->anim) continue;
        /* the overlay's sounds (0x428780, OSiVariant_Impulse_Get): with the sound's variants */
        if (++o->frame == tr_duration(o->anim)) o->anim = NULL, o->frame = 0;
    }
}

void chr_frame_countdowns(Character *c)
{
    if (c->hold && !--c->hold) chr_do_animation(c, CHR_ANIM_WHEN_CAN, CHR_TYPE_STAND);
    if (c->hard_pause) c->hard_pause--;
    if (c->soft_pause) c->soft_pause--;
    if (c->stun_pause) c->stun_pause--;
    if (c->block_pause) c->block_pause--;
    if (c->countdown_1c98) c->countdown_1c98--;
    if (c->countdown_1c9a) c->countdown_1c9a--;
    if (c->hold_damage) c->hold_damage--;
    /* A+0x281c (the footstep display): not kept */
    c->tick_type = c->type;
    double h = height(c->anim, (uint16_t)c->frame);
    if (c->interpolating) {
        double t = (double)c->interp_tick / (double)c->interp_length;
        h = t * h + (1.0 - t) * c->interp_height;
    }
    c->pelvis_height = (float)h;
}

void chr_frame_motion(const Character *c, float v[3])
{
    tr_position(c->anim, (uint16_t)c->frame, v);
    if (c->interpolating) {
        float t = (float)c->interp_tick / (float)c->interp_length, u = 1.0f - t;
        for (int i = 0; i < 3; i++) v[i] = v[i] * t + u * c->interp_velocity[i];
    }
}

void (*chr_sound_hook)(Character *c, int type, const float pos[3], float radius, Character *target);

/* DoCharacterFrame's noise (0x4e003c), once an animation (its serial) is half through: types 4 and
   0x90..0x93 a sound of type 0 (unimportant) within 60, 0x5e one of type 1 (interesting) within 150,
   of the character at its place */
static void animation_noise(Character *c)
{
    if (c->sound_serial == c->sound_heard || c->frame < tr_duration(c->anim) >> 1) return;
    int type = -1;
    float radius = 0.0f;
    if (c->type == 4 || (c->type >= 0x90 && c->type <= 0x93)) type = 0, radius = 60.0f;
    else if (c->type == 0x5e) type = 1, radius = 150.0f;
    if (type >= 0 && chr_sound_hook) chr_sound_hook(c, type, c->pos, radius, NULL);
    c->sound_heard = c->sound_serial;
}

/* TRrAnimation_GetFootstep 0x42bb60: the footstep at a frame (TRAM +0x20's .raw, the count the byte at
   +0x17e: u16 frame, u16 kind; the last that matches), 0 none. */
static uint16_t footstep_at(const void *t, uint16_t frame)
{
    uint16_t kind = 0;
    unsigned n = ((const uint8_t *)t)[0x17e];
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *f = raw_block(t, 0x20, 4u * i, 4);
        if (f && rd16(f) == frame) kind = rd16(f + 2);
    }
    return kind;
}

/* DoCharacterFrame's footsteps (0x4dff65): a footstep at the frame, on the ground (A+0xf6 0), plays the
   animation's impact; with 0x54e154 (1, the port keeps it so) the left (1) and right (2) at the foot's
   bone (3, 6: A+0x232c, A+0x23bc) half a unit above the location's height, the others at the location. */
static void footsteps(Character *c)
{
    uint16_t kind = footstep_at(c->anim, (uint16_t)c->frame);
    if (!kind || c->air_ticks) return;
    float p[3];
    if (kind == 1 || kind == 2) {
        const float *m = c->bone_world[kind == 1 ? 3 : 6];
        p[0] = m[9], p[1] = c->pos[1] + 0.5f, p[2] = m[11];
    } else {
        memcpy(p, c->pos, sizeof p);
    }
    impact_animation(c, p);
}

void chr_frame_advance(Character *c)
{
    footsteps(c);
    animation_noise(c);
    c->frame = (uint16_t)(c->frame + 1);
    if (c->interpolating && ++c->interp_tick == c->interp_length) c->interpolating = false;
    if (c->fight_timer) c->fight_timer--;
    if (tr_duration(c->anim) == c->frame) chr_next_animation(c);
    if ((c->flags & 0x10) && !tr_is_atomic(c->anim, (uint16_t)c->frame)) chr_next_animation(c);
}
