/* The level's characters: see chars.h. */
#include "chars.h"
#include "activity.h"
#include "gametime.h"
#include "movement.h"
#include "obstacle.h"
#include "corpse.h"
#include "neutral.h"
#include "boss.h"
#include "alarm.h"
#include "combat.h"
#include "film.h"
#include "graph.h"

#include "chr_anim.h"
#include "chr_hit.h"
#include "exe.h"
#include "knowledge.h"
#include "melee.h"
#include "objc.h"
#include "particle.h"
#include "persist.h"
#include "patrol.h"
#include "powerup.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "trac.h"
#include "throw.h"
#include "weapon.h"
#include "trigger.h"
#include "ui.h"
#include "camera.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* The CHAR objects, with the flags the game sets on them as it goes (0x20 spawned: the OSD's +0x2c). */
enum { OBJECTS_MAX = 512 };
static ObjcObject objects[OBJECTS_MAX];
static uint32_t object_flags[OBJECTS_MAX];
static int n_objects;

static Chr slots[CHARS_MAX];
static bool used[CHARS_MAX];
static TriggerActor actors[CHARS_MAX];

static bool add_object(const ObjcObject *o, void *user)
{
    (void)user;
    if (o->tag != TM_TAG("CHAR") || n_objects == OBJECTS_MAX) return true;
    objects[n_objects] = *o;
    object_flags[n_objects] = objc_char_flags(o) & ~0x20u;    /* OBJiCharacter_Read 0x4bf290: not spawned yet */
    n_objects++;
    return true;
}

/* The weapons are set up after the characters at the level's start (chars_arm). */
static bool armed;

/* ONrGameState_NewCharacter 0x4dac50's weapon: the CHAR's class in the hand, its ammo at the object's
   percentage ("none" and "" have none). */
static void arm(Chr *c)
{
    if (c->object < 0) return;      /* from a setup (chr_create) */
    const ObjcObject *o = &objects[c->object];
    const char *w = objc_char_weapon(o);
    if (*w && strcmp(w, "none")) weapon_give_class(&c->c, w, objc_char_ammo(o));
}

void chars_arm(void)
{
    armed = true;
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i]) arm(&slots[i]);
}

/* ONrGameState_NewCharacter 0x4dac50, last: the difficulty's share of the hit points and their most
   (C+0x38, C+0x3c, rounded by 0x4340a0), from the game settings: +0xaec for the player (C+0x1e8 0),
   +0xae0 for a character the player's team takes for an enemy (AI2rTeam_FriendOrFoe 2); none without a
   player yet, or at 1. Easy gives the player 1.3, hard the enemies 1.25; normal is 1 for both. */
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static void difficulty_health(Chr *c)
{
    const uint8_t *gs = tm_instance_get(TM_TAG("ONGS"), "game_settings");
    if (!gs) return;
    int d = persist_difficulty();
    float f;
    if (c->player) f = rdf(gs + 0xaec + 4 * d);
    else {
        const Chr *p = chars_player();
        if (!p || ai_team_relation(c->team, p->team) != 2) return;
        f = rdf(gs + 0xae0 + 4 * d);
    }
    if (f == 1.0f) return;
    c->c.hit_points = (uint32_t)lrintf((float)c->c.hit_points * f);
    c->c.max_hit_points = (uint32_t)lrintf((float)c->c.max_hit_points * f);
}

/* 0x4dac50: a CHAR with the flags 0x802 takes a class at random among those of its class's variant
   (ONCC +0x42c, the ONCV's name at +4); with 0x800 in a hard game (difficulty above 1) among those of
   the variant's upgrade (ONCV +0x24, when it names one). None: "ERROR: character %s variant ..." and
   the named class stays. */
static const void *class_variant(const void *oncc, uint32_t flags)
{
    if (!(flags & 0x802)) return oncc;
    const uint8_t *v = tm_link((const uint8_t *)oncc + 0x42c);
    if (!v) return oncc;
    const char *want = (const char *)v + 4;
    if ((flags & 0x800) && persist_difficulty() > 1 && v[0x24]) want = (const char *)v + 0x24;
    static void *all[512];
    const void *pick[512];
    int n = tm_instance_list(TM_TAG("ONCC"), all, 512), k = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *w = tm_link((const uint8_t *)all[i] + 0x42c);
        if (w && !strncmp((const char *)w + 4, want, 32)) pick[k++] = all[i];
    }
    if (!k) return oncc;
    return pick[(uint32_t)trac_local_random() * (uint32_t)k >> 16];
}

/* ONrGameState_NewCharacter 0x4dac50 from a CHAR object: the first free slot; its class, place and
   facing (the rotation's y, degrees), name and team; AI2rInitializeCharacter's script id. */
static Chr *create(int object)
{
    const ObjcObject *o = &objects[object];
    const void *oncc = tm_instance_get(TM_TAG("ONCC"), objc_char_class(o));
    if (!oncc) return NULL;
    oncc = class_variant(oncc, objc_char_flags(o));
    int i = 0;
    while (i < CHARS_MAX && used[i]) i++;
    if (i == CHARS_MAX) return NULL;
    Chr *c = &slots[i];
    memset(c, 0, sizeof *c);
    if (!character_init(&c->c, oncc, o->pos, o->rot[1] * 0.017453293f)) return NULL;
    used[i] = true;
    memset(&actors[i], 0, sizeof actors[i]);
    strncpy(c->name, objc_char_name(o), sizeof c->name - 1);
    c->player = (object_flags[object] & OBJC_CHAR_PLAYER) != 0;
    c->script_id = c->player ? 0 : -1;
    c->team = objc_char_team(o);
    c->object = object;
    c->c.id = (uint16_t)(i + 1);
    c->c.ai = !c->player;
    /* the inventory: carried and dropped at death; a shield of 1 is a full one, invisibility starts
       hidden (C+0x1bc 600); the LSI is the CHAR's flag 0x200 */
    c->c.ammo = objc_char_inventory(o, 0, false);
    c->c.cells = objc_char_inventory(o, 1, false);
    c->c.hypos = objc_char_inventory(o, 2, false);
    c->c.drop_ammo = objc_char_inventory(o, 0, true);
    c->c.drop_cells = objc_char_inventory(o, 1, true);
    c->c.drop_hypos = objc_char_inventory(o, 2, true);
    c->c.shield = objc_char_inventory(o, 3, false);
    if (c->c.shield == 1) c->c.shield = 100;
    c->c.drop_shield = objc_char_inventory(o, 3, true) != 0;
    c->c.invisibility = objc_char_inventory(o, 4, false);
    c->c.drop_invisibility = objc_char_inventory(o, 4, true) != 0;
    c->c.invisible_ticks = c->c.invisibility ? 600 : 0;
    c->c.lsi = (object_flags[object] & 0x200) != 0;
    ai_init(&c->ai);
    c->ai.name = c->name;
    /* AI2rInitializeCharacter 0x48a6b0: the AI's flags (knowledge on; CHAR flag 0x08 non-aggressive,
       0x100 never forgets), the alert level, and the job: a patrol starts its path (by the id) at once */
    if (c->player) c->c.flags |= CHR_FLAG_BOSS;    /* no AI */
    if (!c->player) {
        uint32_t f = object_flags[object];
        c->ai.flags = AI_FLAG_KNOWLEDGE | (f & 0x08 ? AI_FLAG_NON_AGGRESSIVE : 0) | (f & 0x100 ? AI_FLAG_NO_DEGRADE : 0);
        c->c.flags |= CHR_FLAG_AI_MOVEMENT;     /* AI2rInitializeCharacter 0x48a6b0, an AI (C+0x1e8 2) */
        if (f & 0x400) c->c.flags |= CHR_FLAG_BOSS;
        c->ai.alert = (int)objc_char_alert(o);
        c->ai.state = AI_STATE_IDLE;
        melee_start(&c->ai.melee, melee_find(objc_char_melee(o)), oncc);
        combat_settings(&c->ai, objc_char_combat(o));
        if (objc_char_job(o) == OBJC_JOB_PATROL) {
            int path = patrol_find_id(objc_char_job_path(o));
            if (path >= 0) {
                patrol_start(&c->ai, path);
                c->ai.state = AI_STATE_PATROL;
            }
        }
    }
    c->ai.job = c->ai.state;
    c->ai.job_patrol = c->ai.patrol;
    /* AI2rMovementState_Initialize 0x4a8d60 (ONrGameState_NewCharacter): the weapon out by the mode */
    if (!c->player) movement_new_mode(c);
    /* NewCharacter 0x4db713: its room from the location, before the movement's start */
    float loc[3];
    chr_location(&c->c, loc);
    c->c.room = graph_room_at(loc);
    movement_place_clear(c);    /* its end, without a path: off any obstacle it stands on */
    c->ai.job_alert = (int)objc_char_job_alert(o);
    c->ai.pursue_alert = (int)objc_char_pursue_alert(o);
    c->ai.pursue_strong_low = (int)objc_char_pursuit(o, 0);
    c->ai.pursue_weak_low = (int)objc_char_pursuit(o, 1);
    c->ai.pursue_strong_high = (int)objc_char_pursuit(o, 2);
    c->ai.pursue_weak_high = (int)objc_char_pursuit(o, 3);
    c->ai.pursue_lost = (int)objc_char_pursuit(o, 4);
    c->ai.alarm_groups = objc_char_alarm_groups(o);
    neutral_change_behavior(c, objc_char_neutral(o), true);   /* AI2rInitializeCharacter 0x48a6b0 */
    chr_hit_init(&c->c, objc_char_extra_health(o));
    difficulty_health(c);
    if (armed) arm(c);
    activity_new_character(c);  /* the end of NewCharacter: the player's active record; an AI inactive */
    return c;
}

