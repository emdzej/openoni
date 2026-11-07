/* Powerups: see powerup.h. */
#include "powerup.h"
#include "chars.h"

#include "exe.h"
#include "impact.h"
#include "m3gm.h"
#include "objc.h"
#include "persist.h"
#include "sl.h"
#include "sound.h"
#include "tm.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    ONGS_POWERUP_GEOMETRY = 0x98,   /* 0x80 each: the M3GM's name */
    ONGS_POWERUP_GLOW = 0x418,      /* 0x80 each: the glow's TXMP name */
    ONGS_POWERUP_GLOW_SIZE = 0x798, /* 8 each: two floats */
    TYPE_NAMES = 0x5588f8,          /* the exe's table of the type names */
    OBJECT_TYPES = 0x54bed0,        /* the PowerUp objects' types: 6 of (type, tag, name), 12 bytes each */
};

static Powerup powerups[POWERUPS_MAX];      /* 0x620460 */
static uint16_t count;                      /* 0x622160 */
static uint32_t now;                        /* stand-in for the game time (GS+0x144) */

static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

void powerups_reset(void)
{
    memset(powerups, 0, sizeof powerups);
    count = 0;
}

/* 0x51b080 for each PowerUp object; OBJiPowerUp_Read 0x4c9f40 compares its first four bytes (as a big
   end number) with each type's tag, the type left as it was (SetDefaults: 0) without a match. */
static bool place_object(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->size < 4) return true;
    uint32_t tag = (uint32_t)o->data[0] << 24 | (uint32_t)o->data[1] << 16 | (uint32_t)o->data[2] << 8 | o->data[3];
    int type = 0;
    for (int k = 0; k < 6; k++)
        if (exe_u32(OBJECT_TYPES + 12 * k + 4) == tag) type = (int)exe_u32(OBJECT_TYPES + 12 * k);
    powerup_place(type, powerup_default_amount(type), o->pos, o->rot[1] * 0.017453293f);   /* 0x529550 */
    return true;
}

void powerups_level_begin(void) { objc_each("PowerUp", place_object, NULL); }

int powerup_count(void) { return count; }
Powerup *powerup_get(int i) { return i >= 0 && i < count ? &powerups[i] : NULL; }

const char *powerup_type_name(int type)
{
    if (type < 0 || type >= POWERUP_TYPES) return NULL;
    uint32_t va = exe_u32(TYPE_NAMES + 4u * (uint32_t)type);
    return va ? exe_str(va) : NULL;
}

uint16_t powerup_default_amount(int type)
{
    return type == POWERUP_SHIELD ? 100 : type == POWERUP_INVISIBILITY ? 1800 : 1;
}

/* WPrPowerup_SetType 0x51cc10 */
static bool set_type(Powerup *p, int type)
{
    p->type = type;
    p->geometry = p->glow = NULL;
    p->glow_size[0] = p->glow_size[1] = 5.0f;
    if (type < 0 || type >= POWERUP_TYPES) return false;    /* "WPrPowerup_SetType: unknown powerup type" */
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!gs) return true;
    p->glow_size[0] = rdf(gs + ONGS_POWERUP_GLOW_SIZE + 8 * type);
    p->glow_size[1] = rdf(gs + ONGS_POWERUP_GLOW_SIZE + 8 * type + 4);
    char name[129];
    memcpy(name, gs + ONGS_POWERUP_GEOMETRY + 0x80 * type, 128), name[128] = 0;
    p->geometry = tm_instance_get(TM_TAG("M3GM"), name);
    memcpy(name, gs + ONGS_POWERUP_GLOW + 0x80 * type, 128), name[128] = 0;
    p->glow = tm_instance_get(TM_TAG("TXMP"), name);
    return true;
}

Powerup *powerup_new(int type, uint16_t amount, const float pos[3], float facing)
{
    if (amount == 0) return NULL;
    Powerup *p;
    if (count < POWERUPS_MAX) p = &powerups[count];
    else {
        /* the oldest that isn't placed (its physics context goes with it) */
        p = NULL;
        uint32_t oldest = 0xffffffffu;
        for (int i = 0; i < count; i++)
            if (!(powerups[i].flags & 8) && powerups[i].time < oldest) oldest = powerups[i].time, p = &powerups[i];
        if (!p) return NULL;
    }
    memset(p, 0, sizeof *p);        /* (a recycled one's context removed, 0x43aef0) */
    if (!set_type(p, type)) return NULL;
    p->amount = amount;
    memcpy(p->pos, pos, sizeof p->pos);
    p->facing = facing;
    p->time = now;
    if (count < POWERUPS_MAX && p == &powerups[count]) count++;
    return p;
}

