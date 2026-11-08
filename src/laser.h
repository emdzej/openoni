/* The laser triggers (the "Trigger" object type, OBJrTrigger_Initialize 0x4cb420): a row of emitters
   (the TRIG class's TRGE, at most 32) sweeping along the class's object animation, each casting a beam
   500 units into the level. The player crossing a beam sets off the trigger's event list (an alarm, a
   script), once until no beam is crossed. The beams blink by the trigger's on and off times. Scripts:
   trig_activate, trig_deactivate, trig_reset, trig_hide, trig_show, trig_speed; event lists: 8 and 9.

   As the original, the beams are tested where they are drawn (OBJrTrigger_OnTrigger 0x4cb6f0, the type's
   draw), so on the frames that are drawn. The active trigger's ambient sound (TRIG +0x24) plays at
   the object's place, and so does the hit's impulse. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The level's triggers (OBJiTrigger_Read 0x4cbf30 and OBJiTrigger_SetOSD 0x4cc1f0), after the objects. */
int laser_load(void);
void laser_reset(void);
/* The level's environment the beams are cast into (AKEV). */
void laser_set_environment(const void *env);
/* The type's update (0x4cb100), once a tick: the emitters move, the on and off times count. */
void laser_update(void);
/* The draw's test (0x4cb6f0), once a drawn frame: every beam cast, the player tested. */
void laser_frame(void);

/* What the draw draws: emitter k of trigger i (its geometry under m, NULL none) and its beam (false
   when it has none this frame: inactive, off or hidden). */
int laser_count(void);
int laser_emitters(int i);
const void *laser_emitter(int i, int k, float m[12]);
typedef struct { float from[3], to[3]; uint32_t colour; } LaserBeam;
bool laser_beam(int i, int k, LaserBeam *b);

void laser_activate(uint16_t id);
void laser_deactivate(uint16_t id);
void laser_register_commands(void);