/* The level's character setups (AISA, ONLV +0x5c): a u16 count at +0x16, 0x160-byte records from +0x18
   (+0x00 name, +0x20 script id, +0x22 flag id, +0x24 flags, +0x26 team, +0x28 ONCC, +0x4c the scripts,
   +0x150 ONWC). */
static const uint8_t *level_setups(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("ONLV") && in->data) return tm_link(in->data + 0x5c);
    }
    return NULL;
}

/* 0x506d50: the setup of that script id, NULL none. */
static const uint8_t *find_setup(int16_t id)
{
    const uint8_t *a = level_setups();
    if (!a) return NULL;
    unsigned n = (unsigned)(a[0x16] | a[0x17] << 8);
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *r = a + 0x18 + 0x160 * i;
        if ((int16_t)(r[0x20] | r[0x21] << 8) == id) return r;
    }
    return NULL;
}

/* ONrGameState_NewCharacter 0x4dac50 from a setup (no CHAR object) at a flag: the class (+0x28; none,
   the first ONCC), the name (none: "ai_<slot>"), the team; AI2rInitializeCharacter 0x48a6b0's setup
   part: the script id (0 no AI), the default combat and melee, alert lull, job alert low, no alarm
   groups. The setup's weapon (+0x150) and scripts are empty in every level's AISA (not read). */
static Chr *create_setup(const uint8_t *r, const ObjcObject *flag)
{
    const void *oncc = tm_link(r + 0x28);
    if (!oncc) return NULL;
    int i = 0;
    while (i < CHARS_MAX && used[i]) i++;
    if (i == CHARS_MAX) return NULL;
    Chr *c = &slots[i];
    memset(c, 0, sizeof *c);
    if (!character_init(&c->c, oncc, flag->pos, flag->rot[1] * 0.017453293f)) return NULL;
    used[i] = true;
    memset(&actors[i], 0, sizeof actors[i]);
    if (r[0]) {
        memcpy(c->name, r, sizeof c->name - 1);
    } else {
        const char *fmt = exe_str(0x5509f0);     /* the "ai_" and slot format */
        size_t k = fmt ? strcspn(fmt, "%") : 0;
        if (k >= sizeof c->name - 4) k = 0;
        memcpy(c->name, fmt, k);
        snprintf(c->name + k, sizeof c->name - k, "%d", i);
    }
    c->script_id = (int16_t)(r[0x20] | r[0x21] << 8);
    c->team = (uint32_t)(r[0x26] | r[0x27] << 8);
    c->object = -1;
    c->c.id = (uint16_t)(i + 1);
    c->c.ai = true;
    ai_init(&c->ai);
    c->ai.name = c->name;
    uint32_t f = (uint32_t)(r[0x24] | r[0x25] << 8);
    if ((f & 0x400) || !c->script_id || (f & 1)) c->c.flags |= CHR_FLAG_BOSS;
    if (c->script_id && !(f & 1)) {
        c->ai.flags = AI_FLAG_KNOWLEDGE;
        c->c.flags |= CHR_FLAG_AI_MOVEMENT;
        c->ai.state = AI_STATE_IDLE;
        melee_start(&c->ai.melee, NULL, oncc);
        combat_default_settings(&c->ai);
        movement_new_mode(c);
    }
    c->ai.job = c->ai.state;
    c->ai.job_patrol = c->ai.patrol;
    c->ai.job_alert = 1;
    chr_hit_init(&c->c, 0);
    difficulty_health(c);
    activity_new_character(c);
    return c;
}

/* The level's rooms' graph (the original's, from the level's load: AKEV's BNVs). */
static void level_graph(void)
{
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("AKEV") && in->data) graph_for(in->data);
    }
}

int chars_load(void)
{
    chr_cheats_clear();     /* ONrCharacter_LevelBegin 0x4d87b0 */
    chars_unload();
    corpse_level_begin();   /* (0x4d9d50) */
    level_graph();      /* for the AIs' ways (the pursuit's distances) before any of them moves */
    obstacle_level_begin();     /* the rooms' obstacle layers made anew (their times are the last level's) */
    patrol_load();      /* the paths first: the patrol jobs start with their characters */
    melee_initialize();
    melee_load();
    combat_load();
    armed = false;
    objc_each("Character", add_object, NULL);
    int n = 0;
    for (int i = 0; i < n_objects; i++) {     /* 0x48a360: those present at the start are spawned */
        if (object_flags[i] & OBJC_CHAR_NOT_INITIAL) continue;
        object_flags[i] |= 0x20;
        if (create(i)) n++;
    }
    return n;
}

void chars_unload(void)
{
    ai_all_passive = false;     /* 0x48a257 */
    n_objects = 0;
    memset(used, 0, sizeof used);
    memset(actors, 0, sizeof actors);
    knowledge_reset();
}

int chars_slots(void) { return CHARS_MAX; }
Chr *chars_get(int i) { return i >= 0 && i < CHARS_MAX && used[i] ? &slots[i] : NULL; }

Chr *chars_player(void)
{
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i] && slots[i].player) return &slots[i];
    return NULL;
}

bool chars_in_use(const Chr *c) { return c && c >= slots && c < slots + CHARS_MAX && used[c - slots]; }

/* FUN_0048a5a0: by name (case matters), through the living characters. */
Chr *chars_find(const char *name)
{
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i] && !strcmp(slots[i].name, name)) return &slots[i];
    return NULL;
}

/* FUN_004bb880: by script id. */
static Chr *find_id(int32_t id)
{
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i] && slots[i].script_id == (int16_t)id) return &slots[i];
    return NULL;
}

/* The spawn test of 0x48a480 (the enumeration's callback): the first object of the name; spawned once
   unless it may respawn (0x10) or the spawn is forced. */
Chr *chars_spawn(const char *name, bool force)
{
    for (int i = 0; i < n_objects; i++) {
        if (strcmp(objc_char_name(&objects[i]), name)) continue;
        if (!force && (object_flags[i] & 0x20) && !(object_flags[i] & 0x10)) continue;
        object_flags[i] |= 0x20;
        return create(i);
    }
    return NULL;
}

/* ONrGameState_DeleteCharacter 0x4dc480: its weapons deleted (WPrSlot_DeleteAll 0x51b5f0: the hand's and
   the stowed), the slot free. */
void chars_delete(Chr *c)
{
    if (!c) return;
    for (int k = 0; k < 3; k++) {
        Weapon *w = chr_slot(&c->c, k);
        if (w) weapon_delete(w);
    }
    c->c.weapon = c->c.stowed[0] = c->c.stowed[1] = NULL;
    chr_anim_particles_stop(&c->c);
    neutral_deleted(c);         /* 0x48b3e0 */
    knowledge_forget(c);
    used[c - slots] = false;
    actors[c - slots].alive = false;
}

TriggerActor *chars_trigger_actors(void)
{
    for (int i = 0; i < CHARS_MAX; i++) {
        TriggerActor *a = &actors[i];
        a->alive = used[i];
        if (!used[i]) continue;
        a->player = slots[i].player;
        a->name = slots[i].name;
        a->team = slots[i].team;
        a->dead = (slots[i].c.flags & 1) != 0;
        memcpy(a->pos, slots[i].c.pos, sizeof a->pos);
        a->bottom = slots[i].c.bottom;
        a->top = slots[i].c.top;
    }
    return actors;
}

/* 0x48b060: an AI hurt in a calm state (the table 0x52a4d8; not a patrol ignoring the player) is
   startled (C+0x1f0 0x8000), and its pain cries for sure. */
static bool calm_hurt(Character *c)
{
    AiState *a = &((Chr *)c)->ai;
    if (a->state < 0 || a->state > 11 || !exe_data(0x52a4d8 + (uint32_t)a->state, 1)[0]) return false;
    if (a->state == AI_STATE_PATROL && a->ignore_player) return false;
    a->flags |= AI_FLAG_STARTLED;
    return true;
}

/* 0x48b480, for the AI's characters TakeDamage hurts: only a weapon's or a blow's damage is known (a
   fall's, poison's or the kill command's not). */
static void hurt(Character *c, Character *from, uint32_t damage, uint32_t kind)
{
    Chr *v = (Chr *)c;
    if (v->ai.flags & AI_FLAG_PASSIVE) return;
    v->ai.vocal_delay = 0;
    if (kind == CHR_DAMAGE_WEAPON) {
        if (v->ai.daze_timer) v->ai.daze_timer = 1;    /* a shot ends the daze */
    } else if (kind != CHR_DAMAGE_MELEE) return;
    /* an alarm run, hurt, fights on for the fight time */
    if (v->ai.alarm_marker >= 0 && v->ai.state != AI_STATE_ALARM) v->ai.alarm_fight_timer = v->ai.combat_set.alarm_fight;
    knowledge_hurt_by(v, (Chr *)from, damage, kind == CHR_DAMAGE_MELEE);
}

/* AI2rNotifyAttackLanded 0x48b700 (its report on a block let go for another attack than the one it was
   held for, the AI's debugging, left out) and 0x48b9d0. */
static void attack_landed(Character *attacker, Character *defender)
{
    Chr *d = (Chr *)defender;
    if (d->ai.state == AI_STATE_COMBAT && d->ai.melee.target == attacker && d->ai.melee.blocking)
        d->ai.melee.blocking = false;
    if (d->ai.daze_timer) d->ai.daze_timer = 1;
}

static void vocalize(Character *c, uint32_t type) { ai_vocalize((Chr *)c, type, false); }

/* 0x4998f0: the melee cookie's time, ten ticks longer a point the fighter deals, six shorter a point it
   takes. */
static void melee_score(Character *attacker, Character *victim, uint32_t damage)
{
    Chr *a = (Chr *)attacker, *v = (Chr *)victim;
    if (attacker->ai && a->ai.state == AI_STATE_COMBAT && a->ai.melee.cookie) a->ai.melee.cookie_until += damage * 10;
    if (victim->ai && v->ai.state == AI_STATE_COMBAT && v->ai.melee.cookie) v->ai.melee.cookie_until -= damage * 6;
}