Powerup *powerup_place(int type, uint16_t amount, const float pos[3], float facing)
{
    Powerup *p = powerup_new(type, amount, pos, facing);
    if (!p) return NULL;
    p->flags |= 8;
    float box[6], sphere[4];
    if (p->geometry && m3gm_bounds(p->geometry, box, sphere)) p->pos[1] -= box[1];
    return p;
}

void powerup_delete(Powerup *p)
{
    int i = (int)(p - powerups);
    if (i < 0 || i >= count) return;
    count--;
    powerups[i] = powerups[count];
}

/* What of the powerup c can take (0x51b3e0's and 0x51ca30's rules). */
static uint16_t takes(const Character *c, int type, uint16_t amount)
{
    uint16_t have;
    switch (type) {
    case POWERUP_AMMO: have = c->ammo; break;
    case POWERUP_CELL: have = c->cells; break;
    case POWERUP_HYPO: have = c->hypos; break;
    case POWERUP_SHIELD: return c->shield < amount ? (uint16_t)(amount - c->shield) : 0;
    case POWERUP_INVISIBILITY: return c->invisibility < amount ? (uint16_t)(amount - c->invisibility) : 0;
    case POWERUP_LSI: return !c->ai;   /* the player (GS+0xac) */
    default: return 0;
    }
    if (have + amount > 5) return have < 6 ? (uint16_t)(6 - have) : 0;
    return amount;
}

Powerup *powerup_find(const Character *c, float y_lo, float y_hi, const Powerup *only, float r2, bool *full)
{
    Powerup *best = NULL;
    bool saw_full = false;
    float nearest = r2;
    for (int i = 0; i < count; i++) {
        Powerup *p = &powerups[i];
        if (only && only != p) continue;
        if (p->pos[1] < y_lo || y_hi < p->pos[1]) continue;
        float dx = c->pos[0] - p->pos[0], dz = c->pos[2] - p->pos[2], d = dz * dz + dx * dx;
        if (r2 <= d) continue;
        if (!takes(c, p->type, p->amount)) saw_full = true;
        else if (d < nearest) best = p, nearest = d;
    }
    if (full) *full = best ? false : saw_full;
    return best;
}

void (*powerup_new_hook)(int type);

uint16_t powerup_give(Character *c, int type, uint16_t amount, bool force, bool sound)
{
    if (!force) amount = takes(c, type, amount);
    if (!amount) return 0;
    const Chr *pl = chars_player();
    bool player = pl && c == &pl->c;
    sound = sound && player;
    /* the receiving sounds (ONrGameState_EventSound_Play): 7 ammo, 8 a cell, 9 a hypo, 10 the LSI */
    switch (type) {
    case POWERUP_AMMO: c->ammo = (uint16_t)(c->ammo + amount); if (sound) sound_event_play(7); break;
    case POWERUP_CELL: c->cells = (uint16_t)(c->cells + amount); if (sound) sound_event_play(8); break;
    case POWERUP_HYPO: c->hypos = (uint16_t)(c->hypos + amount); if (sound) sound_event_play(9); break;
    case POWERUP_SHIELD: c->shield = (uint16_t)(c->shield + amount); break;
    case POWERUP_INVISIBILITY: c->invisibility = (uint16_t)(c->invisibility + amount); break;
    case POWERUP_LSI: c->lsi = true; if (sound) sound_event_play(10); break;
    default: break;
    }
    /* the player's: a type not had before (the LSI always) is the pause screen's new item (0x50f560,
       0x50f580, 0x5050a0) */
    if (player) {
        if (type != POWERUP_LSI) {
            if (persist_item_seen(type)) return amount;
            persist_set_item_seen(type);
        }
        if (powerup_new_hook) powerup_new_hook(type);
    }
    return amount;
}

bool powerup_glow(const Powerup *p, float pos[3])
{
    if (!p->glow) return false;
    memcpy(pos, p->pos, 3 * sizeof(float));
    float box[6], sphere[4];
    if (p->geometry && m3gm_bounds(p->geometry, box, sphere)) pos[1] += box[1] + 0.1f;    /* 0x529cc4 */
    return true;
}

