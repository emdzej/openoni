/* The level's characters: see chars.h. */
#include "chars.h"
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
#include "patrol.h"
#include "physics.h"
#include "sl.h"
#include "tm.h"
#include "throw.h"
#include "weapon.h"
#include "trigger.h"
#include "ui.h"
#include "camera.h"

#include <ctype.h>
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
    object_flags[n_objects] = objc_char_flags(o);
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

/* ONrGameState_NewCharacter 0x4dac50 from a CHAR object: the first free slot; its class, place and
   facing (the rotation's y, degrees), name and team; AI2rInitializeCharacter's script id. */
static Chr *create(int object)
{
    const ObjcObject *o = &objects[object];
    const void *oncc = tm_instance_get(TM_TAG("ONCC"), objc_char_class(o));
    if (!oncc) return NULL;
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
    ai_init(&c->ai);
    c->ai.name = c->name;
    /* AI2rInitializeCharacter 0x48a6b0: the AI's flags (knowledge on; CHAR flag 0x08 non-aggressive,
       0x100 never forgets), the alert level, and the job: a patrol starts its path (by the id) at once */
    if (!c->player) {
        uint32_t f = object_flags[object];
        c->ai.flags = AI_FLAG_KNOWLEDGE | (f & 0x08 ? AI_FLAG_NON_AGGRESSIVE : 0) | (f & 0x100 ? AI_FLAG_NO_DEGRADE : 0);
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
    c->ai.job_alert = (int)objc_char_job_alert(o);
    c->ai.alarm_groups = objc_char_alarm_groups(o);
    chr_hit_init(&c->c, objc_char_extra_health(o));
    if (armed) arm(c);
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
    if (c->script_id && !(f & 1)) {
        c->ai.flags = AI_FLAG_KNOWLEDGE;
        c->ai.state = AI_STATE_IDLE;
        melee_start(&c->ai.melee, NULL, oncc);
        combat_default_settings(&c->ai);
    }
    c->ai.job = c->ai.state;
    c->ai.job_patrol = c->ai.patrol;
    c->ai.job_alert = 1;
    chr_hit_init(&c->c, 0);
    return c;
}

int chars_load(void)
{
    chars_unload();
    patrol_load();      /* the paths first: the patrol jobs start with their characters */
    melee_initialize();
    melee_load();
    combat_load();
    armed = false;
    objc_each("Character", add_object, NULL);
    int n = 0;
    for (int i = 0; i < n_objects; i++)
        if (!(object_flags[i] & OBJC_CHAR_NOT_INITIAL) && create(i)) n++;
    return n;
}

void chars_unload(void)
{
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
        memcpy(a->pos, slots[i].c.pos, sizeof a->pos);
    }
    return actors;
}

/* 0x48b480, for the AI's characters TakeDamage hurts. */
static void hurt(Character *c, Character *from, uint32_t damage, bool melee)
{
    Chr *v = (Chr *)c;
    if (v->ai.flags & AI_FLAG_PASSIVE) return;
    v->ai.vocal_delay = 0;
    if (!melee && v->ai.daze_timer) v->ai.daze_timer = 1;  /* a shot (kind 0x1000000) ends the daze */
    knowledge_hurt_by(v, (Chr *)from, damage, melee);
}

/* 0x48b530, for an AI's character knocked down from not lying: a new daze to come. (Its other parts,
   the scripted film stopped and the alarm's, aren't kept.) */
static void knocked_down(Character *c)
{
    ((Chr *)c)->ai.daze_started = false;
}

/* AI2rBlockFunction 0x48b870, every AI's block function (AI2rInitializeCharacter 0x48a6b0). */
static bool block(Character *c)
{
    return melee_block(&((Chr *)c)->ai);
}

/* AI2rMelee_NotifyAnimation 0x49fd10, for the AI's characters in combat. */
static void new_animation(Character *c)
{
    Chr *ch = (Chr *)c;
    if (ch->ai.state == AI_STATE_COMBAT) melee_notify_animation(&ch->ai.melee, c->type, chr_is_victim_type(c->type));
}

/* ONrCharacter_Die_1_Animating_Hook 0x4eb550's scripts: the defeated one unless it ran already, then
   the death one, deferred with the character's name and run at once (0x4f2120). The scripts are the
   CHAR object's (AI2rInitializeCharacter 0x48a6b0 copies them); a character made otherwise has none. */