/* AI2rNotifyKnockdown 0x48b530, for an AI's character knocked down from not lying: the melee cookie given
   back (0x4998a0, in combat), a new daze to come; playing a film on patrol, the film stopped (ONrFilm_Stop
   0x4f41f0) and, out of its job's state (C+0xf70 not C+0x200: the port's state not its job's, inferred),
   back to it (or idle under the job's alert); in the alarm state, its knockdown (0x48ea40). */
static void knocked_down(Character *c)
{
    AiState *a = &((Chr *)c)->ai;
    if (a->state == AI_STATE_COMBAT) a->melee.cookie = false;
    a->daze_started = false;
    if (film_playing(c) && a->state == AI_STATE_PATROL) {
        film_stop(c);
        if (a->state != a->job) ai_return_to_job(a);
    } else if (a->state == AI_STATE_ALARM) {
        alarm_knocked_down(a, c);
    }
}

/* AI2rTryToPathfindThrough 0x48b670: the character its movement ignores (AI2rMovement_ShouldIgnoreObstruction
   0x4a81e0: the one its path follows, C+0x14ac), its combat's target, or, talking (the neutral state), the
   player. */
static bool path_through(const Character *c, const Character *o)
{
    const Chr *a = (const Chr *)c;
    /* AI2rMovement_ShouldIgnoreObstruction 0x4a81e0 (+0x14ac; the port's follow is its follow case) */
    if (a->ai.ignore == o || a->ai.follow == o) return true;
    if (!c->ai) return false;
    if (a->ai.state == AI_STATE_COMBAT && a->ai.target && &a->ai.target->c == o) return true;
    return a->ai.state == AI_STATE_NEUTRAL && !o->ai;
}

/* ONrGameState_UpdateCharacters 0x4dce97..0x4dcff4, the dead (C+4 flag 2, then 4): the body settles
   (120 ticks on the ground, 0x78; fallen 330 ticks, 0x14a, or dying 900, 0x384: no corpse, flag
   0x80000), then the AI's weapons go (0x4e4ab0) and the physics (0x43aef0, flag 4). An AI's is then
   deleted once its time is out and it isn't seen (its detail none or the lowest, A+0x221c 6 or 0), its
   body left as a corpse (0x4ef340) unless flag 0x80000 or 0x400; while its shield or invisibility
   shows it stays. True if deleted. */
static bool dying_update(Chr *ch)
{
    Character *c = &ch->c;
    if (c->flags & 4) {
        if (c->invisible_shown || (c->flags & 0x40000)) return false;   /* (A+0x2800, A+0x2815) */
        if (ch->player || !c->ai) return false;
        if (c->lod_shown != 6 && c->lod_shown != 0) return false;
        if ((c->flags & 0x400) || ch->corpse_time) return false;
        if (!(c->flags & 0x80000)) corpse_add(c);
        chars_delete(ch);
        return true;
    }
    if (!(c->flags & 2)) return false;
    bool settled = false;
    if (c->air_ticks >= 330) {
        c->flags |= 0x80000;
    } else {
        if (c->air_ticks == 0 && c->no_ground_ticks == 0) settled = ++c->settled_ticks > 120;
        else c->settled_ticks = 0;
        if (++c->dying_ticks > 900) c->flags |= 0x80000;    /* "character took too long to die" */
        else if (!settled) return false;
    }
    if (c->ai) chr_drop_weapon(c);      /* (the stowed ones aren't dropped: deleted with the character) */
    c->flags |= 4;
    c->has_physics = false;
    return false;
}

/* the AI's movement told of what the character ran into (movement.h) */
static void movement_hit(Character *c, int kind, uintptr_t id, const float point[3], const float plane[4])
{
    Chr *ch = (Chr *)c;
    if (kind == CHR_HIT_WALL) movement_hit_wall(ch, (uint32_t)id, point, plane);
    else if (kind == CHR_HIT_OBJECT) movement_hit_object(ch, id, point, plane);
    else movement_hit_character(ch, (const Chr *)id, point, plane);
}

static void no_sidestep(Character *c, bool walk) { movement_no_sidestep((Chr *)c, walk); }

/* AI2rBlockFunction 0x48b870, every AI's block function (AI2rInitializeCharacter 0x48a6b0). */
static bool block(Character *c)
{
    return melee_block(&((Chr *)c)->ai);
}

/* AI2rMelee_NotifyAnimation 0x49fd10, for the AI's characters in combat. */
static void new_animation(Character *c)
{
    Chr *ch = (Chr *)c;
    /* NewAnimationHook's teleport (chr_anim.c): with the AI's movement, the path made anew (0x4ae3d0) */
    if (c->teleport && c->type == 0xdf) {
        ch->ai.path_made = false;
        ch->ai.local_seg = -1;
    }
    if (ch->ai.state != AI_STATE_COMBAT) return;
    combat_animation(&ch->ai, c);
    /* AI2rMelee_NotifyAnimation 0x49fd10, in the melee maneuver */
    if (ch->ai.combat.maneuver == COMBAT_MANEUVER_MELEE)
        melee_notify_animation(&ch->ai.melee, c->anim, c->type, c->left_type, chr_is_victim_type(c->type));
}

/* AI2rExecutor_TryingToThrow 0x498d50 */
static bool ai_throw(const Character *c, const void **anim, const void **victim, Character **target)
{
    const MeleeState *m = &((const Chr *)c)->ai.melee;
    if (!m->throwing) return false;
    *anim = m->throw_anim;
    *victim = m->throw_victim;
    *target = (Character *)m->throw_target;
    return true;
}

static void action_frame(Character *c) { combat_action(&((Chr *)c)->ai, c); }

/* ONrCharacter_Die_1_Animating_Hook 0x4eb550's scripts: the defeated one unless it ran already, then
   the death one, deferred with the character's name and run at once (0x4f2120). The scripts are the
   CHAR object's (AI2rInitializeCharacter 0x48a6b0 copies them); a character made otherwise has none. */
static void died(Character *c, Character *killer)
{
    Chr *ch = (Chr *)c;
    /* 0x48b370: killed by a fighter in melee (its maneuver 5), it may taunt over the body */
    Chr *by = (Chr *)killer;
    if (by && killer->ai && by->ai.state == AI_STATE_COMBAT && by->ai.combat.maneuver == COMBAT_MANEUVER_MELEE)
        by->ai.combat_taunt_ready = true;
    neutral_died(ch);
    /* C+0x1644: the body stays the class's time (ONCC+0xc8a) */
    const uint8_t *k = (const uint8_t *)c->oncc + 0xc8a;
    ch->corpse_time = (uint16_t)(k[0] | k[1] << 8);
    chr_throw_weapon(c);        /* (Die_1) the hand's weapon thrown (0x51b590) */
    /* (ONrCharacter_Die_1_Animating_Hook 0x4eb550) an AI's inventory: the clips, cells and hypos it drops, one each, a
       shield and an invisibility; then (unless C+4 0x4000000, not kept) what is left of its own shield
       and invisibility, and the LSI */
    if (c->ai) {
        for (unsigned n = 0; n < c->drop_ammo; n++) powerup_drop(c, POWERUP_AMMO, 1, false);
        for (unsigned n = 0; n < c->drop_cells; n++) powerup_drop(c, POWERUP_CELL, 1, false);
        for (unsigned n = 0; n < c->drop_hypos; n++) powerup_drop(c, POWERUP_HYPO, 1, false);
        if (c->drop_shield) powerup_drop(c, POWERUP_SHIELD, 100, false);
        if (c->drop_invisibility) powerup_drop(c, POWERUP_INVISIBILITY, 1800, false);
        if (c->shield) powerup_drop(c, POWERUP_SHIELD, c->shield, false);
        if (c->invisibility) powerup_drop(c, POWERUP_INVISIBILITY, c->invisibility, false);
        if (c->lsi) powerup_drop(c, POWERUP_LSI, 1, false);
    }
    if (ch->object < 0) return;
    const ObjcObject *o = &objects[ch->object];
    const char *defeated = objc_char_script(o, OBJC_SCRIPT_DEFEATED), *death = objc_char_script(o, OBJC_SCRIPT_DIE);
    if (defeated[0] && !(ch->script_flags & CHR_SCRIPTED_DEFEATED)) {
        trigger_defer(defeated, ch->name);
        ch->script_flags |= CHR_SCRIPTED_DEFEATED;
    }
    if (death[0]) {
        ch->script_flags |= CHR_SCRIPTED_DIED;
        trigger_defer(death, ch->name);
        trigger_flush();
    }
}

void chars_ai_scripts(Chr *ch)
{
    static const struct { uint32_t flag, done; int k; } due[] = {
        { AI_FLAG_ALARM_SCRIPT, CHR_SCRIPTED_ALARM, OBJC_SCRIPT_ALARM },
        { AI_FLAG_COMBAT_SCRIPT, CHR_SCRIPTED_COMBAT, OBJC_SCRIPT_COMBAT },
        { AI_FLAG_OUT_OF_AMMO, CHR_SCRIPTED_OUT_OF_AMMO, OBJC_SCRIPT_OUT_OF_AMMO },
        { AI_FLAG_NO_PATH, CHR_SCRIPTED_NO_PATH, OBJC_SCRIPT_NO_PATH },
    };
    if (ch->c.flags & 1) return;
    for (unsigned i = 0; i < sizeof due / sizeof due[0]; i++) {
        if (!(ch->ai.flags & due[i].flag)) continue;
        ch->ai.flags &= ~due[i].flag;
        const char *f = ch->object >= 0 ? objc_char_script(&objects[ch->object], due[i].k) : "";
        if (f[0] && !(ch->script_flags & due[i].done)) {
            trigger_defer(f, ch->name);     /* 0x5100b0 */
            ch->script_flags |= due[i].done;
        }
    }
}