void powerup_matrix(const Powerup *p, float m[12])
{
    float co = cosf(p->facing), si = sinf(p->facing);
    const float r[12] = { co, 0, -si, 0, 1, 0, si, 0, co, p->pos[0], p->pos[1], p->pos[2] };
    memcpy(m, r, sizeof r);
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

/* AUrDeviateXZ 0x4380e0: x and z each moved by up to scale, either way. */
static void deviate_xz(float v[3], float scale)
{
    const float r = 1.0f / 65535.0f;    /* 0x529d74 */
    float dx = (float)trac_random() * r * scale, dz = (float)trac_random() * r * scale;
    if ((uint16_t)(trac_random() * 100u >> 16) < 50) dx = -dx;
    if ((uint16_t)(trac_random() * 100u >> 16) < 50) dz = -dz;
    v[0] += dx, v[2] += dz;
}

static const void *update_env;

/* 0x51c0a0, after the collisions (of the walls only): the first wall met faster than 0.35 into it plays the fall's impact;
   a floor (a normal over 0.5 up) marks it 6 (on a floor), else the mark 4 goes. */
static void post_collision(PhysObject *o, const PhysHit *hits, unsigned n)
{
    Powerup *p = o->owner;
    bool played = false;
    p->flags &= 0xfffb;
    for (unsigned i = 0; i < n; i++) {
        if (hits[i].kind != 0) continue;    /* the walls' only */
        const float *nm = hits[i].plane;
        if (!played && fabsf(nm[1] * o->vel[1] + nm[0] * o->vel[0] + o->vel[2] * nm[2]) > 0.35f) {   /* 0x52adb8 */
            static int type = -2;   /* the "Powerup" impact type (0x627dc0, WPrInitialize 0x51b0f0) */
            if (type == -2) type = impact_type_by_name(exe_str(0x54e6fc));
            weapon_fall_impact(type, &hits[i], update_env);
            played = true;
        }
        if (nm[1] > 0.5f) p->flags |= 6;    /* 0x529560 */
    }
}

/* 0x51c080: the characters left out of the search. */
static bool skip_other(const PhysCtx *self, const PhysCtx *o)
{
    (void)self;
    return o->kind == PHYS_KIND_CHARACTER;
}

/* 0x51c140: pushed by anything but a character. */
static void receive_force(const PhysCtx *self, const PhysCtx *from, const float force[3])
{
    if (from->kind == PHYS_KIND_CHARACTER) return;
    Powerup *p = self->owner;
    physobj_push(&p->phys, force);
}

static int ctx_count(void) { return count; }
static bool ctx_get(int i, PhysCtx *out)
{
    if (!powerups[i].has_phys) return false;
    powerups[i].phys.owner = &powerups[i];
    physobj_ctx(&powerups[i].phys, out);
    return true;
}

/* WPrPowerup_Drop 0x51bd80: a new powerup (not placed) at c's pelvis (the location, C+0x160, raised by
   C+0x18c), moved up to 1 in x and z, 10 ahead if asked (0x52a190), facing anywhere; its physics (type
   2, flags 0x5000, the characters not met (0x51c080); a sphere of the geometry's depth below its
   origin, at least 1, or 2 without one) starting still, moved up to 0.1 in x and z; dropped where it
   stands, a part of c's last hit (A+0xd4, 0.055) and 1.5 up. */
void powerup_drop(const Character *c, int type, uint16_t amount, bool ahead)
{
    float p[3];
    chr_location(c, p);
    p[1] += c->pelvis_height;
    deviate_xz(p, 1.0f);
    if (ahead) p[0] += sinf(c->facing) * 10.0f, p[2] += cosf(c->facing) * 10.0f;
    float facing = (float)trac_random() * 6.2831855f * (1.0f / 65535.0f);   /* 0x529548, 0x5294fc */
    Powerup *u = powerup_new(type, amount, p, facing);
    if (!u) return;
    PhysObject *o = &u->phys;
    physobj_init(o);
    o->flags |= 0x5000u;
    o->owner = u;
    o->kind = PHYS_KIND_POWERUP;    /* the callbacks 0x620100 */
    o->post_collision = post_collision;
    o->skip = skip_other;
    o->receive_force = receive_force;
    o->friction_sub = 0.0025f;      /* 0x3b23d70a */
    o->friction_mul = 0.75f;
    float r = 2.0f, box[6], sphere[4];
    if (u->geometry && m3gm_bounds(u->geometry, box, sphere)) {
        r = fabsf(box[1]);
        if (r <= 1.0f) r = 1.0f;    /* 0x529508 */
    }
    memcpy(o->pos, u->pos, sizeof o->pos);
    memcpy(o->sphere, u->pos, sizeof u->pos);
    o->sphere[3] = r;
    physobj_matrix(o);
    u->has_phys = true;
    u->flags |= 1;
    deviate_xz(o->vel, 0.1f);       /* 0x3ccccccd */
    if (!ahead) {
        for (int k = 0; k < 3; k++) o->vel[k] += c->hit_velocity[k] * 0.055f;  /* 0x52b8dc */
        o->vel[1] += 1.5f;          /* 0x52a774 */
    }
}

void powerups_update(void)
{
    for (int i = 0; i < count; i++) {
        Powerup *p = &powerups[i];
        if (!p->has_phys) continue;
        memcpy(p->phys.sphere, p->phys.pos, sizeof p->phys.pos);
        memcpy(p->pos, p->phys.sphere, sizeof p->pos);
        if (p->phys.vel[1] > 2.25f) p->phys.vel[1] = 2.25f;     /* 0x52b8d4 */
        const float *v = p->phys.vel;
        if ((p->flags & 4) || v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 0.0069444445f) {     /* 0x52b8d0 */
            if (++p->still >= 120) {
                p->has_phys = false;    /* (0x43aef0) */
                p->flags &= 0xfffe;
            }
        } else
            p->still = 0;
        /* the fading (+0x24, +0x26) of one at rest: nothing sets its time */
    }
}

void powerups_physics(const void *env)
{
    update_env = env;
    for (int i = 0; i < count; i++)
        if (powerups[i].has_phys) {
            powerups[i].phys.owner = &powerups[i];  /* (the slot may have moved: powerup_delete) */
            physobj_update(&powerups[i].phys, env);
        }
}

/* powerup_spawn type flag [amount] (AI2iScript_PowerupSpawn 0x4b85e0): a placed powerup at the flag. */
static bool cmd_spawn(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[0].type != SL_STRING) return true;
    int type = -1;
    for (int k = 0; k < POWERUP_TYPES; k++) {
        const char *n = powerup_type_name(k);
        if (n && !strcmp(n, v[0].s)) { type = k; break; }
    }
    if (type < 0) return true;      /* "powerup_spawn: unknown powerup type %s" and the list */
    ObjcObject flag;
    if (!objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;    /* "could not find flag %d" */
    uint16_t amount = c < 3 ? powerup_default_amount(type) : (uint16_t)num(&v[2]);
    powerup_place(type, amount, flag.pos, flag.rot[1] * 0.017453293f);  /* "could not create powerup" */
    return true;
}

/* give_powerup type [amount [player]] (0x51c8f0): to every living character of the player number (the
   player's own by default; -1 every one), 0x51ca30 taking it all. */
static bool cmd_give(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    int type = -1;
    for (int k = 0; k < POWERUP_TYPES; k++) {
        const char *n = powerup_type_name(k);
        if (n && !strcmp(n, v[0].s)) { type = k; break; }
    }
    if (type < 0) return true;      /* "unknown powerup type" and the list */
    uint16_t amount = c < 2 ? powerup_default_amount(type) : (uint16_t)num(&v[1]);
    int32_t who = -1;               /* 0x4fc8b0: the player's index (C+0) */
    for (int i = 0; i < chars_slots() && c < 3; i++)
        if (chars_get(i) && chars_get(i)->player) who = i;
    if (c >= 3) who = num(&v[2]);
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch || (ch->c.flags & 1)) continue;
        if (who >= 0 && who != i) continue;
        powerup_give(&ch->c, type, amount, true, false);
    }
    return true;
}

void powerup_register_commands(void)
{
    sl_register("powerup_spawn", cmd_spawn, NULL);
    physctx_add_source((PhysSource){ ctx_count, ctx_get });
    sl_register("give_powerup", cmd_give, NULL);
}
