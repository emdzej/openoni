/* Hits between characters: see chr_hit.h. */
#include "chr_hit.h"

#include "body.h"
#include "chr_anim.h"
#include "m3gm.h"

#include <math.h>
#include <string.h>

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

void chr_hit_init(Character *c, int32_t extra)
{
    int32_t hp = (int32_t)rd32((const uint8_t *)c->oncc + 0xc50) + extra;
    c->hit_points = c->max_hit_points = (uint32_t)(hp < 1 ? 1 : hp);
}

/* ---- the reactions ---- */

/* The knockdowns a type falls back on when the character's collection has none of it (0x4e4e10). */
static uint16_t knockdown_fallback(uint16_t t)
{
    switch (t) {
    case 0x4c: case 0x4d: case 0x4e: case 0x52: case 0x54: case 0x7e: case 0x7f: return 0x4d;
    case 0x4f: case 0x50: case 0x51: case 0x53: return 0x50;
    case 0x55: case 0x56: case 0x57: case 0x5b: return 0x56;
    case 0x58: case 0x59: case 0x5a: case 0x5c: return 0x59;
    default: return t;
    }
}

/* ONrCharacter_Knockdown 0x4e4e10: fight mode, the combo and the hold dropped, then the reaction from the
   state, else from standing (7), else its fallback the same way; at once. Not ported: the in-air remap
   of the types and the upgrade from crouching (state 0x13), the airborne states' own reactions, and the
   interpolation it changes with (the decompiler loses the values of its switches). */
static bool down_type(uint16_t t);
void (*chr_knockdown_hook)(Character *c);

void chr_knockdown(Character *c, uint16_t type)
{
    /* a knockdown that leaves an AI's character down, from not lying: the AI told (0x48b530) */
    if (down_type(type) && c->state != 0x12 && c->state != 0x32 && c->ai && chr_knockdown_hook) chr_knockdown_hook(c);
    c->varient |= 0x200;
    c->fight_timer = chr_class_fight_time(c->oncc);
    c->combo_type = 0;
    c->combo_time = 0;
    c->hold = 0;
    const void *t = chr_lookup(c, type, c->state);
    if (!t) t = chr_lookup(c, type, 7);
    if (!t) {
        uint16_t f = knockdown_fallback(type);
        t = chr_lookup(c, f, c->state);
        if (!t) t = chr_lookup(c, f, 7);
    }
    if (t) chr_set_animation_external(c, c->state, t, 0);   /* else "failed to find knockdown animation" */
}

/* ONiCharacter_RunDeathAnimation 0x4eb970 (and the same test in TakeDamage): unless already lying
   (state 0x12 or 0x32, come from another state) in a reaction, knocked down by type if it is a reaction,
   else by 0x4f. */
static void death_knockdown(Character *c, uint16_t type)
{
    bool lying = c->state == 0x12 || c->state == 0x32;
    bool settled = lying && c->from_state == c->state;
    if (!settled && chr_is_victim_type(c->type) && lying) return;
    chr_knockdown(c, chr_is_victim_type(type) ? type : 0x4f);
}

/* The reactions that leave the character down, which a killing hit plays (TakeDamage's switch). */
static bool down_type(uint16_t t)
{
    switch (t) {
    case 0x4f: case 0x50: case 0x51: case 0x53: case 0x58: case 0x59: case 0x5a: case 0x5c: case 0x8d:
    case 0xd6: case 0xd7:
        return true;
    default:
        return false;
    }
}

void (*chr_hurt_hook)(Character *c, Character *from, uint32_t damage, bool melee);
bool (*chr_block_hook)(Character *c);
void (*chr_die_hook)(Character *c);
void (*chr_script_hook)(Character *c, int script);

