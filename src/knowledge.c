/* The AI's knowledge: see knowledge.h. */
#include "knowledge.h"
#include "panic.h"
#include "combat.h"

#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "exe.h"
#include "graph.h"
#include "movement.h"
#include "neutral.h"
#include "object.h"
#include "particle.h"
#include "pursuit.h"
#include "trac.h"
#include "weapon.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

enum { ENTRIES_MAX = 128, PENDING_MAX = 64 };

static const float TWO_PI = 6.2831854820251465f, PI = 3.1415927410125732f;

static KnowEntry entries[ENTRIES_MAX];     /* DAT_005eb850's pool */
static int entries_used;                   /* DAT_0063ada0 */
static KnowEntry *free_list;               /* DAT_005eb854 */

typedef struct {
    Chr *owner;
    Chr *target;
    int strength;
    float pos[3];
    int type;
    uint32_t value;
    Chr *other;                 /* a sound's other character (the value, for the sounds) */
    bool silent, force_hostile;
} Pending;
static Pending pending[PENDING_MAX];       /* DAT_0063a2a0, 0x24 bytes each */
static int npending;                       /* DAT_0063b340 */
static const void *sound_env;              /* the AKEV knowledge_update was last given */

static uint32_t now;                       /* stand-in for the game time (GS+0x144) */

