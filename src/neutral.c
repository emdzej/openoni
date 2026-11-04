/* The neutral characters and the speech: see neutral.h. */
#include "neutral.h"

#include "chr_anim.h"
#include "knowledge.h"
#include "objc.h"
#include "powerup.h"
#include "sound.h"
#include "tm.h"
#include "trac.h"
#include "trigger.h"
#include "weapon.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const float PI = 3.1415927f, TWO_PI = 6.2831855f;


/* The Neutral's data (the NEUT object's own, as the reader keeps it): docs/formats/objc.md, "NEUT". */
enum {
    N_ID = 0x20, N_LINES = 0x22, N_FLAGS = 0x24, N_TRIGGER = 0x28, N_TALK = 0x2c, N_FOLLOW = 0x30,
    N_ENEMY = 0x34, N_TRIGGER_SOUND = 0x38, N_ABORT_SOUND = 0x58, N_ENEMY_SOUND = 0x78, N_END_SCRIPT = 0x98,
    N_WEAPON = 0xb8, N_GIFTS = 0xd8, N_LINE = 0xdc, LINE_SIZE = 0x28,
    /* its flags */
    NF_ONCE_TALKED = 1, NF_ONCE_GIVEN = 2, NF_UNINTERRUPTIBLE = 4,
    /* a line's flags (+0): the player says it, it is the give, the speaker's and the listener's
       animations are waited for; its animations (+4, +6), its sound (+8) */
    LF_PLAYER = 1, LF_GIVE = 2, LF_WAIT_SPEAKER = 4, LF_WAIT_LISTENER = 8,
    /* the animation types */
    ANIM_CALL = 0xe6, ANIM_GIVE = 0xcd,
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

typedef struct { int id; const char *name; uint8_t *out; bool found; } Find;

static bool find_one(const ObjcObject *o, void *user)
{
    Find *f = user;
    if (o->size < N_LINE) return true;
    if (f->name ? strncmp((const char *)o->data, f->name, 32) != 0 : rd16(o->data + N_ID) != f->id) return true;
    f->found = true;
    f->id = rd16(o->data + N_ID);
    if (f->out) {
        memset(f->out, 0, NEUTRAL_DATA);
        memcpy(f->out, o->data, o->size < NEUTRAL_DATA ? o->size : NEUTRAL_DATA);
    }
    return false;
}

/* ONrLevel_Neutral_ID_To_Neutral 0x506c00: the Neutral of the id, its data copied out. */
static bool neutral_of_id(int id, uint8_t *out)
{
    Find f = { id, NULL, out, false };
    objc_each("Neutral", find_one, &f);
    return f.found;
}

int neutral_id_named(const char *name)
{
    Find f = { -1, name, NULL, false };
    objc_each("Neutral", find_one, &f);
    return f.found ? f.id : -1;
}

bool neutral_change_behavior(Chr *c, int id, bool force)
{
    AiState *a = &c->ai;
    if (!c->c.ai) return false;
    if (!force && a->neutral_id == id) return a->neutral_id != -1;
    uint8_t d[NEUTRAL_DATA];
    if (id != -1 && id != 0xffff && neutral_of_id(id, d)) {
        a->neutral_id = id;
        a->neutral_line = 0;
        a->neutral_flags = 0;
        a->neutral_time = 0;
        a->neutral_trigger = rdf(d + N_TRIGGER);
        a->neutral_enemy = rdf(d + N_ENEMY);
        return true;
    }
    a->neutral_id = -1;
    a->neutral_line = 0;
    a->neutral_flags = 0;
    a->neutral_time = 0;
    a->neutral_trigger = 0.0f;
    a->neutral_enemy = 100.0f;  /* 0x42c80000 */
    return false;
}

/* ---- the speech ---- */

int speech_say(Character *c, const ChrSpeech *s, bool interrupt)
{
    if (s->waiting) *s->waiting = true;
    if (s->type != 7 && s->type <= c->speech.type) {
        if (!interrupt && c->speech_next.type < s->type) {
            c->speech_next = *s;
            return 1;
        }
        if (s->waiting) *s->waiting = false;
        return 2;
    }
    if (c->speaking) {
        if (c->speech_id != UINT32_MAX) {
            sound_speech_stop(c->speech_id);
            c->speech_id = UINT32_MAX;
        }
        c->speaking = false;
    }
    c->speech_next.type = 0;
    c->speech = *s;
    c->speaking = true;
    c->speech_begun = c->speech_over = false;
    return 0;
}

void speech_update(Chr *ch)
{
    Character *c = &ch->c;
    if (!c->speaking) return;
    if (c->speech.pre_delay && --c->speech.pre_delay) return;
    if (!c->speech_over) {
        if (!c->speech_begun) {
            /* at the head (0x4e3460), as long as its first sound (OSiDialog_Play) */
            float head[3];
            knowledge_head_point(ch, head);
            uint32_t len = 0;
            bool missing = false;
            c->speech_id = sound_speech_play(c->speech.name, head, &len, &missing);
            c->speech_end = knowledge_now() + len;
            if (missing) c->speech_over = true;     /* "%s can't find sound '%s'" */
            if (c->speech_id == UINT32_MAX) c->speech_over = true;
            c->speech_begun = true;
            if (c->speech.waiting) *c->speech.waiting = false;
        } else {
            if (!sound_speech_playing(c->speech_id)) {
                c->speech_id = UINT32_MAX;
                c->speech_over = true;
            }
            if (!c->speech_over && c->speech_end < knowledge_now()) {
                c->speech_id = UINT32_MAX;
                c->speech_over = true;
            }
        }
        if (!c->speech_over) return;
    }
    if (c->speech.post_delay && --c->speech.post_delay) return;
    c->speaking = c->speech_begun = c->speech_over = false;
    if (!c->speech_next.type) {
        c->speech.type = 0;
        return;
    }
    c->speech = c->speech_next;
    c->speech_next.type = 0;
    c->speaking = true;
}

static void say(Chr *c, uint32_t type, const uint8_t *name)
{
    ChrSpeech s = { .type = type };
    memcpy(s.name, name, sizeof s.name - 1);
    speech_say(&c->c, &s, false);
}

/* ---- the conversation ---- */

static float dist2(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

/* ONrCharacter_GetPelvisPosition 0x4e34f0 (the port's: the location raised by the pelvis height). */
static void pelvis(const Character *c, float p[3])
{
    p[0] = c->pos[0];
    p[1] = c->pos[1] + c->pelvis_height;
    p[2] = c->pos[2];
}

/* 0x4e33a0: the way the character faces, its run's turn taken off. */
static float heading(const Character *c)
{
    float a = c->desired_facing - c->facing_offset;
    return a < 0.0f ? a + TWO_PI : a >= TWO_PI ? a - TWO_PI : a;
}

/* AI2rManeuver_CheckBlockage 0x49ea60: the character's sphere tree swept to the point meets something.
   (Not tested by the port yet, as for the consoles: action.c.) */
static bool blocked(const Chr *c, const float p[3])
{
    (void)c, (void)p;
    return false;
}

/* The player in front of c within 35 (1225 squared, 0x52af98), facing it within 60 degrees (0x52af90). */
static bool facing_near(const Chr *c, const Chr *p)
{
    float dx = c->c.pos[0] - p->c.pos[0], dy = c->c.pos[1] - p->c.pos[1], dz = c->c.pos[2] - p->c.pos[2];
    if (dx * dx + dy * dy + dz * dz > 1225.0f) return false;
    float a = atan2f(dx, dz);
    if (a < 0.0f) a += TWO_PI;
    float f = a - heading(&p->c);
    if (f < -PI) f += TWO_PI;
    else if (f > PI) f -= TWO_PI;
    return fabs(f) <= 1.0471975511965976;
}

/* AI2iNeutral_CheckEnemies 0x4acbc0: a living AI character (ONrGameState's living list: the dead leave
   it at death, 0x4eb550) fighting c or p (its combat target) is within the
   enemy range of either. */
static bool enemies_near(const Chr *c, const Chr *p)
{
    float r2 = c->ai.neutral_enemy * c->ai.neutral_enemy;
    for (int i = 0; i < chars_slots(); i++) {
        const Chr *e = chars_get(i);
        if (!e || (e->c.flags & 1) || !e->c.ai || e->ai.state != AI_STATE_COMBAT || (e->ai.target != c && e->ai.target != p)) continue;
        if (dist2(e->c.pos, c->c.pos) <= r2 || dist2(e->c.pos, p->c.pos) <= r2) return true;
    }
    return false;
}

/* 0x4ae070: whether c's contact e means danger near pos (within the enemy range): a hurt, or (heard
   strongly enough) a melee sound, gunfire or a sighting of a character who takes c, or other if given,
   for an enemy; without known, a combat sound by anyone. */
bool ai_dangerous_contact(const Chr *c, const Chr *other, bool known, const KnowEntry *e, const float pos[3])
{
    if (e->strength < KNOW_WEAK) return false;
    if (e->type == KNOW_HURT || e->type == KNOW_HURT_MELEE) return true;
    if (e->type != KNOW_SOUND_MELEE && e->type != KNOW_SOUND_GUNFIRE && e->type != KNOW_SEEN_CENTRAL) return false;
    if (e->target == c || !e->target) return false;
    bool h = ai_is_hostile(e->target, c, true);
    if (!known && !h) h = e->type >= KNOW_SOUND_DANGER && e->type <= KNOW_SOUND_GUNFIRE;    /* 0x49ad70 */
    if (!h && (!other || !ai_is_hostile(e->target, other, true))) return false;
    float r = c->ai.neutral_enemy;
    return dist2(pos, e->pos) <= r * r;
}

/* 0x4ae170 (c, other, 0, NULL): one of c's contacts means danger near it. */
static bool danger(const Chr *c, const Chr *other)
{
    for (const KnowEntry *e = c->ai.know; e; e = e->next)
        if (ai_dangerous_contact(c, other, false, e, c->c.pos)) return true;
    return false;
}

/* AI2rNeutral_CheckTrigger 0x4ac950: whether c, a neutral not talking (nor panicking, nor done), takes
   up with p: by itself (automatic) when not waiting, within its trigger range, every 30 ticks, with p
   seen and no danger or enemies near (else not for 300 more); asked, with p near and facing it. p
   mustn't be its enemy, nor the way to p blocked. */
static bool check_trigger(Chr *c, Chr *p, bool automatic)
{
    AiState *a = &c->ai;
    if (!c->c.ai || a->neutral_id == -1 || c->c.talk_partner || a->state == AI_STATE_PANIC ||
        (a->neutral_flags & NEUTRAL_DONE))
        return false;
    if (automatic && a->neutral_wait) return false;
    if (!automatic) {
        if (!facing_near(c, p)) return false;
    } else if (dist2(c->c.pos, p->c.pos) > a->neutral_trigger * a->neutral_trigger) {
        return false;
    }
    if (ai_is_hostile(c, p, true)) return false;
    if (automatic) {
        if (knowledge_now() < a->neutral_time + 30) return false;
        a->neutral_time = knowledge_now();
        const KnowEntry *e = a->know;
        while (e && (e->target != p || e->strength != KNOW_DEFINITE)) e = e->next;
        if (!e) return false;
        if (a->neutral_enemy > 0.0f && (danger(c, p) || enemies_near(c, p))) {
            a->neutral_time = knowledge_now() + 300;
            return false;
        }
    }
    float pel[3];
    pelvis(&p->c, pel);
    return !blocked(c, pel);
}

/* 0x4ed030 and 0x4ecf80: in a conversation (C+0x04 0x10000: out of fight mode, the variant 0x0200
   gone) with the weapon hidden (0x2000); or out of it again. */
static void talk_flags(Character *c, bool on)
{
    if (on) {
        c->flags |= 0x10000;
        c->varient &= (uint16_t)~0x200;
    } else {
        c->flags &= ~0x10000u;
    }
    chr_hide_weapon(c, on);
}

/* AI2rMovement_LookAtCharacter 0x4a80c0. */
static void look_at(AiState *a, const Character *c)
{
    a->aim.look = 3;
    a->aim.look_char = c;
}

/* AI2rMovement_StopAiming 0x4a8050 and StopGlancing 0x4a8140 (the movement's glance isn't kept). */
static void stop_aiming(AiState *a) { a->aim.look = 0; }

/* 0x4ad090: c's part of the end: out of the conversation's flags; a neutral aborted says its enemy or
   abort line once (none when done), stops looking and moving, and, to_job, goes back to its job. */
static void reset(Chr *c, bool aborted, bool enemy, bool to_job)
{
    talk_flags(&c->c, false);
    AiState *a = &c->ai;
    if (!c->c.ai || !aborted || a->state != AI_STATE_NEUTRAL) return;
    NeutralTalk *t = &a->talk;
    const uint8_t *line = enemy ? t->osd + N_ENEMY_SOUND : (a->neutral_flags & NEUTRAL_DONE) ? (const uint8_t *)"" : t->osd + N_ABORT_SOUND;
    uint32_t type = enemy ? 6 : (a->neutral_flags & NEUTRAL_DONE) ? 0 : 4;
    if (!t->ended) {
        if (line[0]) say(c, type, line);
        t->ended = true;
    }
    stop_aiming(a);
    ai_halt(a);
    if (to_job) ai_return_to_job(a);
}

/* AI2rNeutral_Stop 0x4acf70: c's conversation ends (none: true). Not while the neutral's give is under
   way, unless forced (false). Aborted, the neutral waits 180 ticks before it may start again by itself;
   else c runs the behaviour's end script on its next update. */
bool neutral_stop(Chr *c, bool force, bool aborted, bool enemy, bool to_job)
{
    Chr *o = (Chr *)c->c.talk_partner;
    if (!o) return true;
    Chr *n = NULL;
    if (!(c->c.flags & 1) && !(o->c.flags & 1)) {
        if (c->c.ai && c->ai.state == AI_STATE_NEUTRAL) n = c;
        else if (o->c.ai && o->ai.state == AI_STATE_NEUTRAL) n = o;
        if (n && n->ai.talk.giving && !n->ai.talk.given && !force) return false;
    }
    if (aborted) {
        if (n) n->ai.neutral_wait = 180;
    } else if (n && n->ai.talk.osd[N_END_SCRIPT]) {
        memcpy(c->ai.end_script, n->ai.talk.osd + N_END_SCRIPT, sizeof c->ai.end_script - 1);
        c->ai.flags |= AI_FLAG_END_SCRIPT;
    }
    if (!(c->c.flags & 1)) reset(c, aborted, enemy, to_job);
    if (!(o->c.flags & 1)) reset(o, aborted, enemy, to_job);
    o->c.talk_partner = NULL;
    c->c.talk_partner = NULL;
    o->c.flags &= ~0x800000u;
    c->c.flags &= ~0x800000u;
    return true;
}

void neutral_end_script(Chr *c)
{
    AiState *a = &c->ai;
    if (!(a->flags & AI_FLAG_END_SCRIPT)) return;
    if (a->end_script[0]) {
        trigger_defer(a->end_script, c->name);     /* 0x5100b0 */
        a->end_script[0] = 0;
    }
    a->flags &= ~(uint32_t)AI_FLAG_END_SCRIPT;
}

/* AI2rEnterState 0x48ad50 for the neutral state, after the state left and its block cleared. */
static void enter_state(Chr *c)
{
    ai_exit_state(&c->ai);
    c->ai.state = AI_STATE_NEUTRAL;
    memset(&c->ai.talk, 0, sizeof c->ai.talk);
    neutral_enter(c);
}

void neutral_check(Chr *c)
{
    AiState *a = &c->ai;
    Chr *p = chars_player();
    if (a->state != AI_STATE_NEUTRAL && p && check_trigger(c, p, true)) enter_state(c);
    else if (a->neutral_wait) a->neutral_wait--;
}

void neutral_enter(Chr *c)
{
    AiState *a = &c->ai;
    NeutralTalk *t = &a->talk;
    if (!neutral_of_id(a->neutral_id, t->osd)) {
        a->neutral_id = -1;     /* AI2rError: the Neutral isn't there */
        return;
    }
    /* (the knowledge's handler: the alert's, or the non-aggressive's 0x4adeb0: knowledge.c's notify) */
    memcpy(t->home, c->c.pos, sizeof t->home);
    t->line = -1;
    a->neutral_flags |= NEUTRAL_ENTERED;
    t->follow_time = t->enemy_time = 0;
    t->giving = t->given = t->accepted = false;
    t->player = chars_player();
    t->patience = 600;
    t->ended = false;
    t->idle = ((trac_random() * 0x5au) >> 16) + 300;   /* UUrRandom 0x426e10 */
    if (t->osd[N_TRIGGER_SOUND]) say(c, 4, t->osd + N_TRIGGER_SOUND);
    a->movement_mode = AI_MODE_RUN;     /* AI2rMovement_ChangeMovementMode 0x4a8200 */
    /* (AI2rMovement_FreeFacing 0x4a81c0) */
    stop_aiming(a);
}

void neutral_exit(Chr *c)
{
    AiState *a = &c->ai;
    NeutralTalk *t = &a->talk;
    a->movement_mode = 0;
    stop_aiming(a);
    bool aborted = t->line == -1 || (uint32_t)t->line < rd16(t->osd + N_LINES);
    neutral_stop(c, true, aborted, false, false);
}

/* AI2iNeutral_ConversationAnimation 0x4adb00: who plays the animation of the type. Waited for: busy
   (true) until it has played (done), refused at a standing end ("can't play animation") or, done
   already, unless it may start again. Not waited for: started, or kept going while it plays. */
static bool conversation_animation(Chr *who, uint16_t type, bool wait, bool again, bool *done)
{
    Character *c = &who->c;
    if (wait) {
        if (c->type == type) {
            *done = true;
        } else {
            if (*done && !again) return false;
            if (c->queued == type) return true;
            if (!chr_do_animation(c, CHR_ANIM_WHEN_CAN, type) && tr_to(c->anim) == CHR_STATE_STANDING) {
                *done = true;   /* "%s can't play animation of type %s" */
                return false;
            }
        }
        return true;
    }
    if (c->type == type) {
        c->hold = 2;
        return false;
    }
    chr_do_animation(c, CHR_ANIM_WHEN_CAN, type);
    return false;
}

/* The give (at the AI's give animation's action frame): the gifts to the player, what it can't carry
   dropped by the AI; the weapon into the player's hand, or dropped by the AI with one in it. */
static void give(Chr *n, Chr *p)
{
    AiState *a = &n->ai;
    const uint8_t *d = a->talk.osd;
    a->neutral_flags |= NEUTRAL_GAVE;
    if (rd32(d + N_FLAGS) & NF_ONCE_GIVEN) a->neutral_flags |= NEUTRAL_DONE;
    for (int k = POWERUP_AMMO; k <= POWERUP_HYPO; k++) {
        uint16_t amount = d[N_GIFTS + k];
        if (!amount) continue;
        uint16_t left = (uint16_t)(amount - powerup_give(&p->c, k, amount, false, true));     /* 0x51ca30 */
        if (left) powerup_drop(&n->c, k, left, true);
    }
    uint8_t f = d[N_GIFTS + 3];
    if (f & 1) powerup_give(&p->c, POWERUP_SHIELD, powerup_default_amount(POWERUP_SHIELD), false, true);
    if (f & 2) powerup_give(&p->c, POWERUP_INVISIBILITY, powerup_default_amount(POWERUP_INVISIBILITY), false, true);
    if (f & 4) {
        /* (the amount is invisibility's default, as the exe asks for it) */
        powerup_give(&p->c, POWERUP_LSI, powerup_default_amount(POWERUP_INVISIBILITY), false, true);
        n->c.lsi = false;
    }
    char name[33] = { 0 };
    memcpy(name, d + N_WEAPON, 32);
    const void *cls = name[0] ? tm_instance_get(TM_TAG("ONWC"), name) : NULL;
    if (!cls) return;
    /* 0x4edbd0: a weapon in hand or put away. Then 0x51b790 (the AI's, 10 ahead), else into the hand. */
    bool armed = p->c.weapon || chr_slot(&p->c, 1) || chr_slot(&p->c, 2);
    if (armed) {
        float at[3] = { n->c.pos[0] + sinf(n->c.facing) * 10.0f, n->c.pos[1] + n->c.pelvis_height,
                        n->c.pos[2] + cosf(n->c.facing) * 10.0f };
        weapon_new(cls, at, (float)trac_random() * TWO_PI * (1.0f / 65535.0f));
    } else {
        Weapon *w = weapon_new(cls, p->c.pos, 0.0f);    /* 0x518690 */
        if (w) chr_set_weapon(&p->c, w);
    }
}

void neutral_update(Chr *n)
{
    AiState *a = &n->ai;
    NeutralTalk *t = &a->talk;
    Chr *p = t->player;
    if (!p || !chars_in_use(p) || (p->c.flags & 1) || a->neutral_id == -1) {
        ai_halt(a);
        ai_return_to_job(a);
        return;
    }
    if ((!t->giving || t->given) && knowledge_now() > t->enemy_time + 10) {
        if (enemies_near(n, p) && neutral_stop(n, false, true, true, true)) return;
        t->enemy_time = knowledge_now();
    }
    const uint8_t *d = t->osd;
    if (t->line == -1) {
        float pel[3];
        pelvis(&p->c, pel);
        float d2 = dist2(n->c.pos, pel), talk = rdf(d + N_TALK);
        bool near = d2 < talk * talk && !blocked(n, pel);
        if (near && t->accepted) {
            float ang = atan2f(pel[0] - n->c.pos[0], pel[2] - n->c.pos[2]);
            if (ang < 0.0f) ang += TWO_PI;
            ai_move_near(a, n->c.pos, 0.0f, true, ang);     /* AI2rPath_GoToPoint at its place, facing p */
            look_at(a, &p->c);
            if (p->c.talk_partner) return;
            if (tr_to(n->c.anim) != CHR_STATE_STANDING) return;
            /* the conversation begins */
            uint32_t of = rd32(d + N_FLAGS);
            a->neutral_flags |= NEUTRAL_TALKED;
            if (of & NF_ONCE_TALKED) a->neutral_flags |= NEUTRAL_DONE;
            n->c.talk_partner = &p->c;
            p->c.talk_partner = &n->c;
            if (of & NF_UNINTERRUPTIBLE) {
                n->c.flags |= 0x800000;
                p->c.flags |= 0x800000;
            }
            ang += PI;
            if (ang >= TWO_PI) ang -= TWO_PI;
            p->c.desired_facing = ang;      /* 0x4e3130: the player turns to it */
            if (chr_facing_delta(&p->c) <= 0.0f) p->c.turning_left = true;
            else p->c.turning_right = true;
            p->c.talk_moves = 0;
            talk_flags(&n->c, true);
            talk_flags(&p->c, true);
            t->line_begun = false;
            t->line = a->neutral_line;
        } else {
            if (dist2(n->c.pos, t->home) > rdf(d + N_FOLLOW) * rdf(d + N_FOLLOW)) {
                /* too far from its place: back to it and its job (the port's exit halts the path, so the
                   way back is asked after it, when the job doesn't move it) */
                a->neutral_wait = 180;
                float home[3];
                memcpy(home, t->home, sizeof home);
                ai_return_to_job(a);
                if (a->patrol < 0) ai_move_near(a, home, 0.0f, false, 0.0f);
                return;
            }
            if (near) {
                /* waiting to be asked: facing the player, a call knowledge_now() and then */
                look_at(a, &p->c);
                ai_halt(a);
                if (!t->patience) a->neutral_wait = 180;
                else t->patience--;
                if (t->idle) {
                    t->idle--;
                    return;
                }
                t->idle = ((trac_random() * 0x5au) >> 16) + 0x168;
                if (d[N_TRIGGER_SOUND]) say(n, 4, d + N_TRIGGER_SOUND);
                chr_do_animation(&n->c, CHR_ANIM_WHEN_CAN, ANIM_CALL);
                return;
            }
            float r = rdf(d + N_TRIGGER) * 1.5f;   /* 0x52a774 */
            if (d2 <= r * r) {
                /* after the player, every 20 ticks */
                if (knowledge_now() <= t->follow_time + 20) return;
                ai_move_near(a, p->c.pos, 25.0f, false, 0.0f);
                t->follow_time = knowledge_now();
                return;
            }
            ai_return_to_job(a);
            return;
        }
    }
    uint32_t count = rd16(d + N_LINES);
    if ((uint32_t)t->line >= count || t->line >= NEUTRAL_LINES_MAX) {
        a->neutral_flags |= NEUTRAL_FINISHED | NEUTRAL_DONE;
        ai_return_to_job(a);
        return;
    }
    const uint8_t *line = d + N_LINE + t->line * LINE_SIZE;
    uint16_t lf = rd16(line);
    Chr *speaker = lf & LF_PLAYER ? p : n, *listener = lf & LF_PLAYER ? n : p;
    if (!t->line_begun) {
        if (line[8]) {
            ChrSpeech s = { .type = 3 };
            memcpy(s.name, line + 8, sizeof s.name - 1);
            speech_say(&speaker->c, &s, false);
        }
        if (lf & LF_GIVE) {
            a->movement_mode = AI_MODE_WALK;
            ai_move_near(a, p->c.pos, 20.0f, false, 0.0f);
        } else {
            t->anim_done[0] = t->anim_done[1] = false;
        }
        t->line_begun = true;
    }
    bool busy = false;
    if (!(lf & LF_GIVE)) {
        if (rd16(line + 4)) busy = conversation_animation(speaker, rd16(line + 4), (lf & LF_WAIT_SPEAKER) != 0, false, &t->anim_done[0]);
        if (rd16(line + 6)) busy |= conversation_animation(listener, rd16(line + 6), (lf & LF_WAIT_LISTENER) != 0, false, &t->anim_done[1]);
    } else {
        bool go = t->giving;
        if (!go) {
            if (dist2(n->c.pos, p->c.pos) < 400.0f) {   /* 0x529dec */
                t->giving = true;
                t->give_done[0] = t->give_done[1] = false;
                t->give_wait = 15;
                go = true;
            } else {
                busy = true;
            }
        }
        if (go) {
            busy |= conversation_animation(n, ANIM_GIVE, true, !t->given, &t->give_done[0]);
            if (t->give_done[0]) {
                if (t->give_wait) t->give_wait--;
                if (t->give_wait) {
                    busy = true;
                } else {
                    busy |= conversation_animation(p, ANIM_GIVE, true, !t->given, &t->give_done[1]);
                    if (!t->given && n->c.type == ANIM_GIVE && n->c.frame >= rd16((const uint8_t *)n->c.anim + TRAM_ACTION_FRAME)) {
                        give(n, p);
                        a->neutral_line = (uint16_t)(t->line + 1);
                        t->giving = false;
                        t->given = true;
                    }
                }
            }
        }
    }
    if (line[8]) busy |= speaker->c.speaking;
    if (busy) return;
    t->line_begun = false;
    t->line++;
    a->neutral_line = (uint16_t)t->line;
}

bool neutral_try_interaction(Chr *player, bool start)
{
    if (player->c.talk_partner) return true;
    Chr *found = NULL;
    bool talking = false;
    for (int i = 0; i < chars_slots(); i++) {
        Chr *c = chars_get(i);
        if (!c || (c->c.flags & 1) || !c->c.ai) continue;
        if (c->ai.state == AI_STATE_NEUTRAL) {
            /* 0x4acc90: its player, near and facing it */
            if (c->ai.talk.player == player && facing_near(c, player) && !talking) {
                found = c;
                talking = true;
            }
        } else if (!found && check_trigger(c, player, false)) {
            found = c;
        }
    }
    if (!found) return false;
    if (start) {
        if (!talking) enter_state(found);
        found->ai.talk.accepted = true;
    }
    return true;
}

void neutral_filter_input(Chr *player, CharacterInput *in)
{
    Character *c = &player->c;
    if (!c->talk_partner) return;
    uint64_t k = chr_input_bits(in);
    const uint64_t blows = CHR_KEY_PUNCH | CHR_KEY_KICK, moves = 0x1e600000u;
    if ((k & blows) || (k & (CHR_KEY_FIRE | CHR_KEY_ALT_FIRE | ((uint64_t)0x100 << 32)))) {
        neutral_stop(player, false, true, false, true);
    } else if (!(k & moves)) {
        if (c->talk_moves) c->talk_moves -= 2;
    } else {
        if (!(c->flags & 0x800000)) c->talk_moves += 2;
        if (c->talk_moves > 14) neutral_stop(player, false, true, false, true);
    }
    /* still talking: no movement, blows, jumps or the action keys (the turn keys and walk stay) */
    if (c->talk_partner) chr_input_mask(in, k & ((uint64_t)0xfffffe01u << 32 | 0x819fffffu));
}

void neutral_died(Chr *c)
{
    if (c->c.talk_partner) neutral_stop(c, true, true, true, true);
}

void neutral_deleted(Chr *c)
{
    Character *o = c->c.talk_partner;
    if (!o) return;
    c->c.flags &= ~0x800000u;
    o->flags &= ~0x800000u;
    o->talk_partner = NULL;
    c->c.talk_partner = NULL;
}
