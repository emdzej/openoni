/* The AI's knowledge: see knowledge.h. */
#include "knowledge.h"
#include "combat.h"

#include "akev.h"
#include "chars.h"
#include "chr_anim.h"
#include "chr_hit.h"
#include "exe.h"

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
    bool silent, force_hostile;
} Pending;
static Pending pending[PENDING_MAX];       /* DAT_0063a2a0, 0x24 bytes each */
static int npending;                       /* DAT_0063b340 */

static uint32_t now;                       /* stand-in for the game time (GS+0x144) */

static float wrap_2pi(float a) { return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a; }

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

void knowledge_reset(void)
{
    memset(entries, 0, sizeof entries);
    entries_used = 0;
    free_list = NULL;
    npending = 0;
    now = 0;
}

int ai_team_relation(uint32_t a, uint32_t b)
{
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
   the highest strengths decide). Not ported: its debug ordering by the characters' flag 0x10000000. */
static int compare(const KnowEntry *a, const KnowEntry *b, bool exact, const Chr *owner)
{
    if (a->strength <= 2 && b->strength > 2) return 1;
    if (b->strength <= 2 && a->strength > 2) return -1;
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
   state against the contact's character. Returns whether it startled. Not ported: the movement's
   notification, the turn during the startle (C+0xfac, C+0xfb0). */
static bool upgrade_alert(Chr *c, int level, KnowEntry *e)
{
    AiState *a = &c->ai;
    bool startled = false;
    if (level != a->alert) {
        a->alert = level;
        if (level > 1 && !a->alert_time) a->alert_time = e ? e->posted : now;
        if (!(oncc_u32(c, 0x120) & 1) && a->alert >= 3 && e && e->hostility != 3 && !(!e->raised && e->highest > 3) &&
            (!a->alert_time || e->posted <= a->alert_time + 60)) {
            float to = wrap_2pi(atan2f(e->pos[0] - c->c.pos[0], e->pos[2] - c->c.pos[2]));
            float rel = wrap_2pi(to - c->c.facing + 0.78539819f);
            uint16_t type = rel < 1.5707964f ? 0xc3 : rel < PI ? 0xc0 : rel < 4.712389f ? 0xc2 : 0xc1;
            chr_enter_fight_mode(&c->c);
            const void *anim = chr_lookup(&c->c, type, c->c.from_state);
            if (anim) {
                a->startle_ticks = tr_duration(anim);
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
   to pursue it. Not ported: the glance's look (0x4a8150), the vocalizations, the neutral state's
   danger test. */
static void alert_notify(Chr *c, KnowEntry *e, bool degraded)
{
    AiState *a = &c->ai;
    if (degraded || (a->state == AI_STATE_PATROL && a->ignore_player)) return;
    if (e != a->know && a->know->strength == 4) return;
    if (e->type < 0 || e->type > 9) return;     /* "unknown knowledge type" */
    uint32_t glance = exe_u32(0x52a64c + 4u * (uint32_t)e->type);
    bool combat_sound = e->type > 1 && e->type < 5;
    if (!combat_sound && e->hostility < 1) {
        if (e->last_strength < e->strength) a->glance = glance;
        return;
    }
    if (e != a->know || e->strength <= 1) return;
    int before = a->alert;
    int level = (int)exe_u32(0x52a624 + 4u * (uint32_t)e->type);
    if (before <= level) {
        bool startled = upgrade_alert(c, level, e);
        if (before < a->alert && !(a->flags & AI_FLAG_STARTLED)) {
            if (startled || a->alert == 4) {
                a->vocal_delay = 0;     /* AI2rVocalize: the sound */
                a->flags |= AI_FLAG_STARTLED;
            } else if (!a->vocal_delay) {
                a->vocal_delay = 60;    /* + a random part of 30 (UUrRandom): with the vocalizations */
            }
        }
    }
    if (a->alert && a->state != AI_STATE_PURSUIT && a->state != AI_STATE_COMBAT && a->state != AI_STATE_ALARM) {
        /* the pursuit state (C+0x8c0 the contact) */
        ai_exit_state(a);
        a->state = AI_STATE_PURSUIT;
        a->point_started = false;
        a->target = e->target;
        a->target_entry = e;
    }
}

/* C+0xf80: the state's handler. */
static void notify(Chr *c, KnowEntry *e, bool degraded)
{
    switch (c->ai.state) {
    case AI_STATE_NONE: case AI_STATE_IDLE: case AI_STATE_PATROL: case AI_STATE_ALARM: case AI_STATE_NEUTRAL:
        if (!(c->ai.flags & AI_FLAG_NON_AGGRESSIVE)) alert_notify(c, e, degraded);
        break;              /* non-combatants (0x4adeb0): with panic */
    case AI_STATE_PURSUIT:
        /* 0x4b1750: the calm states' handler first (it may raise the alert to combat), then the
           pursuit takes the most important contact if it is a hostile one. Not ported: the alarm and
           the pursuit's modes (AI2iPursuit_ChangeMode 0x4b1260, AI2iPursuit_DesirePursuit 0x4b1620). */
        alert_notify(c, e, degraded);
        if (c->ai.state == AI_STATE_PURSUIT && c->ai.know && c->ai.know != c->ai.target_entry &&
            c->ai.know->hostility >= 1) {
            c->ai.target_entry = c->ai.know;
            c->ai.target = c->ai.know->target;
            c->ai.point_started = false;
        }
        break;
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
            /* hostile once hurt for half the hit points (C+0x3c), or with no enemy seen near: the
               distance C+0x1014 is the combat profile's, not ported yet (0) */
            e->hostile = true;
            e->hostile_until = e->posted + 900;
            if (e->hostile) e->hostility = 2;
        } else {
            if (e->hostility > 2) e->hostility = 2;
            e->hostile = true;
            e->hostile_until = 0xffffffffu;
        }
    }
    /* types 3 and 4 with a source (sounds of a fight between others): with the sounds */
    while (e->prev && compare(e, e->prev, false, o) <= 0) move_up(o, e);
    e->updated = now;
    if (!p->silent) notify(o, e, false);
    e->last_strength = e->strength;
}

void knowledge_post(Chr *owner, int type, int strength, const float pos[3], Chr *target, uint32_t value, bool silent,
                    bool force_hostile)
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
    p->silent = silent;
    p->force_hostile = force_hostile;
}

void knowledge_hurt_by(Chr *victim, Chr *attacker, uint32_t damage, bool melee)
{
    if (attacker && ai_team_relation(victim->team, attacker->team) != 0)
        knowledge_post(victim, melee ? KNOW_HURT_MELEE : KNOW_HURT, melee ? 4 : 3, attacker->c.pos, attacker, damage,
                       false, false);
    /* not melee: a sound of 120 (AI2rKnowledge_Sound): with the sounds */
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
        else i++;
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
            if (e->owner) remove_entry(e);
        }
        e = prev;
    }
}

/* The head (0x4e3460: A+0x247c, else the location 15 up), the pelvis (0x4e34f0) and the bottom
   (0x4e3590: the pelvis's x and z, the body's bottom + 1). */
static void head_point(const Chr *c, float p[3])
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
   flags 0x42080 let it through). */