uint32_t knowledge_now(void) { return now; }

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }
static float rdf32(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static uint32_t oncc_u32(const Chr *c, unsigned off)
{
    const uint8_t *p = (const uint8_t *)c->c.oncc + off;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float oncc_float(const Chr *c, unsigned off)
{
    uint32_t v = oncc_u32(c, off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static bool alive(const Chr *c) { return c && !(c->c.flags & 1); }
static bool is_ai(const Chr *c) { return c && !c->player; }     /* C+0x1e8 2 */

/* ai2_boss_battle (0x5ec0c3, false at the level's start, 0x48a250): the contacts' order goes by the
   boss flag (C+4 0x10000000) too. */
bool knowledge_boss_battle;

/* The dangers: the dangerous projectiles' records and the guns' firing spreads (below). */
enum { PROJECTILES_MAX = 64, SPREADS_MAX = 32, DANGER_IN_USE = 1, DANGER_GONE = 2 };
typedef struct {
    uint32_t flags;             /* +0x00: DANGER_IN_USE, DANGER_GONE (its particle deleted) */
    float start[3];             /* +0x04: where it was made */
    float prev[3], pos[3];      /* +0x10, +0x1c: its last two places (every 5 ticks) */
    float vel[3];               /* +0x28: its velocity variable's (none: 0) */
    float dodge_radius;         /* +0x34: the class's +0x90 */
    float alert_radius;         /* +0x38: the class's +0x94 */
    uint32_t handle;            /* +0x3c: the particle's when made */
    P3Class *cls;               /* +0x40 */
    P3Particle *p;              /* +0x44 */
    uint32_t owner;             /* +0x48: its owner variable's (none: 0) */
} Projectile;
typedef struct {
    uint32_t flags;             /* +0x00: DANGER_IN_USE */
    float pos[3], dir[3];       /* +0x04, +0x10: the gun's place and the way it fires */
    float length, radius, angle;    /* +0x1c..+0x24: the class's +0x144..+0x14c */
    const Weapon *w;            /* +0x28 */
} Spread;
static Projectile projectiles[PROJECTILES_MAX];     /* 0x638fa0 */
static unsigned nprojectiles;                       /* 0x63aba8: the last one's index + 1 */
static Spread spreads[SPREADS_MAX];                 /* 0x63adc0 */
static unsigned nspreads;                           /* 0x63aba4 */

void knowledge_reset(void)
{
    knowledge_boss_battle = false;
    memset(entries, 0, sizeof entries);
    entries_used = 0;
    free_list = NULL;
    npending = 0;
    now = 0;
    memset(projectiles, 0, sizeof projectiles);
    nprojectiles = 0;
    memset(spreads, 0, sizeof spreads);
    nspreads = 0;
}

bool ai_last_man_standing;

int ai_team_relation(uint32_t a, uint32_t b)
{
    if (ai_last_man_standing) return 2;
    if (a == b || a == 6 || b == 6) return 0;       /* 6: Switzerland */
    switch (a) {
    case 0: return b == 1 ? 0 : b == 2 ? 2 : 1;     /* Konoko */
    case 1: if (b == 0) return 0; /* fall through: TCTF */
    case 4: return b == 2 || b == 5 ? 2 : 1;        /* security guards */
    case 2: return b == 7 ? 0 : 2;                  /* the Syndicate */
    case 5: return b == 2 || b == 1 ? 2 : 1;        /* rogue Konoko */
    case 7: return b != 2;                          /* Syndicate accessories */
    default: return 1;                              /* neutrals */
    }
}

KnowEntry *knowledge_find(const Chr *owner, const Chr *target)
{
    KnowEntry *e = owner->ai.know;
    while (e && e->target != target) e = e->next;
    return e;
}

bool ai_is_hostile(const Chr *a, const Chr *b, bool known)
{
    if (!a || !b || a == b || (a->ai.flags & AI_FLAG_PASSIVE)) return false;
    int r = ai_team_relation(a->team, b->team);
    if (r == 0) return false;
    if (r == 2) return true;
    if (is_ai(a)) {
        if (a->ai.flags & AI_FLAG_NON_AGGRESSIVE) return false;
        if (!known) return false;
        const KnowEntry *e = knowledge_find(a, b);
        if (e && (e->hostile || e->hostility > 1)) return true;
    }
    if (known && is_ai(b)) {
        const KnowEntry *e = knowledge_find(b, a);
        if (e && (e->hostile || e->hostility > 1)) return true;
    }
    return false;
}

/* The time a strength lasts (ONCC +0x410 for 4, +0x414 for 3, +0x418 for 2). */
static uint32_t degrade_time(const Chr *owner, int strength)
{
    switch (strength) {
    case 2: return oncc_u32(owner, 0x418);
    case 3: return oncc_u32(owner, 0x414);
    case 4: return oncc_u32(owner, 0x410);
    default: return 0;
    }
}

/* 0x49ba10: which of two contacts matters more to owner: < 0 a, > 0 b, 0 neither (with exact; else
   the highest strengths decide). In a boss battle the contact whose character has the owner's boss
   flag (C+4 0x10000000: the player and the bosses) goes first, a weaker one counting from strength 2. */
static int compare(const KnowEntry *a, const KnowEntry *b, bool exact, const Chr *owner)
{
    int side = 0;
    if (knowledge_boss_battle && owner && a->target && b->target) {
        bool fo = (owner->c.flags & CHR_FLAG_BOSS) != 0, fa = (a->target->c.flags & CHR_FLAG_BOSS) != 0,
             fb = (b->target->c.flags & CHR_FLAG_BOSS) != 0;
        if (fo == fa) {
            if (fo != fb) side = -1;
        } else if (fo == fb) {
            side = 1;
        }
    }
    int weak = side ? 1 : 2;
    if (a->strength <= weak && b->strength > 2) return 1;
    if (b->strength <= weak && a->strength > 2) return -1;
    if (side) return side;
    if (b->hostility < a->hostility) return -1;
    if (a->hostility < b->hostility) return 1;
    if (owner) {
        bool ha = a->target && ai_is_hostile(owner, a->target, true);
        bool hb = b->target && ai_is_hostile(owner, b->target, true);
        if (ha && !hb) return -1;
        if (hb && !ha) return 1;
    }
    if (b->strength < a->strength) return -1;
    if (a->strength < b->strength) return 1;
    if (a->hurt != b->hurt) return a->hurt ? -1 : 1;
    if (a->hurt_melee != b->hurt_melee) return a->hurt_melee ? -1 : 1;
    if (exact) return 0;
    return a->highest > b->highest ? -1 : a->highest < b->highest ? 1 : 0;
}

/* Moves e before its previous one. */
static void move_up(Chr *owner, KnowEntry *e)
{
    KnowEntry *p = e->prev;
    e->prev = p->prev;
    if (!e->prev) owner->ai.know = e;
    else e->prev->next = e;
    p->next = e->next;
    if (p->next) p->next->prev = p;
    e->next = p;
    p->prev = e;
}

/* 0x49b8e0: back to the pool. */
static void remove_entry(KnowEntry *e)
{
    Chr *o = e->owner;
    if (o) {
        o->ai.know_count--;
        if (o->ai.know == e) o->ai.know = e->next;
        if (o->ai.target_entry == e) o->ai.target_entry = NULL;
    }
    if (e->prev) e->prev->next = e->next;
    if (e->next) e->next->prev = e->prev;
    e->owner = NULL;
    e->prev = NULL;
    e->next = free_list;
    free_list = e;
}

/* AI2rAlert_UpgradeStatus 0x48eee0: the new alert level; above low, the startle (the animations of type
   0xc0..0xc3 by the contact's side) unless the class has none (ONCC+0x120 bit 0); at combat, the combat
   state against the contact's character. Returns whether it startled. The startle turns the character
   toward the contact over its frames (C+0xfac, C+0xfb0: the way left after the animation's own turn,
   TRAM+0x48), ai_update. */
static bool upgrade_alert(Chr *c, int level, KnowEntry *e);
bool knowledge_upgrade_alert(Chr *c, int level) { return upgrade_alert(c, level, NULL); }

/* AI2rAlert_DowngradeStatus 0x48f180: the alert set lower (in combat only if forced); under low the
   time it rose (C+0xf90) is forgotten; the movement hears of it (AI2rMovement_NotifyAlertChange 0x4a8230). */
void knowledge_downgrade_alert(Chr *c, int level, bool force)
{
    AiState *a = &c->ai;
    if ((!force && a->state == AI_STATE_COMBAT) || a->alert == level) return;
    a->alert = level;
    movement_alert_change(c);
    if (level < 2) a->alert_time = 0;
}

static bool upgrade_alert(Chr *c, int level, KnowEntry *e)
{
    AiState *a = &c->ai;
    bool startled = false;
    if (level != a->alert) {
        a->alert = level;
        movement_alert_change(c);   /* 0x4a8230 */
        if (level > 1 && !a->alert_time) a->alert_time = e ? e->posted : now;
        if (!(oncc_u32(c, 0x120) & 1) && a->alert >= 3 && e && e->hostility != 3 && !(!e->raised && e->highest > 3) &&
            (!a->alert_time || e->posted <= a->alert_time + 60)) {
            float to = wrap_2pi(atan2f(e->pos[0] - c->c.pos[0], e->pos[2] - c->c.pos[2]));
            float rel = wrap_2pi(to - c->c.facing + 0.78539819f);
            uint16_t type = rel < 1.5707964f ? 0xc3 : rel < PI ? 0xc0 : rel < 4.712389f ? 0xc2 : 0xc1;
            chr_enter_fight_mode(&c->c);
            const void *anim = chr_lookup(&c->c, type, c->c.from_state);
            if (anim) {
                a->startle_from = c->c.facing;
                a->startle_ticks = tr_duration(anim);
                float turn = (to - c->c.facing) - rdf32((const uint8_t *)anim + 0x48);
                if (turn < -PI) turn += TWO_PI;
                else if (turn > PI) turn -= TWO_PI;
                a->startle_turn = turn;
                chr_set_animation_external(&c->c, tr_from(anim), anim, 8);
                startled = true;
                a->startle_time = e->posted;
            }
        }
    }
    if (level == 4 && e && e->target) {
        /* AI2rExitState, then the combat state (C+0x8c0 the target, C+0x8c8 the contact) */
        ai_exit_state(a);
        a->state = AI_STATE_COMBAT;
        combat_enter(a);
        ai_combat_set_target(a, e);
    }
    return startled;
}

/* AI2rAlert_NotifyKnowledge 0x48ecc0, the calm states' reaction to a contact: an unimportant one is
   glanced at; a combat sound, or one with hostility, from the most important contact raises the
   alert to the type's level (0x52a624) if not already above it, and any alert sends a calm character
   to pursue it (its cry, AI2rVocalize 0x48ba60 type 2, at once when startled or at combat, else after
   60 to 90 ticks: type 1, AI2rUpdateAI); a talking (neutral) character meeting danger stops the
   conversation. */
void knowledge_alert_notify(Chr *c, KnowEntry *e, bool degraded)
{
    AiState *a = &c->ai;
    if (degraded || (a->state == AI_STATE_PATROL && a->ignore_player)) return;
    if (e != a->know && a->know->strength == 4) return;
    if (e->type < 0 || e->type > 9) return;     /* "unknown knowledge type" */
    uint32_t glance = exe_u32(0x52a64c + 4u * (uint32_t)e->type);
    bool combat_sound = e->type > 1 && e->type < 5;
    if (!combat_sound && e->hostility < 1) {
        if (e->last_strength < e->strength) {
            /* the movement's glance at it (0x4a8150: a point, for the ticks, not a full turn, no delay) */
            a->glance_kind = 1;
            a->glance_ticks = glance;
            a->glance_delay = 0;
            memcpy(a->glance_at, e->pos, sizeof a->glance_at);
            a->glance = glance;
        }
        return;
    }
    if (e != a->know || e->strength <= 1) return;
    int before = a->alert;
    int level = (int)exe_u32(0x52a624 + 4u * (uint32_t)e->type);
    if (before <= level) {
        bool startled = upgrade_alert(c, level, e);
        if (before < a->alert && !(a->flags & AI_FLAG_STARTLED)) {
            if (startled || a->alert == 4) {
                ai_vocalize(c, 2, false);
                a->vocal_delay = 0;
                a->flags |= AI_FLAG_STARTLED;
            } else if (!a->vocal_delay) {
                a->vocal_delay = 60 + (uint32_t)(((int32_t)trac_random() * 30) >> 16);  /* UUrRandom */
            }
        }
    }
    if (!a->alert) return;
    if (a->state == AI_STATE_NEUTRAL) {
        float loc[3];
        chr_location(&c->c, loc);
        if (!ai_dangerous_contact(c, a->talk.player, true, e, loc)) return;
        neutral_stop(c, false, true, true, true);   /* AI2rNeutral_Stop 0x4acf70 */
    }
    if (a->alert && a->state != AI_STATE_PURSUIT && a->state != AI_STATE_COMBAT && a->state != AI_STATE_ALARM) {
        ai_pursue(a, e);    /* the pursuit state (C+0x8c0 the contact) */
    }
}

/* C+0xf80: the state's handler. */
static void notify(Chr *c, KnowEntry *e, bool degraded)
{
    switch (c->ai.state) {
    case AI_STATE_NONE: case AI_STATE_IDLE: case AI_STATE_PATROL: case AI_STATE_ALARM: case AI_STATE_NEUTRAL:
        if (!(c->ai.flags & AI_FLAG_NON_AGGRESSIVE)) knowledge_alert_notify(c, e, degraded);
        else panic_notify(c, e, degraded);      /* the non-combatants' (0x4adeb0) */
        break;
    case AI_STATE_PANIC: panic_notify(c, e, degraded); break;
    case AI_STATE_PURSUIT: pursuit_notify(c, e, degraded); break;
    case AI_STATE_COMBAT: ai_combat_notify(&c->ai, &c->c, e, degraded); break;
    default: break;
    }
}

static KnowEntry *new_entry(Chr *owner)
{
    KnowEntry *e = free_list;
    if (!e) {
        if (entries_used < ENTRIES_MAX) e = &entries[entries_used++];
        else {
            for (int i = 0; i < ENTRIES_MAX; i++)
                if (entries[i].owner && (entries[i].strength < 2 || !alive(entries[i].owner))) remove_entry(&entries[i]);
            e = free_list;
            if (!e) return NULL;    /* "too many knowledge entries" */
        }
    }
    if (e == free_list) free_list = e->next;
    memset(e, 0, sizeof *e);
    e->owner = owner;
    e->next = owner->ai.know;
    if (e->next) e->next->prev = e;
    owner->ai.know = e;
    owner->ai.know_count++;
    return e;
}

/* AI2iKnowledge_PostContact 0x49b3f0. */
static void post_now(const Pending *p)
{
    Chr *o = p->owner;
    if (!o || !is_ai(o) || !(o->ai.flags & AI_FLAG_KNOWLEDGE)) return;
    KnowEntry *e = knowledge_find(o, p->target);
    int type = p->type;
    if (!e) {
        e = new_entry(o);
        if (!e) return;
        e->target = p->target;
        if (p->target) {
            if (ai_is_hostile(o, p->target, false)) e->hostility = 2;
            else if (ai_is_hostile(p->target, o, false)) e->hostility = 1;
        }
        e->first = now;
        e->highest = 1;
        e->hostile_until = 0xffffffffu;
        e->strength = 1;
    }
    if (p->force_hostile) e->hostility = 3;
    if (e->type == 0 && type == 0 && now - e->posted < 240) {   /* 0x545780, 0x54577c */
        if (++e->repeats >= 6) type = KNOW_SOUND_INTERESTING;
    } else {
        e->repeats = 0;
    }
    e->posted = now;
    memcpy(e->pos, p->pos, sizeof e->pos);
    e->type = type;
    e->value = p->value;
    e->raised = e->highest < p->strength;
    if (e->raised) e->highest = p->strength;
    if (e->strength <= p->strength) {
        e->strength = p->strength;
        e->degrade_at = now + degrade_time(o, p->strength);
    }
    if (type == KNOW_HURT || type == KNOW_HURT_MELEE) {
        e->hurt = true;
        if (type == KNOW_HURT_MELEE) e->hurt_melee = true;
        e->damage += p->value;
        if (e->hostility == 0) {
            /* hostile (for 900 ticks) once hurt for more than half the hit points (C+0x3c), or with
               no enemy near (a contact above the strength 2, hostile, within the Neutral's enemy
               range, C+0x1014): else a hurt by the way, in a fight with another */
            bool enemy_near = false;
            if (e->damage <= o->c.max_hit_points >> 1) {
                float r = o->ai.neutral_enemy;
                for (const KnowEntry *k = o->ai.know; k && !enemy_near; k = k->next) {
                    float dx = k->pos[0] - o->c.pos[0], dy = k->pos[1] - o->c.pos[1], dz = k->pos[2] - o->c.pos[2];
                    enemy_near = k->strength > 2 && k->hostility == 2 && dx * dx + dy * dy + dz * dz < r * r;
                }
            }
            if (!enemy_near) {
                e->hostile = true;
                e->hostile_until = e->posted + 900;
                e->hostility = 2;
            }
        } else {
            if (e->hostility > 2) e->hostility = 2;
            e->hostile = true;
            e->hostile_until = 0xffffffffu;
        }
    }
    /* a melee or gunfire sound of target fighting other: heard by one neutral to the target and a
       friend of the other, a definite contact not yet taken sides on is taken against the target
       (hostility 1, for 900 ticks) when the other, an AI, holds it hostile, no longer than the other
       does */
    else if ((type == KNOW_SOUND_MELEE || type == KNOW_SOUND_GUNFIRE) && e->target && p->other &&
             e->hostility == 0 && e->strength == KNOW_DEFINITE && ai_team_relation(o->team, e->target->team) == 1 &&
             ai_team_relation(o->team, p->other->team) == 0) {
        const KnowEntry *k = NULL;
        bool take = true;
        if (is_ai(p->other)) {
            k = knowledge_find(p->other, e->target);
            take = k && (k->hostile || k->hostility >= 2);
        }
        if (take) {
            e->hostility = 1;
            e->hostile = true;
            e->hostile_until = e->posted + 900;
            if (k && k->hostile && k->hostile_until < e->hostile_until) e->hostile_until = k->hostile_until;
        }
    }
    while (e->prev && compare(e, e->prev, false, o) <= 0) move_up(o, e);
    e->updated = now;
    if (!p->silent) notify(o, e, false);
    e->last_strength = e->strength;
}

static void add_pending(Chr *owner, int type, int strength, const float pos[3], Chr *target, uint32_t value, Chr *other,
                        bool silent, bool force_hostile)
{
    Pending *p = NULL;
    for (int i = 0; i < npending; i++)
        if (target && pending[i].owner == owner && pending[i].target == target) {
            if (strength < pending[i].strength && type < 7) return;
            if (pending[i].type < 7) p = &pending[i];
            break;
        }
    if (!p) {
        if (npending >= PENDING_MAX) return;    /* "too many pending" */
        p = &pending[npending++];
    }
    p->owner = owner;
    p->target = target;
    p->strength = strength;
    memcpy(p->pos, pos, sizeof p->pos);
    p->type = type;
    p->value = value;
    p->other = other;
    p->silent = silent;
    p->force_hostile = force_hostile;
}

void knowledge_post(Chr *owner, int type, int strength, const float pos[3], Chr *target, uint32_t value, bool silent,
                    bool force_hostile)
{
    add_pending(owner, type, strength, pos, target, value, NULL, silent, force_hostile);
}

/* A sound's maker or the one it was made at, as one listener hears of them (AI2rKnowledge_Sound): none
   (or itself, a dead one, one not in use) is not heard of; the player is, unless the listener ignores
   it; another AI only when one of the two takes the other for an enemy, and never by a combat sound
   (2..4: what an AI hears of a fight between AIs is the one fought) */
static bool unheard(const Chr *listener, const Chr *who, bool combat)
{
    if (!who || who == listener || !alive(who) || !chars_in_use(who)) return true;
    if (!is_ai(who)) return (listener->ai.flags & AI_FLAG_IGNORE_PLAYER) != 0;     /* (or the cheat 0x5ec0c2) */
    return combat || (!ai_is_hostile(listener, who, true) && !ai_is_hostile(who, listener, true));
}

/* AI2rKnowledge_Sound 0x49a8a0 (the debug list of the last 20 sounds, 0x63abc0, and the all-deaf and
   all-passive switches, 0x5ec0c1 and 0x5ec0c4, left out). */
void knowledge_sound(int type, const float pos[3], float radius, Chr *source, Chr *target)
{
    if (ai_all_passive) return;     /* 0x49a907 */
    bool combat = type >= KNOW_SOUND_DANGER && type <= KNOW_SOUND_GUNFIRE;     /* 0x49ad70 */
    float at_source[3], at_target[3] = { 0, 0, 0 };
    /* the places posted: the maker's location (C+0x160), else the sound's */
    if (source) chr_location(&source->c, at_source);
    else memcpy(at_source, pos, sizeof at_source);
    if (target) chr_location(&target->c, at_target);
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (!c || !is_ai(c) || (c->ai.flags & (AI_FLAG_PASSIVE | AI_FLAG_DEAF))) continue;
        bool no_source = source && unheard(c, source, combat), no_target = unheard(c, target, combat);
        if (no_source && no_target) continue;
        /* within the radius times the class's hearing (ONCC +0x428) */
        float r = radius * oncc_float(c, 0x428);
        float dx = pos[0] - c->c.pos[0], dy = pos[1] - c->c.pos[1], dz = pos[2] - c->c.pos[2];
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        /* heard in a line to the head (AKrLineOfSight 0x4410a0: the walls skipping 0x42080, then the
           objects' spheres), or by a way through the rooms no longer than the
           radius (PHrPathDistance 0x50edb0, from the sound's room to the listener's node, C+0x188, with
           no character and the sound's type as the key: every door class has type 2 and danger 0.5, so only gunfire carries through the shut doors,
           their danger doubling the way beyond) */
        float head[3];
        knowledge_head_point(c, head);
        float v[3] = { head[0] - pos[0], head[1] - pos[1], head[2] - pos[2] };
        bool heard = (!sound_env || !akev_segment_hits(sound_env, pos, v, 0x42080)) && !object_line_blocked(pos, head);
        if (!heard) {
            float loc[3];
            int from = graph_room_at(pos), to = chr_room(&c->c, loc);
            float d = r;
            heard = from >= 0 && to >= 0 && graph_find_path_within(from, pos, to, head, false, type, NULL, &d) != 0;
        }
        if (!heard) continue;
        /* a hint is strong; a fight's sound within 40 of the head definite */
        int strength = KNOW_STRONG;
        if (type >= KNOW_SOUND_MELEE) {
            float hx = head[0] - pos[0], hy = head[1] - pos[1], hz = head[2] - pos[2];
            if (hx * hx + hy * hy + hz * hz < 1600.0f) strength = KNOW_DEFINITE;     /* 0x52a92c */
        }
        if (!no_source) add_pending(c, type, strength, at_source, source, 0, target, false, false);
        if (!no_target) add_pending(c, type, KNOW_STRONG, at_target, target, 0, source, false, false);
    }
}

void knowledge_hurt_by(Chr *victim, Chr *attacker, uint32_t damage, bool melee)
{
    if (ai_all_passive) return;     /* 0x49a7f0 */
    if (attacker && ai_team_relation(victim->team, attacker->team) != 0)
        knowledge_post(victim, melee ? KNOW_HURT_MELEE : KNOW_HURT, melee ? 4 : 3, attacker->c.pos, attacker, damage,
                       false, false);
    /* not melee: a gunfire sound of 120 at the victim, of the attacker at the victim (without one, of
       the victim) */
    if (!melee) knowledge_sound(KNOW_SOUND_GUNFIRE, victim->c.pos, 120.0f, attacker ? attacker : victim, attacker ? victim : NULL);
}

void knowledge_forget(Chr *c)
{
    for (int i = 0; i < entries_used; i++) {
        KnowEntry *e = &entries[i];
        if (!e->owner) continue;
        if (e->owner == c || e->target == c) remove_entry(e);
    }
    for (int i = 0; i < npending;)
        if (pending[i].owner == c || pending[i].target == c) pending[i] = pending[--npending];
        else {
            if (pending[i].other == c) pending[i].other = NULL;
            i++;
        }
}

void knowledge_forget_contacts(Chr *owner, const Chr *target)
{
    /* from the last contact back (the list's tail, then +0x50) */
    KnowEntry *e = owner->ai.know;
    while (e && e->next) e = e->next;
    while (e) {
        KnowEntry *prev = e->prev;
        if (!target || e->target == target) {
            e->strength = KNOW_FORGOTTEN;
            notify(owner, e, true);
            /* 0x49b8e0's state handlers: the pursuit's (the combat's 0x4908b0 and the panic's not yet) */
            if (e->owner && owner->ai.state == AI_STATE_PURSUIT) pursuit_forgotten(owner, e);
            if (e->owner) remove_entry(e);
        }
        e = prev;
    }
}

/* The head (0x4e3460: A+0x247c, else the location 15 up), the pelvis (0x4e34f0) and the bottom
   (0x4e3590: the pelvis's x and z, the body's bottom + 1). */
void knowledge_head_point(const Chr *c, float p[3])
{
    if (!chr_part_point(&c->c, 10, p)) p[0] = c->c.pos[0], p[1] = c->c.pos[1] + 15.0f, p[2] = c->c.pos[2];
}

static void pelvis_point(const Chr *c, float p[3])
{
    if (!chr_part_point(&c->c, 0, p)) p[0] = c->c.pos[0], p[1] = c->c.pos[1] + c->c.pelvis_height, p[2] = c->c.pos[2];
}

/* AI2iKnowledge_CheckVisible 0x49adf0: what looker sees of the characters. Each one in range is seen
   centrally (strength 4, type 6) or peripherally (2, type 5) by the angle from the view direction
   (ONCC +0x3f4 the central distance, +0x3f8 the peripheral, +0x3fc the vertical cosine, +0x400 and
   +0x404 the central cosines of full and no range, +0x408 and +0x40c the peripheral's), one already
   seen keeps being seen while within the peripheral angle and the central distance; then a line
   from the head to the pelvis, the head or the bottom of the other must be clear (quads with
   flags 0x42080 let it through). The invisible past their first 90 ticks aren't looked at. */
static void check_visible(Chr *looker, const void *env)
{
    float eye[3], fwd[3] = { sinf(looker->c.facing), 0.0f, cosf(looker->c.facing) }, up[3] = { 0.0f, 1.0f, 0.0f };
    /* aiming (A+0x2195), along the aim (A+0x21a4) and its up across it and (-z, 0, x) */
    if (looker->ai.aim.aiming) {
        memcpy(fwd, looker->ai.aim.dir, sizeof fwd);
        float side[3] = { -fwd[2], 0.0f, fwd[0] };
        up[0] = fwd[1] * side[2] - fwd[2] * side[1];
        up[1] = fwd[2] * side[0] - fwd[0] * side[2];
        up[2] = fwd[0] * side[1] - fwd[1] * side[0];
        float l = 1.0f / sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        for (int k = 0; k < 3; k++) up[k] *= l;
    }
    knowledge_head_point(looker, eye);
    float cdist = oncc_float(looker, 0x3f4), pdist = oncc_float(looker, 0x3f8), vert = oncc_float(looker, 0x3fc);
    float cmax = oncc_float(looker, 0x400), cmin = oncc_float(looker, 0x404);
    float pmax = oncc_float(looker, 0x408), pmin = oncc_float(looker, 0x40c);
    for (int i = 0; i < chars_slots(); i++) {
        Chr *t = chars_get(i);
        if (!t || t == looker || !alive(t) || (looker->ai.flags & AI_FLAG_PASSIVE)) continue;
        /* the invisible unseen once their first 90 ticks are over (C+0x1ba, C+0x1bc) */
        if (t->c.invisibility && t->c.invisible_ticks >= 90) continue;
        if (!is_ai(t)) {
            if (looker->ai.flags & AI_FLAG_IGNORE_PLAYER) continue;
        } else if (!ai_is_hostile(looker, t, true) && !ai_is_hostile(t, looker, true)) {
            continue;
        }
        bool known = false;
        for (KnowEntry *e = looker->ai.know; e; e = e->next) {
            if (e->target == t) { known = e->strength > 3; break; }
            if (e->strength < 4) break;
        }
        float at[3] = { t->c.pos[0], t->c.pos[1] + t->c.pelvis_height, t->c.pos[2] };
        float d[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
        float dist2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (dist2 > cdist * cdist) continue;
        float dist = sqrtf(dist2);
        if (dist > 20.0f && fabsf((d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / dist) > vert) continue;
        float cosa = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / dist;
        int strength;
        if (known) {
            if (!(pmin <= cosa && dist <= cdist)) continue;
            strength = 4;
        } else {
            if (cosa < pmin) continue;
            float crange = 0.0f, prange;
            if (cosa < pmax) {
                prange = (cosa - pmin) * pdist / (pmax - pmin);
            } else {
                prange = pdist;
                if (cosa >= cmin) crange = cosa < cmax ? (cosa - cmin) * cdist / (cmax - cmin) : cdist;
            }
            if (dist < crange) strength = 4;
            else if (dist < prange) strength = 2;
            else continue;
        }
        /* the sight lines: from the eye to the other's pelvis, head and feet (ONrCharacter_GetFeetPosition
           0x4e3590: the pelvis's x and z at the body's bottom, C+0x190, + 1), the first clear one (the
           walls only, AKrCollision_Point 0x441c80) the place posted */
        float pts[3][3];
        pelvis_point(t, pts[0]);
        knowledge_head_point(t, pts[1]);
        memcpy(pts[2], pts[0], sizeof pts[2]);
        pts[2][1] = t->c.bottom + 1.0f;
        int seen = -1;
        for (int k = 0; k < 3 && seen < 0; k++) {
            float v[3] = { pts[k][0] - eye[0], pts[k][1] - eye[1], pts[k][2] - eye[2] };
            if (!env || !akev_segment_hits(env, eye, v, 0x42080)) seen = k;
        }
        if (seen >= 0)
            knowledge_post(looker, strength == 4 ? KNOW_SEEN_CENTRAL : KNOW_SEEN_PERIPHERAL, strength, pts[seen], t, 0, false, false);
    }
}

/* ---- the dangers: projectiles and firing spreads ---- */


/* The particle's place (P3rGetRealPosition 0x45acc0) and its velocity variable (class flag 0x1000). */
static void projectile_read(Projectile *r)
{
    p3_real_position(r->cls, r->p, r->pos);
    const float *v = p3_block(r->cls, r->p, P3_X_VELOCITY);
    if (v) memcpy(r->vel, v, sizeof r->vel);
}

int knowledge_projectile_new(P3Class *c, P3Particle *p, float dodge_radius, float alert_radius)
{
    /* AI2rKnowledge_NewDodgeProjectile 0x49bc00: the first free one ("out of projectile slots": 64) */
    unsigned i = 0;
    while (i < nprojectiles && (projectiles[i].flags & DANGER_IN_USE)) i++;
    if (i >= PROJECTILES_MAX) return -1;
    if (i >= nprojectiles) nprojectiles = i + 1;
    Projectile *r = &projectiles[i];
    memset(r, 0, sizeof *r);
    r->flags = DANGER_IN_USE;
    /* 0x49bd20 */
    r->dodge_radius = dodge_radius;
    r->alert_radius = alert_radius;
    r->cls = c;
    r->p = p;
    p3_real_position(c, p, r->start);
    memcpy(r->prev, r->start, sizeof r->prev);
    memcpy(r->pos, r->start, sizeof r->pos);
    const float *v = p3_block(c, p, P3_X_VELOCITY);
    if (v) memcpy(r->vel, v, sizeof r->vel);
    const uint32_t *o = p3_block(c, p, P3_X_OWNER);
    r->owner = o ? *o : 0;
    r->handle = p3_handle(p);
    return (int)i;
}

void knowledge_projectile_deleted(int slot, P3Class *c, P3Particle *p)
{
    /* AI2rKnowledge_DeleteDodgeProjectile 0x49bc70: its last place, then gone (the record is let go at the next refresh) */
    (void)c, (void)p;
    if (slot < 0 || slot >= PROJECTILES_MAX) return;
    Projectile *r = &projectiles[slot];
    if (r->p && p3_handle(r->p) == r->handle) {
        memcpy(r->prev, r->pos, sizeof r->prev);
        projectile_read(r);
    } else
        r->flags &= ~(uint32_t)DANGER_IN_USE;
    r->flags |= DANGER_GONE;
    r->p = NULL;
    r->cls = NULL;
    r->handle = 0;
}

/* The gun's place and its firing direction (the class's +0x104 through the weapon's matrix,
   MUrMatrix3x3_MultiplyPoint 0x433a40). */
static void spread_place(Spread *f, const Weapon *w)
{
    const float *v = (const float *)(w->cls + 0x104), *m = w->m;
    memcpy(f->pos, m + 9, sizeof f->pos);
    for (int k = 0; k < 3; k++) f->dir[k] = v[0] * m[k] + v[1] * m[3 + k] + v[2] * m[6 + k];
}

int knowledge_spread_new(const Weapon *w)
{
    /* AI2rKnowledge_NewDodgeFiringSpread 0x49c160 ("out of firing spread slots": 32) */
    unsigned i = 0;
    while (i < nspreads && (spreads[i].flags & DANGER_IN_USE)) i++;
    if (i >= SPREADS_MAX) return -1;
    if (i >= nspreads) nspreads = i + 1;
    Spread *f = &spreads[i];
    f->flags |= DANGER_IN_USE;
    /* AI2rKnowledge_SetupDodgeFiringSpread 0x49c210 */
    const float *k = (const float *)(w->cls + 0x144);
    f->length = k[0], f->radius = k[1], f->angle = k[2];
    spread_place(f, w);
    f->w = w;
    return (int)i;
}

void knowledge_spread_delete(int i)
{
    /* AI2rKnowledge_DeleteDodgeFiringSpread 0x49c1e0 */
    if (i < 0 || i >= SPREADS_MAX) return;
    spreads[i].flags &= ~(uint32_t)DANGER_IN_USE;
    if ((unsigned)i + 1 == nspreads) nspreads--;
}

/* AI2iKnowledge_UpdateProjectileAlertness (in AI2rKnowledge_Update 0x499e80): a projectile with an alert
   radius and a character owner tells every AI not its friend (and neither passive nor deaf) that its
   last 5 ticks' path passed within the radius of the pelvis of a gunshot of strength 3 from the owner,
   at the place it was made. */
static void projectile_alerts(void)
{
    for (unsigned i = 0; i < nprojectiles; i++) {
        Projectile *r = &projectiles[i];
        if (!(r->flags & DANGER_IN_USE) || r->alert_radius == 0.0f) continue;
        uint32_t kind = r->owner & 0xff000000u;
        if (kind != 0x1000000u && kind != 0x2000000u) continue;
        Chr *owner = p3_owner_character(r->owner);
        if (!owner) continue;
        float v[3] = { r->pos[0] - r->prev[0], r->pos[1] - r->prev[1], r->pos[2] - r->prev[2] };
        float a = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        for (int k = 0; k < chars_slots(); k++) {
            Chr *c = chars_get(k);
            if (!c || !chars_in_use(c) || !alive(c) || !is_ai(c) || (c->ai.flags & (AI_FLAG_PASSIVE | AI_FLAG_DEAF)) ||
                c == owner || ai_team_relation(c->team, owner->team) == 0)
                continue;
            float pel[3];
            pelvis_point(c, pel);
            float d[3] = { r->prev[0] - pel[0], r->prev[1] - pel[1], r->prev[2] - pel[2] };
            float b = 2.0f * (d[0] * v[0] + d[1] * v[1] + d[2] * v[2]);
            float cc = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - r->alert_radius * r->alert_radius;
            bool near = cc <= 0.0f;
            if (!near) {
                if (!((double)fabsf(a) > 0.001)) continue;     /* 0x5296e0 */
                float disc = b * b - cc * a * 4.0f;
                if (!(disc > 0.0f)) continue;
                float t = (sqrtf(disc) - b) / (a + a);
                near = t > 0.0f && t < 1.0f;
            }
            if (near) knowledge_post(c, KNOW_SOUND_GUNFIRE, 3, r->start, owner, 0, false, false);
        }
    }
}

/* AI2rKnowledge_Update 0x499e80, every 5 ticks: the projectiles' places and velocities (a deleted one's
   record let go, one whose particle is no longer its own dropped), the spreads' guns' places (a gun
   no longer in use drops its spread). */
static void dangers_refresh(void)
{
    for (unsigned i = 0; i < nprojectiles; i++) {
        Projectile *r = &projectiles[i];
        if (!(r->flags & DANGER_IN_USE)) continue;
        if (r->flags & DANGER_GONE) {
            r->flags &= ~(uint32_t)(DANGER_IN_USE | DANGER_GONE);
            if (i + 1 == nprojectiles) nprojectiles--;
        } else if (r->p && p3_handle(r->p) == r->handle) {
            memcpy(r->prev, r->pos, sizeof r->prev);
            projectile_read(r);
        } else
            r->flags &= ~(uint32_t)DANGER_IN_USE;
    }
    for (unsigned i = 0; i < nspreads; i++) {
        Spread *f = &spreads[i];
        if (!(f->flags & DANGER_IN_USE)) continue;
        if (!f->w || !(f->w->flags & WEAPON_IN_USE)) f->flags &= ~(uint32_t)DANGER_IN_USE;    /* 0x518680 */
        else spread_place(f, f->w);
    }
}

void knowledge_dangers(Chr *ch)
{
    const void *env = p3_environment();
    Character *c = &ch->c;
    float pel[3];
    pelvis_point(ch, pel);
    float body = -1.0f;     /* A+0x13c, worked out when first needed */
    /* AI2rKnowledge_FindNearbyProjectiles 0x49bdf0: each projectile with a dodge radius not of ch's own, its next 5 ticks' motion (its
       velocity by 0.8333, 0x52a930) coming within the radius and the body sphere's of the pelvis: its
       nearest point there in sight of the pelvis given as a projectile danger */
    for (unsigned i = 0; i < nprojectiles; i++) {
        Projectile *r = &projectiles[i];
        if (!(r->flags & DANGER_IN_USE)) continue;
        uint32_t kind = r->owner & 0xff000000u;
        if ((kind == 0x1000000u || kind == 0x2000000u) && p3_owner_character(r->owner) == ch) continue;
        if (!(r->dodge_radius > 0.0f)) continue;
        if (body < 0.0f) body = p3_body_radius(c);
        float d[3] = { r->pos[0] - pel[0], r->pos[1] - pel[1], r->pos[2] - pel[2] };
        float w[3] = { r->vel[0] * 0.8333333f, r->vel[1] * 0.8333333f, r->vel[2] * 0.8333333f };
        float rr = body + r->dodge_radius;
        float a = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
        float b = 2.0f * (d[0] * w[0] + d[1] * w[1] + d[2] * w[2]);
        float cc = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - rr * rr;
        float t;
        if (cc <= 0.0f) t = 0.0f;
        else {
            if (!((double)fabsf(a) > 0.001) || !(b <= 0.0f)) continue;      /* 0x5296e0; going away */
            float disc = b * b - cc * a * 4.0f;
            if (!(disc > 0.0f)) continue;
            float root = (sqrtf(disc) - b) / (a + a);
            if (!(root > 0.0f) || !(root < 1.0f)) continue;
            t = -(b / (a + a));
        }
        float q[3] = { t * w[0] + r->pos[0], t * w[1] + r->pos[1], t * w[2] + r->pos[2] };
        float to[3] = { pel[0] - q[0], pel[1] - q[1], pel[2] - q[2] };
        if (env && akev_segment_hits(env, q, to, 0x242000)) continue;
        /* the nearest point on the 5 ticks' motion, and its distance from the world's origin, not from
           the pelvis (so a projectile is dodged only near the origin): kept */
        float k = t < 0.0f ? -t : t > 1.0f ? 1.0f - t : 0.0f;
        float e[3] = { k * w[0] + q[0], k * w[1] + q[1], k * w[2] + q[2] };
        float dist = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        combat_danger_projectile(ch, r, w, dist, q, r->dodge_radius);
    }
    /* AI2rKnowledge_FindNearbyFiringSpreads 0x49c290: each firing spread not of ch's own gun, its cone (as long as its length, widening from
       its radius by its angle, and the body sphere's) holding the pelvis, the gun in sight of it: a
       spread danger, with the way from the pelvis to the cone's axis */
    for (unsigned i = 0; i < nspreads; i++) {
        Spread *f = &spreads[i];
        if (!(f->flags & DANGER_IN_USE)) continue;
        if (f->w && f->w->owner == c) continue;     /* WPrGetOwner 0x518610 */
        float d[3] = { pel[0] - f->pos[0], pel[1] - f->pos[1], pel[2] - f->pos[2] };
        float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (!(dd <= f->length * f->length)) continue;
        float along = f->dir[0] * d[0] + f->dir[1] * d[1] + f->dir[2] * d[2];
        if (along < 0.0f) continue;
        float perp[3] = { d[0] - along * f->dir[0], d[1] - along * f->dir[1], d[2] - along * f->dir[2] };
        if (body < 0.0f) body = p3_body_radius(c);
        float rr = sqrtf(dd) * tanf(f->angle) + f->radius + body;
        float pp = perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2];
        if (!(pp <= rr * rr)) continue;
        if (env && akev_segment_hits(env, f->pos, d, 0x242000)) continue;
        float to_axis[3] = { -perp[0], -perp[1], -perp[2] };
        combat_danger_spread(ch, f, d, rr, sqrtf(pp), to_axis);
    }
}

void knowledge_update(const void *env)
{
    /* the pending posts of last tick */
    sound_env = env;
    for (int i = 0; i < npending; i++) post_now(&pending[i]);
    npending = 0;
    now++;
    /* the projectiles' alerts (unless everyone is deaf or passive, 0x5ec0c1, 0x5ec0c4); every 5 ticks
       the dangers' places */
    projectile_alerts();
    if (now % 5 == 0) dangers_refresh();
    if (now % 60 == 0) {
        for (int i = 0; i < entries_used; i++) {
            KnowEntry *e = &entries[i];
            if (!e->owner || e->strength < 2) continue;
            Chr *o = e->owner;
            if (!is_ai(o) || !alive(o)) { remove_entry(e); continue; }
            if (e->target && !alive(e->target)) {
                e->strength = 0;
                e->degrade_at = now + degrade_time(o, 0);
            } else if (e->hostile && now > e->hostile_until) {
                e->hostile = false;
                if (e->hostility == 2) e->hostility = 1;
            } else {
                if (now < e->degrade_at || (o->ai.flags & AI_FLAG_NO_DEGRADE)) continue;
                e->strength--;
                e->degrade_at = now + degrade_time(o, e->strength);
            }
            while (e->next && compare(e, e->next, false, o) >= 0) move_up(o, e->next);
            notify(o, e, true);
            e->last_strength = e->strength;
        }
    }
    /* who looks this tick: every 4 ticks for 3 seconds after a startle (not in combat), 15 in pursuit,
       59 otherwise, each at its own tick (by its index); and at once when a glance ends */
    Chr *last = NULL;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (!c) continue;
        last = c;
        if (!alive(c) || !is_ai(c) || (c->ai.flags & (AI_FLAG_PASSIVE | AI_FLAG_BLIND))) continue;
        bool look;
        if (c->ai.glance && --c->ai.glance == 0) {
            look = true;
        } else {
            uint32_t period = now - c->ai.startle_time < 180 && c->ai.state != AI_STATE_COMBAT ? 4 :
                              c->ai.state == AI_STATE_PURSUIT ? 15 : 59;
            look = ((uint32_t)(c->c.id - 1) * 13u) % period == now % period;
        }
        if (look) check_visible(c, env);
    }
    /* then the most important contact, if fresh (within a second), is told again: the original's loop
       means every character but tests the last of the list each time (it never moves on), so only
       that one is told, as many times as there are characters */
    if (last && alive(last) && is_ai(last) && !(last->ai.flags & AI_FLAG_PASSIVE))
        for (int i = 0; i < chars_slots(); i++) {
            KnowEntry *e = last->ai.know;
            if (!chars_get(i) || !e || now > e->updated + 60) continue;
            e->updated = now;
            notify(last, e, false);
        }
}