void chr_take_damage(Character *c, uint32_t damage, float knockback, const float dir[3], uint16_t knockdown,
                     Character *from)
{
    c->block_pause = c->stun_pause = c->countdown_1c98 = c->countdown_1c9a = 0;
    if (c->flags & 1) {                 /* dead already: the "defeated" script, once */
        if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_DEFEATED);
        return;
    }
    /* A+0xd4, the hit's direction for the body's fall: with the physics */
    if (!(c->flags & 0x2000000)) {      /* not invincible */
        if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_HURT);   /* once (C+0x44 bit 0x10) */
        if ((c->flags & 0x20) && c->hit_points <= damage) {
            /* unkillable (chr_unkillable): one hit point left and the defeated script, except for the
               damage of kind 0x6000000 (the port has none of that kind yet) */
            damage = c->hit_points - 1;
            if (chr_script_hook) chr_script_hook(c, CHR_SCRIPT_DEFEATED);
        }
        if (damage < c->hit_points) {
            c->hit_points -= damage;    /* ONrCharacter_Pain: the sound */
            if (c->ai && chr_hurt_hook) chr_hurt_hook(c, from, damage, from != NULL);
        } else {
            c->hit_points = 0;
            /* the reaction that kills, if it leaves the character down (else the death's) */
            uint16_t k = knockdown ? knockdown : c->knockdown_type;
            if (knockdown || down_type(k)) death_knockdown(c, chr_is_victim_type(c->type) ? k : 0x4f);
            /* ONrCharacter_Die_1_Animating_Hook 0x4eb550: dead (no more animations asked), then
               ONiCharacter_RunDeathAnimation 0x4eb970. The scripts go through chr_die_hook; not
               ported: the weapons and powerups dropped, the living list, the particles, the sound. */
            c->flags |= 1;
            if (chr_die_hook) chr_die_hook(c);
            death_knockdown(c, 0);
        }
    }
    if (!(c->flags & 0x100) && knockback != 0.0f && dir)
        for (int k = 0; k < 3; k++) c->knockback[k] += dir[k] * knockback;
    /* C+0x166c, the HUD's hit direction: with the HUD */
}

/* The angle from d's facing to the direction of x, in (-pi, pi]. */
static float angle_to(const Character *d, const Character *x)
{
    float a = wrap_2pi(atan2f(x->pos[0] - d->pos[0], x->pos[2] - d->pos[2]));
    a = wrap_2pi(a - d->facing);
    if (a >= PI) a -= TWO_PI;
    return a;
}

/* The reaction a hit from behind plays instead. */
static uint16_t from_behind(uint16_t t)
{
    switch (t) {
    case 0x4c: return 0x55;
    case 0x4d: case 0x4e: case 0x7e: case 0x7f: return 0x56;
    case 0x4f: return 0x58;
    case 0x50: return 0x59;
    case 0x51: return 0x5a;
    case 0x52: return 0x5b;
    case 0x53: return 0x5c;
    case 0xc6: return 0xd5;
    case 0xd6: return 0xd7;
    default: return t;  /* 0x54, 0xd4; others "failed to adjust for behind" */
    }
}

static bool is_from_behind(uint16_t t)
{
    switch (t) {
    case 0x55: case 0x56: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0xd5: case 0xd7:
        return true;
    default:
        return false;
    }
}

/* 0x4e38b0: a state a block can start from: the animation from standing, crouching or 0x2f (tested
   twice; the original reads the animation's to state and doesn't use it) in a standing, turning,
   crouching or block type, or any backward move. */
static bool block_state(const Character *c)
{
    uint16_t from = tr_from(c->anim), t = c->type;
    switch (t) {
    case 9: case 0xf: case 0x3c: case 0x3d: case 0xd2: case 0xd3:
        return true;
    default:
        break;
    }
    bool f = from == 0x13 || from == 0x2f || from == 7;
    return f && (t == 0x8e || t == 0x8f || t == 7 || t == 8 || t == 0x17 || t == 6 || t == 0x11);
}

/* ONrCharacter_CouldBlock 0x4e62b0: in a state to block, not stunned (A+0x1c94), with a block (0x17)
   from the state in the fight variant, and the attacker within 20 degrees (0x54e168) of where the
   character aims (C+0x170 - C+0x174). high, low: the block animation's flags 3 and 2. Not ported: the
   weapon's test (none are held yet). */
static bool could_block(const Character *d, const Character *x, bool *high, bool *low)
{
    if (!block_state(d) || d->block_pause) return false;
    const void *b = chr_lookup_varient(d, 0x17, d->state, (uint16_t)(d->varient | 0x200));
    if (!b) return false;
    float aim = wrap_2pi(d->desired_facing - d->facing_offset);
    float a = wrap_2pi(wrap_2pi(atan2f(x->pos[0] - d->pos[0], x->pos[2] - d->pos[2])) - aim);
    if (a >= PI) a -= TWO_PI;
    if (fabsf(a) > 20.0f * 0.017453292f) return false;
    *high = tr_test_flag(b, 3);
    *low = tr_test_flag(b, 2);
    return true;
}

/* HandleAttackMask 0x4e7740, the hit that isn't blocked: once per animation and character; the reaction
   (+0x12), heavier when the stun (+0x14) is over 20; from behind; the defender turned to the attacker;
   the damage (+0xc) and knockback (+0x4, along the attacker's facing); then the stun. */
