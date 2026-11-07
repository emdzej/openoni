/* Throws: see throw.h. */
#include "throw.h"
#include "inventory.h"

#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "knowledge.h"
#include "physics.h"
#include "trac.h"

#include <math.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */
static const float HALF_PI = 1.5707963705062866f;                               /* 0x529cd0 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }


/* The throw block (TRAM +0x1c, .raw): the victim's offset from the thrower (+0x00..+0x08), its turn
   from the thrower's facing (+0x0c), the reach (+0x10, TRrAnimation_GetThrowDistance 0x4291c0) and the
   victim's animation type (+0x14). */
enum { THROW_SIZE = 0x18 };   /* TRAM_THROW, chr_anim.h */

static const uint8_t *throw_block(const void *tram) { return tr_raw_block(tram, TRAM_THROW, 0, THROW_SIZE); }

static float throw_distance(const void *tram)
{
    const uint8_t *t = throw_block(tram);
    return t ? rdf(t + 0x10) : 0.0f;
}

static bool can_be_thrown(const Character *c) { return ((const uint8_t *)c->oncc)[0xc8f] == 0; }

/* TRrAnimation_IsInvulnerable 0x42c2f0: whether the frame is in the animation's invulnerable run
   (TRAM +0x178 the first frame, +0x179 how many). */
static bool invulnerable_frame(const void *tram, uint32_t frame)
{
    const uint8_t *t = tram;
    return frame >= t[0x178] && frame < (uint32_t)t[0x178] + t[0x179];
}

/* ONiCharacter_FindNearestOpponent 0x4ea2b0: the enemy (not a friend) nearest the aim within max_angle
   of it and max_dist away, the one with the least angle. With throw_check, those that can't be thrown
   are passed over. *angle gets the angle to it from the aim, in (-pi, pi]. */
static Character *find_target(Character *c, float *angle, float offset, float max_dist, float max_angle, bool throw_check)
{
    Character *best = NULL;
    float best_abs = max_angle + 1.0f, best_angle = 0.0f;
    *angle = 0.0f;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch) continue;
        Character *t = &ch->c;
        if ((t->flags & 1) || t == c) continue;
        if (!ai_team_relation(((Chr *)c)->team, ch->team)) continue;
        if (throw_check) {
            if (chr_cheat_unstoppable && !t->ai) continue;     /* 0x5ece91 */
            if (t->flags & 0x40100) continue;
            if (!can_be_thrown(t)) continue;
            if (!t->ai && chr_cheat_unthrowable) continue;
            /* the original tests the animation of an active character only: the port's all are */
            if (t->anim && tr_test_flag(t->anim, 1)) continue;
        }
        float dx = t->pos[0] - c->pos[0], dy = t->pos[1] - c->pos[1], dz = t->pos[2] - c->pos[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);      /* MUrPoint_Distance 0x433270 */
        float aim = c->desired_facing - c->facing_offset;
        if (aim < 0.0f) aim += TWO_PI;
        else if (aim >= TWO_PI) aim -= TWO_PI;
        float a = atan2f(dx, dz);
        if (a < 0.0f && (a += TWO_PI) < 0.0f) a += TWO_PI;
        a -= aim;
        if (a < 0.0f) a += TWO_PI;
        /* the original's turn into (-pi, pi] here is undone by the test after it: a stays in [0, 2 pi) */
        a -= offset;
        if (a < 0.0f) a += TWO_PI;
        if (a > PI) a -= TWO_PI;
        float abs_a = fabsf(a);
        if (dist <= max_dist && abs_a <= max_angle && abs_a < best_abs) {
            best = t;
            best_abs = abs_a;
            best_angle = a;
        }
    }
    if (best) *angle = best_angle;
    return best;
}

typedef struct {
    const void *anim, *victim;
    uint32_t score;
} Best;

/* AttemptSpecificThrow 0x4ea950: a throw that reaches the target (dist2 its distance squared) and has
   the victim's animation in the thrower's collection, for the target's state and its variant in fight
   mode; the best by the victim's variant, then the throw's. */
