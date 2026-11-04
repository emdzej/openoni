/* Trigger volumes (OBJ's "Trigger Volume" type, OBJrTriggerVolume_Initialize 0x4cc960): oriented boxes in
   the level that call a BSL function when a character enters, stays inside or leaves them
   (ONrGameState_UpdateTrigger 0x4e0cf0), and the trigvolume_* script commands. Format:
   docs/formats/objc.md (TRGV). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* What a trigger needs to know of a character. inside: the ids of the volumes the character is in, four at
   most (the character's slots at +0x168 in the original); 0 is a free slot. Owned by the caller, kept
   between updates. */
typedef struct {
    float pos[3];           /* the feet */
    uint32_t team;          /* bit in the volume's team mask */
    bool player;            /* the 0x80 flag (player only) passes just the player */
    bool alive;
    bool dead;              /* C+0x04 bit 0: a body (out of the living list) */
    const char *name;       /* passed to the scripts */
    uint32_t inside[4];
} TriggerActor;

enum {
    TRIGGER_ONCE_ENTRY = 0x01, TRIGGER_ONCE_INSIDE = 0x02, TRIGGER_ONCE_EXIT = 0x04,
    TRIGGER_NO_ENTRY = 0x08, TRIGGER_NO_INSIDE = 0x10, TRIGGER_NO_EXIT = 0x20,
    TRIGGER_DISABLED = 0x40, TRIGGER_PLAYER_ONLY = 0x80,
};

/* Reads the level's volumes from the loaded files (objc.h); returns how many. */
int trigger_load(void);
void trigger_unload(void);
int trigger_count(void);
/* Volume i's name, id and flags (now), for tests. */
const char *trigger_name(int i);
uint32_t trigger_id(int i);
uint32_t trigger_flags(int i);
/* Volume i's script now (kind 0 entry, 1 inside, 2 exit), "" none. */
const char *trigger_script(int i, int kind);
/* Volume i's box: its origin, axes (unit, by column) and size. */
void trigger_box(int i, float origin[3], float axis[3][3], float size[3]);
/* Volume i's middle (the box: origin + axis * (0..size)). */
void trigger_centre(int i, float c[3]);
/* Whether a character (its feet at pos) is in volume i, ignoring the team and flags. */
bool trigger_contains(int i, const float pos[3]);

/* One tick: every volume, every character (ONrGameState_UpdateTrigger for each volume), the scripts
   queued and then started with the character's name (sl.h). */
void trigger_update(TriggerActor *actors, int n);
/* 0x4f2070: a script deferred, with the character's name, to the end of the next volume's update (the
   event lists' "Execute Script" queues there too). */
void trigger_defer(const char *func, const char *who);
/* 0x4f2120: runs the queue now (the volumes' update ends with it). */
void trigger_flush(void);
/* Registers trigvolume_enable, _reset, _setscript, _trigger and _count with the script engine. */
void trigger_register_commands(void);
