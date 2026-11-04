/* Doors (OBJ's "Door" type, OBJrDoor_Initialize 0x4c31a0): the level's DOOR objects. A closed door is
   part of the environment, its gunk (the quads the level's editor gave the door's object id): drawn and
   in the way. Opening hides the gunk and makes the door's panels, one or two objects of the level's
   object array (OBOA, the ids of the door and of the door | 0x1000), played by the door class's object
   animation: its frames 0 to "open" open it, the rest close it. A door opens for a character near it
   (the activation radius) that may pass (lock, key, side) and closes when nobody is (OBJrDoor_Update
   0x4c44f0). Format: docs/formats/objc.md (DOOR); notes: docs/re/door.md.

   Not yet: the panels' physics (the characters don't collide with a moving panel, and the panels'
   blocked counter, +0x140 of their animation context, stays 0, so the "stuck" logic never runs), the
   keys (WPrInventory_HasKey 0x424370: nobody holds one), the door's events, the graph's door links
   (PHrNotifyDoorStateChange 0x50ed90), the AI's door sound (0x49a8a0) and the sounds' place. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Reads the level's doors (objc.h) and does what OBJrDoor_LevelBegin 0x4c48a0 does for each: the door
   frame, the panels' elements, the gunk and its shade. env: the level's AKEV (its quads change). Returns
   how many. */
int door_load(void *env);
void door_unload(void);
int door_count(void);

/* The door's OSD fields as they are now (for tests): id, flags (DOOR_*), state (1 open), the OBJC id. */
uint16_t door_id(int i);
uint16_t door_flags(int i);
int door_state(int i);
uint32_t door_object_id(int i);
void door_position(int i, float pos[3]);
void door_centre(int i, float pos[3]);
/* (for the route driver) half its frame's width (0 unknown), its turn about y in radians */
float door_half_width(int i);
float door_facing(int i);     /* S+0xc0: the middle of its frame */
/* The panels' animation frame (-1 none: closed, no panels). */
int door_frame(int i);

enum {
    DOOR_LOCKED_AT_START = 0x0001,  /* the OSD's: LevelBegin makes it DOOR_LOCKED */
    DOOR_LOCKED = 0x0002,
    DOOR_HIDE_FRAME = 0x0004,       /* its door frame quad (AKDA) isn't drawn */
    DOOR_MANUAL = 0x0010,           /* opens with the action key (not yet), closes by itself */
    DOOR_ANIMATING = 0x0020,        /* also while it stays open, counting down */
    DOOR_STARTED = 0x0040,          /* LevelBegin ran */
    DOOR_MIRROR = 0x0100,           /* the first panel gets the half turns (else the second does) */
    DOOR_ONE_WAY = 0x0200,          /* a lock passes the characters of one side */
    DOOR_ONE_WAY_REVERSED = 0x0400, /* the other side */
    DOOR_JAMMED = 0x0800,           /* door_jam: nothing opens or closes it */
    DOOR_STUCK = 0x2000,
};

/* The panels' animations step (PHrPhysics_Update_Animation, from the physics update 0x4385e0, before the
   characters). now: the game time, ticks. */
void door_physics(uint32_t now);
/* OBJrDoor_Update for every door (the objects' update 0x507880, after the characters). */
void door_update(void);
/* OBJrDoor_Open 0x4c26c0 and OBJrDoor_Close 0x4c2590 (what door_open and door_close do). */
void door_open(int i);
void door_close(int i);

/* The action key's door (OBJrDoor_TryDoorAction 0x4c2a50): the door the character faces, -1 none;
   can: whether it may pass. door_action_open (0x4c2520) opens it if it may. */
struct Chr;
int door_try_action(const struct Chr *c, const float pelvis[3], bool *can);
bool door_action_open(int i, const struct Chr *c);

/* may_pass's lock test for a character at p, without keys: not jammed, and unlocked or one way with p
   on the side it lets through. */
bool door_lock_passes(int i, const float p[3]);

/* OBJrDoor_Lock_ID 0x4c1d80 (on) and OBJrDoor_Unlock_ID 0x4c1e30: every door of the id, 0xffff all. */
void door_lock_id(uint16_t id, bool on);

/* The panels to draw: door i's panel k (0, 1): its geometry array (M3GA, object.h's layout), NULL if
   there is no such panel; its matrix now (PHrPhysics_MaintainMatrix 0x43b410), 3x4 by columns. */
const void *door_panel(int i, int k, float m[12]);

/* door_open, door_close, door_lock, door_unlock, door_jam, door_unjam. */
void door_register_commands(void);
