/* The AI (AI2, docs/re/port-plan.md step 9). So far its executor (AI2rExecutor_Update 0x498550): what
   the AI wants, a kind of move, a movement mode and a facing, made into the character's input, the
   way the player's keys are. The movement layer (AI2rMovement) is a stand-in: it walks the global
   path the rooms' graph gives (graph.h: from door to door, then to the point), straight between its
   points, without the rooms' grids (the local paths). Notes:
   docs/re/character.md, "The AI's executor". */
#pragma once
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
} CombatSettings;

/* The targeting (AI2rTargeting, the combat state's +0x90): where the target will be, the aim toward
   it from the weapon, and when to fire. */
enum { TARGETING_MEMORY = 64 };
typedef struct {
    int axis;                   /* +0x00: the barrel's axis (0 x) */
    const Character *target;    /* +0x08 */
    const uint8_t *cls;         /* +0x0c: the class's AI block (ONCC+0x138) */
    bool active;                /* +0x14: aiming a weapon */
    float speed;                /* +0x18: the target's */
    float predict;              /* +0x1c: how much of its motion is led */
    unsigned head, count;       /* +0x20, +0x24: the target's places, a ring of cls+0x18 */
    float ring[TARGETING_MEMORY][3];    /* +0x28 */
    float vel_short[3], vel_long[3];    /* +0x2c, +0x38 */
    float point[3];             /* +0x44: where it is aimed at (the target, led) */
    bool has_point;             /* +0x50 */
    bool inaccurate;            /* +0x51: the first shots' error on */
    float inaccuracy;           /* +0x54 */
    float grouping;             /* +0x58 */
    float offset[3];            /* +0x5c */
    bool first;                 /* +0x68: not on target since the last delay */
    uint32_t off_ticks;         /* +0x6c */
    uint32_t delay;             /* +0x70: ticks before it may fire */
    uint32_t close_ticks;       /* +0x74 */
    int mode;                   /* +0x78: 1 aiming the weapon, 2 looking */
    float aim[3];               /* +0x7c */
    float from[3];              /* +0x88: the shooter's place (its pelvis) */
    float muzzle[3];            /* +0x94 */
    float barrel[3];            /* +0xa0 */
    float vector[3];            /* +0xac: from the muzzle to the aim */
    float dist, aim_dist;       /* +0xb8, +0xbc */
    float close;                /* +0xc0: 1 within 10, 0 beyond 25 */
    bool stopped, on_target, fire;      /* +0xc4, +0xc5, +0xc6 */
    float error, tolerance;     /* +0xc8, +0xcc */
    const uint8_t *params;      /* +0xd0: the weapon's AI firing mode (ONWC+0xd0, +0x150 the alternate) */
    const uint8_t *skill;       /* +0xd4: the class's shooting skill for it */
    float matrix[12];           /* +0xdc: the weapon's (the body's node 19) */
    bool ballistic;             /* +0xe0: a ballistic solution (or none needed) */
    float bal_h, bal_v, bal_angle;      /* +0xe4, +0xe8, +0xec */
} AiTargeting;

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
    uint32_t hurt_time;         /* +0x88: of the last hit (message 10) */
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
    int getup;                  /* +0x1f8 */
    bool melee_ok;              /* +0x454: may fight hand to hand */
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

/* The AI's states (C+0x1f8). */
enum { AI_STATE_NONE = 0, AI_STATE_IDLE = 1, AI_STATE_PATROL = 3, AI_STATE_COMBAT = 5, AI_STATE_PURSUIT = 6,
       AI_STATE_ALARM = 7, AI_STATE_NEUTRAL = 8, AI_STATE_PANIC = 9 };
/* Its flags (C+0x1f0, AI2rInitializeCharacter 0x48a6b0 starts them at 1). */
enum { AI_FLAG_KNOWLEDGE = 0x01, AI_FLAG_PASSIVE = 0x02, AI_FLAG_NON_AGGRESSIVE = 0x04, AI_FLAG_NO_DEGRADE = 0x400,
       AI_FLAG_BLIND = 0x800, AI_FLAG_DEAF = 0x1000, AI_FLAG_IGNORE_PLAYER = 0x2000, AI_FLAG_STARTLED = 0x8000 };

typedef struct {
    int mode;               /* A+0x1a98: 0 the default for the alert level */
    int kind;               /* A+0x1a9c */
    float facing;           /* A+0x1aa4 */
    bool full_turn;         /* A+0x1ab0: walking turns at the full rate */
    float turn_once;        /* A+0x1aa8: the turn rate for one tick, 0 none */
    float facing_override;  /* A+0x1ac8: 1000 none (reset every tick) */
    int alert;              /* C+0xf94: 0 lull .. 4 combat */
    int state;              /* C+0x1f8: AI_STATE_* */
    int job;                /* C+0x1f4: the state it goes back to (ai2_setjobstate) */
    int job_patrol;         /* the job's patrol path (C+0x200's copy of the state block), -1 none */
    int job_point;          /* and the waypoint it was at */
    int job_alert;          /* C+0xf9c: the alert at or above which leaving combat goes back to the job */
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
    bool ignore_player;     /* +0x538 */
    /* the stand-in mover */
    bool moving;
    float goal[3];
    bool goal_facing;
    float goal_angle;
    float goal_radius;      /* arrived within this */
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
/* AI2rExecutor_Move 0x498b00. */
void ai_executor_move(AiState *a, float facing, int mode, int kind, bool full_turn);
/* One tick of a character the AI moves: the stand-in mover, then the executor, then the character's
   update with the input it made. */
void ai_update(AiState *a, Character *c, const void *env);
/* The stand-in mover: walk to a point (then face angle when face). */
void ai_move_to(AiState *a, const float p[3], bool face, float angle);
/* The same, arriving within radius (and not stopping there when through). */
void ai_move_near(AiState *a, const float p[3], float radius, bool face, float angle);