static void attempt_specific(Character *c, Character *t, float dist2, const void *anim, Best *best)
{
    float d = throw_distance(anim);
    if (d * d < dist2) return;
    const uint8_t *th = throw_block(anim);
    if (!th) return;
    const void *v = chr_lookup_varient(c, rd16(th + 0x14), t->state, (uint16_t)(t->varient | 0x200));
    if (!v) return;
    /* "animation %s requires the fight varient" when the victim's hasn't 0x200: printed, then taken */
    uint32_t score = (uint32_t)tr_varient(v) << 16 | tr_varient(anim);
    if (score > best->score) {
        best->anim = anim;
        best->victim = v;
        best->score = score;
    }
}

/* AttemptThrow 0x4eaa10: the enemy in front (24 units, 45 degrees) that can be thrown now; the front
   type if it faces the thrower (over 90 degrees off its facing), else the behind type; of the thrower's
   animations of that type from its state with no variant bits the thrower lacks (fight mode aside),
   the best. On success the thrower turns to the target and enters fight mode. */
static const void *attempt_throw(Character *c, uint16_t front, uint16_t behind)
{
    float angle;
    Character *t = find_target(c, &angle, 0.0f, 24.0f, 0.7853982f, true);
    if (!t || !can_be_thrown(t) || (!t->ai && chr_cheat_unthrowable)) return NULL;
    if (!t->anim || tr_test_flag(t->anim, 1) || invulnerable_frame(t->anim, t->frame)) return NULL;
    float diff = t->facing - c->facing;
    if (diff < 0.0f) diff += TWO_PI;
    if (diff > PI) diff -= TWO_PI;
    uint16_t type = fabsf(diff) > HALF_PI ? front : behind;
    float dx = t->pos[0] - c->pos[0], dy = t->pos[1] - c->pos[1], dz = t->pos[2] - c->pos[2];
    float dist2 = dx * dx + dy * dy + dz * dz;                  /* MUrPoint_Distance_Squared 0x433220 */
    uint16_t mask = (uint16_t)~(c->varient | 0x200);
    Best best = { NULL, NULL, 0 };
    const void *list[64];
    int n = trac_list(chr_collection(c), type, c->state, list, 64);
    for (int i = 0; i < n; i++)
        if (!(tr_varient(list[i]) & mask)) attempt_specific(c, t, dist2, list[i], &best);
    if (best.anim && best.victim) {
        t->thrown_by = c;
        c->facing = wrap_2pi(angle + c->facing);
        c->throw_target = t;
        c->throw_victim_anim = best.victim;
        chr_enter_fight_mode(c);
    }
    return best.anim;
}

bool (*throw_ai_hook)(const Character *c, const void **anim, const void **victim, Character **target);

bool throw_target_ok(const Character *t)
{
    if (!can_be_thrown(t) || (!t->ai && chr_cheat_unthrowable) || (t->flags & 0x100)) return false;
    return t->anim && !tr_test_flag(t->anim, 1) && !invulnerable_frame(t->anim, t->frame);
}

/* RemapAnimationHook 0x4ea550, the AI's part: the throw its executor asks for, of a target within 45
   degrees of the facing that the throw reaches (AttemptSpecificThrow, which may find another victim's
   animation: "AI tried to throw ... got ...", taken); the thrower turns to it and enters fight mode. */
static const void *remap_ai(Character *c, const void *next)
{
    const void *anim, *victim;
    Character *t;
    if (!throw_ai_hook || !throw_ai_hook(c, &anim, &victim, &t) || !t) return next;
    float a = wrap_2pi(atan2f(t->pos[0] - c->pos[0], t->pos[2] - c->pos[2]));
    float f = wrap_2pi(c->desired_facing - c->facing_offset);   /* C+0x170 less C+0x174 */
    float d = a - f;
    if (d < 0.0f) d += TWO_PI;
    if (d >= PI) d -= TWO_PI;
    if (fabsf(d) > 0.7853982f) return next;     /* ("AI %s tried to throw %s at %f degrees") */
    float dx = c->pos[0] - t->pos[0], dy = c->pos[1] - t->pos[1], dz = c->pos[2] - t->pos[2];
    Best best = { NULL, NULL, 0 };
    attempt_specific(c, t, dx * dx + dy * dy + dz * dz, anim, &best);
    if (!best.anim) return next;
    t->thrown_by = c;
    c->facing = wrap_2pi(d + c->facing);
    c->throw_target = t;
    c->throw_victim_anim = best.victim;
    chr_enter_fight_mode(c);
    return best.anim;
}

/* RemapAnimationHook 0x4ea550: a character the AI doesn't drive (C+0x04 flag 0x400000) turns its throw
   moves into a throw of the character in front. */