/* TakeDamage's hurt and defeated scripts (0x4e65f0), deferred once each with the character's name. */
static void script(Character *c, int which)
{
    Chr *ch = (Chr *)c;
    if (ch->object < 0) return;
    const ObjcObject *o = &objects[ch->object];
    uint32_t bit = which == CHR_SCRIPT_HURT ? CHR_SCRIPTED_HURT : CHR_SCRIPTED_DEFEATED;
    const char *f = objc_char_script(o, which == CHR_SCRIPT_HURT ? OBJC_SCRIPT_HURT : OBJC_SCRIPT_DEFEATED);
    if (!f[0] || (ch->script_flags & bit)) return;
    trigger_defer(f, ch->name);
    ch->script_flags |= bit;
}

static void sound(Character *c, int type, const float pos[3], float radius, Character *target)
{
    knowledge_sound(type, pos, radius, (Chr *)c, (Chr *)target);
}

/* The rest of ONiCharacter_PlayerKeyFilter 0x4fa520, after the conversation's part: unarmed, or with fire
   gone down on an empty weapon with nothing to reload (C+8 bit 0), fire1 punches; fire2 kicks unless the
   weapon has an alternate fire (ONWC +0xcc 0x400); fire3 crouches. A weapon with ONWC +0xcc 0x1000 makes
   the player walk (C+4 0x8000000, cleared otherwise). The fire bits stay. */
static void player_key_filter(Character *c, CharacterInput *in)
{
    const uint8_t *wc = c->weapon ? c->weapon->cls : NULL;
    uint32_t wf = wc ? (uint32_t)wc[0xcc] | (uint32_t)wc[0xcd] << 8 | (uint32_t)wc[0xce] << 16 | (uint32_t)wc[0xcf] << 24 : 0;
    if (in->fire && (!wc || c->reload_wanted)) in->punch = true;
    if ((in->extra & CHR_KEY_ALT_FIRE) && (!wc || !(wf & 0x400) || c->reload_wanted)) in->kick = true;
    if (in->extra & CHR_KEY_FIRE3) in->crouch = true;
    if (wf & 0x1000) {
        c->flags |= 0x8000000u;
        in->walk = true;
    } else {
        c->flags &= ~0x8000000u;
    }
}

void chars_update(const CharacterInput *player_in, const void *env)
{
    activity_install();
    chr_hurt_hook = hurt;
    chr_vocalize_hook = vocalize;
    chr_melee_score_hook = melee_score;
    chr_attack_landed_hook = attack_landed;
    chr_die_hook = died;
    chr_script_hook = script;
    chr_animation_hook = new_animation;
    chr_sound_hook = sound;
    character_action_frame_hook = action_frame;
    chr_block_hook = block;
    chr_calm_hurt_hook = calm_hurt;
    chr_knockdown_hook = knocked_down;
    chr_path_through_hook = path_through;
    chr_movement_hit_hook = movement_hit;
    character_sidestep_hook = no_sidestep;
    throw_install();
    throw_ai_hook = ai_throw;
    weapons_install();
    if (!ai_all_passive) knowledge_update(env);     /* AI2rKnowledge_Update (AI2rUpdateAI 0x489d64), before the characters */
    chr_physics_later = true;
    for (int i = 0; i < CHARS_MAX; i++) {
        if (!used[i]) continue;
        /* ONrGameState_UpdateCharacters 0x4dccf0: a body's time runs out; an AI's (C+0x1e8 not 0) is
           deleted (ONrGameState_DeleteCharacter 0x4dc480; only mad_bomber has a time, the other classes 0) */
        if (slots[i].corpse_time && --slots[i].corpse_time == 0 && !slots[i].player) {
            chars_delete(&slots[i]);
            continue;
        }
        if (dying_update(&slots[i])) continue;
        /* the frame (DoCharacterFrame 0x4ddba0) is the active ones' (0x4fc9b0's list); an inactive AI's
           own update moves it by its stub */
        if (!slots[i].c.inactive) speech_update(&slots[i]);     /* ONrSpeech_Update 0x516a60, first in the frame */
        if (slots[i].player && (slots[i].c.flags & CHR_FLAG_AI_MOVEMENT)) ai_control_update(&slots[i].ai, &slots[i].c, env);
        else if (slots[i].player) {
            CharacterInput in = *player_in;
            neutral_filter_input(&slots[i], &in);     /* 0x4fa520, the player's keys */
            player_key_filter(&slots[i].c, &in);
            character_update(&slots[i].c, &in, env);
        } else {
            ai_update(&slots[i].ai, &slots[i].c, env);
        }
    }
    Character *list[CHARS_MAX];
    int n = 0;
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i] && !slots[i].c.inactive) list[n++] = &slots[i].c;
    chr_collide_all(list, n);
    throw_update(list, n);
    /* 0x4dda33: each one's room from its location (C+0x184, C+0x188), before the physics */
    for (int i = 0; i < n; i++) {
        float loc[3];
        chr_location(list[i], loc);
        list[i]->room = graph_room_at(loc);
    }
    /* OBiPhysics_Update 0x43af50: every context, after the frames and the contacts */
    chr_physics_later = false;
    for (int i = 0; i < n; i++)
        if (list[i]->phys_pending) list[i]->phys_pending = false, physics_update(list[i], env);
    activity_update();      /* the end of ONrGameState_UpdateCharacters 0x4dccf0 */
}

/* ---- the commands ---- */

static int32_t num(const SlValue *v) { return v->type == SL_FLOAT ? (int32_t)v->f : v->i; }

/* The character argument of the chr_ and ai2_ commands: an int is a script id; a string is a script id
   if it reads as a number (FUN_00426850), else a name. */
static Chr *arg_char(const SlValue *v)
{
    if (v->type == SL_INT || v->type == SL_FLOAT) return find_id(num(v));
    if (v->type != SL_STRING) return NULL;
    char *end;
    long id = strtol(v->s, &end, 10);
    if (*v->s && !*end) return find_id((int32_t)id);
    return chars_find(v->s);
}

/* chr_vocalize char type (0x4b9e00..): the character (the player when not found, after the AI's report)
   cries the vocalization by its name (0x52a4e4, 16 bytes each: taunt, alert, startle, checkbody,
   pursue, cower, superpunch, superkick, super3, super4), forced (AI2rVocalize 0x48ba60). */
static bool cmd_vocalize(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[1].type != SL_STRING || !v[1].s) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) ch = chars_player();
    if (!ch) return true;
    for (uint32_t i = 0; i < 10; i++)
        if (!strcmp(exe_str(0x52a4e4 + 16 * i), v[1].s)) {
            ai_vocalize(ch, i, true);   /* (refused: the AI's report) */
            return true;
        }
    return true;    /* "unknown vocalization type" and the list */
}

/* chr_create id [start] (AI2iScript_ChrCreate 0x4b7710): a character from the level's setup of that
   script id at the setup's flag, if the flag (15 above it) is in a room. The second argument isn't
   read. */
static bool cmd_chr_create(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1) return true;
    const uint8_t *s = find_setup((int16_t)num(&v[0]));
    if (!s) return true;            /* "no character setup with script id %d" and the ids there are */
    ObjcObject flag;
    if (!objc_flag_find((uint16_t)(s[0x22] | s[0x23] << 8), &flag)) return true;     /* "flag %d not found" */
    float p[3] = { flag.pos[0], flag.pos[1] + 15.0f, flag.pos[2] };     /* 0x529d84 */
    level_graph();
    if (graph_room_at(p) < 0) return true;      /* "flag was not inside a BNV" */
    create_setup(s, &flag);                       /* "failed to create a new character" */
    return true;
}

/* ai2_takecontrol 0|1 (AI2iScript_TakeControl 0x4b8940): the player moved by its AI (the movement
   commands then take it) or by the keys again. */
static bool cmd_takecontrol(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *p = chars_player();
    if (!p || c < 1) return true;
    if (num(&v[0])) ai_take_control(&p->ai, &p->c);
    else p->c.flags &= ~(uint32_t)CHR_FLAG_AI_MOVEMENT;
    return true;
}

/* trigvolume_kill id (AI2iScript_TriggerVolume_DeleteCharacters 0x4ba420, 0x4dcc70 for each volume of
   the id) deletes the AI characters in it; trigvolume_corpse id (0x4ba450, 0x4dcbd0) the bodies in it,
   the dead characters there at the next tick (C+0x1644 1). The original's test (0x4cc680) is the
   volume's own, with the team mask; here trigger_contains, without it. The second also empties the
   characters' corpses in the volume (corpse.h). */
static bool volume_delete(int c, const SlValue *v, bool corpses)
{
    if (c < 1) return true;
    uint32_t id = (uint32_t)num(&v[0]);
    for (int k = 0; k < trigger_count(); k++) {
        if (trigger_id(k) != id) continue;
        if (corpses) corpse_clear_volume(k);
        Chr *in[CHARS_MAX];
        int n = 0;
        for (int i = 0; i < CHARS_MAX; i++)
            if (used[i] && trigger_contains(k, slots[i].c.pos, slots[i].c.bottom, slots[i].c.top)) in[n++] = &slots[i];
        for (int i = 0; i < n; i++) {
            if (corpses) {
                if (in[i]->c.flags & 1) in[i]->corpse_time = 1;
            } else if (!in[i]->player) {
                chars_delete(in[i]);
            }
        }
    }
    return true;
}

static bool cmd_trigvolume_kill(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return volume_delete(c, v, false);
}

static bool cmd_trigvolume_corpse(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return volume_delete(c, v, true);
}

