/* The AI (AI2, docs/re/port-plan.md step 9). So far its executor (AI2rExecutor_Update 0x498550): what
   the AI wants, a kind of move, a movement mode and a facing, made into the character's input, the
   way the player's keys are. The movement layer (AI2rMovement) is a stand-in: it walks the global
   path the rooms' graph gives (graph.h: from door to door, then to the point), straight between its
   points, without the rooms' grids (the local paths). Notes:
   docs/re/character.md, "The AI's executor". */
#pragma once
#include "targeting.h"
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

/* The kinds of move (A+0x1a9c) and the movement modes (A+0x1a98, A+0x1aa0). */
enum { AI_MOVE_FORWARD, AI_MOVE_BACK, AI_MOVE_LEFT, AI_MOVE_RIGHT, AI_MOVE_NONE };
enum { AI_MODE_DEFAULT = 0, AI_MODE_CROUCH = 3, AI_MODE_WALK = 4, AI_MODE_WALK_AIMING = 5, AI_MODE_RUN = 6,
       AI_MODE_RUN_AIMING = 7 };

struct Chr;
struct KnowEntry;

struct MeleeProfile;
struct MeleeTech;

/* The melee state (the combat state's melee part, C+0xf70 + 0x458.., and the executor's attack
   override A+0x1acc..): see melee.h. */
enum { MELEE_MEMORY = 32 };
typedef struct {
    const struct MeleeProfile *profile;     /* C+0x1068 */
    const struct MeleeTech *tech;           /* +0x0c: the technique under way */
    int move;                               /* +0x34: its move */
    bool move_started;
    bool pressed;                           /* +0x40: the attack's keys are pressed */
    uint32_t move_ticks;
    float leftover;                         /* +0x1c */
    float memory[MELEE_MEMORY];             /* +0xa4: the attacks done lately (1000 failed) */
    uint32_t available[MELEE_MEMORY];       /* C+0x106c: when a technique with a delay is free again */
    float dist, angle;                      /* +0x80, +0x88 */
    float face;                             /* +0x98: the facing wanted, 1000 the target */
    bool cookie;                            /* +0x484: may attack */
    uint32_t cookie_until;                  /* +0x488 */
    /* the executor's attack override */
    bool override;                          /* A+0x1acc */
    uint16_t override_type, override_prev;  /* A+0x1ace, A+0x1ad0 */
    uint64_t override_held, override_down;  /* A+0x1ad8, A+0x1ae0 */
} MeleeState;

/* The combat settings (C+0x1028..0x1050, AI2rInitializeCharacter 0x48a6b0 copies them from the
   Combat object of the CHAR's id, else the defaults). */
typedef struct {
    uint16_t id;                /* C+0x1028 */
    float medium;               /* C+0x102c: the medium range */
    float near;                 /* C+0x1030: the short range */
    int behaviour[5];           /* C+0x1034: for the ranges 1..5 (COMBAT_RANGE_*) */
    int no_gun;                 /* C+0x104c: when the unarmed fight (COMBAT_NO_GUN_*) */
    int no_gun_else;            /* C+0x1050: what they do otherwise (0 melee, 1 retreat, 2 the alarm) */
    /* the alarm's (the object's +0x7c..+0x8c): how far a console is looked for, the enemy distance
       beyond which it is ignored and within which it is fought, the damage that makes it fight, and
       how long a fight lasts before the alarm again (ticks) */
    float alarm_find, alarm_ignore, alarm_chase;    /* C+0xfec, 0xff0, 0xff4 */
    uint32_t alarm_damage, alarm_fight;             /* C+0xff8, 0xffc */
    float pursuit_distance;     /* C+0x1054 (the object's +0x68): how far from its job it pursues, 0 any */
    /* how long a non-combatant panics (ticks, and up to half again): at a melee sound, gunfire, a
       sighting, a hurt (C+0x1058..0x1064, the object's +0x6c..+0x78) */
    uint32_t panic_melee, panic_gunfire, panic_sight, panic_hurt;
} CombatSettings;

/* The guard's scan (AI2rGuard_UpdateScan 0x499b50: the pursuit's look around, at its block +0x2c): a
   look direction picked now and then, within a range of a facing (or all round), swept a little. */