static void attack_lands(Character *x, Character *d, const uint8_t *e)
{
    int slot = 0;
    for (; slot < 8 && x->hit_list[slot]; slot++)
        if (x->hit_list[slot] == d->id) return;
    if (slot == 8) return;
    x->hit_list[slot] = d->id;
    bool unstoppable = (d->flags & 0x100) != 0;
    uint32_t damage = rd16(e + 0xc);
    float knock = rdf(e + 4);
    uint8_t aflags = e[8];
    float dir[3] = { sinf(x->facing), 0.0f, cosf(x->facing) };
    /* the block: not for unblockable attacks (flag 1); a low one (flag 2) needs a block with flag 2, a
       high one (4) a block with flag 3. Then the character's block function (C+0x15ec, the AI's
       AI2rBlockFunction; none for the player) has the last word. */
    bool blocked = false;
    if (!(aflags & 1)) {
        bool high, low;
        blocked = could_block(d, x, &high, &low);
        if (blocked && (aflags & 2) && !low) blocked = false;
        if (blocked && (aflags & 4) && !high) blocked = false;
        if (blocked && d->ai && chr_block_hook) blocked = chr_block_hook(d);
    }
    if (blocked) {
        float a = angle_to(d, x);
        d->facing = wrap_2pi(d->facing + a);
        if (aflags & 8) chr_take_damage(d, (damage & ~1u) < 2 ? 1 : damage >> 1, knock * 0.5f, dir, 0, x);
        else chr_take_damage(d, 0, 1.0f, dir, 0, x);
        if (unstoppable) d->flags |= 0x80;
        else if (rd16(e + 0x18)) {
            d->knockdown_type = 0xc6;
            d->countdown_1c9a = rd16(e + 0x18);
        } else {
            d->flags |= 0x80;
            d->stun_pause = rd16(e + 0x16);
        }
        return;
    }
    bool staggered = d->countdown_1c98 || d->countdown_1c9a;
    uint16_t stun = rd16(e + 0x14), react = rd16(e + 0x12);
    bool heavy = stun > 0x14;
    if (staggered) {
        if (heavy) {
            heavy = false;
            switch (react) {
            case 0x4d: react = 0x50; break;
            case 0x4e: react = 0x51; break;
            case 0x52: react = 0x53; break;
            case 0x55: react = 0x58; break;
            case 0x56: react = 0x59; break;
            case 0x57: react = 0x5a; break;
            case 0x5b: react = 0x5c; break;
            default: react = 0x4f; break;
            }
        }
    } else if (heavy)
        react = 0xc6;
    if (!unstoppable && react) {
        uint16_t r = react;
        if (!(chr_is_victim_type(react) && d->air_ticks)) {
            if (d->state == 0x12 || d->state == 0x32) r = 0x54;
        }
        float a = angle_to(d, x);
        if (fabsf(a) > 1.5707964f) r = from_behind(r);
        d->knockdown_type = r;
        if (r != 0x54 && r != 0xd4) {
            d->facing = wrap_2pi(d->facing + a);
            if (is_from_behind(r)) d->facing = wrap_2pi(d->facing + PI);
        }
    }
    if (d->hit_points <= damage) d->flags |= 0x20000000;   /* killed by a blow */
    chr_take_damage(d, damage, knock, dir, 0, x);
    if (heavy) d->countdown_1c9a = stun;
    else d->block_pause = stun;
}

/* ---- the contacts ---- */

typedef struct { float s[BODY_MAX_BONES][4]; uint32_t n; } Spheres;

/* The parts' bounding spheres in the world: each geometry's (PNTA +0x24) under the part's matrix. */
static void part_spheres(const Character *c, Spheres *out)
{
    out->n = 0;
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES) return;
    BodyMatrix root, m[BODY_MAX_BONES];
    chr_root_matrix(c, root);
    if (!body_matrices(body, root, c->aimed, m)) return;
    for (uint32_t b = 0; b < bones; b++) {
        float box[6], sph[4] = { 0, 0, 0, 0 };
        m3gm_bounds(body_geometry(body, b), box, sph);
        body_transform(m[b], sph, out->s[b]);
        out->s[b][3] = sph[3];
    }
    out->n = bones;
}

bool chr_part_point(const Character *c, unsigned part, float out[3])
{
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES || part >= bones) return false;
    BodyMatrix root, m[BODY_MAX_BONES];
    chr_root_matrix(c, root);
    if (!body_matrices(body, root, c->aimed, m)) return false;
    const float zero[3] = { 0, 0, 0 };
    body_transform(m[part], zero, out);
    return true;
}