static void died(Character *c)
{
    Chr *ch = (Chr *)c;
    /* C+0x1644: the body stays the class's time (ONCC+0xc8a) */
    const uint8_t *k = (const uint8_t *)c->oncc + 0xc8a;
    ch->corpse_time = (uint16_t)(k[0] | k[1] << 8);
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

void chars_update(const CharacterInput *player_in, const void *env)
{
    chr_hurt_hook = hurt;
    chr_die_hook = died;
    chr_script_hook = script;
    chr_animation_hook = new_animation;
    chr_block_hook = block;
    chr_knockdown_hook = knocked_down;
    throw_install();
    weapons_install();
    knowledge_update(env);      /* AI2rKnowledge_Update, before the characters */
    for (int i = 0; i < CHARS_MAX; i++) {
        if (!used[i]) continue;
        /* ONrGameState_UpdateCharacters 0x4dccf0: a body's time runs out; an AI's (C+0x1e8 not 0) is
           deleted (ONrGameState_DeleteCharacter 0x4dc480; only mad_bomber has a time, the other classes 0) */
        if (slots[i].corpse_time && --slots[i].corpse_time == 0 && !slots[i].player) {
            chars_delete(&slots[i]);
            continue;
        }
        if (slots[i].player) character_update(&slots[i].c, player_in, env);
        else ai_update(&slots[i].ai, &slots[i].c, env);
    }
    Character *list[CHARS_MAX];
    int n = 0;
    for (int i = 0; i < CHARS_MAX; i++)
        if (used[i]) list[n++] = &slots[i].c;
    chr_collide_all(list, n);
    throw_update(list, n);
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
    TmFile *f = tm_file_at(tm_file_slots() - 1);
    for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag == TM_TAG("AKEV") && in->data) graph_for(in->data);
    }
    if (graph_room_at(p) < 0) return true;      /* "flag was not inside a BNV" */
    create_setup(s, &flag);                       /* "failed to create a new character" */
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
   player, who needs ai2_takecontrol) walks to the flag, then faces the flag's way if asked. The walk
   is the stand-in mover (ai.h) until the movement layer is ported. */
static bool cmd_movetoflag(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    ObjcObject flag;
    if (!ch || !objc_flag_find((uint16_t)num(&v[1]), &flag)) return true;   /* "flag %d does not exist" */
    if (ch->player) return true;    /* "cannot move player character without first using ai2_takecontrol" */
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
   physics context's flags 0x44 from it. The port's physics has no character-to-character collision yet,
   so only the flag is kept. */
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
    return true;
}

/* chr_lock_active char (AI2iScript_LockActive 0x4b94b0): an AI character made active
   (ONrCharacter_MakeActive 0x4f11b0) and kept so, C+0x04 flag 0x100000. The port's characters are all
   active: only the flag is kept. */
static bool cmd_lock_active(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (ch && !ch->player) ch->c.flags |= 0x100000u;    /* else "chr_lock_active: no such AI" */
    return true;
}

/* chr_death_lock char 0|1 (AI2iScript_ChrDeathLock 0x4b9f80): C+0x04 flag 0x400, which only
   ONrCharacter_Die_4_Gone_Hook 0x4ebb60 reads (a locked body doesn't go: not ported yet). */
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
        chr_take_damage(&ch->c, 0x6000000, 0.0f, NULL, 0, NULL);
    }
    return true;
}

/* chr_set_health char hp (AI2iScript_SetHealth 0x4b7a30, ONrCharacter_SetHitPoints 0x4eb220): 0 kills
   a living character (the death hook); a dead one given hit points coming back isn't ported. */
static bool cmd_set_health(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;       /* "chr_set_health" */
    int32_t hp = num(&v[1]);
    if (hp <= 0) {
        if (!(ch->c.flags & 1)) chr_take_damage(&ch->c, ch->c.hit_points ? ch->c.hit_points : 1, 0.0f, NULL, 0, NULL);
        return true;
    }
    if (!(ch->c.flags & 1)) ch->c.hit_points = (uint32_t)hp;
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
   (AI2rMovement_ChangeMovementMode 0x4a8200, +0x14a4) by its name (0x52adf8, 16 bytes each). The
   executor takes it where the alert level's default would be (the movement state's port: a stand-in). */
