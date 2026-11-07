/* The AI's movement layer (AI2rMovement, AI2iMovementState, AI2rPath; docs/re/ai.md "The movement"):
   between the states, which ask for a path to a point, a facing, a look and modifiers, and the
   executor (ai.c), which makes a facing, a kind of move and a mode into keys. The path (AI2rPath_Update
   0x4ae670) walks the rooms' graph and each room's local path; the movement state (0x18f8 bytes, A+0x190)
   keeps what the character ran into, and AI2iMovementState_Calculate_Movement 0x4aa250 steers around it
   toward the path's next point, turning, slowing and stopping as the class can. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { MOVE_COLLISIONS = 16, MOVE_ARCS = 64 };
/* a collision's flags: hit this tick, aged (not hit since it was made or renewed), confirmed */
enum { MOVE_HIT = 1, MOVE_OLD = 2, MOVE_CONFIRMED = 4 };
/* its kinds: a wall (matched by its quad), an object or a character (by its id) */
enum { MOVE_WALL = 0, MOVE_OBJECT = 1 };

/* AI2iMovementState_MakeCollision 0x4ab6b0's record (0x44 bytes, at +0x1c). */
typedef struct {
    uint32_t flags;         /* +0x00 */
    int kind;               /* +0x04 */
    uintptr_t id;           /* +0x08 */
    uint32_t quad;          /* +0x0c: a wall's (its high bit for the back of a two-sided one) */
    float made[3];          /* +0x10: the character's location at the last hit */
    float point[3];         /* +0x1c: the point hit */
    float plane[4];         /* +0x28 */
    uint16_t age, hits;     /* +0x38: ticks since it aged, +0x3a */
    float angle;            /* +0x3c: the direction into it, atan2(-nx, -nz) */
    float strength;         /* +0x40 */
} MoveCollision;

/* A breakpoint of the arcs' costs (+0x460, 16 bytes each). */
typedef struct {
    float cost, add, angle;
    int next;
} MoveArc;

typedef struct {
    bool aiming;            /* +0x00: the weapon out (AI2rMovementState_NewMovementMode 0x4a9300) */
    bool rekind;            /* +0x08: the kind of move to be worked out again */
    bool no_side_walk;      /* +0x09: walking, no sideways moves (the class's sidestep is a turned run) */
    bool no_side_run;       /* +0x0a: and running */
    bool look_ahead;        /* +0x0b: the look within 90 degrees of the move */
    bool waiting;           /* +0x0c: arrived, the path halted after +0x10 ticks */
    uint32_t wait;          /* +0x10 */
    unsigned ncoll;         /* +0x14 */
    unsigned nconfirmed;    /* +0x18 */
    MoveCollision coll[MOVE_COLLISIONS];
    unsigned narc;          /* +0x45c */
    MoveArc arc[MOVE_ARCS];
    bool stuck;             /* +0x860: a collision confirmed for 30 hits */
    unsigned counted;       /* +0x864: the modifiers that counted */
    bool reweigh;           /* +0x868: the modifiers' weights worked out again */
    bool has_point;         /* +0x86c: the local path's point (a->local[a->local_k]) */
    float vec[3];           /* +0x870: from the character's location to it */
    float dist2;            /* +0x87c: on the ground, squared */
    float dir;              /* +0x880: the move's direction, 1000 none */
    float last_dir;         /* +0x884 */
    float hold;             /* +0x888: the facing held while it stands, 1000 none */
    int kind;               /* +0x88c: AI_MOVE_* */
    bool arrived_seen;      /* +0x890: the arrival's facing set (0x4a9b10) */
    bool slowed;            /* +0x891 */
    int slow_mode;          /* +0x894 */
    bool stop_turn;         /* +0x898: stopped to turn */
    bool stop_arrive;       /* +0x899: stopped to arrive, +0x89a its ticks */
    uint16_t arrive_ticks;
    uint16_t kind_ticks;    /* +0x89c: before the kind may change again */
    bool near_all;          /* +0x18c4: every point passed within 3 */
    int room, prev_room;    /* +0x18bc, +0x18c0: the local path's room and the one before (-1 none) */
} MoveState;

struct AiState_;
struct Chr;
struct Character;

/* AI2rMovementState_Initialize 0x4a8d60's: the state as a character starts. */
void movement_init(MoveState *m);
/* AI2rMovementState_NewMovementMode 0x4a9300 (AI2rMovement_ChangeMovementMode 0x4a8200 after the mode
   is set; AI2rMovement_NotifyAlertChange 0x4a8230 when it is by the alert): the weapon out or not. */
void movement_new_mode(struct Chr *ch);
/* AI2iMovementState_FindClearPoint 0x4a9170: p moved (x and z) off an obstacle of its room's layer the
   character may not ignore (mode 0), to a nearby point (AI2rFindNearbyPoint: 15 toward its facing, at
   least 5, 30 degrees a step, 6 steps): not off a door it can't open, nor off a character farther than 6
   (its location). True if moved. */
bool movement_find_clear_point(const struct Character *c, float p[3]);
/* AI2rMovementState_Initialize 0x4a8d60's end without a path (ONrGameState_NewCharacter, a character the
   AI moves): put on a clear point (ONrCharacter_Teleport 0x4e9210) if it stands on an obstacle. */
void movement_place_clear(struct Chr *ch);
void movement_alert_change(struct Chr *ch);
/* HandleStartSidestep 0x4f83f0's 0x4a93c0: a sidestep done as a turned run, walking or not. */
void movement_no_sidestep(struct Chr *ch, bool walk);
/* AI2rMovementState_NotifyWallCollision 0x4ab5b0, 0x4ab830 (a physics context) and 0x4ab920 (a
   character): the character ran into it (the point hit, the plane). */
void movement_hit_wall(struct Chr *ch, uint32_t quad, const float point[3], const float plane[4]);
void movement_hit_object(struct Chr *ch, uintptr_t id, const float point[3], const float plane[4]);
void movement_hit_character(struct Chr *ch, const struct Chr *other, const float point[3], const float plane[4]);
/* AI2rPath_Update 0x4ae670: the arrival, the doors, the rooms passed and the local path's point. */
void movement_path_update(struct Chr *ch, const void *env);
/* AI2rPath_GoToPoint 0x4ae480 makes the path at once (the port makes a given path at the movement's
   next update): a path given and not yet made is made now. True if it failed now (the pathfinding's
   report, which AI2iManeuver_CheckAdvance's error hook 0x49e370 notes in 0x5eb8a4). */
bool movement_path_now(struct Chr *ch, const void *env);
/* AI2rPath_SimplePath 0x4aef90: the path arrived, or within one room with its local path's last waypoint next (0x4a9520). */
bool movement_path_short(const struct Chr *ch);
/* The movement state's update 0x4a97b0 (the collisions' decay, then Calculate_Movement), for
   AI2rMovement_Update 0x4a8550: the executor's move and aim. */
void movement_update(struct Chr *ch);
/* 0x4a9510: the way the movement goes this tick (+0x880), 1000 none. */
float movement_direction(const struct Chr *ch);