/* ai2_lookatme [char] (0x4b49e0, 0x48bce0): the AI (an AI, C+0x1e8 2), or every AI, looks at the
   player (AI2rMovement_LookAtCharacter; with no player, stops looking). */
static bool cmd_lookatme(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *p = chars_player(), *one = c >= 1 ? arg_char(&v[0]) : NULL;
    if (c >= 1 && !one) return true;
    for (int i = 0; i < CHARS_MAX; i++) {
        Chr *ch = &slots[i];
        if (!used[i] || (one && ch != one) || ch->player) continue;
        ch->ai.aim.look = p ? 3 : 0;
        ch->ai.aim.look_char = p ? &p->c : NULL;
    }
    return true;
}

/* ai2_followme [char] (AI2iScript_Debug_FollowMe 0x4b4a60, AI2rDebug_FollowMe 0x48bd40): the AI (an AI,
   C+0x1e8 2), or every living AI, follows the player (AI2rPath_FollowCharacter); nothing without one. */
static bool cmd_followme(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *p = chars_player(), *one = c >= 1 ? arg_char(&v[0]) : NULL;
    if ((c >= 1 && !one) || !p) return true;     /* "ai2_followme": no such AI */
    for (int i = 0; i < CHARS_MAX; i++) {
        Chr *ch = &slots[i];
        if (!used[i] || (one && ch != one) || !ch->c.ai || (ch->c.flags & 1)) continue;
        ai_follow(&ch->ai, &p->c);
    }
    return true;
}

/* ai2_lookatchar a b (AI2iScript_LookAtChar 0x4b5550): a (moved by its AI: C+4 0x400000, the player
   only with ai2_takecontrol) turns where it stands to face b (AI2rPath_GoToPoint, within 3) and looks
   at b. */
static bool cmd_lookatchar(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *a = arg_char(&v[0]), *b = arg_char(&v[1]);
    if (!a || !b) return true;
    if (!(a->c.flags & CHR_FLAG_AI_MOVEMENT)) return true;  /* "cannot move player..." */
    float f = atan2f(b->c.pos[0] - a->c.pos[0], b->c.pos[2] - a->c.pos[2]);
    if (f < 0.0f) f += 6.2831855f;
    ai_move_near(&a->ai, a->c.pos, 3.0f, true, f);
    a->ai.aim.look = 3;
    a->ai.aim.look_char = &b->c;
    return true;
}

/* chr_boss_shield char (AI2iScript_BossShield 0x4ba160): C+4 0x40000000, the boss's shield: its hits
   take the shield's material (ONrCharacter_GetMaterialType 0x4e6200; the impact effects are
   stand-ins). chr_shadow char 0|1 (AI2iScript_ChrShadow 0x4ba0c0): C+4 0x1000000 no shadow (0 or
   less). chr_super char amount (0x4ba640): the super value (C+0x1664, the Daodan glow's). */
static bool cmd_boss_shield(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (ch) ch->c.flags |= 0x40000000u;
    return true;
}

static bool cmd_shadow(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    if (c >= 2 && num(&v[1]) > 0) ch->c.flags &= ~0x1000000u;
    else ch->c.flags |= 0x1000000u;
    return true;
}

static bool cmd_super(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (ch) ch->c.super = v[1].type == SL_FLOAT ? v[1].f : (float)v[1].i;
    return true;
}

/* ai2_spawn name [force] (AI2iScript_Spawn 0x4b4780) */
static bool cmd_spawn(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1 || v[0].type != SL_STRING) return true;
    bool force = c >= 2 && v[1].type == SL_STRING && !strcmp(v[1].s, "force");
    chars_spawn(v[0].s, force);
    return true;
}

/* ai2_movetoflag char flag [setfacing] (AI2iScript_MoveToFlag 0x4b53e0): an AI character (not the
   player, who needs ai2_takecontrol) walks to the flag (AI2rPath_GoToPoint 0x4ae480, within 3), then
   faces the flag's way if asked (else the facing 1000, none). */
static bool cmd_movetoflag(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    ObjcObject flag;
    if (!ch || !objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;   /* "flag %d does not exist" */
    if (ch->player && !(ch->c.flags & CHR_FLAG_AI_MOVEMENT)) return true;   /* "cannot move player character without first using ai2_takecontrol" */
    bool face = c >= 3 && v[2].type == SL_STRING && !strcmp(v[2].s, "setfacing");
    ai_move_to(&ch->ai, flag.pos, face, flag.rot[1] * 0.017453293f);
    return true;
}

/* ai2_dopath char path (AI2iScript_SetState_Patrol 0x4b4c70): an AI character (C+0x1e8 2) starts the
   patrol path, by its name (AI2rPatrol_FindByName), as its job (C+0x1f8 3). A passive one (C+0x1f0
   bit 2) refuses: the port has no passive yet. */
static bool cmd_dopath(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[1].type != SL_STRING) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || ch->player) return true;
    int path = patrol_find(v[1].s);
    if (path >= 0) {
        patrol_start(&ch->ai, path);
        ch->ai.state = AI_STATE_PATROL;
    }   /* else "unknown path" */
    return true;
}

/* ai2_passive char 0|1 (AI2iScript_Passive 0x4b4be0, AI2rPassive 0x48b160): a passive AI does nothing, sees
   nothing and is no one's enemy until made active again. */
static bool cmd_passive(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || ch->player) return true;
    if (num(&v[1])) ch->ai.flags |= AI_FLAG_PASSIVE;
    else ch->ai.flags &= ~(uint32_t)AI_FLAG_PASSIVE;
    return true;
}

/* ai2_neutralbehavior char name (AI2iScript_SetNeutralBehavior 0x4b87b0): an AI character's Neutral, by its
   name ("none": none), the one it has kept ("can't find behavior" for an unknown one). */
static bool cmd_neutralbehavior(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[1].type != SL_STRING) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || ch->player) return true;
    int id = -1;
    if (strcmp(v[1].s, "none") != 0 && (id = neutral_id_named(v[1].s)) < 0) return true;
    neutral_change_behavior(ch, id, false);
    return true;
}

/* chr_changeteam char team (AI2iScript_ChangeTeam 0x4b8b10): the team by its name in the exe's list of
   eight (0x54b38c, case matters), else "unknown team". Only C+0x12 changes. */
enum { TEAM_NAMES_VA = 0x54b38c, TEAMS = 8 };
static bool cmd_changeteam(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2 || v[1].type != SL_STRING) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    for (uint32_t k = 0; k < TEAMS; k++) {
        const char *name = exe_str(exe_u32(TEAM_NAMES_VA + 4 * k));
        if (name && !strcmp(name, v[1].s)) {
            ch->team = k;
            return true;
        }
    }
    return true;
}

/* chr_nocollision char 0|1 (AI2iScript_NoCollision 0x4b6a90): C+0x04 flag 0x20000, then 0x4ed080 sets the
   physics context's flags 0x44 from it: no walls, no floor (physics.c), no characters (the port's physics
   has no character-to-character collision yet). */
static bool cmd_nocollision(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    if (num(&v[1])) ch->c.flags |= 0x20000u;
    else ch->c.flags &= ~0x20000u;
    return true;
}

/* chr_forceholster char holster [force] (AI2iScript_ForceHolster 0x4b86f0): holster above 0 puts the
   character's weapon away (0x4eff60), else draws the stowed one (0x4effa0; the third parameter alike). */
static bool cmd_forceholster(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;       /* "chr_forceholster" (0x54805c): no such character */
    if (c >= 2 && num(&v[1]) > 0) chr_force_holster(&ch->c);
    else chr_force_draw(&ch->c);
    return true;
}

/* ai2_doalarm char [console] (alarm.c) */
static bool cmd_doalarm(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1) return true;
    alarm_script(arg_char(&v[0]), c >= 2 ? (uint16_t)num(&v[1]) : 0);
    return true;
}

/* ai2_tripalarm alarm [char] (0x4b5dd0): the alarm of that group (1..100) set off by the AI given, else
   the player (0x49ac30, alarm.c). */
static bool cmd_tripalarm(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1) return true;
    int32_t id = num(&v[0]);
    Chr *who = c >= 2 && (uint32_t)(id - 1) < 100 ? arg_char(&v[1]) : NULL;
    if (who && who->player) who = NULL;
    if (!who) who = chars_player();
    alarm_notify((unsigned)id, who);
    return true;
}

/* ai2_setalert char level (AI2iScript_Debug_SetAlert 0x4b60a0): by the level's name (the five at
   0x52a5d4, 16 bytes apart: lull to combat), raised (AI2rAlert_UpgradeStatus) or lowered, forced
   (AI2rAlert_DowngradeStatus). */
static bool cmd_setalert(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player || v[1].type != SL_STRING) return true;     /* "ai2_setalert" */
    for (int k = 0; k < 5; k++) {
        const char *name = exe_str(0x52a5d4 + 16u * (uint32_t)k);
        if (!name || strcmp(name, v[1].s)) continue;
        if (k <= ch->ai.alert) knowledge_downgrade_alert(ch, k, true);
        else knowledge_upgrade_alert(ch, k);
        return true;
    }
    return true;        /* "could not find alert status" */
}

/* chr_peace char (AI2iScript_Leave_FightMode 0x4ba370 -> 0x4eb130): out of fight mode (the 0x200
   variant, the fight timer A+0x1ca2). */
static bool cmd_peace(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    ch->c.varient &= (uint16_t)~0x200;
    ch->c.fight_timer = 0;
    return true;
}

/* chr_facetoflag char flag (AI2iScript_FaceToFlag 0x4b68c0): the facing and the desired facing the
   flag's (its y rotation), C+0x174 cleared. */
