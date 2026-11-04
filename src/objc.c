/* The object database: see objc.h. */
#include "objc.h"

#include "tm.h"

#include <string.h>

enum { BINA_TAG = 0x42494e41, OBJC_VERSION = 39 };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

void objc_each(const char *type, ObjcFn fn, void *user)
{
    for (int s = tm_file_slots() - 1; s >= 0; s--) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag != BINA_TAG || !in->data || !in->name || strncmp(in->name, "BINACJBO", 8)) continue;
            if (type && strcmp(in->name + 8, type)) continue;
            size_t rs;
            const uint8_t *raw = tm_raw(in->data, &rs);
            uint32_t size = rd32(in->data), off = rd32(in->data + 4);
            if (!raw || (uint64_t)off + size > rs || size < 12 || memcmp(raw + off, "CJBO", 4) ||
                rd32(raw + off + 8) != OBJC_VERSION) continue;
            const uint8_t *p = raw + off + 12, *end = raw + off + 4 + rd32(raw + off + 4);
            if (end > raw + off + size) end = raw + off + size;
            while (p + 4 <= end) {
                uint32_t n = rd32(p);
                if (n < 36 || p + 4 + n > end) break;
                ObjcObject o;
                o.tag = (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
                o.id = rd32(p + 8);
                o.flags = rd32(p + 12);
                for (int k = 0; k < 3; k++) o.pos[k] = rdf(p + 16 + 4 * k), o.rot[k] = rdf(p + 28 + 4 * k);
                o.data = p + 40;
                o.size = n - 36;
                if (!fn(&o, user)) return;
                p += 4 + n;
            }
        }
    }
}

uint32_t objc_char_flags(const ObjcObject *o) { return o->size >= 4 ? rd32(o->data) : 0; }
const char *objc_char_class(const ObjcObject *o) { return o->size >= 0x44 ? (const char *)o->data + 4 : ""; }
const char *objc_char_name(const ObjcObject *o) { return o->size >= 0x64 ? (const char *)o->data + 0x44 : ""; }
const char *objc_char_weapon(const ObjcObject *o) { return o->size >= 0xa4 ? (const char *)o->data + 0x64 : ""; }
int32_t objc_char_extra_health(const ObjcObject *o) { return o->size >= 0x1a8 ? (int32_t)rd32(o->data + 0x1a4) : 0; }
const char *objc_char_script(const ObjcObject *o, int k)
{
    return k >= 0 && k < OBJC_SCRIPTS && o->size >= 0xa4 + 32u * (unsigned)(k + 1) ? (const char *)o->data + 0xa4 + 32 * k : "";
}
uint32_t objc_char_team(const ObjcObject *o) { return o->size >= 0x1d0 ? rd32(o->data + 0x1cc) : 0; }

uint32_t objc_char_job(const ObjcObject *o) { return o->size >= 0x1ac ? rd32(o->data + 0x1a8) : 0; }
uint16_t objc_char_job_path(const ObjcObject *o) { return o->size >= 0x1b0 ? (uint16_t)rd32(o->data + 0x1ac) : 0; }

uint16_t objc_char_melee(const ObjcObject *o) { return o->size >= 0x1b2 ? (uint16_t)(o->data[0x1b0] | o->data[0x1b1] << 8) : 0xffff; }
uint16_t objc_char_neutral(const ObjcObject *o) { return o->size >= 0x1b4 ? (uint16_t)(o->data[0x1b2] | o->data[0x1b3] << 8) : 0xffff; }
uint16_t objc_char_combat(const ObjcObject *o) { return o->size >= 0x1b0 ? (uint16_t)(o->data[0x1ae] | o->data[0x1af] << 8) : 0; }
uint16_t objc_char_inventory(const ObjcObject *o, int k, bool drop)
{
    unsigned off = 0x1b4 + 4u * (unsigned)k + (drop ? 2u : 0u);
    return o->size >= off + 2 ? rd16(o->data + off) : 0;
}
uint32_t objc_char_ammo(const ObjcObject *o) { return o->size >= 0x1d4 ? rd32(o->data + 0x1d0) : 100; }
uint32_t objc_char_alarm_groups(const ObjcObject *o) { return o->size >= 0x1e8 ? rd32(o->data + 0x1e4) : 0; }
uint32_t objc_char_alert(const ObjcObject *o) { return o->size >= 0x1d8 ? rd32(o->data + 0x1d4) : 0; }
uint32_t objc_char_job_alert(const ObjcObject *o) { return o->size >= 0x1e0 ? rd32(o->data + 0x1dc) : 0; }
uint32_t objc_char_pursue_alert(const ObjcObject *o) { return o->size >= 0x1e4 ? rd32(o->data + 0x1e0) : 2; }
uint32_t objc_char_pursuit(const ObjcObject *o, int k)
{
    static const uint32_t none[5] = { 5, 1, 6, 4, 0 };     /* AI2rInitializeCharacter 0x48a6b0's defaults */
    return o->size >= 0x1ecu + 4u * (unsigned)k ? rd32(o->data + 0x1e8 + 4 * k) : none[k];
}

typedef struct { uint16_t id; ObjcObject *out; bool found; } FlagFind;

static bool flag_match(const ObjcObject *o, void *user)
{
    FlagFind *f = user;
    if (o->size < 8 || (uint16_t)(o->data[6] | o->data[7] << 8) != f->id) return true;
    *f->out = *o;
    f->found = true;
    return false;
}

bool objc_flag_find(uint16_t id, ObjcObject *out)
{
    FlagFind f = { id, out, false };
    objc_each("Flag", flag_match, &f);
    return f.found;
}
