/* The corpses: see corpse.h. */
#include "corpse.h"
#include "body.h"
#include "m3gm.h"
#include "tm.h"
#include "trigger.h"

#include <string.h>

enum { ENTRY_SIZE = 0x44c, ENTRIES = 0x18 };

static Corpse corpses[CORPSE_MAX];
static uint32_t nstatic, next, cap;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

/* the level's CRSA: the last loaded file's (each level has one, unnamed) */
static const uint8_t *level_array(uint32_t *size)
{
    for (int s = tm_file_slots() - 1; s >= 0; s--) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (in->tmpl->tag == TM_TAG("CRSA") && in->data) {
                *size = in->size - 8;
                return in->data;
            }
        }
    }
    return NULL;
}

void corpse_level_begin(void)
{
    memset(corpses, 0, sizeof corpses);
    nstatic = next = cap = 0;
    uint32_t size = 0;
    const uint8_t *a = level_array(&size);
    if (!a || size < ENTRIES) return;
    nstatic = rd32(a + 0x0c);
    cap = rd32(a + 0x14);
    if (cap > CORPSE_MAX) cap = CORPSE_MAX;
    if (nstatic > cap) nstatic = cap;
    for (uint32_t i = 0; i < nstatic && ENTRIES + (i + 1) * ENTRY_SIZE <= size; i++) {
        const uint8_t *e = a + ENTRIES + i * ENTRY_SIZE;
        Corpse *k = &corpses[i];
        memcpy(k->name, e, sizeof k->name - 1);
        k->oncc = tm_link(e + 0xa0);
        for (int b = 0; b < CHR_BONES; b++)
            for (int j = 0; j < 12; j++) k->m[b][j] = rdf(e + 0xa4 + 0x30 * b + 4 * j);
        for (int j = 0; j < 6; j++) k->box[j] = rdf(e + 0x434 + 4 * j);
    }
    /* the characters' slots after them, emptied (the level's own may have been saved with some) */
    next = nstatic;
}

/* 0x4e5ff0's box: the pelvis's sphere, grown by the parts 1 to 10 (the legs, the torso, the head) */
static void body_box(const Character *c, float box[6])
{
    const void *body = body_of_class(c->oncc, 4);
    uint32_t bones = body ? body_bones(body) : 0;
    for (int j = 0; j < 3; j++) box[j] = box[j + 3] = c->pos[j];
    for (uint32_t b = 0; b <= 10 && b < bones && b < CHR_BONES; b++) {
        float bb[6], sph[4] = { 0, 0, 0, 0 }, w[3];
        m3gm_bounds(body_geometry(body, b), bb, sph);
        body_transform(c->bone_world[b], sph, w);
        for (int j = 0; j < 3; j++) {
            float lo = w[j] - sph[3], hi = w[j] + sph[3];
            if (b == 0 || lo < box[j]) box[j] = lo;
            if (b == 0 || hi > box[j + 3]) box[j + 3] = hi;
        }
    }
}

void corpse_add(const Character *c)
{
    if (cap <= nstatic || next >= cap) return;
    Corpse *k = &corpses[next];
    memset(k, 0, sizeof *k);
    strcpy(k->name, "dynamic_corpse");
    k->oncc = c->oncc;
    memcpy(k->m, c->bone_world, sizeof k->m);
    body_box(c, k->box);
    if (++next >= cap) next = nstatic;
}

void corpse_clear_volume(int volume)
{
    /* OBJrTriggerVolume_IntersectsBBox_Approximate 0x4cc630: the box's diagonal through the volume */
    for (uint32_t i = nstatic; i < cap; i++) {
        Corpse *k = &corpses[i];
        if (k->oncc && trigger_segment(volume, k->box, k->box + 3)) k->oncc = NULL;
    }
}

int corpse_count(void) { return (int)cap; }
const Corpse *corpse_get(int i) { return i >= 0 && (uint32_t)i < cap ? &corpses[i] : NULL; }

bool corpse_visible(int i)
{
    Corpse *k = (Corpse *)corpse_get(i);
    if (!k) return false;
    if (!k->listed) vis_list(k->box, VIS_LIST_MAX, &k->leaves), k->listed = true;
    return vis_list_visible(&k->leaves);
}