static bool cmd_facetoflag(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    ObjcObject flag;
    if (!ch || !objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;   /* "flag %d does not exist" */
    float f = flag.rot[1] * 0.017453292f;
    while (f < 0.0f) f += 6.2831855f;
    while (f >= 6.2831855f) f -= 6.2831855f;
    ch->c.facing = ch->c.desired_facing = f;
    return true;
}

/* chr_wait_health char hp (AI2iScript_WaitForHealth 0x4b7ac0): waits while its hit points (C+0x38) are
   above hp. */
static bool cmd_wait_health(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    return !(num(&v[1]) < (int32_t)ch->c.hit_points);
}

/* ai2_makedeaf/ai2_makeblind char 0|1 (AI2iScript_MakeDeaf 0x4b90e0 and its neighbour): C+0x1f0 bits
   0x1000, 0x800. */
static bool set_ai_flag(int c, const SlValue *v, uint32_t bit)
{
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player) return true;
    if (num(&v[1])) ch->ai.flags |= bit;
    else ch->ai.flags &= ~bit;
    return true;
}
static bool cmd_makedeaf(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return set_ai_flag(c, v, AI_FLAG_DEAF); }
static bool cmd_makeblind(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return set_ai_flag(c, v, AI_FLAG_BLIND); }

/* ai2_setjobstate char (AI2iScript_MakeCurrentStateJob 0x4b4b00): an AI character takes its state (C+0x1f8, the
   state block at C+0xf70) as its job (C+0x1f4, C+0x200), what it goes back to after combat. */
static bool cmd_setjobstate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 1) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || ch->player) return true;     /* "ai2_setjobstate" error */
    ch->ai.job = ch->ai.state;
    ch->ai.job_patrol = ch->ai.patrol;
    ch->ai.job_point = ch->ai.point;
    ch->ai.job_pos_set = false;     /* the job's place, again */
    return true;
}

/* chr_lock_active char (AI2iScript_LockActive 0x4b94b0): an AI character made active
   (ONrForceActiveCharacter 0x4f11b0) and kept so, C+0x04 flag 0x100000. */
static bool cmd_lock_active(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player) return true;     /* "chr_lock_active: no such AI" */
    chr_make_active(&ch->c);
    ch->c.flags |= 0x100000u;
    return true;
}

/* ai2_active char (AI2iScript_MakeActive 0x4b89c0) and ai2_inactive char (AI2iScript_MakeInactive
   0x4b8a70): an AI character (C+0x1e8 2) made active from its stub (ONrCharacter_MakeActive 0x4f10f0)
   or inactive (ONrCharacter_MakeInactive 0x4f1120). */
static bool cmd_ai_active(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player || !ch->c.ai) return true;    /* the AI's error: no such AI */
    if (u) activity_make_inactive(ch);
    else if (ch->c.inactive && chr_activate_hook) chr_activate_hook(&ch->c, true);
    return true;
}

/* chr_death_lock char 0|1 (AI2iScript_ChrDeathLock 0x4b9f80): C+0x04 flag 0x400, which only
   ONrCharacter_Die_4_Gone_Hook 0x4ebb60 reads: a locked body doesn't go (dying_update). */
static bool set_flag(int c, const SlValue *v, uint32_t bit);
static bool cmd_death_lock(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return set_flag(c, v, 0x400u);
}

/* chr_unkillable char 0|1 (0x4b9720: C+0x04 flag 0x20, a hit leaves one hit point) and chr_invincible
   char 0|1 (0x4b9680: 0x2000000, no damage). */
static bool set_flag(int c, const SlValue *v, uint32_t bit)
{
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    if (num(&v[1]) > 0) ch->c.flags |= bit;
    else ch->c.flags &= ~bit;
    return true;
}
static bool cmd_unkillable(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return set_flag(c, v, 0x20u); }
static bool cmd_invincible(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return set_flag(c, v, 0x2000000u); }

/* ai2_makeignoreplayer char 0|1 (AI2iScript_MakeIgnorePlayer 0x4b9190): C+0x1f0 bit 0x2000, the AI
   doesn't see the player. */
static bool cmd_make_ignore_player(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || ch->player) return true;
    if (num(&v[1]) > 0) ch->ai.flags |= AI_FLAG_IGNORE_PLAYER;
    else ch->ai.flags &= ~(uint32_t)AI_FLAG_IGNORE_PLAYER;
    return true;
}

/* ai2_attack char target (AI2iScript_Attack 0x4b4fd0): an AI character told of the target, seen and
   hostile (AI2rKnowledge_CreateMagicAwareness 0x49ac00: a pending contact of type 6, strength 4, at
   its place, made hostile). */
static bool cmd_attack(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]), *target = arg_char(&v[1]);
    if (!ch || ch->player || !target) return true;     /* "ai2_attack", "ai2_attack: target" */
    knowledge_post(ch, KNOW_SEEN_CENTRAL, KNOW_DEFINITE, target->c.pos, target, 0, false, true);
    return true;
}

/* ai2_makeaware char target (AI2iScript_MakeAware 0x4b4ed0): as ai2_attack, without the hostility. */
static bool cmd_make_aware(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]), *target = arg_char(&v[1]);
    if (!ch || ch->player || !target) return true;
    knowledge_post(ch, KNOW_SEEN_CENTRAL, KNOW_DEFINITE, target->c.pos, target, 0, false, false);
    return true;
}

/* ai2_forget [char [target]] (0x4b5fb0, 0x48be30): the AI's characters (one, or all), alive and in use,
   forget their contacts (of the target, or all). */
static bool cmd_forget(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *one = c >= 1 ? arg_char(&v[0]) : NULL, *target = c >= 2 ? arg_char(&v[1]) : NULL;
    for (int i = 0; i < CHARS_MAX; i++) {
        Chr *ch = used[i] ? &slots[i] : NULL;
        if (!ch || ch->player || (ch->c.flags & 1) || (one && ch != one)) continue;
        knowledge_forget_contacts(ch, target);
    }
    return true;
}

/* ai2_kill [except] [char] (AI2iScript_Debug_Smite 0x4b5e60, AI2rSmite 0x48bda0): the AI's living
   characters killed (damage 0x6000000): all, all but the one named after "except", or the one. */
static bool cmd_kill(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    bool except = c >= 1 && v[0].type == SL_STRING && !strcmp(v[0].s, "except");
    Chr *one = except ? (c >= 2 ? arg_char(&v[1]) : NULL) : (c >= 1 ? arg_char(&v[0]) : NULL);
    if (!except && c >= 1 && !one) return true;
    for (int i = 0; i < CHARS_MAX; i++) {
        Chr *ch = used[i] ? &slots[i] : NULL;
        if (!ch || ch->player || (ch->c.flags & 1)) continue;
        if (except ? ch == one : (one && ch != one)) continue;
        chr_take_damage(&ch->c, ch->c.hit_points, 0.0f, NULL, 0, NULL, CHR_DAMAGE_KILL);
    }
    return true;
}

/* ONrCharacter_SetHitPoints 0x4eb220: the hit points set; 0 kills a living character outright
   (Die_1_Animating_Hook: no damage, so invincibility doesn't stop it); a dead one given hit points comes
   back: on the living list again (the port's are the flags), a physics context if it has none
   (0x4dc2b0), the dead, dying and gone bits (C+0x04 0x1..0x8) cleared. Not kept: the physics context's
   flags (+0x128) set again. */
static void set_hit_points(Chr *ch, uint32_t hp)
{
    Character *c = &ch->c;
    c->hit_points = hp;
    if (!hp) {
        if (!(c->flags & 1)) {
            c->hit_points = 0;
            chr_die(c, NULL);
        }
        return;
    }
    if (!(c->flags & 1)) return;
    if (!c->has_physics) physics_start(c);
    c->flags &= ~0xfu;
}

/* chr_set_health char hp (AI2iScript_SetHealth 0x4b7a30): set_hit_points; a negative is 0. */
static bool cmd_set_health(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;       /* "chr_set_health" */
    int32_t hp = num(&v[1]);
    set_hit_points(ch, hp <= 0 ? 0u : (uint32_t)hp);
    return true;
}

/* ONrCharacter_Poison 0x4f21e0: a call within 30 ticks of the last counts on (else from 0); at initial
   counted, the damage, then again every interval. */
static void poison(Chr *ch, uint32_t damage, int32_t interval, int32_t initial)
{
    uint32_t now = knowledge_now();
    if (ch->c.poison_last + 30 < now) ch->c.poison_count = 0;
    ch->c.poison_last = now;
    if (++ch->c.poison_count >= initial) {
        chr_take_damage(&ch->c, damage, 0.0f, NULL, 0, NULL, 0);
        ch->c.poison_count = initial - interval;
    }
}

/* chr_poison char damage interval [initial] (0x4ba480): the initial interval is the interval unless
   given. */
static bool cmd_poison(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 3 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;       /* "chr_poison" */
    poison(ch, (uint32_t)num(&v[1]), num(&v[2]), c >= 4 ? num(&v[3]) : num(&v[2]));
    return true;
}

/* chr_is_player char -> bool (AI2iScript_IsPlayer 0x4b8db0): whether it is the player (G+0xac); false
   with no such character. */
static bool cmd_is_player(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    r->type = SL_BOOL;
    r->i = ch && ch->player;     /* (none: "chr_is_player" reported) */
    return true;
}

/* chr_unstoppable char on (AI2iScript_Unstoppable 0x4b95e0): C+0x04 bit 0x100 (no knockback, no
   knockdown); chr_weapon_immune char (AI2iScript_WeaponImmune 0x4ba1f0): bit 0x80000000, set only. */
static bool cmd_unstoppable(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    if (c >= 2 && num(&v[1]) > 0) ch->c.flags |= 0x100;
    else ch->c.flags &= ~0x100u;
    return true;
}

static bool cmd_weapon_immune(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (ch) ch->c.flags |= 0x80000000u;
    return true;
}

