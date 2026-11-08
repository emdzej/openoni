/* The action key's objects: see action.h. */
#include "action.h"
#include "gametime.h"
#include "weapon.h"

#include "chars.h"
#include "chr_anim.h"
#include "console.h"
#include "door.h"
#include "localpath.h"
#include "neutral.h"
#include "particle.h"
#include "physics.h"
#include "tram.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

enum {
    TYPE_CONSOLE = 0xc4, TYPE_CONSOLE_PUNCH = 0xdd, TYPE_CONSOLE_WALK = 0xc5,
    CONSOLE_TIME = 180,             /* ticks to reach the marker */
};

static void pelvis(const Character *c, float p[3]) { chr_pelvis_position(c, p); }

bool action_can_use(const Character *c, int marker, bool wide)
{
    const ActionMarker *m = console_marker(marker);
    if (!m) return false;
    /* (0x4ed140 first: the character can act, not dying and the like) */
    float p[3];
    pelvis(c, p);
    float dx = p[0] - m->pos[0], dy = p[1] - m->pos[1], dz = p[2] - m->pos[2];
    if (fabsf(dy) > 8.0f) return false;                             /* "pelvis height" */
    float s = sinf(m->facing), co = cosf(m->facing);
    float ahead = -(s * dx + co * dz);
    if (ahead < -2.0f || ahead > 10.0f) return false;               /* "distance" */
    if (fabsf(co * dx - s * dz) > (wide ? 16.0f : 8.0f)) return false; /* "sideways" */
    /* the facing (the player's own, an AI's desired one, C+0x16c) within 70 degrees (105 wide) */
    float f = c->facing - m->facing;
    if (f < -3.1415927f) f += 6.2831855f;
    else if (f >= 3.1415927f) f -= 6.2831855f;
    if (fabsf(f) > (wide ? 1.8325957f : 1.2217305f)) return false; /* "facing" */
    /* within 3 (squared 9, 0x52a5b0): fine; farther, the way to 3 before the marker (0x529808) mustn't be
       blocked, by the environment (AI2rManeuver_CheckBlockage 0x49ea60) nor by another character
       (AI2rManeuver_CheckInterveningCharacters 0x49eb80) */
    if (dx * dx + dy * dy + dz * dz > 9.0f) {
        float q[3] = { m->pos[0] - s * 3.0f, m->pos[1], m->pos[2] - co * 3.0f };
        if (local_blockage(p3_environment(), c, q)) return false;   /* "blocked by environment" */
        if (local_intervening(c, q)) return false;                  /* "blocked by character(s)" */
    }
    return true;
}

void action_start(Character *c, int marker)
{
    c->console_marker = marker;
    if (c->weapon) chr_force_holster(c);    /* ONrCharacter_Holster(forced) */
    c->console_deadline = 0;
}

/* The end, aborted or done: the marker forgotten. (WPrInventory_Weapon_Purge_Magic, the player's
   GS+0x1679ae.) */
static void finish(Character *c) { c->console_marker = -1; }

static bool walking_type(uint16_t t)
{
    switch (t) {
    case 2: case 3: case 6: case 7: case 8: case 9: case 10: case 0xb: case 0xd: case 0xe: case 0xf: case 0x18:
    case 0xce: case 0xcf: case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd8: case 0xd9: case 0xda:
        return true;
    default:
        return false;
    }
}

/* ONrCharacter_ConsoleAction_Update 0x4ee220 */
static void console_action(Character *c)
{
    const ActionMarker *m = console_marker(c->console_marker);
    if (!m) {
        finish(c);
        return;
    }
    bool using = c->type == TYPE_CONSOLE || c->type == TYPE_CONSOLE_PUNCH;
    if (c->console_deadline == 0) {
        /* the animation must be a standing or moving one, not in the 0x100 variant */
        if ((c->varient & 0x100) || !walking_type(c->type)) {
            finish(c);      /* "aborting console wait for state" */
            return;
        }
        if (c->state == CHR_STATE_STANDING &&
            (c->type == 6 || c->type == 7 || c->type == 8 || c->type == 0xce || c->type == 0xcf || c->type == 0xd8))
            c->console_deadline = game_time() + CONSOLE_TIME;
    }
    if (using) {
        int last = (int)rd16((const uint8_t *)c->anim + TRAM_FRAMES) - 1;
        int at = (int16_t)rd16((const uint8_t *)c->anim + TRAM_ACTION_FRAME);
        if ((int)c->frame != (at <= last ? at : last)) return;
        const Chr *who = (const Chr *)c;    /* the character is the first field of its slot */
        float loc[3];
        chr_location(c, loc);
        console_on_activate(m->console, who->name, loc);
        finish(c);
        return;
    }
    /* the target: the marker's x and z, the console's own y */
    const float goal[3] = { m->pos[0], m->floor, m->pos[2] };
    float v[3] = { goal[0] - c->pos[0], goal[1] - c->pos[1], goal[2] - c->pos[2] };
    float d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (d < 2.0f) {
        /* there: on the marker, facing the console, standing, then the console animation */
        c->facing = c->desired_facing = m->facing;
        physics_place(c, goal);
        chr_do_animation(c, CHR_ANIM_NOW, CHR_TYPE_STAND);
        uint16_t t = (console_flags(m->console) & CONSOLE_PUNCH) ? TYPE_CONSOLE_PUNCH : TYPE_CONSOLE;
        if (!chr_do_animation(c, CHR_ANIM_NOW, t)) chr_do_animation(c, CHR_ANIM_NOW, TYPE_CONSOLE);
        return;
    }
    if (game_time() > c->console_deadline) {
        finish(c);
        return;
    }
    if (c->type != TYPE_CONSOLE_WALK) {
        chr_make_active(c);         /* 0x4f10f0, or C+0x1640 cleared */
        const void *t = chr_lookup(c, TYPE_CONSOLE_WALK, c->state);
        if (t) chr_set_animation_interpolated(c, c->state, t, 12);
    }
    c->desired_facing = m->facing;
    float step = d < 0.25f ? d : 0.25f, p[3];
    for (int k = 0; k < 3; k++) p[k] = c->pos[k] + step / d * v[k];
    physics_place(c, p);
}

/* HandleStartTauntOrAction 0x4f80d0, before the taunt */
static bool hook(Character *c, const ChrInput *in)
{
    if (c->console_marker >= 0) {
        console_action(c);
        return true;
    }
    if (!(in->down & CHR_KEY_ACTION)) return false;
    /* (a weapon in hand with flag 2 of its class blocks the console; the film's playback, 0x4e4100, and
       C+0x04 flag 0x400000 aren't kept) */
    float p[3];
    pelvis(c, p);
    int marker = console_marker_nearest(p);
    if (marker >= 0 && action_can_use(c, marker, false)) {
        action_start(c, marker);
        return true;
    }
    const Chr *who = (const Chr *)c;
    bool can = false;
    int door = door_try_action(who, p, &can);
    if (door >= 0 && can && door_action_open(door, who)) return true;
    if (neutral_try_interaction((Chr *)who, true)) return true;     /* AI2rTryNeutralInteraction 0x48bb40 */
    if (door >= 0) return true;     /* (the locked door's sound) */
    return false;
}

void action_install(void) { character_action_hook = hook; }
