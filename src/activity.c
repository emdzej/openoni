/* The characters' activity: see activity.h. */
#include "activity.h"

#include "ai.h"
#include "akev_collide.h"
#include "chr_anim.h"
#include "exe.h"
#include "movement.h"
#include "physics.h"
#include "weapon.h"

#include <string.h>

bool chr_all_active, chr_disable_visactive;
static const void *level_env;

void activity_set_environment(const void *env) { level_env = env; }

static float rdf(const void *p) { float f; memcpy(&f, p, 4); return f; }

/* The record's start (0x4db830, its memset and the values it sets): the animation's state, the pose
   at rest, no overlays, throws, falls, hits, film nor physics; the levels of detail none yet; the
   invisible shown faded in. */
static void reset_active(Character *c)
{
    c->sound_heard = 0;
    c->tick_type = 0;
    c->lod_base = 0;
    c->lod_shown = c->lod_wanted = 6;       /* A+0x221c, 0x2220 */
    c->lod_frames = 0;
    c->interpolating = false;
    c->interp_height = 0.0f;
    c->interp_state = 0;
    memset(c->interp_velocity, 0, sizeof c->interp_velocity);
    c->interp_tick = c->interp_length = 0;
    c->frame = 0;
    c->hard_pause = c->soft_pause = c->next_interpolation = 0;
    c->hold = 0;
    c->block_pause = c->stun_pause = c->countdown_1c98 = c->countdown_1c9a = 0;
    c->last_particle = 0;
    c->varient = 0;                         /* A+0x1ca0 */
    c->fight_timer = 0;
    c->combo_type = 0;                      /* A+0x1eb0 */
    c->combo_time = 0;
    c->throw_target = NULL;
    c->throw_victim_anim = NULL;
    c->threw = c->thrown_by = NULL;         /* A+0x1ed8, 0x1eda: 0xffff */
    for (int b = 0; b < CHR_BONES; b++) {   /* the quaternions at rest (0x529898..) */
        float *q = c->quats + 4 * b, *r = c->aimed + 4 * b;
        q[0] = q[1] = q[2] = 0.0f, q[3] = 1.0f;
        r[0] = r[1] = r[2] = 0.0f, r[3] = 1.0f;
    }
    memset(c->overlay, 0, sizeof c->overlay);
    c->shield_on = c->shield_super = false;
    memset(c->shield_alpha, 0, sizeof c->shield_alpha);
    c->invisible_shown = c->invisibility != 0;      /* C+0x1ba: A+0x2815, A+0x2818 at 1 */
    c->visibility = c->invisibility ? 1.0f : 0.0f;
    c->fire_down = c->fire2_down = c->fire_again = c->fire_empty = c->stop_fire = c->stop_fire2 = false;
    c->reload_key = false;
    c->aim_lr = c->aim_ud = c->aim_lr_add = c->aim_ud_add = 0.0f;
    c->aim_screen = c->aim_screen_prev = NULL;
    c->aim_screen_time = 0;
    c->turning_left = c->turning_right = false;
    c->held = 0;
    c->film_flags = c->film_frame = c->film_interp = 0;
    c->film = NULL;
    memset(c->film_delta, 0, sizeof c->film_delta);
    c->no_ground_ticks = c->jumped = c->air_ticks = 0;
    c->fall_height = 0.0f;
    c->settled_ticks = c->dying_ticks = 0;
    memset(c->air_velocity, 0, sizeof c->air_velocity);
    c->teleport = false;
    c->anim_particles = 0;
    c->anim_particles_on = 0;
    memset(c->anim_particle, 0, sizeof c->anim_particle);
    c->super_made = c->super_started = false;
    c->super_count = 0;
    memset(c->super_particle, 0, sizeof c->super_particle);
    memset(c->bone_world, 0, sizeof c->bone_world);
    memset(c->weapon_at, 0, sizeof c->weapon_at);
    memset(c->jump_velocity, 0, sizeof c->jump_velocity);
    c->jump_given = false;
    c->held_from_set = false;
    c->hold_ticks = 0;
    memset(c->held_from, 0, sizeof c->held_from);
    c->held_by = NULL;
    c->hold_damage = 0;
    c->start_falling = c->landing = c->hard_landing = false;
    memset(c->knockback, 0, sizeof c->knockback);
    memset(c->hit_velocity, 0, sizeof c->hit_velocity);
    c->knockdown_type = 0;                  /* A+0x1b06 */
    memset(c->hit_list, 0, sizeof c->hit_list);
    memset(c->spheres, 0, sizeof c->spheres);
    memset(c->move, 0, sizeof c->move);
    c->drop = 0.0f;
    c->ground_quad = -1;
    c->ground_height = c->pos[1] + 0.5f;    /* A+0x12c: the location's height (C+0x164, the stub's place) */
    memcpy(c->ground_point, c->pos, sizeof c->ground_point);   /* A+0xe4 */
    c->wall_hits = c->obj_hits = c->pushes = 0;
    c->blocked_by_char = false;
    c->phys_pending = false;
    c->has_physics = false;
    memset(&c->envanim, 0, sizeof c->envanim);
    c->sprint_time = 0;
}

/* ONrCharacter_FirstAnimation 0x4d7f90: the record's first animation. Made from an AI's stub (the
   character AI-moved), the move its mode has under way (creeping, walking, running) while its line is
   on, else standing (crouched, creeping); the weapon hidden by the mode (4 and 6 without aiming,
   0x4ecec0), a panicking one's variant 0x8000. Otherwise standing. Then the variant of the hand's weapon. */