typedef struct {
    bool on;                    /* +0x00: about the facing (else all round) */
    float face;                 /* +0x04: that facing, 1000 the character's at the pick */
    float range;                /* +0x08: the pick's range about it */
    float center;               /* +0x0c: the pick's centre, 1000 all round */
    float first;                /* +0x10: the first look, 1000 the character's facing */
    bool started, again;        /* +0x14, +0x15 */
    float angle;                /* +0x18: the look picked */
    float v[3];                 /* +0x1c: along it (30 long) */
    float sweep;                /* +0x28: the sweep's offset */
    float sweep_range;          /* +0x2c: 0 none */
    float look[3];              /* +0x30: the direction looked along */
    uint32_t hold_base, hold_rand;      /* +0x3c, +0x40: ticks a look is held (the base and a random part) */
    uint32_t sweep_base, sweep_rand;    /* +0x44, +0x48: ticks between sweeps */
    uint32_t hold, sweep_timer;         /* +0x4c, +0x50 */
} AiScan;

/* The pursuit's modes (+0x0c, the CHAR's pursuit fields). */
enum { PURSUIT_NONE, PURSUIT_FORGET, PURSUIT_GOTO, PURSUIT_WAIT, PURSUIT_LOOK, PURSUIT_MOVE, PURSUIT_HUNT,
       PURSUIT_GLANCE };

/* The pursuit state's block (C+0x8b8 when C+0x1f8 is 6; +0x08, the contact, is target_entry). */
typedef struct {
    int mode;                   /* +0x0c: PURSUIT_* */
    bool strong;                /* +0x10: the contact was seen (its highest strength above 3) */
    bool alerted;               /* +0x11: the alert above low */
    bool reached;               /* +0x12: the contact's place reached once */
    uint32_t vocal;             /* +0x14: ticks to the next vocalization */
    uint32_t halt;              /* +0x18: ticks held still before the mode starts */
    uint32_t timer;             /* +0x1c: the mode's ticks left, 0 none */
    uint32_t start;             /* +0x20 */
    uint32_t close;             /* +0x24: ticks seen close on the way */
    bool run;                   /* +0x28: hurrying (0x4b0f90) */
    bool looked;                /* +0x29: the scan begun */
    AiScan scan;                /* +0x2c */
} PursuitState;


/* Barabbas's behaviours' part of the combat block (+0x2c..+0x40, boss.c). */
typedef struct {
    int mode, prev;             /* +0x2c, +0x30 */
    uint32_t shot_wait;         /* +0x34: ticks after a shot before the next */
    uint32_t regen_damage;      /* +0x38: the damage taken while healing */
    uint32_t regen_wait;        /* +0x3c: ticks before he may heal again */
    uint32_t retreat;           /* +0x40: ticks of backing off */
} BarabbasState;

/* Mutant Muro's behaviours' part (+0x2c..+0x54, boss.c). */
typedef struct {
    uint32_t engaged;           /* +0x2c: 720 once his behaviour has run (counted down, set again) */
    uint32_t charge;            /* +0x30: ticks of a thunderbolt's chance (the target far or in the open) */
    uint32_t cooldown;          /* +0x34: ticks without one */
    uint32_t drop_length;       /* +0x38: the shield's drop, its length */
    uint32_t drop;              /* +0x3c: ticks of it left */
    uint32_t off;               /* +0x40: ticks the shield stays down */
    uint32_t rise;              /* +0x44: ticks of its rise left */
    uint32_t rise_length;       /* +0x48 */
    uint32_t up;                /* +0x4c: ticks it has been full */
    uint32_t damage;            /* +0x50: the damage taken lately */
    uint32_t damage_timer;      /* +0x54: ticks before it is forgotten */
} MuroState;

