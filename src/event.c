/* Event lists: see event.h. */
#include "event.h"
#include "alarm.h"
#include "chars.h"

#include "console.h"
#include "door.h"
#include "laser.h"
#include "trigger.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The argument's size by the type (the table's kind: 1 a u16, 3 a 32-byte name; 2, a u32, no type has). */
static uint32_t arg_size(uint16_t type)
{
    if (type == EVENT_SCRIPT) return 32;
    if (type >= EVENT_TURRET_ON && type <= EVENT_UNLOCK_DOOR) return 2;
    return UINT32_MAX;
}

uint32_t event_list_size(const uint8_t *p, uint32_t avail)
{
    if (avail < 2) return 0;
    uint32_t n = rd16(p), at = 2;
    for (uint32_t k = 0; k < n; k++) {
        if (at + 2 > avail) return 0;
        uint32_t s = arg_size(rd16(p + at));
        if (s == UINT32_MAX || at + 2 + s > avail) return 0;
        at += 2 + s;
    }
    return at;
}

void event_list_run(const uint8_t *p, const char *who)
{
    if (!p) return;
    uint32_t n = rd16(p), at = 2;
    for (uint32_t k = 0; k < n; k++) {
        uint16_t type = rd16(p + at);
        const uint8_t *a = p + at + 2;
        uint32_t s = arg_size(type);
        if (s == UINT32_MAX) return;
        uint16_t id = rd16(a);
        switch (type) {
        case EVENT_SCRIPT: {
            /* 0x4f3d40: deferred (0x4f2070) with the character's name, or "none", the cause "eventlist" */
            char name[33];
            memcpy(name, a, 32);
            name[32] = 0;
            if (name[0]) trigger_defer(name, who ? who : "none");
            break;
        }
        case EVENT_CONSOLE_ON: console_activate(id); break;     /* 0x4f3d90 -> 0x4c09a0 */
        case EVENT_CONSOLE_OFF: console_deactivate(id); break;  /* 0x4f3da0 -> 0x4c09f0 */
        case EVENT_TRIGGER_ON: laser_activate(id); break;       /* -> 0x4cb200 */
        case EVENT_TRIGGER_OFF: laser_deactivate(id); break;    /* -> 0x4cb280 */
        case EVENT_LOCK_DOOR: door_lock_id(id, true); break;    /* 0x4f3de0 -> OBJrDoor_Lock_ID */
        case EVENT_UNLOCK_DOOR: door_lock_id(id, false); break; /* 0x4f3df0 -> OBJrDoor_Unlock_ID */
        case EVENT_ALARM_ON: alarm_notify(id, who ? chars_find(who) : NULL); break;   /* 0x49ac30 */
        case EVENT_ALARM_OFF: break;                            /* 0x4cae10: nothing */
        default:
            /* turrets (0x4cd990, 0x4cda80) aren't ported */
            break;
        }
        at += 2 + s;
    }
}