static void first_animation(Chr *ch, bool from_stub)
{
    Character *c = &ch->c;
    uint16_t type = 6, state = 7;
    if (from_stub && (c->flags & CHR_FLAG_AI_MOVEMENT)) {
        int mode = ch->ai.movement_mode ? ch->ai.movement_mode : ai_default_mode(&ch->ai, ch->player);
        if (!ch->ai.stub.on) {
            state = mode == 3 ? 0x13 : 7;
        } else if (mode == 3) {
            type = 0x3a, state = 0x39;
        } else if (mode == 4 || mode == 5) {
            type = 2, state = 5;
        } else if (mode == 6 || mode == 7) {
            type = 3, state = 2;
        }
        if (mode >= 3 && mode <= 7) chr_hide_weapon(c, mode == 4 || mode == 6);
        if (ch->ai.state == AI_STATE_PANIC) c->varient |= 0x8000;
        else c->varient &= 0x7fff;
    }
    c->varient &= 0x87ff;
    chr_hand_varient(c);
    c->type = c->queued = type;
    c->state = state;
    const void *t = chr_lookup(c, type, state);
    if (!t) t = chr_lookup(c, CHR_TYPE_STAND, CHR_STATE_STANDING);
    if (t) chr_set_animation(c, t);
}

/* ONrGameState_NewActiveCharacter 0x4db830 */
static void activate(Character *c, bool from_stub)
{
    Chr *ch = (Chr *)c;
    if (!c->inactive || (c->flags & 1)) return;
    c->inactive = false;
    c->active_ticks = 0;
    reset_active(c);
    first_animation(ch, from_stub);
    chr_build_quaternions(c);
    chr_bone_matrices(c);
    physics_start(c);       /* 0x4e5ff0, 0x4dc160, ONrCharacter_EnablePhysics 0x4dc2b0 */
    /* off the ground (FindGroundCollision 0x4ec040), the floor under it: a ray from 12 above the feet,
       12 and the class's first gravity long */
    const void *env = level_env;
    if (env && !physics_find_ground(c, env)) {
        const float from[3] = { c->pos[0], c->pos[1] + 12.0f, c->pos[2] };
        const float v[3] = { 0.0f, -rdf(c->oncc) - 12.0f, 0.0f };
        static AkevHit hit[AKEV_HITS_MAX];
        if (akev_collide_point(env, from, v, 0x148800, 1, hit)) {
            physics_place(c, hit[0].point);
            chr_bone_matrices(c);
            physics_body(c);
        }
    }
    /* an AI-moved one: the movement state (AI2rMovementState_Initialize 0x4a8d60) on the stub's line, or
       off any obstacle without one; the executor's (AI2rExecutor_Initialize 0x498490) */
    if (c->flags & CHR_FLAG_AI_MOVEMENT) {
        AiState *a = &ch->ai;
        movement_init(&a->move);
        movement_activated(ch, from_stub && a->stub.on);
        a->kind = AI_MOVE_NONE;
        a->facing = c->facing;
        a->turn_once = 0.0f;
        a->key_override = false;
        a->facing_override = 1000.0f;
        a->melee.override = false;
        a->melee.throwing = false;
        a->aim.on = a->aim.stop = a->aim.weapon_speed = false;
        a->aim.done = true;
        a->aim.speed = 1.0f;
        a->aim.last_facing = 1000.0f;
        chr_build_quaternions(c);
        chr_bone_matrices(c);
    }
    memcpy(c->prev_pos, c->pos, sizeof c->prev_pos);
}

static void activate_hook(Character *c, bool from_stub) { activate(c, from_stub); }

void activity_install(void) { chr_activate_hook = activate_hook; }

void activity_new_character(Chr *ch)
{
    Character *c = &ch->c;
    c->inactive = true;
    if (!c->ai) {
        activate(c, false);
        return;
    }
    movement_stub_init(ch, false);
}

void activity_make_inactive(Chr *ch)
{
    Character *c = &ch->c;
    if (c->inactive || !c->ai) return;
    ai_return_to_job_forced(&ch->ai);       /* AI2rReturnToJob 0x48af80 (1, 1) */
    /* 0x4dc580: the stub from the record's path, the particles killed, the physics context gone */
    movement_stub_init(ch, true);
    chr_anim_particles_delete(c);
    c->has_physics = false;
    c->inactive = true;
}

/* 0x48b090: states that keep an AI active: not a calm one (the table 0x52a4d8) but the panic, the
   neutral's, a conversation, a patrol shooting at a flag (its block's +0x5b9) */
static bool keeps_active(const Chr *ch)
{
    const AiState *a = &ch->ai;
    const uint8_t *calm = exe_data(0x52a4d8 + (uint32_t)a->state, 1);
    if (!(calm && calm[0]) && a->state != AI_STATE_PANIC) return true;
    if (a->state == AI_STATE_NEUTRAL) return true;
    if (ch->c.talk_partner) return true;
    return a->state == AI_STATE_PATROL && a->shooting;
}

void activity_update(void)
{
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch) continue;
        Character *c = &ch->c;
        if (chr_all_active && c->inactive) activate(c, true);   /* 0x4dce3f, at the start */
        /* then the active ones (0x4fc9b0's list), unless all are kept active */
        if (chr_all_active || c->inactive) continue;
        if (++c->active_ticks <= 0x59 || !c->ai || (c->film_flags & 1) || ch->corpse_time) continue;
        if (c->air_ticks || keeps_active(ch) || (c->flags & 0x300000) || (c->flags & 1)) continue;
        activity_make_inactive(ch);     /* "%s going inactive" */
    }
}

void activity_seen(Chr *ch)
{
    if (chr_disable_visactive) return;
    chr_make_active(&ch->c);    /* 0x4f11b0 */
    ch->c.active_ticks = 0;
}