/* The super ninja's behaviours' part (+0x2c..+0x58, boss.c). */
typedef struct {
    int mode;                   /* +0x2c: 1 melee, 2 fireball, 3 advance, 4 the alternate fireball, 5 teleport
                                   to a flag, 6 teleport behind the target */
    int prev;                   /* +0x30 */
    uint32_t fire_cool, alt_cool, teleport_cool;    /* +0x34, +0x38, +0x3c */
    bool fire_started, alt_started, teleport_started;   /* +0x44, +0x45, +0x46: their animations under way */
    bool rolled, chance;        /* +0x47, +0x48: the even chance between the alternate and the teleport */
    uint32_t count;             /* +0x4c: weights messages to the damage's next decay */
    uint32_t damage_hold;       /* +0x50: ticks before the damage taken decays */
    uint32_t damage;            /* +0x54: the damage taken lately */
    uint32_t fallen;            /* +0x58: ticks below the level (message 13) */
} NinjaState;

/* The combat state's block (C+0x8b8, its +8 on: what the behaviours get). */
enum { COMBAT_MANEUVERS = 7 };
typedef struct {
    float dist;                 /* +0x18: to the target's point */
    int range;                  /* +0x1c: COMBAT_RANGE_* */
    int behaviour;              /* +0x20 */
    bool check_env, check_friends, aim, fire;   /* +0x5c..+0x5f (behaviour message 5) */
    bool alt;                   /* +0x60: the alternate fire */
    uint32_t melee_wait;        /* +0x64 */
    uint32_t backed, backed2;   /* +0x68, +0x6c */
    bool gun_rolled, gun_go;    /* +0x71, +0x72: the class's chance to go for a gun drawn, and its outcome */
    bool run_rolled, run_go;    /* +0x73, +0x74: the chance to pick it up running */
    uint32_t hurt_any_time;     /* +0x84: of the last hit by the target (message 10) */
    uint32_t hurt_time;         /* +0x88: of the last hit hand to hand by the target */
    union {                     /* +0x2c..: the bosses' behaviours' own (boss.c) */
        BarabbasState barabbas;
        MuroState muro;
        NinjaState ninja;
    };
    bool targeting;             /* +0x8c */
    AiTargeting t;              /* +0x90 */
    const void *weapon;         /* +0x180: the weapon the targeting has */
    bool weapon_alt;            /* +0x184 */
    bool firing, firing_alt;    /* +0x185, +0x186 */
    bool lof, lof2;             /* +0x187, +0x188: a clear line of fire */
    int maneuver;               /* +0x18c: COMBAT_MANEUVER_* */
    float weight[COMBAT_MANEUVERS];     /* +0x194 */
    float adv_near, adv_far;    /* +0x1b8, +0x1bc: the advance's */
    float ret_near, ret_far;    /* +0x1c4, +0x1c8: the retreat's */
    float gun_near, gun_far;    /* +0x1cc, +0x1d0: the gun's (the farthest looked for) */
    bool gun_running;           /* +0x1d4: picked up running */
    struct Weapon *gun;         /* +0x1d8: the one it goes for */
    uint32_t gun_thwarted;      /* +0x1dc: maneuvers there (or stopped) without reaching it */
    uint32_t gun_below;         /* +0x1e0: maneuvers next to it but not level with it */
    uint32_t gun_ignored_n;     /* +0x1e4 */
    const struct Weapon *gun_ignored[4];    /* +0x1e8: the ones found out of reach */
    int alarm_marker;           /* +0x200: the alarm maneuver's console marker */
    bool dazed;                 /* +0x189: lying through the daze (behaviour message 6) */
    int getup;                  /* +0x1f8: 4 none, else the getting up's weight is given */
    bool melee_ok;              /* +0x454: may fight hand to hand */
    bool fall_check;            /* +0x5d8: message 13 below fall_y */
    float fall_y;               /* +0x5dc */
} CombatState;

/* The executor's aim (A+0x1a88..A+0x1ab8, A+0x2195..A+0x21c8). */
typedef struct {
    bool on, stop, at_point, weapon_speed;  /* A+0x1a88..0x1a8b */
    float at[3];                /* A+0x1a8c: a point, or a direction */
    float speed;                /* A+0x1ab4 */
    bool done;                  /* A+0x1ab8 */
    float last_facing;          /* A+0x1aac, 1000 none */
    bool aiming;                /* A+0x2195 */
    float point[3];             /* A+0x2198 */
    float yaw, pitch;           /* A+0x21c4, 0x21c8 */
    int look;                   /* the movement's (+0x1584): 0 none, 1 a point, 2 a direction, 3 a character */
    float look_at[3];           /* +0x1588 */
    const Character *look_char;
    bool fire, fire_alt;        /* A+0x21e0, 0x21e1: the trigger held */
} AiAim;