bool chr_weapon_matrix(const Character *c, float out[12])
{
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    if (!bones || bones > BODY_MAX_BONES || bones != CHR_BONES) return false;
    BodyMatrix root, m[BODY_MAX_BONES];
    chr_root_matrix(c, root);
    if (!body_matrices(body, root, c->aimed, m)) return false;
    /* ONrCharacter_SetCharacterClass 0x4d7c30: the weapon node hangs from the right fist (18), or the
       left (14) for a class with +0xc8c; the left hand's half turn about x (ONrCharacter_SetHand 0x4332b0) */
    bool left = ((const uint8_t *)c->oncc)[0xc8c] != 0;
    const float *f = m[left ? 14 : 18];
    memcpy(out, f, 12 * sizeof(float));
    if (left)
        for (int k = 3; k < 9; k++) out[k] = -f[k];
    return true;
}

/* 0x4e8e70: of a's attacks at its frame, those whose parts touch one of b's (touch_b: per part of b,
   the parts of a it touches). Returns the landed attacks' bits (over the attacks' order), the entries,
   and the highest priority (+0x1a). */
static uint32_t landed(const Character *a, const Spheres *sb, const uint32_t *touch_b, const uint8_t **entries, uint16_t *prio)
{
    uint32_t bits = 0;
    *prio = 0;
    const uint8_t *t = a->anim;
    for (unsigned i = 0; i < t[TRAM_ATTACK_COUNT] && i < 32; i++) {
        const uint8_t *e = tr_attack(t, i);
        entries[i] = NULL;
        if (!e || rd16(e + 0xe) > a->frame || a->frame > rd16(e + 0x10)) continue;
        entries[i] = e;
        for (uint32_t j = 0; j < sb->n; j++)
            if (touch_b[j] & rd32(e)) {
                bits |= 1u << i;
                if (rd16(e + 0x1a) >= *prio) *prio = rd16(e + 0x1a);
                break;
            }
    }
    return bits;
}

/* iCollideTwoLikelyCharacters 0x4e73e0. HandleGenericMask (the push apart) waits for the physics. A thrower and
   the one it throws (A+0x1ed8) don't touch. */
static void collide_two(Character *a, Character *b)
{
    if (a->threw == b || b->threw == a) return;
    Spheres sa, sb;
    part_spheres(a, &sa);
    part_spheres(b, &sb);
    if (!sa.n || !sb.n) return;
    uint32_t ta[BODY_MAX_BONES] = { 0 }, tb[BODY_MAX_BONES] = { 0 };
    bool any = false;
    for (uint32_t i = 0; i < sa.n; i++)
        for (uint32_t j = 0; j < sb.n; j++) {
            float dx = sa.s[i][0] - sb.s[j][0], dy = sa.s[i][1] - sb.s[j][1], dz = sa.s[i][2] - sb.s[j][2];
            float r = sa.s[i][3] + sb.s[j][3];
            if (dx * dx + dy * dy + dz * dz < r * r) {      /* 0x4331d0 */
                any = true;
                ta[i] |= 1u << j;
                tb[j] |= 1u << i;
            }
        }
    if (!any) return;
    const uint8_t *ea[32], *eb[32];
    uint16_t pa, pb;
    uint32_t la = landed(a, &sb, tb, ea, &pa), lb = landed(b, &sa, ta, eb, &pb);
    if (pb <= pa) {
        for (unsigned i = 0; i < 32; i++)
            if (la >> i & 1) attack_lands(a, b, ea[i]);
        if (pb < pa) return;
    }
    for (unsigned i = 0; i < 32; i++)
        if (lb >> i & 1) attack_lands(b, a, eb[i]);
}

void chr_collide_all(Character **chars, int n)
{
    for (int i = 0; i < n; i++) {
        Character *a = chars[i];
        if (a->flags & 0x40004) continue;
        for (int j = i + 1; j < n; j++) {
            Character *b = chars[j];
            if ((b->flags & 0x40004) || (a->flags & 2) || (b->flags & 2)) continue;
            float dx = a->pos[0] - b->pos[0], dy = a->pos[1] - b->pos[1], dz = a->pos[2] - b->pos[2];
            if (dx * dx + dy * dy + dz * dz < 625.0f) collide_two(a, b);    /* 0x52a99c */
        }
    }
    /* the reactions asked: a knockdown, or a block held (flag 0x80) */
    for (int i = 0; i < n; i++) {
        Character *c = chars[i];
        if (c->knockdown_type) {
            if (!(c->flags & 1)) chr_knockdown(c, c->knockdown_type);
            c->knockdown_type = 0;
        } else if (c->flags & 0x80) {
            c->varient |= 0x200;
            c->fight_timer = chr_class_fight_time(c->oncc);
            c->hold = 0;
            if (c->type != 0x17 && !chr_do_animation(c, CHR_ANIM_NOW, 0x17)) chr_do_animation(c, CHR_ANIM_NOW, 0x17);
        }
        c->flags &= ~0x80u;
    }
}