static bool cmd_set_movement_mode(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    static const char *const names[] = { "by_alert_level", "stop", "crouch", "creep", "walk_noaim", "walk", "run_noaim", "run" };
    Chr *ch = c >= 2 ? arg_char(&v[0]) : NULL;
    if (!ch || ch->player || v[1].type != SL_STRING) return true;
    for (int k = 0; k < 8; k++)
        if (!strcmp(v[1].s, names[k])) ch->ai.movement_mode = k;   /* else "unknown movement mode" */
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
   start over ticks (30). Not ported: the physics context's pending motion cleared first (+0x130..0x13c,
   0x4ed080), and an AI that was running a path, stopped if the film fails (0x4b0420). */
static bool cmd_playback(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    if (c < 2) return true;
    Chr *ch = arg_char(&v[0]);
    if (!ch || v[1].type != SL_STRING) return true;     /* "character %s cannot be made active" */
    const void *film = tm_instance_get(TM_TAG("FILM"), v[1].s);
    if (!film) return true;                             /* "can not find any film named %s" */
    int mode = FILM_START_HERE_PLACED;
    uint32_t ticks = 0;
    if (c > 2 && v[2].type == SL_STRING) {
        if (!strcmp(v[2].s, "fromhere")) mode = FILM_START_FROM_HERE;
        else if (!strcmp(v[2].s, "interp")) mode = FILM_START_INTERP, ticks = c > 3 ? (uint32_t)num(&v[3]) : 30;
    }
    film_start(&ch->c, film, mode, ticks);
    /* playback_block (0x4b6d80): the script sleeps the film's length (FILM +0x1c) */
    if (u) sl_thread_sleep(t, (uint32_t)(((const uint8_t *)film)[0x1c] | ((const uint8_t *)film)[0x1d] << 8 |
                                         ((const uint8_t *)film)[0x1e] << 16 | (uint32_t)((const uint8_t *)film)[0x1f] << 24));
    return true;
}

/* chr_full_health char (0x4b7980): the hit points to the most (ONrCharacter_SetHitPoints 0x4eb220 with
   C+0x3c). Not ported: a dead character given hit points comes back (0x4eb220's other half). */
static bool cmd_full_health(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)r, (void)u;
    Chr *ch = c >= 1 ? arg_char(&v[0]) : NULL;
    if (!ch) return true;
    if (!(ch->c.flags & 1)) ch->c.hit_points = ch->c.max_hit_points;
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

static void all_passive(bool on)
{
    for (int i = 0; i < chars_slots(); i++) {
        Chr *ch = chars_get(i);
        if (!ch || ch->player) continue;
        if (!on) ch->c.last_action = ch->c.time;
        if (on) ch->ai.flags |= AI_FLAG_PASSIVE;
        else ch->ai.flags &= ~(uint32_t)AI_FLAG_PASSIVE;
    }
}

/* begin_cutscene [weapon | jello | ai | invis | nosync | keepparticles | animation | nojump]...
   (0x4bca90, ONrGameState_BeginCutscene 0x4fd070): the letterbox on; the player's sprint stopped
   (0x4eb130), stood (ForceStand) unless "animation", her jump's motion across stopped with "nojump"; her
   weapon holstered unless "weapon" (0x4eff60); her invisibility gone unless
   "invis"; the AI passive unless "ai"; the camera's jello off unless "jello"; the input off; the sync on
   unless "nosync". Not ported: the particles' cleanup (0x458310) unless "keepparticles", the sound's
   switch (OSrSetEnabled). */
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
    ui_set_input(false);
    ui_set_cutscene(true, !(flags & 0x10));
    return true;
}

/* end_cutscene (0x4bcbb0, ONrGameState_EndCutscene 0x4fd1a0): the input back, the jello on, the AI out
   of passive, the letterbox off, the cutscene over, the player's holstered weapon drawn again (0x4effa0)
   if her hand is empty. Not ported: the messages cleared (0x431d50), the skip's fade (G+0x40), the
   sound's switch. */
static bool cmd_end_cutscene(SlThread *t, int c, const SlValue *v, SlValue *r, void *u)
{
    (void)t, (void)c, (void)v, (void)r, (void)u;
    ui_set_input(true);
    camera_enable_jello(true);
    all_passive(false);
    ui_set_letterbox(false);
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

void chars_register_commands(void)
{
    sl_register("chr_wait_animation", cmd_wait_animation, NULL);
    sl_register("ai2_spawn", cmd_spawn, NULL);
    sl_register("chr_create", cmd_chr_create, NULL);
    sl_register("chr_delete", cmd_delete, NULL);
    sl_register("ai2_movetoflag", cmd_movetoflag, NULL);
    sl_register("ai2_dopath", cmd_dopath, NULL);
    sl_register("ai2_passive", cmd_passive, NULL);
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
