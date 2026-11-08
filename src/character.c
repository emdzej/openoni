/* The character: see character.h. The input side of ONrGameState_UpdateCharacters 0x4dccf0 (step 5,
   ONrCharacter_HandleHeartbeatInput 0x4f6ad0) and the frame (step 7, ONrGameState_DoCharacterFrame
   0x4ddba0). The animation engine is in chr_anim.c. */
#include "character.h"
#include "graph.h"

#include "akev.h"
#include "akev_collide.h"
#include "body.h"
#include "chr_anim.h"
#include "inventory.h"
#include "film.h"
#include "chr_hit.h"
#include "exe.h"
#include "physics.h"
#include "tm.h"
#include "trac.h"
#include "tram.h"
#include "weapon.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static float oncc_float(const Character *c, unsigned off)
{
    uint32_t v = rd32((const uint8_t *)c->oncc + off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;     /* 0x529548, 0x529544 */

/* ---- input moves ---- */

/* An entry of the input tables the handlers try in order (0x18 bytes in the original): the keys that
   must have gone down, the keys that must be held, the attack the move must follow (0 any), the
   animation type (0 ends the table), and whether the player may take it from the previous state. */
typedef struct {
    uint64_t down, held;
    uint16_t combo, type;
    uint8_t from_previous;
} Move;

#define K(x) ((uint64_t)(x))
#define FWD K(CHR_KEY_FORWARD)
#define BACK K(CHR_KEY_BACK)
#define STEP_L K(CHR_KEY_STEP_LEFT)
#define STEP_R K(CHR_KEY_STEP_RIGHT)
#define CROUCH K(CHR_KEY_CROUCH)
#define WALK CHR_KEY_WALK
#define JUMP K(CHR_KEY_JUMP)

/* The tables the handlers build on their stacks (instruction operands in the original). */
static const Move run_moves[] = {        /* HandleStartRunning 0x4f8ef0 */
    { 0, FWD | WALK, 0, 0xd1, 0 }, { 0, FWD | WALK, 0, 2, 0 }, { 0, BACK | WALK, 0, 0xd3, 0 },
    { 0, BACK | WALK, 0, 0xf, 0 }, { 0, FWD, 0, 0xd0, 0 }, { 0, FWD, 0, 3, 0 }, { 0, FWD, 0, 0xd1, 0 },
    { 0, FWD, 0, 2, 0 }, { 0, BACK, 0, 0xd2, 0 }, { 0, BACK, 0, 9, 0 }, { 0, BACK, 0, 0xd3, 0 },
    { 0, BACK, 0, 0xf, 0 }, { 0 },
};
static const Move crouch_run_moves[] = {
    { 0, CROUCH | FWD | WALK, 0, 0x3b, 0 }, { 0, CROUCH | FWD, 0, 0x3a, 0 }, { 0, CROUCH | FWD, 0, 0x3b, 0 },
    { 0, CROUCH | BACK | WALK, 0, 0x3d, 0 }, { 0, CROUCH | BACK, 0, 0x3c, 0 }, { 0, CROUCH | BACK, 0, 0x3d, 0 },
    { 0 },
};
static const Move walk_only_moves[] = {
    { 0, FWD, 0, 0xd1, 0 }, { 0, FWD, 0, 2, 0 }, { 0, BACK, 0, 0xd3, 0 }, { 0, BACK, 0, 0xf, 0 }, { 0 },
};
static const Move sidestep_moves[] = {   /* HandleStartSidestep 0x4f83f0 */
    { 0, CROUCH | STEP_L, 0, 0x3e, 0 }, { 0, CROUCH | STEP_R, 0, 0x3f, 0 }, { 0, STEP_L, 0, 0xd9, 0 },
    { 0, STEP_L, 0, 10, 0 }, { 0, STEP_L, 0, 0xd, 0 }, { 0, STEP_R, 0, 0xda, 0 }, { 0, STEP_R, 0, 0xb, 0 },
    { 0, STEP_R, 0, 0xe, 0 }, { 0 },
};
static const Move walk_sidestep_moves[] = {
    { 0, CROUCH | STEP_L | WALK, 0, 0x40, 0 }, { 0, CROUCH | STEP_R | WALK, 0, 0x41, 0 },
    { 0, STEP_L | WALK, 0, 0xd, 0 }, { 0, STEP_L, 0, 0xd, 0 }, { 0, STEP_R | WALK, 0, 0xe, 0 },
    { 0, STEP_R, 0, 0xe, 0 }, { 0 },
};
static const Move walk_only_sidestep_moves[] = {
    { 0, CROUCH | STEP_L | WALK, 0, 0xd, 0 }, { 0, CROUCH | STEP_R | WALK, 0, 0xe, 0 },
    { 0, STEP_L | WALK, 0, 0xd, 0 }, { 0, STEP_L, 0, 0xd, 0 }, { 0, STEP_R | WALK, 0, 0xe, 0 },
    { 0, STEP_R, 0, 0xe, 0 }, { 0 },
};
static const Move stop_moves[] = {       /* HandleStop 0x4f9720 */
    { 0, 0, 0, 0xd8, 0 }, { 0, 0, 0, 0xce, 0 }, { 0, 0, 0, 0xcf, 0 }, { 0, CROUCH, 0, 0x11, 0 },
    { 0, 0, 0, 6, 0 }, { 0 },
};
static const Move crouch_moves[] = { { 0, CROUCH, 0, 0x11, 0 }, { 0 } };    /* HandleChangeStance 0x4f9940 */
static const Move stand_moves[] = { { 0, 0, 0, 6, 0 }, { 0 } };
static const Move turn_left_moves[] = {  /* HandleStandingTurn 0x4f9fa0 */
    { 0, CROUCH, 0, 0x8e, 0 }, { 0, 0, 0, 7, 0 }, { 0, 0, 0, 0x8e, 0 }, { 0 },
};
static const Move turn_right_moves[] = {
    { 0, CROUCH, 0, 0x8f, 0 }, { 0, 0, 0, 8, 0 }, { 0, 0, 0, 0x8f, 0 }, { 0 },
};
static const Move fall_through_moves[] = {  /* HandleFallThrough 0x4f9a70 */
    { 0, 0, 0, 0xd8, 0 }, { 0, 0, 0, 0xce, 0 }, { 0, 0, 0, 0xcf, 0 }, { 0, CROUCH, 0, 0x11, 0 },
    { 0, CROUCH, 0, 0x4a, 0 }, { 0, 0, 0, 6, 0 }, { 0, 0, 0, 0x11, 0 }, { 0, 0, 0, 0x4a, 0 }, { 0 },
};
static const Move leave_stun_moves[] = {    /* HandleLeaveStun, in 0x4f6ad0 */
    { 0, CROUCH, 0, 0x11, 0 }, { 0, 0, 0, 6, 0 }, { 0, 0, 0, 0x11, 0 }, { 0 },
};
static const Move slide_moves[] = { { CROUCH, 0, 0, 4, 0 }, { 0 } };       /* HandleStartSliding */
/* HandleStartJumping 0x4fa2f0. Sideways right asks for type 0x29, a landing type (the jumps are 0x12
   to 0x15): no state leads to it from the ground, so the jump right is a jump in place. */
static const Move jump_moves[] = {
    { 0, FWD, 0, 0x12, 0 }, { 0, STEP_L, 0, 0x14, 0 }, { 0, STEP_R, 0, 0x29, 0 }, { 0, BACK, 0, 0x13, 0 },
    { 0, 0, 0, 5, 0 }, { 0 },
};
/* HandleStartLanding, in 0x4f6ad0: the landing, and the hard one after a fall that hurt. */
static const Move land_moves[] = {
    { 0, FWD, 0, 0x28, 0 }, { 0, STEP_L, 0, 0x2a, 0 }, { 0, STEP_R, 0, 0x29, 0 }, { 0, BACK, 0, 0x2b, 0 },
    { 0, 0, 0, 0x18, 0 }, { 0 },
};
static const Move hard_land_moves[] = {
    { 0, FWD, 0, 0x89, 0 }, { 0, STEP_L, 0, 0x8b, 0 }, { 0, STEP_R, 0, 0x8a, 0 }, { 0, BACK, 0, 0x8c, 0 },
    { 0, 0, 0, 0x88, 0 }, { 0 },
};

/* The attacks are a table in the exe's data (0x551c00, 0x18 bytes each, to a type 0): read at the first
   use. */
enum { ATTACKS_VA = 0x551c00, ATTACK_STRIDE = 0x18, MAX_ATTACKS = 128 };
static Move attack_moves[MAX_ATTACKS + 1];
static bool attacks_read;

static const Move *attacks(void)
{
    if (attacks_read) return attack_moves;
    attacks_read = true;
    for (int i = 0; i < MAX_ATTACKS; i++) {
        const uint8_t *e = exe_data(ATTACKS_VA + (uint32_t)i * ATTACK_STRIDE, ATTACK_STRIDE);
        if (!e || !rd16(e + 0x12)) break;
        Move *m = &attack_moves[i];
        m->down = (uint64_t)rd32(e) | (uint64_t)rd32(e + 4) << 32;
        m->held = (uint64_t)rd32(e + 8) | (uint64_t)rd32(e + 0xc) << 32;
        m->combo = rd16(e + 0x10);
        m->type = rd16(e + 0x12);
        m->from_previous = e[0x14];
    }
    return attack_moves;
}

/* Two switches in the exe's data: 0x5533e9, a byte (1 on the CD) that lets HandleFallThrough keep a stand
   by queueing it, and 0x5533d8, a u32 (8): input queued more than that many frames before the end of
   an animation is forgotten. */
static bool keep_stand_queued(void)
{
    const uint8_t *p = exe_data(0x5533e9, 1);
    return p && *p;
}
static uint32_t queue_window(void) { return exe_u32(0x5533d8); }

/* 0x4fa1c0: the first move of the table the input allows and DoAnimation takes. Unless keep, the lookups
   see the fight variant (0x0200), and a fighting move puts the character in fight mode. */
static bool try_moves(Character *c, const ChrInput *in, const Move *m, unsigned flags, bool keep, const void **out)
{
    uint16_t v = c->varient;
    const void *t = NULL;
    if (!keep) c->varient |= 0x200;
    for (; m->type; m++) {
        if ((in->held & m->held) != m->held) continue;
        if (m->combo && c->combo_type != m->combo) continue;
        if ((in->down & m->down) != m->down) continue;
        /* the player (C+0x1e8 = 0): the port's only controller */
        unsigned f = flags | (!keep && m->from_previous ? CHR_ANIM_FROM_PREVIOUS : 0u);
        if ((t = chr_do_animation(c, f, m->type))) break;
    }
    c->varient = v;
    if (!keep && t && (tr_varient(t) & 0x200)) chr_enter_fight_mode(c);
    if (out) *out = t;
    return t != NULL;
}

/* ---- facing ---- */

/* 0x4e31e0: the desired facing turns, kept in [0, 2 pi). */
static void turn_desired(Character *c, float d)
{
    float f = d + c->desired_facing;
    c->desired_facing = f < 0.0f ? f + TWO_PI : f >= TWO_PI ? f - TWO_PI : f;
}

/* The diagonal or sideways run's turn: the offset becomes a, the desired facing follows. */
static void set_facing_offset(Character *c, float a)
{
    float d = a - c->facing_offset;
    c->facing_offset += d;
    turn_desired(c, d);
}

void character_set_facing_offset(Character *c, float a) { set_facing_offset(c, a); }

static float wrap_pi(float a)
{
    if (a > PI) return a - TWO_PI;
    if (a < -PI) return a + TWO_PI;
    return a;
}

/* 0x4e3230: how far the facing is from the desired one. When the aim and the offset turn the same way,
   the long way round is kept (so a diagonal run doesn't turn back through the other side). */
static float facing_delta(const Character *c)
{
    float aim = c->desired_facing - c->facing_offset;
    float o = wrap_pi(c->desired_facing - aim);
    float a = wrap_pi(aim - c->facing);
    float d = c->desired_facing - c->facing;
    if (o > 0.0f && a > 0.0f) return d < 0.0f ? d + TWO_PI : d;
    if (o < 0.0f && a < 0.0f) return d > 0.0f ? d - TWO_PI : d;
    return wrap_pi(d);
}

float chr_facing_delta(const Character *c) { return facing_delta(c); }

/* 0x4e3d00: whether the character doesn't turn to the desired facing by itself (standing still, a
   standing turn's job; throws; victims). */
bool chr_holds_facing(const Character *c)
{
    bool b = false;
    switch (c->type) {
    case 6: case 0x11:
        /* standing: the player always; a character the AI's movement drives (C+4 0x400000) only in
           the states 4, 0xc, 0x12, 0x1e, 0x1f and 0x32 (A+0x1afc) */
        if (!(c->flags & CHR_FLAG_AI_MOVEMENT)) return true;
        switch (c->state) {
        case 4: case 0xc: case 0x12: case 0x1e: case 0x1f: case 0x32: b = true; break;
        default: break;
        }
        break;
    case 0x17: case 0x4a:
        b = true;
        break;
    default:
        break;
    }
    b = b || tr_test_flag(c->anim, TRAM_FLAG_THROW_SOURCE) || tr_test_flag(c->anim, TRAM_FLAG_THROW_TARGET);
    return b || chr_is_victim_type(c->type);
}

/* The turn rate per tick: 0.1 for AI not under film (0x4e9670), else 0.0785, or half that walking. */
static float turn_rate(const ChrInput *in) { return (in->held & WALK) ? 0.039269909f : 0.078539819f; }

/* HandleTurnInput 0x4f7b90: the mouse turns (or, with the look key, aims sideways) and aims up and down;
   a weapon's aim speed (class +0x7c, by half) scales it, and its recoil (C+0x1d0, 0x1d4) settles. */
static void handle_turn_input(Character *c, const ChrInput *in)
{
    const uint8_t *wc = c->weapon ? c->weapon->cls : NULL;     /* WPrGetClass 0x5185f0 */
    float tx = in->turn_x, ty = in->turn_y, turn = 0.0f;
    if (wc) {
        float k = rdf(wc + 0x7c) * 0.5f;
        tx = k * tx, ty = k * ty;
    }
    if (!(in->held & CHR_KEY_LOOK)) turn = tx * -0.015625f;
    else c->aim_lr -= tx * 0.015625f;
    c->aim_ud = ty * 0.015625f + c->aim_ud;
    if (!wc) {
        c->aim_kick = 0.0f;
        c->recoil = 0.0f;
    } else {
        /* the recoil's parameters (0x51cd20, class +0x80: per shot, the most, the speed's divisor, the
           return idle and the return while the shot's delay runs; the "wp_hypo" override is off) */
        const uint8_t *r = wc + 0x80;
        if (c->aim_kick == 0.0f) {
            if (c->recoil != 0.0f) {
                const Weapon *w = c->weapon;    /* 0x51cea0: the delay's part left, 0 none */
                float left = w->delay ? (float)w->delay / (float)w->delay2 : 0.0f;
                c->recoil -= rdf(r + (left == 0.0f ? 0xc : 0x10));
            }
        } else {
            c->recoil += c->aim_kick;
            c->aim_kick /= rdf(r + 8);
            if (c->aim_kick < 0.001f) c->aim_kick = 0.0f;
        }
        if (!c->ai && (rd32(wc + 0xcc) & 0x10)) {
            /* the player's recoil goes into the aim itself */
            c->aim_ud += c->recoil;
            c->recoil = 0.0f;
        } else {
            float m = rdf(r + 4), v = c->recoil;
            c->recoil = v < 0.0f ? 0.0f : v > m ? m : v;
        }
    }
    const float lim = 1.5707964f;
    c->aim_lr = c->aim_lr < -lim ? -lim : c->aim_lr > lim ? lim : c->aim_lr;
    c->aim_ud = c->aim_ud < -lim ? -lim : c->aim_ud > lim ? lim : c->aim_ud;
    if (tr_test_flag(c->anim, TRAM_FLAG_NO_TURN)) return;
    float rate = turn_rate(in);
    if (in->held & CHR_KEY_TURN_LEFT) turn += rate;
    else if (in->held & CHR_KEY_TURN_RIGHT) turn -= rate;
    turn_desired(c, turn);
}

/* 0x4f7e80: the facing turns toward the desired one at the turn rate, while moving or in a standing
   turn (whose direction must be the one under way). */
static void update_facing(Character *c, const ChrInput *in)
{
    bool right = c->turning_right, left = c->turning_left, turn;
    float r = turn_rate(in), d = facing_delta(c), step;
    if (-r <= d) {
        if (d < r) c->turning_left = false;
        c->turning_right = false;
    } else
        c->turning_left = false;
    switch (c->type) {
    case 7: case 8: case 0x8e: case 0x8f:
        turn = true;
        break;
    default:
        left = right = turn = !chr_holds_facing(c);
        if (!turn) return;
    }
    if (d > 0.0f && left) step = r < d ? r : d;
    else {
        if (!turn || d >= 0.0f || !right) return;
        step = d < -r ? -r : d;
    }
    float f = step + c->facing;
    c->facing = f < 0.0f ? f + TWO_PI : f >= TWO_PI ? f - TWO_PI : f;
}

/* ---- the movement handlers ---- */

static bool in_list_moving(uint16_t t)
{
    switch (t) {
    case 2: case 3: case 7: case 8: case 9: case 10: case 0xb: case 0xd: case 0xe: case 0xf:
    case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41:
    case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd9: case 0xda:
        return true;
    default:
        return false;
    }
}

bool (*character_action_hook)(Character *c, const ChrInput *in);
bool (*character_pickup_hook)(Character *c, const ChrInput *in);
void (*character_weapon_input_hook)(Character *c, const ChrInput *in);
void (*character_frame_hook)(Character *c, int step);
void (*character_action_frame_hook)(Character *c);
void (*character_sidestep_hook)(Character *c, bool walk);

/* HandleStartTauntOrAction 0x4f80d0: the console action under way, the action key's console or door
   (the hook), else the taunt (type 0x5e, the action key gone down). The taunt waits for the game time
   to pass GS+0x110 for the player (never set yet) and isn't done with a weapon in hand. */
static bool handle_start_taunt_or_action(Character *c, const ChrInput *in)
{
    static const Move taunt[] = { { CHR_KEY_ACTION, 0, 0, 0x5e, 0 }, { 0 } };
    if (character_action_hook && character_action_hook(c, in)) return true;
    if (!(in->down & CHR_KEY_ACTION)) return false;
    return try_moves(c, in, taunt, CHR_ANIM_WHEN_CAN, false, NULL);
}

/* HandlePriorAction 0x4f82d0: an action or attack is queued; nothing else is asked for. */
static bool handle_prior_action(const Character *c)
{
    uint16_t t = c->queued;
    return t == 4 || t == 5 || t == 0xc || (t >= 0x12 && t <= 0x16) || (t >= 0x1a && t <= 0x27) ||
           (t >= 0x2c && t <= 0x33) || (t >= 0x42 && t <= 0x49) || (t >= 0x4c && t <= 0x5c) || t == 0x5e ||
           (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) || t == 0x7d || (t >= 0x80 && t <= 0x87) ||
           t == 0x8d || (t >= 0x9d && t <= 0xa3) || (t >= 0xac && t <= 0xbf) || t == 0xd6 || t == 0xd7 ||
           t == 0xdb || (t >= 0xe3 && t <= 0xe5) || t == 0xe7;
}

/* HandleStartRunning 0x4f8ef0. A diagonal (forward or back with a step) turns the body 45 degrees off
   the aim. */
static bool handle_start_running(Character *c, const ChrInput *in)
{
    /* A+0xf6, ticks in the air: always 0 until the physics */
    if (!(in->held & (FWD | BACK))) return false;
    bool crouch = (in->held & CROUCH) != 0;
    const Move *table = crouch ? crouch_run_moves : run_moves;
    if (c->flags & 0x8000000) {
        if (crouch) return false;
        table = walk_only_moves;
    }
    const void *t;
    if (!try_moves(c, in, table, CHR_ANIM_WHEN_CAN, true, &t)) {
        /* fallen without the fight variant: "help, I am fallen but without the fight varient I can't
           get up" */
        if (c->state == 0x12 || c->state == 0x32) return try_moves(c, in, table, CHR_ANIM_WHEN_CAN, false, NULL);
        return false;
    }
    float a = (in->held & FWD) ? 0.78539819f : -0.78539819f;
    switch (tr_type(t)) {
    case 2: case 3: case 9: case 0xf: case 0xce: case 0xcf: case 0xd0: case 0xd1: case 0xd2: case 0xd3:
    case 0xd9: case 0xda:
        if (in->held & STEP_R) set_facing_offset(c, -a);
        else if (in->held & STEP_L) set_facing_offset(c, a);
        break;
    default:
        break;
    }
    return true;
}

static uint16_t start_of(uint16_t t)
{
    switch (t) {
    case 2: return 0xd1;
    case 3: return 0xd0;
    case 9: return 0xd2;
    case 0xf: return 0xd3;
    default: return 0;
    }
}

static const void *lookup(const Character *c, uint16_t type)
{
    const uint8_t *col = tm_link((const uint8_t *)c->oncc + ONCC_ANIMATIONS);
    return col ? trac_lookup(col, type, c->state, c->varient) : NULL;
}

/* HandleStartSidestep 0x4f83f0: a sidestep if the class has one for the variant; else (or when the run
   has the better variant) a run turned 90 degrees. */
static bool handle_start_sidestep(Character *c, const ChrInput *in)
{
    bool left = (in->held & STEP_L) != 0, right = (in->held & STEP_R) != 0;
    bool walk = (in->held & WALK) != 0, crouch = (in->held & CROUCH) != 0, walk_only = (c->flags & 0x8000000) != 0;
    if (!left && !right) return false;
    const Move *table = walk_only ? walk_only_sidestep_moves : walk ? walk_sidestep_moves : sidestep_moves;
    const void *side = NULL;
    for (const Move *m = table; m->type; m++) {
        if ((in->held & m->held) != m->held || (m->combo && c->combo_type != m->combo) ||
            (in->down & m->down) != m->down)
            continue;
        const void *t = lookup(c, m->type);
        if (!t) continue;
        if (tr_varient(t) == c->varient) {
            if (try_moves(c, in, table, CHR_ANIM_WHEN_CAN, true, NULL)) return true;
        } else
            side = t;
        break;
    }
    /* The runs to turn sideways, in order (the player's; AI with its movement mode, 0x4a93b0, takes the
       backward ones: with the AI). */
    uint16_t cand[4] = { 0, 0, 0, 0 };
    if (walk_only) cand[0] = 2;
    else if (walk) {
        if (crouch) cand[0] = 0x3b, cand[1] = 0x3a, cand[2] = 2, cand[3] = 3;
        else cand[0] = 2, cand[1] = 3;
    } else {
        if (crouch) cand[0] = 0x3a, cand[1] = 0x3b, cand[2] = 3, cand[3] = 2;
        else cand[0] = 3, cand[1] = 2;
    }
    const void *run = NULL;
    uint16_t chosen = 0;
    for (int i = 0; i < 4 && !run; i++) {
        if (!cand[i]) continue;
        if ((run = lookup(c, cand[i]))) chosen = cand[i];
        else if (start_of(cand[i]) && (run = lookup(c, start_of(cand[i])))) chosen = start_of(cand[i]);
    }
    if (run && (!side || tr_varient(run) > tr_varient(side))) {
        if (chr_do_animation(c, CHR_ANIM_WHEN_CAN, chosen)) {
            if ((c->flags & CHR_FLAG_AI_MOVEMENT) && character_sidestep_hook) character_sidestep_hook(c, walk);
            switch (c->type) {
            case 2: case 3: case 0x3a: case 0x3b: case 0xd0: case 0xd1: case 0xd2: case 0xd3:
                set_facing_offset(c, left ? 1.5707964f : -1.5707964f);
                break;
            default:
                break;
            }
            return true;
        }
    }
    return side && try_moves(c, in, table, CHR_ANIM_WHEN_CAN, true, NULL);
}

/* HandleChangeStance 0x4f9940: crouching down and getting up. */
static bool handle_change_stance(Character *c, const ChrInput *in)
{
    bool crouch = (in->held & CROUCH) != 0;
    switch (c->state) {
    case CHR_STATE_STANDING:
        return crouch && try_moves(c, in, crouch_moves, CHR_ANIM_WHEN_CAN, true, NULL);
    case CHR_STATE_CROUCHING:
        return !crouch && try_moves(c, in, stand_moves, CHR_ANIM_WHEN_CAN, true, NULL);
    case 0x2f:
        return try_moves(c, in, crouch_moves, CHR_ANIM_WHEN_CAN, true, NULL) ||
               try_moves(c, in, stand_moves, CHR_ANIM_WHEN_CAN, true, NULL);
    default:
        return false;
    }
}

/* HandleStandingTurn 0x4f9fa0: past 70 degrees from the desired facing, a turn on the spot. */
static bool handle_standing_turn(Character *c, const ChrInput *in)
{
    float th = 70.0f * 0.017453292f, d = facing_delta(c);
    if (th < d || c->turning_left) {
        bool r = try_moves(c, in, turn_left_moves, CHR_ANIM_WHEN_CAN, true, NULL);
        c->turning_left = true;
        return r;
    }
    if (-th <= d && !c->turning_right) return false;
    bool r = try_moves(c, in, turn_right_moves, CHR_ANIM_WHEN_CAN, true, NULL);
    c->turning_right = true;
    return r;
}

/* HandleStop 0x4f9720: a moving animation stops when its keys let go. */
static bool handle_stop(Character *c, const ChrInput *in)
{
    return in_list_moving(c->type) && try_moves(c, in, stop_moves, CHR_ANIM_WHEN_CAN, true, NULL);
}

/* The moving types HandleFallThrough knocks down in the air. */
static bool in_air_knocked_down(uint16_t t)
{
    switch (t) {
    case 2: case 3: case 9: case 10: case 0xb: case 0xd: case 0xe: case 0xf: case 0x3a: case 0x3b: case 0x3c:
    case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x41: case 0xce: case 0xcf: case 0xd0: case 0xd1:
    case 0xd2: case 0xd3: case 0xd8: case 0xd9: case 0xda:
        return true;
    default:
        return false;
    }
}

/* ONrCharacter_Knockdown 0x4e4e10 with the fall (0x19), from HandleFallThrough (0x4f9d1a). */
static void knockdown(Character *c)
{
    chr_knockdown(c, 0x19);
}

/* HandleStartJumping 0x4fa2f0: the jump key gone down, unless jumping already. 0x4dc7e0 first keeps the
   motion so far as the jump's (A+0x104); a jump refused takes it back. */
static bool handle_start_jumping(Character *c, const ChrInput *in)
{
    if (!(in->down & JUMP)) return false;
    switch (c->type) {
    case 5: case 0x12: case 0x13: case 0x14: case 0x29:
        return false;
    default:
        break;
    }
    chr_velocity_estimate(c, c->jump_velocity);
    c->jumped = 1;
    c->jump_given = true;
    bool r = try_moves(c, in, jump_moves, CHR_ANIM_WHEN_CAN, true, NULL);
    if (!r) c->jump_given = false;
    return r;
}

/* HandleStartLanding, in 0x4f6ad0: the ground met (A+0x21dd). The hard landing is tried from the state,
   then from 0x26 and 0x25, then the soft one. */
static bool handle_start_landing(Character *c, const ChrInput *in)
{
    bool r;
    if (!c->hard_landing) {
        r = try_moves(c, in, land_moves, CHR_ANIM_NOW, true, NULL);
    } else {
        uint16_t st = c->state;
        r = try_moves(c, in, hard_land_moves, CHR_ANIM_NOW, true, NULL);
        if (!r) {
            c->state = 0x26;
            r = try_moves(c, in, hard_land_moves, CHR_ANIM_NOW, true, NULL);
        }
        if (!r) {
            c->state = 0x25;
            r = try_moves(c, in, hard_land_moves, CHR_ANIM_NOW, true, NULL);
        }
        if (!r) {
            c->state = st;
            r = try_moves(c, in, land_moves, CHR_ANIM_NOW, true, NULL);
        }
    }
    c->landing = c->hard_landing = false;
    return r;
}

/* HandleFallThrough 0x4f9a70: nothing else asked; idle, keep standing, or settle into a stand. */
static bool handle_fall_through(Character *c, const ChrInput *in)
{
    unsigned flags = CHR_ANIM_QUEUE;
    bool bad = chr_in_bad_varient(c);
    if (bad && !tr_is_atomic(c->anim, (uint16_t)c->frame)) flags = CHR_ANIM_NOW;
    /* in the air, a moving type is knocked down */
    if (c->air_ticks && in_air_knocked_down(c->type)) {
        knockdown(c);
        return false;
    }
    switch (c->type) {
    case 5: case 0x12: case 0x13: case 0x14: case 0x15: case 0x19:
        if (chr_do_animation(c, CHR_ANIM_QUEUE, 0x19)) return true;
        break;
    default:
        break;
    }
    if (c->air_ticks) {
        /* the fall (0x19), else the long fall (0xdc), else knocked down */
        if (chr_do_animation(c, CHR_ANIM_QUEUE, 0x19) || chr_do_animation(c, CHR_ANIM_QUEUE, 0xdc)) return true;
        knockdown(c);
        return false;
    }
    /* AI2rTryingToThrow 0x48b9a0 lets the player idle */
    bool r = false;
    if (c->idle_delay + c->last_action <= c->time) {
        r = chr_do_animation(c, flags, CHR_TYPE_IDLE) != NULL;
        chr_recalculate_idle_delay(c);
    }
    if (r) return true;
    if (keep_stand_queued() && !bad) {
        bool crouch = (in->held & CROUCH) != 0;
        if (c->type == CHR_TYPE_CROUCH && c->state == CHR_STATE_CROUCHING && crouch) {
            c->queued = CHR_TYPE_CROUCH;
            return true;
        }
        if (c->type == CHR_TYPE_STAND && c->state == CHR_STATE_STANDING && !crouch) {
            c->queued = CHR_TYPE_STAND;
            return true;
        }
    }
    if (try_moves(c, in, fall_through_moves, flags, true, NULL)) return true;
    /* "help, I could not s/c/p without the fight varient" */
    if (try_moves(c, in, fall_through_moves, flags, false, NULL)) return true;
    chr_force_stand(c);
    return false;
}

/* The handlers of HandleHeartbeatInput, in order, until one takes the input. */
static void handlers(Character *c, const ChrInput *in, bool can_fight)
{
    if (c->landing && handle_start_landing(c, in)) return;
    /* HandleStartFalling: long in the air (91 ticks), the long fall */
    if (c->air_ticks && (c->hold_ticks || c->air_ticks >= 0x5b) && chr_do_animation(c, CHR_ANIM_NOW, 0xdc)) return;
    /* Stun and block (the countdowns A+0x1c96, 0x1c94, 0x1c98, 0x1c9a, HandleStun): with the
       damage; they stay 0. */
    switch (c->type) {
    case 0x17: case 0xc6: case 0xd4: case 0xd5:     /* HandleLeaveStun */
        if (try_moves(c, in, leave_stun_moves, CHR_ANIM_NOW, true, NULL)) return;
        break;
    default:
        break;
    }
    /* HandleStartPickup (the swap key, A+0x1d8, 0x4fa4b0): weapon.c's */
    if (character_pickup_hook && character_pickup_hook(c, in)) return;
    if (can_fight) {
        switch (c->type) {
        case 3: case 9: case 10: case 0xb: case 0xd0: case 0xd2: case 0xd9: case 0xda:     /* HandleStartSliding */
            if (try_moves(c, in, slide_moves, CHR_ANIM_WHEN_CAN, true, NULL)) return;
            break;
        default:
            break;
        }
        /* HandleStartAttack: not while an overlay plays in slot 1 (A+0x1c64, 0x4d7ba0) or a weapon fires
           (A+0x21e0, 0x21e1): neither exists yet. */
        if (in->down && try_moves(c, in, attacks(), CHR_ANIM_WHEN_CAN, false, NULL)) return;
        if (handle_start_jumping(c, in)) return;
    }
    if (handle_start_taunt_or_action(c, in)) return;
    if (handle_prior_action(c)) return;
    if (handle_start_running(c, in)) return;
    if (handle_start_sidestep(c, in)) return;
    if (handle_change_stance(c, in)) return;
    if (handle_standing_turn(c, in)) return;
    if (handle_stop(c, in)) return;
    handle_fall_through(c, in);
}

static bool victim_input_type(uint16_t t)
{
    return (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) || (t >= 0x80 && t <= 0x84);
}

static void clear_input(ChrInput *in)
{
    in->held = in->down = in->up = 0;
    in->not_held = ~(uint64_t)0;
}

/* ONrCharacter_HandleHeartbeatInput 0x4f6ad0 for the player: the weapon's input (reload, holster, fire:
   0x4e8f80..0x4e9020) and the pickup's (0x4eda40) through character_weapon_input_hook (weapon.c), the
   jump's rise while held; playing a film, the film's special animation in place of the handlers. */
static void heartbeat_input(Character *c, ChrInput *in)
{
    if ((c->flags & 1) && chr_is_victim_type(c->type)) return;
    if (c->hold) return;
    /* A+0x2150, the time of the last input: not kept */
    if (c->combo_time != 0xffffffffu && c->time - c->combo_time > 10) c->combo_type = 0;
    if (c->flags & 1) {
        clear_input(in);
        handle_fall_through(c, in);
        return;
    }
    if (victim_input_type(c->type)) clear_input(in);
    set_facing_offset(c, 0.0f);     /* the last tick's diagonal is undone; the handlers set it again */
    handle_turn_input(c, in);
    const void *start = c->anim;
    uint16_t queued = c->queued;
    /* the weapon's keys (reload, fire: C+0x08, A+0x21df..0x21e4), then HandlePickupInput 0x4eda40 */
    if (character_weapon_input_hook) character_weapon_input_hook(c, in);
    /* playing a film (0x4e4100), its special animation (0x4f720c: from the state, interpolated over 6) */
    const void *special = film_special_animation(c, in);
    if (special) chr_set_animation_external(c, c->state, special, 6);
    else handlers(c, in, !(c->flags & 0x8000000));
    /* Input queued long before the end is forgotten: the queue goes back to what it was. */
    uint32_t w = queue_window();
    if (w && c->anim == start && (int)(c->frame + 1 + w) < (int)tr_duration(c->anim)) c->queued = queued;
    /* jump held: the rise grows (ONCC+0x10) for the first ticks in the air (ONCC+0x16) */
    if (c->air_ticks && (in->held & JUMP) && c->air_ticks < rd16((const uint8_t *)c->oncc + 0x16))
        c->air_velocity[1] += oncc_float(c, 0x10);
    chr_update_animation_for_varient(c);
    update_facing(c, in);
}

/* ---- the frame ---- */

/* The types whose ground motion isn't kept on the ground (the switch of step 9): knockdowns, getting up,
   the victims, stuns. */
static bool keeps_off_ground(uint16_t t)
{
    return (t >= 0x4c && t <= 0x5c) || (t >= 0x60 && t <= 0x65) || (t >= 0x72 && t <= 0x77) ||
           (t >= 0x7e && t <= 0x84) || t == 0x8d || t == 0xc6 || (t >= 0xd4 && t <= 0xd7);
}

/* 0x4de759: the tractor beam's pull on a held character (the air velocity, in the character's space):
   beyond 40 across, toward the shooter by 0.04 (turned by the facing); up toward 12 over the shooter
   (more by 0.3 of the way past 40), no higher than 1.2 times the way from where the pull found it (A+0x114)
   over that, by 0.003 of the gap (at most 40); past a speed of 2.5, the excess cut to 0.8. */
static void held_pull(Character *c, const Character *by)
{
    float dx = by->pos[0] - c->pos[0], dz = by->pos[2] - c->pos[2];
    float d = sqrtf(dx * dx + dz * dz);
    if (!c->held_from_set) {
        memcpy(c->held_from, c->pos, sizeof c->held_from);
        c->held_from_set = true;
    }
    float *w = c->air_velocity;
    if (d > 40.0f) {
        float cs = cosf(c->facing), sn = sinf(c->facing);
        w[0] += (cs * dx - sn * dz) * 0.04f / d;
        w[2] += (sn * dx + cs * dz) * 0.04f / d;
    }
    float y = by->pos[1] + 12.0f;
    if (d > 40.0f) y += (d - 40.0f) * 0.3f;
    float fx = c->held_from[0] - c->pos[0], fz = c->held_from[2] - c->pos[2];
    float top = sqrtf(fz * fz + fx * fx) * 1.2f + c->held_from[1];
    if (y > top) y = top;
    float dy = y - c->pos[1];
    if (dy < -40.0f) dy = -40.0f;
    else if (dy > 40.0f) dy = 40.0f;
    w[1] += dy * 0.12f / 40.0f;
    float sp = sqrtf(w[0] * w[0] + w[2] * w[2] + w[1] * w[1]);
    if (sp > 2.5f) {
        float k = ((sp - 2.5f) * 0.8f + 2.5f) / sp;
        w[0] *= k, w[1] *= k, w[2] *= k;
    }
}

/* Step 9 of DoCharacterFrame: the jump's delay, the start of a jump or a fall, and the tick's motion
   (in the character's space): on the ground the animation's root motion, kept down on the ground
   (AKrCollision_Point 0x441c80 down 4: the gap if it is 0.5 to 4, else 0.5); in the air the air
   velocity, which gravity (ONCC+4) slows down to the fall's limit (ONCC+0xc). Held by a tractor beam
   (A+0x112), the air velocity instead, slowing by a tenth, and the pull toward the shooter (0x4de759).
   Past 300 ticks in the air the fall kills. Not ported: the film's movement log. */
static void jump_and_fall(Character *c, const void *env, uint64_t held, float v[3])
{
    if (c->hold_ticks && !--c->hold_ticks) {
        c->air_ticks = 1;
        c->held_by = NULL;
        c->fall_height = c->pos[1];
    }
    if (c->hold_ticks) {
        memcpy(v, c->air_velocity, sizeof c->air_velocity);
        c->air_ticks++;
        for (int k = 0; k < 3; k++) c->air_velocity[k] *= 0.9f;   /* 0x5295a4 */
        if (c->held_by) held_pull(c, c->held_by);
        return;
    }
    c->held_from_set = false;
    if (c->start_falling) {
        c->no_ground_ticks = 0;
        if (!c->air_ticks) {
            if (!c->jump_given) {
                c->jumped = 0;
                chr_velocity_estimate(c, c->air_velocity);
                c->air_velocity[1] = -0.5f;
            } else {
                /* the jump: the motion kept as it was asked for, up by ONCC+8, and at least half that
                   the way the keys go (forward, back, left, right in that order) */
                float j = oncc_float(c, 8), h = j * 0.5f;
                float x = c->jump_velocity[0], z = c->jump_velocity[2];
                if (held & FWD) z = h < z ? z : h;
                else if (held & BACK) z = z <= -h ? z : -h;
                else if (held & STEP_L) x = h < x ? x : h;
                else if (held & STEP_R) x = x <= -h ? x : -h;
                c->jump_given = false;
                c->air_velocity[0] = x;
                c->air_velocity[1] = c->jump_velocity[1] + j;
                c->air_velocity[2] = z;
            }
            c->air_ticks = 1;
        }
        c->start_falling = false;
    }
    if (!c->air_ticks) {
        chr_frame_motion(c, v);
        if (!keeps_off_ground(c->type) && !tr_is_attack(c->anim)) {
            static AkevHit hit[AKEV_HITS_MAX];
            const float down[3] = { 0.0f, -4.0f, 0.0f };
            if (env && akev_collide_point(env, c->pos, down, 0x48800, 1, hit)) {
                float d = c->pos[1] - hit[0].point[1];
                if (d < 4.0f && d >= 0.5f) {
                    v[1] -= d;
                    return;
                }
            }
        }
        v[1] -= 0.5f;
        return;
    }
    memcpy(v, c->air_velocity, sizeof c->air_velocity);
    c->air_ticks++;
    /* 300 ticks in the air (5 s): the fall kills, all the hit points as its damage (0x4deb4c) */
    if (c->air_ticks > 300) chr_take_damage(c, c->hit_points, 0.0f, NULL, 0, NULL, CHR_DAMAGE_FALL);
    float lim = oncc_float(c, 0xc);
    c->air_velocity[1] -= oncc_float(c, 4);
    if (c->air_velocity[1] <= lim) c->air_velocity[1] = lim;
}

/* Step 14's part: the shown super value (C+0x1668) moved toward the super value (C+0x1664), or for a
   class with ONCC+0xc8d its health over the maximum (as a part of the maximum) if more; down by 0.06 a
   tick (0x52b2d8), up by 0.03 (0x52b2f0); then the super particles by it. */
static void super_level_step(Character *c)
{
    float want = c->super;
    if (((const uint8_t *)c->oncc)[0xc8d] && c->hit_points > c->max_hit_points) {
        float over = (float)(c->hit_points - c->max_hit_points) / (float)c->max_hit_points;
        if (want <= over) want = over;
    }
    if (want <= 0.0f) want = 0.0f;
    if (want <= c->super_level) {
        float d = c->super_level - 0.06f;
        if (want < d) want = d;
    } else {
        float u = c->super_level + 0.03f;
        if (u <= want) want = u;
    }
    c->super_level = want;
    chr_super_particles(c);
}

/* ONrGameState_DoCharacterFrame 0x4ddba0 (docs/re/character.md, its steps): the countdowns, the
   jump and fall, the knockback, the physics' move (physics.c), the pickup, aim, weapons and overlays
   (through character_frame_hook), the animation, the pose and the body box, the pain. Not kept: the
   footstep display (A+0x281c). */
/* Step 4 of DoCharacterFrame, the player's sprint (variant 0x0100): forward gone down twice within 15
   ticks starts it (A+0x2190 = -1, else the time of the last); it lasts while forward is held. */
static void sprint(Character *c, const ChrInput *in)
{
    if (c->ai) return;
    if (c->sprint_time == 0xffffffffu) {
        c->varient |= 0x100;
        if (in->not_held & FWD) c->sprint_time = 0;
    } else {
        c->varient &= (uint16_t)~0x100;
        if (in->down & FWD) c->sprint_time = c->time - c->sprint_time < 15 ? 0xffffffffu : c->time;
    }
}

bool chr_physics_later;

/* 0x4e0785: C+0x1c4, the weapon's move over the tick (its matrix's place now less before), 0 without a
   weapon in hand at the tick's start (the shooters' particles take it, WPiDoEvent) */
static void weapon_move(Character *c, bool had, const float was[3])
{
    for (int k = 0; k < 3; k++) c->weapon_move[k] = had ? c->weapon_at[k] - was[k] : 0.0f;
}

static void do_frame(Character *c, const void *env, const ChrInput *in)
{
    uint64_t held = in->held;
    /* the pickup animation's action frame (weapon.c) */
    if (character_frame_hook) character_frame_hook(c, CHR_FRAME_PICKUP);
    inventory_update(c);                                /* WPrInventory_Update */
    if (character_action_frame_hook && c->ai && c->anim && (int)c->frame == (int16_t)rd16((const uint8_t *)c->anim + 0x174))
        character_action_frame_hook(c);
    chr_anim_particles_frame(c);
    sprint(c, in);
    chr_frame_countdowns(c);
    if (c->flags & 2) {         /* step 8, dying: the body as it is (its drift with the physics) */
        inventory_invis_update(c);  /* (ONrGameState_UpdateCharacters' for the dying) */
        return;
    }
    /* 0x4de3f5: with a weapon in hand (C+0x194) its place before the tick, for its move (0x4e0785) */
    float weapon_was[3];
    bool had_weapon = c->weapon != NULL;
    memcpy(weapon_was, c->weapon_at, sizeof weapon_was);
    float v[3], before[3] = { c->pos[0], c->pos[1], c->pos[2] };
    jump_and_fall(c, env, held, v);
    /* 0x433010: the motion in the character's space (+z forward, +x left) turned by the facing */
    float s = sinf(c->facing), co = cosf(c->facing);
    float w[3] = { co * v[0] + s * v[2], v[1], -s * v[0] + co * v[2] };
    /* step 10: the knockback on the ground (0.07 of it a tick, decaying by 0.93, gone under 0.01
       squared); in the air it is dropped */
    if (!c->air_ticks) {
        for (int k = 0; k < 3; k++) w[k] += c->knockback[k] * 0.07f, c->knockback[k] *= 0.93f;
        float kk = c->knockback[0] * c->knockback[0] + c->knockback[1] * c->knockback[1] + c->knockback[2] * c->knockback[2];
        if (kk < 0.01f) memset(c->knockback, 0, sizeof c->knockback);
    } else
        memset(c->knockback, 0, sizeof c->knockback);
    film_interp_step(c);
    if (!env) {
        /* no environment (the tests' bare characters): the move taken whole, the height kept */
        memcpy(c->prev_pos, c->pos, sizeof c->pos);     /* step 12: C+0x154 */
        for (int k = 0; k < 3; k++) c->pos[k] += w[k];
        c->pos[1] = before[1];
        super_level_step(c);
        chr_frame_advance(c);
        chr_overlays_advance(c);
        if (character_frame_hook) character_frame_hook(c, CHR_FRAME_FIRE);
        chr_build_quaternions(c);
        chr_bone_matrices(c);
        weapon_move(c, had_weapon, weapon_was);
        inventory_shield_update(c);
        inventory_invis_update(c);
        chr_pain_frame(c);
        return;
    }
    if (!c->has_physics) physics_start(c);
    physics_take(c, w);                                 /* step 12 */
    super_level_step(c);
    chr_frame_advance(c);
    chr_overlays_advance(c);
    if (character_frame_hook) character_frame_hook(c, CHR_FRAME_FIRE);
    chr_build_quaternions(c);
    chr_bone_matrices(c);       /* 0x4e5a30: the parts' matrices (the pelvis, where particles hang) */
    inventory_shield_update(c);                         /* ONrCharacter_Shield_Update, the living's */
    inventory_invis_update(c);                          /* ONrCharacter_Invis_Update, after the matrices */
    physics_body(c);                                    /* step 19 */
    c->wall_hits = c->obj_hits = c->pushes = 0;         /* (0x4e06cb: A+0xb0..0xb6 cleared) */
    c->blocked_by_char = false;
    weapon_move(c, had_weapon, weapon_was);
    chr_pain_frame(c);                                  /* 0x4e07ef: the pain's cries */
    /* OBiPhysics_Update runs after all the characters' frames and contacts (chr_physics_later) */
    if (chr_physics_later) c->phys_pending = true;
    else physics_update(c, env);
}

uint64_t chr_input_bits(const CharacterInput *in)
{
    uint64_t k = 0;
    if (in->forward > 0) k |= FWD;
    if (in->forward < 0) k |= BACK;
    if (in->strafe < 0) k |= STEP_L;
    if (in->strafe > 0) k |= STEP_R;
    if (in->walk) k |= WALK;
    if (in->punch) k |= K(CHR_KEY_PUNCH);
    if (in->kick) k |= K(CHR_KEY_KICK);
    if (in->jump) k |= JUMP;
    if (in->crouch) k |= CROUCH;
    if (in->action) k |= CHR_KEY_ACTION;
    if (in->fire) k |= CHR_KEY_FIRE;
    if (in->swap) k |= CHR_KEY_SWAP;
    if (in->drop) k |= CHR_KEY_DROP;
    if (in->reload) k |= CHR_KEY_RELOAD;
    if (in->hypo) k |= CHR_KEY_HYPO;
    return k | in->extra;
}

void chr_input_mask(CharacterInput *in, uint64_t keys)
{
    if ((in->forward > 0 && !(keys & FWD)) || (in->forward < 0 && !(keys & BACK))) in->forward = 0;
    if ((in->strafe < 0 && !(keys & STEP_L)) || (in->strafe > 0 && !(keys & STEP_R))) in->strafe = 0;
    in->walk &= (keys & WALK) != 0;
    in->punch &= (keys & K(CHR_KEY_PUNCH)) != 0;
    in->kick &= (keys & K(CHR_KEY_KICK)) != 0;
    in->jump &= (keys & JUMP) != 0;
    in->crouch &= (keys & CROUCH) != 0;
    in->action &= (keys & CHR_KEY_ACTION) != 0;
    in->fire &= (keys & CHR_KEY_FIRE) != 0;
    in->swap &= (keys & CHR_KEY_SWAP) != 0;
    in->drop &= (keys & CHR_KEY_DROP) != 0;
    in->reload &= (keys & CHR_KEY_RELOAD) != 0;
    in->hypo &= (keys & CHR_KEY_HYPO) != 0;
    in->extra &= keys;
}

bool character_init(Character *c, const void *oncc, const float pos[3], float facing)
{
    memset(c, 0, sizeof *c);
    c->room = -1;
    c->oncc = oncc;
    c->lod_shown = c->lod_wanted = 6;
    memcpy(c->pos, pos, sizeof c->pos);
    memcpy(c->prev_pos, pos, sizeof c->prev_pos);
    c->facing = c->desired_facing = facing;
    c->pelvis_height = 7.5f;    /* ONrGameState_NewCharacter 0x4dac50 */
    chr_hit_init(c, 0);
    c->console_marker = -1;
    c->speech_id = UINT32_MAX;
    /* the end of ONrCharacter_SetCharacterClass 0x4d7c30: the size, between ONCC +0xc58 and +0xc5c by
       UUrRandom (over 65535, 0x529d74) */
    float lo = rdf((const uint8_t *)oncc + 0xc58), hi = rdf((const uint8_t *)oncc + 0xc5c);
    c->scale = (hi - lo) * (float)trac_random() * 1.5259022e-05f + lo;
    chr_recalculate_idle_delay(c);
    if (!tm_link((const uint8_t *)oncc + ONCC_ANIMATIONS)) return false;
    /* 0x4d7f90, the first animation of ONrGameState_NewActiveCharacter 0x4db830: stand, from standing,
       for the variant without the weapon bits */
    c->varient &= 0x87ff;
    c->type = c->queued = CHR_TYPE_STAND;
    c->state = CHR_STATE_STANDING;
    const void *t = lookup(c, CHR_TYPE_STAND);
    if (!t) return false;
    chr_set_animation(c, t);
    chr_build_quaternions(c);
    return true;
}

bool chr_set_class(Character *c, const void *oncc)
{
    /* ONrCharacter_SetCharacterClass 0x4d7c30: a class with its variant (+0x42c), animations and body
       set (+0xc34), else nothing ("had a NULL ..."); the old class's animation particles go */
    const uint8_t *k = oncc;
    if (!k || !tm_link(k + 0x42c) || !tm_link(k + ONCC_ANIMATIONS) || !tm_link(k + 0xc34)) return false;
    chr_anim_particles_stop(c);
    const void *old = c->oncc;
    c->oncc = oncc;
    /* another animation collection: its first animation (0x4d7f90, standing) */
    if (!old || tm_link((const uint8_t *)old + ONCC_ANIMATIONS) != tm_link(k + ONCC_ANIMATIONS)) {
        c->varient &= 0x87ff;
        c->type = c->queued = CHR_TYPE_STAND;
        c->state = CHR_STATE_STANDING;
        const void *t = lookup(c, CHR_TYPE_STAND);
        if (t) chr_set_animation(c, t);
        chr_build_quaternions(c);
    }
    /* the hand's variant from the weapon again, and a size of the class's range */
    chr_hand_varient(c);
    float lo = rdf(k + 0xc58), hi = rdf(k + 0xc5c);
    c->scale = (hi - lo) * (float)trac_random() * 1.5259022e-05f + lo;
    return true;
}

void character_update(Character *c, const CharacterInput *ci, const void *env)
{
    character_update_aim(c, chr_input_bits(ci), ci->turn, ci->pitch, env);
}

void character_update_keys(Character *c, uint64_t held, float turn, const void *env)
{
    character_update_aim(c, held, turn, 0.0f, env);
}

void character_update_aim(Character *c, uint64_t held, float turn, float pitch, const void *env)
{
    c->time++;
    ChrInput in;
    in.turn_x = turn * -64.0f;      /* so HandleTurnInput's mouse turn is turn (before a weapon's aim speed) */
    in.turn_y = pitch * 64.0f;
    in.held = held;
    in.not_held = ~in.held;
    in.down = in.held & ~c->held;
    in.up = c->held & ~in.held;
    film_update(c, &in);            /* DoFilmUpdate 0x4ed420, from the player's input (0x4fa6a0) */
    c->held = in.held;
    if (!(c->flags & 2)) heartbeat_input(c, &in);     /* the dying get no input */
    do_frame(c, env, &in);
}

void character_update_input(Character *c, const ChrInput *in, const void *env)
{
    c->time++;
    ChrInput b = *in;
    film_update(c, &b);             /* DoFilmUpdate 0x4ed420, from the AI's (0x489d40) */
    c->held = b.held;
    if (!(c->flags & 2)) heartbeat_input(c, &b);
    do_frame(c, env, &b);
}

/* A dead AI's frame: AI2rUpdateAI 0x489d40 passes it by (no executor, no DoFilmUpdate), so its input
   is the last tick's, which the dead's heartbeat clears. */
void character_update_dead(Character *c, const void *env)
{
    c->time++;
    ChrInput b;
    memset(&b, 0, sizeof b);
    b.held = c->held;
    b.not_held = ~b.held;
    if (!(c->flags & 2)) heartbeat_input(c, &b);
    do_frame(c, env, &b);
}

/* ---- the environment animations (chr_envanim) ---- */

enum { ENVANIM_NO_ROTATION = 0x02 };    /* P+0x134 */

bool chr_envanim_playing(const Character *c) { return c->envanim.oban != NULL; }

void chr_envanim_start(Character *c, const void *oban, bool no_rotation)
{
    oban_player_init(&c->envanim, oban);
    if (!oban) return;      /* the stop: everything cleared */
    c->envanim.frame = 1;
    c->envanim.step = 1;
    if (no_rotation) c->envanim.state = ENVANIM_NO_ROTATION;
    oban_start(&c->envanim, 0);     /* OBrAnim_Start 0x439580 */
    c->envanim.frame++;
    /* (0x4ed080: the context's flags 0x44 while animated, no gravity nor walls: physics_update) */
}

bool chr_mini_me;

/* 0x4e5a30's size: the player's 0.5 (0x54e170) with chr_mini_me (0x5ecb99), else the character's own */
static float body_scale(const Character *c) { return !c->ai && chr_mini_me ? 0.5f : c->scale; }

static void scale_axes(float m[12], float s)
{
    if (s != 1.0f)
        for (int k = 0; k < 9; k++) m[k] *= s;
}

void chr_root_matrix(const Character *c, float out[12])
{
    /* 0x4e5a30: the place, the turn (or the object animation's matrix), then the size (MUrMatrix_Scale
       0x434b90, unless 1), then up by the pelvis's height: the size shrinks the body toward the feet */
    float s = body_scale(c);
    if (c->envanim.oban) {
        bool no_rot = c->envanim.state & ENVANIM_NO_ROTATION;
        float m[12];
        if (oban_matrix(c->envanim.oban, c->envanim.frame, no_rot, m)) {
            if (no_rot) {
                memcpy(out, m, sizeof m);
                scale_axes(out, s);
                return;
            }
            /* 0x434c30, 0x434cf0 (pi/2 about x), 0x434d70 and 0x434df0 (0 about y and z), behind the
               animation's (0x434b80) */
            float cx = cosf(1.5707964f), sx = sinf(1.5707964f);
            const float r[9] = { 1, 0, 0, 0, cx, sx, 0, -sx, cx };
            for (int col = 0; col < 3; col++)
                for (int k = 0; k < 3; k++)
                    out[3 * col + k] = m[k] * r[3 * col] + m[3 + k] * r[3 * col + 1] + m[6 + k] * r[3 * col + 2];
            memcpy(out + 9, m + 9, 12);
            scale_axes(out, s);
            return;
        }
    }
    float co = cosf(c->facing), si = sinf(c->facing);
    const float r[12] = { co, 0, -si, 0, 1, 0, si, 0, co, c->pos[0], c->pos[1] + c->pelvis_height * s, c->pos[2] };
    memcpy(out, r, sizeof r);
    scale_axes(out, s);
}

bool chr_big_head;

bool chr_pose_matrices(const Character *c, float (*m)[12])
{
    const void *body = body_of_class(c->oncc, 4);
    if (!body || body_bones(body) != CHR_BONES) return false;
    BodyMatrix root;
    chr_root_matrix(c, root);
    if (!body_matrices(body, root, c->aimed, m)) return false;
    /* the end of 0x4e5a30: with chr_big_head (0x5ecb98) the head's matrix (part 10, A+0x2458) times a
       scale of 4 (0x54e16c, MUrMatrix_Scale 0x434b90): its axes grown, its origin kept */
    if (chr_big_head)
        for (int k = 0; k < 9; k++) m[10][k] *= 4.0f;
    return true;
}

void chr_location(const Character *c, float out[3])
{
    /* at creation, or put somewhere (0x4e9210), until the next frame: the position, y + 0.5 */
    if (c->placed || (c->bone_world[0][9] == 0.0f && c->bone_world[0][10] == 0.0f && c->bone_world[0][11] == 0.0f)) {
        out[0] = c->pos[0], out[1] = c->pos[1] + 0.5f, out[2] = c->pos[2];
        return;
    }
    /* the end of ONrGameState_DoCharacterFrame */
    out[0] = c->bone_world[0][9];
    out[2] = c->bone_world[0][11];
    float y = c->bottom;
    if (c->no_ground_ticks == 0 && c->air_ticks == 0 && !(c->flags & 0x20000) && y < c->ground_point[1])
        y = c->ground_point[1];
    out[1] = y + 0.5f;
}

int chr_room(const Character *c, float loc[3])
{
    chr_location(c, loc);
    return c->room >= 0 ? c->room : graph_room_at(loc);
}
