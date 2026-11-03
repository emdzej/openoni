/* Powerups: see powerup.h. */
#include "powerup.h"

#include "exe.h"
#include "m3gm.h"
#include "objc.h"
#include "sl.h"
#include "tm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    ONGS_POWERUP_GEOMETRY = 0x98,   /* 0x80 each: the M3GM's name */
    ONGS_POWERUP_GLOW = 0x418,      /* 0x80 each: the glow's TXMP name */
    ONGS_POWERUP_GLOW_SIZE = 0x798, /* 8 each: two floats */
    TYPE_NAMES = 0x5588f8,          /* the exe's table of the type names */
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
    memset(p, 0, sizeof *p);
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

uint16_t powerup_give(Character *c, int type, uint16_t amount, bool force)
{
    if (!force) amount = takes(c, type, amount);
    if (!amount) return 0;
    switch (type) {
    case POWERUP_AMMO: c->ammo = (uint16_t)(c->ammo + amount); break;
    case POWERUP_CELL: c->cells = (uint16_t)(c->cells + amount); break;
    case POWERUP_HYPO: c->hypos = (uint16_t)(c->hypos + amount); break;
    case POWERUP_SHIELD: c->shield = (uint16_t)(c->shield + amount); break;
    case POWERUP_INVISIBILITY: c->invisibility = (uint16_t)(c->invisibility + amount); break;
    case POWERUP_LSI: c->lsi = true; break;
    default: break;
    }
    /* the player's: the pickup sound (ONrGameState_EventSound_Play) and the HUD's display (0x50f560,
       0x50f580, 0x5050a0): with the HUD */
    return amount;
}

void powerup_matrix(const Powerup *p, float m[12])
{
    float co = cosf(p->facing), si = sinf(p->facing);
    const float r[12] = { co, 0, -si, 0, 1, 0, si, 0, co, p->pos[0], p->pos[1], p->pos[2] };
    memcpy(m, r, sizeof r);
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

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

void powerup_register_commands(void)
{
    sl_register("powerup_spawn", cmd_spawn, NULL);
}
