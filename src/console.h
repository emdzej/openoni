/* Consoles (OBJ's "Console" type, registered by 0x4c0fe0's group with ONrMechanics_Register): the
   level's CONS objects. The console's body is the environment's gunk; the object draws its screen (the
   class's screen geometry with the texture of its state: inactive, active, used: 0x4c0c80). Each has an
   action marker (ONrLevel_ActionMarker_New 0x506ee0) where a character stands to use it with the
   action key; using an active console plays its event list (OBJrConsole_OnActivate 0x4c0880). Format:
   docs/formats/objc.md (CONS); notes: docs/re/console.md. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    CONSOLE_USED = 0x02,            /* triggered (run time) */
    CONSOLE_ACTIVE = 0x04,          /* run time */
    CONSOLE_ACTIVE_AT_START = 0x08, /* the OSD's: the level's start and console_reset copy it to 0x04 */
    CONSOLE_PUNCH = 0x20,           /* used with the punch (animation type 0xdd) instead of 0xc4 */
    CONSOLE_ALARM = 0x40,           /* an alarm console: the AI runs to it (AI2rAlarm_FindAlarmConsole) */
};

/* Reads the level's consoles (objc.h) and does what OBJiConsole_LevelBegin 0x4c1c20 does: the marker,
   the start state. Returns how many. */
int console_load(void);
void console_unload(void);
int console_count(void);
uint16_t console_id(int i);
uint16_t console_flags(int i);

/* 0x4c09a0, 0x4c09f0, 0x4c0a40: by id, 0xffff every console. Reset: not used, active as at the start. */
void console_activate(uint16_t id);
void console_deactivate(uint16_t id);
void console_reset(uint16_t id);

/* The action markers (ONLV +0x74, 0x14 bytes each: the console, the place, the facing). */
typedef struct {
    int console;
    float pos[3];
    float facing;
    float floor;            /* the console's own y (D+0x10): the console action's target height */
} ActionMarker;
/* 0x506f20: the marker nearest p (distance squared), -1 none. */
int console_marker_nearest(const float p[3]);
const ActionMarker *console_marker(int i);
int console_marker_count(void);

/* OBJrConsole_OnActivate 0x4c0880: the sound of the state; an active, unused console becomes used and
   runs its events (who: the character's name). */
void console_on_activate(int i, const char *who);

/* The screen to draw (0x4c0c80): the class's screen geometry (M3GM), its texture for the console's
   state (TXMP, NULL: the geometry's own) and the matrix (T(position) R), 3x4 by columns. NULL if none. */
const void *console_screen(int i, const void **txmp, float m[12]);

/* console_activate, console_deactivate, console_reset. */
void console_register_commands(void);