/* The neutral state's block (C+0x8b8 +8 when C+0x1f8 is 8; AI2rNeutral, neutral.c). */
enum { NEUTRAL_LINES_MAX = 10, NEUTRAL_DATA = 0x26c };
typedef struct {
    struct Chr *player;         /* +0x08 */
    uint8_t osd[NEUTRAL_DATA];  /* +0x0c: the Neutral's data (docs/formats/objc.md, "NEUT") */
    bool accepted;              /* +0x278: the player asked (the action key) */
    bool line_begun;            /* +0x279 */
    bool anim_done[2];          /* +0x27a, 0x27b: the speaker's and the listener's animations played */
    int32_t line;               /* +0x27c: -1 not talking yet */
    uint32_t idle;              /* +0x280: ticks to the next call */
    uint32_t patience;          /* +0x284: ticks it waits for the player to ask */
    uint32_t follow_time;       /* +0x288: the last move after the player */
    uint32_t enemy_time;        /* +0x28c: the last look for enemies */
    float home[3];              /* +0x290: where it started */
    bool ended;                 /* +0x29c: the abort or enemy line said */
    bool giving, given;         /* +0x29d, 0x29e */
    bool give_done[2];          /* +0x29f, 0x2a0: its and the player's give animations played */
    uint32_t give_wait;         /* +0x2a4 */
} NeutralTalk;

/* The AI's states (C+0x1f8). */
enum { AI_STATE_NONE = 0, AI_STATE_IDLE = 1, AI_STATE_PATROL = 3, AI_STATE_COMBAT = 5, AI_STATE_PURSUIT = 6,
       AI_STATE_ALARM = 7, AI_STATE_NEUTRAL = 8, AI_STATE_PANIC = 9 };
/* Its flags (C+0x1f0, AI2rInitializeCharacter 0x48a6b0 starts them at 1). */
enum { AI_FLAG_KNOWLEDGE = 0x01, AI_FLAG_PASSIVE = 0x02, AI_FLAG_NON_AGGRESSIVE = 0x04, AI_FLAG_COMBAT_SCRIPT = 0x08,
       AI_FLAG_ALARM_SCRIPT = 0x10, AI_FLAG_END_SCRIPT = 0x20, AI_FLAG_OUT_OF_AMMO = 0x40, AI_FLAG_NO_PATH = 0x80,
       AI_FLAG_NO_DEGRADE = 0x400, AI_FLAG_BLIND = 0x800, AI_FLAG_DEAF = 0x1000, AI_FLAG_IGNORE_PLAYER = 0x2000, AI_FLAG_STARTLED = 0x8000 };