static const void *remap(Character *c, const void *next)
{
    if (c->ai) return remap_ai(c, next);
    uint16_t front, behind;
    switch (tr_type(next)) {
    case 0x1f: front = 0x6a, behind = 0x6c; break;
    case 0x1a: front = 0x6b, behind = 0x6d; break;
    case 0x43: front = 0x6e, behind = 0x6f; break;
    case 0x42: front = 0x70, behind = 0x71; break;
    default: return next;
    }
    const void *t = attempt_throw(c, front, behind);
    return t ? t : next;
}

/* The victim at the thrower's hands: the thrower's place plus the throw's offset turned by its facing
   (MUrPoint_RotateYAxis 0x433010), moved there through the physics (0x4e9330): against a wall it stays
   on this side. */
static void hold_victim(Character *c, Character *t)
{
    const uint8_t *th = throw_block(c->anim);
    if (!th) return;
    float o[3] = { rdf(th), rdf(th + 4), rdf(th + 8) };
    float co = cosf(c->facing), si = sinf(c->facing);
    float p[3] = { c->pos[0] + co * o[0] + si * o[2], c->pos[1] + o[1], c->pos[2] - si * o[0] + co * o[2] };
    physics_move_to(t, p);
}

/* NewAnimationHook 0x4e97a0, a throw (TRAM flag 15): the victim, alive, in fight mode if the throw is,
   takes its animation from its own state (remapped, through the animation hooks, 6 frames of
   interpolation), goes to the thrower's hands and turns by the throw's angle; it is being thrown
   (C+0x04 flag 0x1000) and the thrower can't hit it nor it the thrower (A+0x1ed8). */
static void throw_starts(Character *c)
{
    Character *t = c->throw_target;
    if (!t) return;     /* "throwtarget was NULL" */
    if ((t->flags & 1) || !chars_in_use((Chr *)t)) return;
    if (tr_varient(c->anim) & 0x200) chr_enter_fight_mode(t);
    /* both seen again if invisible, and shown to at least 0.6 */
    inventory_reveal(c, true);
    inventory_reveal(t, true);
    /* a melee sound within 150, of the thrower at its victim */
    if (chr_sound_hook) chr_sound_hook(c, 3, c->pos, 150.0f, t);
    chr_set_animation_external(t, t->state, c->throw_victim_anim, 6);
    hold_victim(c, t);
    c->threw = t;
    const uint8_t *th = throw_block(c->anim);
    float f = (th ? rdf(th + 0xc) : 0.0f) + c->facing;
    t->facing = f >= TWO_PI ? f - TWO_PI : f;
    t->flags |= 0x1000;
}

void throw_install(void)
{
    chr_remap_hook = remap;
    chr_throw_hook = throw_starts;
}

/* TRAM +0x10 (.raw): 4 bytes each, the count the byte at +0x17b: damage (u16) at a frame (u16).
   TRrAnimation_GetDamage 0x429160 adds up the frame's. */
static uint32_t self_damage(const void *tram, uint32_t frame)
{
    const uint8_t *t = tram;
    uint32_t sum = 0;
    for (unsigned i = 0; i < t[0x17b]; i++) {
        const uint8_t *e = tr_raw_block(tram, 0x10, 4u * i, 4);
        if (e && rd16(e + 2) == frame) sum += rd16(e);
    }
    return sum;
}

void throw_update(Character **chars, int n)
{
    for (int i = 0; i < n; i++) {
        Character *c = chars[i];
        Character *t = c->throw_target;
        if (t && c->frame == 0 && !(t->flags & 1) && chars_in_use((Chr *)t) && throw_block(c->anim))
            hold_victim(c, t);
        if (c->flags & 1) continue;
        uint32_t d = self_damage(c->anim, c->frame);
        if (!d) continue;
        /* a victim's is its thrower's (WPrOwner_MakeMeleeDamageFromCharacter 0x51ce00) */
        Character *from = tr_test_flag(c->anim, TRAM_FLAG_THROW_TARGET) ? c->thrown_by : NULL;
        if (from && chr_cheat_omnipotent && !from->ai && d <= c->hit_points) d = c->hit_points;   /* 0x4dd862 */
        chr_take_damage(c, d, 0.0f, NULL, 0, from, CHR_DAMAGE_MELEE);
    }
}