static void check_visible(Chr *looker, const void *env)
{
    float eye[3], fwd[3] = { sinf(looker->c.facing), 0.0f, cosf(looker->c.facing) }, up[3] = { 0.0f, 1.0f, 0.0f };
    head_point(looker, eye);
    float cdist = oncc_float(looker, 0x3f4), pdist = oncc_float(looker, 0x3f8), vert = oncc_float(looker, 0x3fc);
    float cmax = oncc_float(looker, 0x400), cmin = oncc_float(looker, 0x404);
    float pmax = oncc_float(looker, 0x408), pmin = oncc_float(looker, 0x40c);
    for (int i = 0; i < chars_slots(); i++) {
        Chr *t = chars_get(i);
        if (!t || t == looker || !alive(t) || (looker->ai.flags & AI_FLAG_PASSIVE)) continue;
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
        /* the sight lines: from the eye to the other's pelvis, head and bottom (C+0x190: its feet
           here), the first clear one the place posted */
        float pts[3][3];
        pelvis_point(t, pts[0]);
        head_point(t, pts[1]);
        memcpy(pts[2], pts[0], sizeof pts[2]);
        pts[2][1] = t->c.pos[1] + 1.0f;
        int seen = -1;
        for (int k = 0; k < 3 && seen < 0; k++) {
            float v[3] = { pts[k][0] - eye[0], pts[k][1] - eye[1], pts[k][2] - eye[2] };
            if (!env || !akev_segment_hits(env, eye, v, 0x42080)) seen = k;
        }
        if (seen >= 0)
            knowledge_post(looker, strength == 4 ? KNOW_SEEN_CENTRAL : KNOW_SEEN_PERIPHERAL, strength, pts[seen], t, 0, false, false);
    }
}

void knowledge_update(const void *env)
{
    /* the pending posts of last tick */
    for (int i = 0; i < npending; i++) post_now(&pending[i]);
    npending = 0;
    now++;
    /* projectiles and firing spreads, sounds' places (every 5 ticks): with the weapons */
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
