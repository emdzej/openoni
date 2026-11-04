/* The level's object database: the BINA instances named "CJBO<type>" (Character, Door, Flag, Furniture,
   Trigger Volume, ...) each hold an OBJC chunk in the .raw: a header ("OBJC", size, version 39), then
   objects, each a u32 size and that many bytes: tag (four characters, stored reversed: "RAHC" for
   CHAR), id, flags, position, rotation (degrees), then the type's own data. Format: docs/formats/objc.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t tag;           /* 'CHAR', 'DOOR', 'FLAG', ... as TM_TAG writes them */
    uint32_t id, flags;
    float pos[3], rot[3];
    const uint8_t *data;    /* the type's own data, past the common header */
    uint32_t size;
} ObjcObject;

/* Calls fn for every object of the loaded level files (the last file loaded first), stopping when it
   returns false. type: the BINA name's part after "CJBO" ("Character"), NULL for every type. */
typedef bool (*ObjcFn)(const ObjcObject *o, void *user);
void objc_each(const char *type, ObjcFn fn, void *user);

/* CHAR's data (OBJcCharacter, read by 0x4bf290): flags, then the class name, the character's name, the
   weapon. Flags: 0x01 the player (ONrGameState_NewCharacter 0x4dac50 gives it the player's controller),
   0x04 not there at the start (a script spawns it). */
enum { OBJC_CHAR_PLAYER = 0x01, OBJC_CHAR_NOT_INITIAL = 0x04 };
uint32_t objc_char_flags(const ObjcObject *o);
const char *objc_char_class(const ObjcObject *o);      /* 64 bytes at +4 */
const char *objc_char_name(const ObjcObject *o);       /* 32 bytes at +0x44 */
const char *objc_char_weapon(const ObjcObject *o);     /* 64 bytes at +0x64 */
/* Then eight scripts of 32 bytes (spawn, die, combat, alarm, hurt, defeated, out of ammo, no path), the
   extra health at +0x1a4 and, at +0x1cc, the team (inferred from the data: the player 0, the warehouse's
   training dummies 3, its thugs 2; ONrGameState_NewCharacter copies it to the character's +0x12). */
uint32_t objc_char_team(const ObjcObject *o);
/* Script k (32 bytes from +0xa4, "" none). AI2rInitializeCharacter 0x48a6b0 copies them to the
   character (C+0x48, +0x88, +0x68, +0xa8, +0xc8, +0xe8, +0x108, +0x128); the death hook 0x4eb550 runs
   C+0x88 as "death" and C+0xe8 as "defeated", which confirms those two. */
enum { OBJC_SCRIPT_SPAWN, OBJC_SCRIPT_DIE, OBJC_SCRIPT_COMBAT, OBJC_SCRIPT_ALARM, OBJC_SCRIPT_HURT,
       OBJC_SCRIPT_DEFEATED, OBJC_SCRIPT_OUT_OF_AMMO, OBJC_SCRIPT_NO_PATH, OBJC_SCRIPTS };
const char *objc_char_script(const ObjcObject *o, int k);
int32_t objc_char_extra_health(const ObjcObject *o);  /* +0x1a4 */
/* The job (+0x1a8: 3 patrol) and its patrol path's id (the low u16 of +0x1ac). */
enum { OBJC_JOB_PATROL = 3 };
uint32_t objc_char_job(const ObjcObject *o);
uint16_t objc_char_job_path(const ObjcObject *o);
/* The melee profile's id (the u16 at +0x1b0: 0x27 THUG_Wh for the warehouse thugs, inferred from the
   data; 0xffff none). */
uint16_t objc_char_melee(const ObjcObject *o);
/* The combat settings' id (the u16 at +0x1ae: a Combat object's, AI2rInitializeCharacter 0x48a6b0). */
uint16_t objc_char_combat(const ObjcObject *o);
/* The weapon's ammo, a percentage (+0x1d0, D+0x1c4 in memory: ONrGameState_NewCharacter 0x4dac50
   passes it to 0x519b30; 100 for most, 10 for one of level 1's strikers). */
uint32_t objc_char_ammo(const ObjcObject *o);
/* The initial alert level (+0x1d4, C+0xf94: 1 for the thugs, 0 for Konoko). */
uint32_t objc_char_alert(const ObjcObject *o);
/* The alert at or above which the AI goes back to its job (+0x1dc, C+0xf9c: AI2rInitializeCharacter
   0x48a6b0 copies +0x1d4..+0x1e0 to C+0xf94..C+0xfa0). */
uint32_t objc_char_job_alert(const ObjcObject *o);

/* FLAG's data (the flag type's read method 0x4c55b0): a colour (u32), a prefix (u16), the id (u16 at +6,
   what OBJiFlag's search 0x4c56c0 compares), a note (128 bytes). ONrLevel_Flag_ID_To_Flag 0x506c80
   finds the flag by id; its place is the object's position. False if there is none. */
bool objc_flag_find(uint16_t id, ObjcObject *out);