typedef struct {
    int mode;               /* A+0x1a98: 0 the default for the alert level */
    int movement_mode;      /* the movement's (+0x14a4, ai2_setmovementmode): 0 by the alert level */
    int kind;               /* A+0x1a9c */
    float facing;           /* A+0x1aa4 */
    bool full_turn;         /* A+0x1ab0: walking turns at the full rate */
    float turn_once;        /* A+0x1aa8: the turn rate for one tick, 0 none */
    float facing_override;  /* A+0x1ac8: 1000 none (reset every tick) */
    bool key_override;      /* A+0x1ab9: these keys for one tick (AI2rExecutor_MoveOverride 0x498b60) */
    uint64_t override_keys; /* A+0x1ac0 */
    bool daze_started;      /* C+0x101c: this fall's daze has begun */
    uint32_t healed;        /* C+0x1024: the hit points Barabbas has healed */
    uint32_t daze_timer;    /* C+0x1020: ticks left lying */
    int alert;              /* C+0xf94: 0 lull .. 4 combat */
    /* the alarm (alarm.c) */
    uint32_t alarm_groups;  /* C+0xfd8: the alarm groups it answers (bits) */
    int alarm_marker;       /* C+0xfdc: the console's action marker it runs to, -1 none */
    int alarm_last;         /* C+0xfe0: the one its tries are counted for */
    uint32_t alarm_tries;   /* C+0xfe4 */
    uint32_t alarm_fight_timer;     /* C+0xfe8: ticks of fighting left before the alarm again */
    float alarm_goal[3];    /* the alarm state's block +8: 5 back from the marker */
    uint32_t alarm_wait;    /* +0x14: ticks to wait, not standing, before trying again */
    int state;              /* C+0x1f8: AI_STATE_* */
    int job;                /* C+0x1f4: the state it goes back to (ai2_setjobstate) */
    int job_patrol;         /* the job's patrol path (C+0x200's copy of the state block), -1 none */
    int job_point;          /* and the waypoint it was at */
    int job_alert;          /* C+0xf9c: the alert at or above which leaving combat goes back to the job */
    int pursue_alert;       /* C+0xfa0: the least alert that pursues (the CHAR's +0x1e0) */
    /* the CHAR's pursuit modes (+0x1e8..+0x1f8): for a strong and a weak contact below medium alert,
       the same at medium or above, and what a lost one does (0 the job, 1 keep looking, 2 the alarm) */
    int pursue_strong_low, pursue_weak_low, pursue_strong_high, pursue_weak_high, pursue_lost; /* C+0xfb4..0xfc4 */
    bool job_pos_set;       /* C+0xfc8: the place of its job, kept while at it */
    float job_pos[3];       /* C+0xfcc */
    PursuitState pursuit;
    /* the combat state (C+0x8b8's block when C+0x1f8 is 5) */
    bool combat_lost;               /* +0x0c: the target dead, gone or forgotten */
    int combat_last_strength;       /* +0x14: the target's contact's strength, last told */
    bool combat_taunt_ready;        /* +0x75 (nothing in the port sets it yet) */
    bool combat_taunted;            /* +0x76 */
    uint32_t combat_wait_reload;    /* +0x78 */
    uint32_t combat_wait;           /* +0x7c: ticks before it gives up, once lost */
    uint32_t combat_pursue_delay;   /* +0x80: ticks before it pursues a contact it lost sight of */
    uint32_t flags;         /* C+0x1f0: AI_FLAG_* */
    /* knowledge (knowledge.h) */
    struct KnowEntry *know; /* C+0xf7c: the contacts, the most important first */
    int know_count;         /* C+0xf78 */
    uint32_t glance;        /* C+0xf74: ticks left of a glance at a contact */
    uint32_t alert_time;    /* C+0xf90: when the alert went above low */
    uint32_t vocal_delay;   /* C+0xf8c */
    uint32_t startle_time;  /* C+0xfa4 */
    uint32_t startle_ticks; /* C+0xfa8 */
    struct Chr *target;     /* C+0x8c0: the state's target (combat, pursuit) */
    struct KnowEntry *target_entry;  /* C+0x8c8 (combat), C+0x8c0 (pursuit) */
    MeleeState melee;
    CombatSettings combat_set;
    CombatState combat;
    AiAim aim;
    uint64_t held;          /* A+0x2188: last tick's keys */
    const char *name;       /* the character's (C+0x14), for the scripts it runs */
    /* the patrol (AI2rPatrol, C+0xf70's patrol state) */
    int patrol;             /* the path, -1 none */
    int point;              /* +0x528 */
    uint32_t wait;          /* the pause's ticks left */
    bool point_started;
    uint32_t script;        /* +0x5ac: the script a point of type 0x15 waits for (sl_start), 0 none */
    bool ignore_player;     /* +0x538 */
    /* the stand-in mover */
    bool moving;
    float goal[3];
    bool goal_facing;
    float goal_angle;
    float goal_radius;      /* arrived within this */
    const Character *follow;    /* +0x10f4 with the path's type 2 (+0x10f0): the character followed, its
                                   place the goal (AI2rPath_FollowCharacter 0x4ae580), NULL none */
    /* the global path (AI2iPath_MakePath 0x4aeb10, graph.h): the links to cross, then the goal */
    int path[199];          /* +0x1174 (GRAPH_PATH_MAX) */
    int path_n, path_k;     /* the links, the next one */
    int path_from, path_to; /* +0x116c, +0x1170: the rooms it was made for */
    float path_goal[3];     /* +0x1120 */
    uint32_t path_time;     /* +0x110c */
    bool path_made;
    uint32_t ticks;
    /* the local path through the room (AI2rMovementState_MakePath 0x4a9f20, localpath.h) */
    struct LocalPoint *local;   /* LOCAL_PATH_MAX of them, +0x8b8 */
    int local_n, local_k;       /* +0x18b8, +0x18ba */
    int local_seg;              /* the global path's link it was made for (path_n: the last room), -1 none */
    /* the neutral behaviour (neutral.h, C+0x1000..0x1018) */
    int neutral_id;             /* C+0x100c: the Neutral object's id, -1 none */
    uint16_t neutral_line;      /* C+0x100e: the line a conversation starts (or resumes) at */
    uint32_t neutral_flags;     /* C+0x1000: NEUTRAL_* */
    uint32_t neutral_time;      /* C+0x1004: the last automatic check (and the one put off to) */
    uint32_t neutral_wait;      /* C+0x1008: ticks before it may start one by itself again */
    float neutral_trigger;      /* C+0x1010: the Neutral's trigger range */
    float neutral_enemy;        /* C+0x1014: and its enemy range (100 without one) */
    char end_script[32];        /* C+0x1018: the conversation's end script to run (flag 0x20) */
    NeutralTalk talk;           /* the neutral state's block (C+0x8b8 when C+0x1f8 is 8) */
    /* the panic state's block (C+0x8b8 +8 when C+0x1f8 is 9; panic.c) */
    struct KnowEntry *panic_entry;  /* +0x08: what it fears */
    uint32_t panic_until;           /* +0x0c */
    bool panic_cower;               /* +0x10: crouching */
    bool panic_dazed;               /* +0x11: lying through its daze */
    bool panic_known;               /* +0x12: the fear is someone's hostility */
    uint32_t panic_vocal;           /* +0x14: ticks to its next cry */
} AiState;

