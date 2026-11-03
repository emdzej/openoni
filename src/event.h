/* Event lists (ONrEventList, 0x4f3960..0x4f3e00): what a console's use or a door's opening sets off.
   Stored in the object's OBJC data (ONrEventList_Read 0x4f39e0): a u16 count, then each event a u16
   type and its argument, by the type: a u16 id, or (Execute Script) a 32-byte function name. Run by
   0x4f3cc0, each through its type's handler (the table at 0x552098). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    EVENT_SCRIPT = 1, EVENT_TURRET_ON = 2, EVENT_TURRET_OFF = 3, EVENT_CONSOLE_ON = 4, EVENT_CONSOLE_OFF = 5,
    EVENT_ALARM_ON = 6, EVENT_ALARM_OFF = 7, EVENT_TRIGGER_ON = 8, EVENT_TRIGGER_OFF = 9, EVENT_LOCK_DOOR = 10,
    EVENT_UNLOCK_DOOR = 11,
};

/* The list's size in bytes from p (at most avail), 0 if it doesn't fit or has an unknown type. */
uint32_t event_list_size(const uint8_t *p, uint32_t avail);
/* 0x4f3cc0: every event of the list, in order. who: the character that set it off (its name goes to the
   scripts; NULL: "none"). */
void event_list_run(const uint8_t *p, const char *who);