/* ai2_allpassive on (0x4b4bc0): AI2rAllPassive. */
static bool cmd_all_passive(SlThread *t, int c, const SlValue *v, SlValue *r, void *u);

/* ai2_setmovementmode char mode (AI2iScript_Debug_SetMovementMode 0x4b52b0): the movement's mode
   (AI2rMovement_ChangeMovementMode 0x4a8200, +0x14a4) by its name (the exe's 8 names at 0x52adf8, 16
   bytes each: by alert level, stop, crouch, creep, walk without aiming, walk, run without aiming, run). */
static bool cmd_set_movement_mode(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player || v[1].type != SL_STRING) return true;
    for (int k = 0; k < 8; k++) {
        const char *name = exe_str(0x52adf8 + 16u * (uint32_t)k);
        if (name && !strcmp(v[1].s, name)) ai_set_movement_mode(&ch->ai, k);   /* else "unknown movement mode" */
    }
    return true;
}

/* chr_delete char (AI2iScript_ChrDelete 0x4b7860) */
static bool cmd_delete(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c >= 1) chars_delete(arg_char(&v[0]));
    return true;
}

/* playback char film [fromhere | interp [ticks]] (AI2iScript_Playback_Internal 0x4b6b50): the film
   (film.h) played on the character, from the film's start, from where the character is, or moved to the
   start over ticks (30). The character's environment animation (the physics context's +0x130..0x13c)
   stops first; an AI patrolling (C+0x1e8 2, C+0x1f8 3) finishes its patrol (AI2rPatrol_Finish 0x4b0420)
   when there is no such film or it doesn't start. */
static bool cmd_playback(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || v[1].type != SL_STRING) return true;     /* "character %s cannot be made active" */
    chr_make_active(&ch->c);
    if (chr_envanim_playing(&ch->c)) chr_envanim_start(&ch->c, NULL, false);
    const void *film = tm_instance_get(TM_TAG("FILM"), v[1].s);
    if (!film) {                                        /* "can not find any film named %s" */
        if (!ch->player && ch->ai.state == AI_STATE_PATROL) patrol_finish(&ch->ai);
        return true;
    }
    int mode = FILM_START_HERE_PLACED;
    uint32_t ticks = 0;
    if (c > 2 && v[2].type == SL_STRING) {
        if (!strcmp(v[2].s, "fromhere")) mode = FILM_START_FROM_HERE;
        else if (!strcmp(v[2].s, "interp")) mode = FILM_START_INTERP, ticks = c > 3 ? (uint32_t)num(&v[3]) : 30;
    }
    if (!film_start(&ch->c, film, mode, ticks)) {
        if (!ch->player && ch->ai.state == AI_STATE_PATROL) patrol_finish(&ch->ai);
        return true;
    }
    /* playback_block (0x4b6d80): the script sleeps the film's length (FILM +0x1c) */
    if (u) sl_thread_sleep(t, (uint32_t)(((const uint8_t *)film)[0x1c] | ((const uint8_t *)film)[0x1d] << 8 |
                                         ((const uint8_t *)film)[0x1e] << 16 | (uint32_t)((const uint8_t *)film)[0x1f] << 24));
    return true;
}

/* chr_full_health char (0x4b7980): the hit points to the most (set_hit_points with C+0x3c), which
   brings a dead one back. */
static void set_hit_points(Chr *ch, uint32_t hp);
static bool cmd_full_health(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    set_hit_points(ch, ch->c.max_hit_points);
    return true;
}

/* AI2rAllPassive 0x48b0d0: every AI-run character (+0x1e8 2) made passive (+0x1f0 bit 2: its state left,
   its path halted) or not; going back, each one's last action (C+0x1e0) is now. */
static void all_passive(bool on);

static bool cmd_all_passive(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    all_passive(c >= 1 && num(&v[0]) != 0);
    return true;
}

/* AI2rAllPassive 0x48b0d0: the flag (0x5ec0c4); off, every character's last action now (C+0x1e0, the
   idle's wait from here); each AI's passive flag set (and the state left, the executor and the path
   stopped: ai_update's passive halt) or cleared. */
static void all_passive(bool on)
{
    ai_all_passive = on;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch) continue;
        if (!on) ch->c.last_action = game_time();
        if (ch->player) continue;
        if (on) ch->ai.flags |= AI_FLAG_PASSIVE;
        else ch->ai.flags &= ~(uint32_t)AI_FLAG_PASSIVE;
    }
}

/* begin_cutscene [weapon | jello | ai | invis | nosync | keepparticles | animation | nojump]...
   (0x4bca90, ONrGameState_BeginCutscene 0x4fd070): the letterbox on; the player's sprint stopped
   (0x4eb130), stood (ForceStand) unless "animation", her jump's motion across stopped with "nojump"; her
   weapon holstered unless "weapon" (0x4eff60); her invisibility gone unless
   "invis"; the AI passive unless "ai"; the camera's jello off unless "jello"; the input off; the sync on
   unless "nosync"; the dangerous particles removed (0x458310) unless "keepparticles". The sound's switch
   on (OSrSetEnabled 0x514e20: the master volume restored if it was off) is a no-op: it starts on
   (0x5134ec) and nothing turns it off. */
static bool cmd_begin_cutscene(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    static const char *const names[] = { "jello", "weapon", "invis", "ai", "nosync", "keepparticles", "animation", "nojump" };
    unsigned flags = 0;
    for (int i = 0; i < c; i++) {
        if (v[i].type != SL_STRING) continue;   /* "begin_cutscene: invalid parameter" */
        for (unsigned k = 0; k < 8; k++) if (!strcmp(v[i].s, names[k])) flags |= 1u << k;
    }
    ui_set_letterbox(true);
    Chr *p = chars_player();
    if (p) {
        p->c.varient &= 0xfdff;         /* 0x4eb130: the sprint's variant off */
        if (!(flags & 0x40)) {
            chr_force_stand(&p->c);
            /* A+0x1b14..0x1b1c set to a vector constant (0x5298b8): not mapped */
        }
        if ((flags & 0x80) && p->c.air_ticks) p->c.air_velocity[0] = p->c.air_velocity[2] = 0.0f;
        if (!(flags & 2)) chr_force_holster(&p->c);
        if (!(flags & 4)) p->c.invisibility = 0;
    }
    if (!(flags & 8)) all_passive(true);
    if (!(flags & 1)) camera_enable_jello(false);
    if (!(flags & 0x20)) p3_remove_dangerous();
    ui_set_input(false);
    ui_set_cutscene(true, !(flags & 0x10));
    return true;
}

/* end_cutscene (0x4bcbb0, ONrGameState_EndCutscene 0x4fd1a0): the input back, the jello on, the AI out
   of passive, the letterbox off, the console's subtitles gone (0x431d50), the cutscene over, the
   player's holstered weapon drawn again (0x4effa0) if her hand is empty. An active skip (G+0x40) would
   turn to its fade's last state (4), but nothing in v1.0 starts one (no writer of state 1: inferred
   dead); the sound's switch is a no-op, as in begin_cutscene. */
static bool cmd_end_cutscene(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    ui_set_input(true);
    camera_enable_jello(true);
    all_passive(false);
    ui_set_letterbox(false);
    ui_console_clear_subtitles();
    ui_set_cutscene(false, false);
    Chr *p = chars_player();
    if (p && !p->c.weapon) chr_force_draw(&p->c);
    return true;
}

/* chr_teleport char flag (AI2iScript_Teleport 0x4b69b0): the character to the flag's place (0x4e9210:
   its position and the ones kept with it; the facing stays). */
static bool cmd_teleport(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    ObjcObject flag;
    if (!ch || !objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;   /* "flag %d does not exist" */
    physics_place(&ch->c, flag.pos);
    return true;
}

/* chr_animate char anim [frames [interpolation]] (AI2iScript_Animate_Internal 0x4b6e10): the animation
   (0x4eb340) from the character's from state, interpolated if asked; the character held in it (A+0x1c90)
   for frames ticks, its length for -1, not at all for 0 or none. The block form waits while the
   character is held from before; it doesn't wait for the animation it starts. */
static bool animate(int c, const SlValue *v, bool block)
{
    if (c < 2 || v[1].type != SL_STRING) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    chr_make_active(&ch->c);     /* 0x4f11b0 */
    if (block && ch->c.hold > 1) return false;
    const void *tram = tm_instance_get(TM_TAG("TRAM"), v[1].s);
    if (!tram) return true;      /* failed to find animation */
    int32_t frames = c >= 3 ? num(&v[2]) : 0;
    int32_t interp = c >= 4 ? (uint16_t)num(&v[3]) : -1;
    chr_set_animation_external(&ch->c, ch->c.from_state, tram, interp);
    ch->c.flags |= 0x200000;
    if (frames == -1) ch->c.hold = (uint32_t)tr_duration(tram) + 1;
    else ch->c.hold = frames > 0 ? (uint32_t)frames + 1 : 0;
    return true;
}

static bool cmd_animate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return animate(c, v, false);
}

static bool cmd_animate_block(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    return animate(c, v, true);
}

/* chr_envanim char anim [norotation] (AI2iScript_EnvAnimate_Internal 0x4b7010): the character moved by
   the object animation (its physics context's), turned by it unless "norotation". The block form waits
   while an earlier one plays; it doesn't wait for the one it starts. chr_envanim_stop char (0x4b7240). */
static bool envanim(int c, const SlValue *v, bool block)
{
    if (c < 2 || v[1].type != SL_STRING) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;       /* "chr_envanim or chr_envanim_block: no such character" */
    chr_make_active(&ch->c);     /* 0x4f11b0 */
    if (!ch->c.has_physics) return true;    /* "has no physics context" */
    if (block && ch->c.envanim.oban && ch->c.envanim.frame + 1 < (int)(((const uint8_t *)ch->c.envanim.oban)[OBAN_FRAMES] | ((const uint8_t *)ch->c.envanim.oban)[OBAN_FRAMES + 1] << 8))
        return false;
    const void *oban = tm_instance_get(TM_TAG("OBAN"), v[1].s);
    if (!oban) return true;     /* "failed to find animation" */
    bool no_rot = c >= 3 && v[2].type == SL_STRING && v[2].s && !strcmp(v[2].s, "norotation");
    chr_envanim_start(&ch->c, oban, no_rot);
    return true;
}