void ai_init(AiState *a);
/* AI2rExitState 0x48ae80: the state left (the job's patrol kept for its return). */
void ai_exit_state(AiState *a);
/* AI2rCombat_SetNewTarget 0x4908e0's resets, for the contact e (the combat state's target entry). */
struct KnowEntry;
void ai_combat_set_target(AiState *a, struct KnowEntry *e);
/* AI2rCombat_NotifyKnowledge 0x490510: what the combat state does with a change of its contacts. */
void ai_combat_notify(AiState *a, const Character *c, struct KnowEntry *e, bool degraded);
/* 0x48af80 (with both flags, as the combat state calls it): back to the job, or idle when the alert is
   below the job's start level. */
void ai_return_to_job(AiState *a);
/* AI2rPath_Halt 0x4ae660: no path (+0x10f0) and the movement's state cleared (0x4a8440), so no move
   goes to the executor (the port's mover would leave its last one: it is stopped too). */
void ai_halt(AiState *a);
/* AI2rExecutor_Move 0x498b00. */
void ai_executor_move(AiState *a, float facing, int mode, int kind, bool full_turn);
/* One tick of a character the AI moves: the stand-in mover, then the executor, then the character's
   update with the input it made. */
void ai_update(AiState *a, Character *c, const void *env);
/* The player under ai2_takecontrol: its AI's movement and executor only, no states (the player isn't
   an AI, C+0x1e8 0). */
void ai_control_update(AiState *a, Character *c, const void *env);
/* ai2_takecontrol 1 (AI2iScript_TakeControl 0x4b8940): the run's turn undone, the movement flag, the
   movement and path reset (AI2rExecutor_Initialize). */
void ai_take_control(AiState *a, Character *c);
/* The stand-in mover: walk to a point (then face angle when face). */
void ai_move_to(AiState *a, const float p[3], bool face, float angle);
/* The same, arriving within radius (and not stopping there when through). */
void ai_move_near(AiState *a, const float p[3], float radius, bool face, float angle);
/* AI2rPath_FollowCharacter 0x4ae580: the path's goal is the character, wherever it goes (to within 12);
   NULL halts. */
void ai_follow(AiState *a, const Character *who);
/* The pursuit state (C+0x1f8 6, C+0x8c0 the contact), as the combat and alarm states enter it
   (AI2rExitState, then AI2rEnterState on C+0x8b8's block). */
void ai_pursue(AiState *a, struct KnowEntry *e);