static bool cmd_envanim(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return envanim(c, v, false); }
static bool cmd_envanim_block(SlThread *t, int c, const SlValue *v, SlValue *r, void *u) { (void)t, (void)r, (void)u; return envanim(c, v, true); }
static bool cmd_envanim_stop(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (ch && ch->c.envanim.oban) chr_envanim_start(&ch->c, NULL, false);
    return true;
}

/* The animation types' names: the table at 0x54d7c0 in the exe, 8 bytes an entry (the type as a u16
   in a u32, a pointer to its name), ended by a null name. 0x4f0370 sorts it and searches it without
   case; the port searches it in place. */
enum { ANIM_TYPE_NAMES = 0x54d7c0, ANIM_STATE_NAMES = 0x54df10 };
static bool name_lookup(uint32_t table, const char *name, uint16_t *type)
{
    for (uint32_t va = table;; va += 8) {
        uint32_t p = exe_u32(va + 4);
        if (!p) return false;
        const char *s = (const char *)exe_data(p, 1);
        if (!s) return false;
        size_t k = 0;
        while (name[k] && tolower((unsigned char)name[k]) == tolower((unsigned char)s[k])) k++;
        if (!name[k] && !s[k]) {
            *type = (uint16_t)exe_u32(va);
            return true;
        }
    }
}

static bool anim_type(const char *name, uint16_t *type) { return name_lookup(ANIM_TYPE_NAMES, name, type); }

/* The animation states' names (0x4f0450: the table at 0x54df10, the same form). */
static bool anim_state(const char *name, uint16_t *state) { return name_lookup(ANIM_STATE_NAMES, name, state); }

/* chr_wait_animstate char state... (AI2iScript_WaitForAnimState 0x4b75c0): waits until the character's
   animation state (A+0x1afc) is one of the states (16 at most, "too many states"). */
static bool cmd_wait_animstate(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    uint16_t states[16];
    int n = 0;
    for (int i = 1; i < c && n < 16; i++) {
        if (v[i].type != SL_STRING) continue;
        if (anim_state(v[i].s, &states[n])) n++;     /* else "unknown anim state" */
    }
    if (!n) return true;
    for (int i = 0; i < n; i++)
        if (states[i] == ch->c.state) return true;
    return false;
}

/* chr_wait_animtype char type... (AI2iScript_WaitForAnimType 0x4b7400): waits until the character's
   animation, or one of its four overlays playing (0x4d7ba0), is of one of the types (16 at most). */
static bool cmd_wait_animtype(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;
    uint16_t types[16];
    int n = 0;
    for (int i = 1; i < c && n < 16; i++) {
        if (v[i].type != SL_STRING) continue;
        if (anim_type(v[i].s, &types[n])) n++;   /* else "unknown anim type" */
    }
    if (!n) return true;
    uint16_t have[1 + CHR_OVERLAYS];
    int m = 0;
    have[m++] = ch->c.type;
    for (int k = 0; k < CHR_OVERLAYS; k++)
        if (ch->c.overlay[k].anim) have[m++] = tr_type(ch->c.overlay[k].anim);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < m; k++)
            if (types[i] == have[k]) return true;
    return false;
}

/* ONrActiveCharacter_IsPlayingThisAnimationName 0x4f15d0: the animation's instance name
   (TMrInstance_GetInstanceName 0x423d90, without the tag) is the name, without case. The original also
   tries the four overlays (A+0x1c58); the port has none. */
static bool playing_named(const Character *c, const char *name)
{
    if (!c->anim) return false;
    const TmInstance *in = tm_handle_instance(tm_handle_of(c->anim));
    if (!in || !in->name || strlen(in->name) < 4) return false;
    const char *s = in->name + 4;
    size_t k = 0;
    while (name[k] && tolower((unsigned char)name[k]) == tolower((unsigned char)s[k])) k++;
    return !name[k] && !s[k];
}

/* chr_wait_animation char name... (AI2iScript_WaitForAnimation 0x4b7320): waits until the character
   plays one of the named animations. */
static bool cmd_wait_animation(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch) return true;       /* "no such character" */
    for (int i = 1; i < c; i++)
        if (v[i].type == SL_STRING && playing_named(&ch->c, v[i].s)) return true;
    return false;
}

/* The characters' contexts (the callbacks 0x5ec918), for the others' searches. */
static int ctx_count(void) { return chars_slots(); }
static bool ctx_get(int i, PhysCtx *out)
{
    Chr *ch = chars_get(i);
    return ch && chars_in_use(ch) && physics_ctx(&ch->c, out);
}

void chars_register_commands(void)
{
    physctx_add_source((PhysSource){ ctx_count, ctx_get });
    sl_register("chr_wait_animation", cmd_wait_animation, NULL);
    sl_register("ai2_spawn", cmd_spawn, NULL);
    sl_register("chr_create", cmd_chr_create, NULL);
    sl_register("chr_vocalize", cmd_vocalize, NULL);
    sl_register("ai2_takecontrol", cmd_takecontrol, NULL);
    sl_register("trigvolume_kill", cmd_trigvolume_kill, NULL);
    sl_register("ai2_lookatme", cmd_lookatme, NULL);
    sl_register("ai2_followme", cmd_followme, NULL);
    sl_register("ai2_lookatchar", cmd_lookatchar, NULL);
    sl_register("chr_boss_shield", cmd_boss_shield, NULL);
    sl_register("chr_shadow", cmd_shadow, NULL);
    sl_register("chr_super", cmd_super, NULL);
    sl_register("trigvolume_corpse", cmd_trigvolume_corpse, NULL);
    sl_register("chr_delete", cmd_delete, NULL);
    sl_register("ai2_movetoflag", cmd_movetoflag, NULL);
    sl_register("ai2_dopath", cmd_dopath, NULL);
    sl_register("ai2_passive", cmd_passive, NULL);
    sl_register("chr_neutral", cmd_passive, NULL);     /* the same handler (0x4b4be0) */
    sl_register("ai2_neutralbehavior", cmd_neutralbehavior, NULL);
    sl_register_bool("chr_all_active", &chr_all_active);            /* 0x5ecbac */
    sl_register_bool("chr_disable_visactive", &chr_disable_visactive);   /* 0x5ecbad */
    sl_register_bool("ai2_boss_battle", &knowledge_boss_battle);
    sl_register_bool("ai2_barabbas_run", &boss_barabbas_run);
    sl_register("ai2_setjobstate", cmd_setjobstate, NULL);
    sl_register("ai2_doalarm", cmd_doalarm, NULL);
    sl_register("ai2_tripalarm", cmd_tripalarm, NULL);
    sl_register("ai2_setalert", cmd_setalert, NULL);
    sl_register("chr_peace", cmd_peace, NULL);
    sl_register("chr_facetoflag", cmd_facetoflag, NULL);
    sl_register("chr_wait_health", cmd_wait_health, NULL);
    sl_register("ai2_makedeaf", cmd_makedeaf, NULL);
    sl_register("ai2_makeblind", cmd_makeblind, NULL);
    sl_register("chr_changeteam", cmd_changeteam, NULL);
    sl_register("chr_nocollision", cmd_nocollision, NULL);
    sl_register("chr_forceholster", cmd_forceholster, NULL);
    sl_register("chr_lock_active", cmd_lock_active, NULL);
    sl_register("ai2_active", cmd_ai_active, NULL);
    sl_register("ai2_inactive", cmd_ai_active, (void *)1);
    sl_register("chr_death_lock", cmd_death_lock, NULL);
    sl_register("chr_unkillable", cmd_unkillable, NULL);
    sl_register("chr_invincible", cmd_invincible, NULL);
    sl_register("ai2_makeignoreplayer", cmd_make_ignore_player, NULL);
    sl_register("ai2_attack", cmd_attack, NULL);
    sl_register("ai2_makeaware", cmd_make_aware, NULL);
    sl_register("ai2_forget", cmd_forget, NULL);
    sl_register("ai2_kill", cmd_kill, NULL);
    sl_register("ai2_allpassive", cmd_all_passive, NULL);
    sl_register("ai2_setmovementmode", cmd_set_movement_mode, NULL);
    sl_register("chr_set_health", cmd_set_health, NULL);
    sl_register("chr_is_player", cmd_is_player, NULL);
    sl_register("chr_poison", cmd_poison, NULL);
    sl_register("chr_unstoppable", cmd_unstoppable, NULL);
    sl_register("chr_weapon_immune", cmd_weapon_immune, NULL);
    sl_register("chr_teleport", cmd_teleport, NULL);
    sl_register("playback", cmd_playback, NULL);
    sl_register("playback_block", cmd_playback, (void *)1);
    sl_register("chr_full_health", cmd_full_health, NULL);
    sl_register("begin_cutscene", cmd_begin_cutscene, NULL);
    sl_register("end_cutscene", cmd_end_cutscene, NULL);
    sl_register("chr_animate", cmd_animate, NULL);
    sl_register("chr_animate_block", cmd_animate_block, NULL);
    sl_register("chr_envanim", cmd_envanim, NULL);
    sl_register("chr_envanim_block", cmd_envanim_block, NULL);
    sl_register("chr_envanim_stop", cmd_envanim_stop, NULL);
    sl_register("chr_wait_animtype", cmd_wait_animtype, NULL);
    sl_register("chr_wait_animstate", cmd_wait_animstate, NULL);
}
