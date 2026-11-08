/* The characters' animation sounds: see sound_anim.h. */
#include "sound_anim.h"

#include "chr_anim.h"
#include "exe.h"
#include "sound.h"
#include "tm.h"
#include "tram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    KINDS = 30,             /* the kinds' names: the table at 0x5579d8, "Any" (0) to "Falling Flail" (29) */
    KIND_ANIMATION = 1,
    MOD_HEAVY = 3, MOD_MEDIUM = 4, MOD_LIGHT = 5,
    TYPE_TABLE = 0x557ad0,  /* 12 bytes an animation type below 0xe9: the type, the kind (-1 none), the modifier */
    TYPES = 0xe9,
    KIND_NAMES = 0x5579d8,  /* { name, value } pairs, a null name last; its value what a name not there gives */
    MOD_NAMES = 0x5579a0,   /* the modifiers': "Any" (0) to "Light Damage" (5) */
    NAME = 32,
};

/* A record (0x54 bytes in the original): +0 the kind, +4 the modifier, +8 the impulse as it was when
   added (the lookup finds it again by name), +0xc the frame, +0x10 the animation (kind 1 only), +0x14
   its name, +0x34 the impulse's name. */
typedef struct {
    uint32_t kind, modifier;
    const void *impulse;
    uint16_t frame;
    const void *tram;
    char anim[NAME + 1], impulse_name[NAME + 1];
} Record;

/* A variant's entry (0x7c bytes): the ONCV and an array of records for each kind. */
typedef struct {
    const uint8_t *variant;
    Record *rec[KINDS];
    uint32_t n[KINDS], cap[KINDS];
} Entry;

/* An animation's own sound (TRAM +0x150, +0x154): the run time sets it (0x428800), the files leave it 0.
   The original keeps a pointer to the record's name, in an array that moves as it grows; the port keeps
   the name. */
typedef struct {
    const void *tram;
    char name[NAME + 1];
    uint16_t frame;
} TramSound;

void (*sound_anim_log)(const char *line);
void (*sound_anim_trace)(const char *line);

static Entry *entries;          /* 0x6200b8 */
static uint32_t nentries;
static TramSound *tram_sounds;
static uint32_t ntram_sounds, cap_tram_sounds;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* UUrString_Compare_NoCase 0x4266d0: a..z folded. */
static int nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 0x20;
        if (y >= 'a' && y <= 'z') y -= 0x20;
        if (x != y || !x) return x - y;
    }
}

static void copy_name(char out[NAME + 1], const uint8_t *in)
{
    memcpy(out, in, NAME);
    out[NAME] = 0;
}

/* TRrAnimation_GetSoundName 0x4287b0 */
static TramSound *tram_sound(const void *tram)
{
    for (uint32_t i = 0; i < ntram_sounds; i++)
        if (tram_sounds[i].tram == tram) return &tram_sounds[i];
    return NULL;
}

static const char *tram_sound_name(const void *tram)
{
    TramSound *s = tram ? tram_sound(tram) : NULL;
    return s && s->name[0] ? s->name : NULL;
}

/* TRrAnimation_SetSoundName 0x428800: the animation's sound and its frame (a null name clears it). */
static void tram_set_sound(const void *tram, const char *name, uint16_t frame)
{
    TramSound *s = tram_sound(tram);
    if (!s) {
        if (!name) return;
        if (ntram_sounds == cap_tram_sounds) {
            uint32_t nc = cap_tram_sounds ? cap_tram_sounds * 2 : 16;
            TramSound *t = realloc(tram_sounds, nc * sizeof *t);
            if (!t) return;
            tram_sounds = t, cap_tram_sounds = nc;
        }
        s = &tram_sounds[ntram_sounds++];
        s->tram = tram;
    }
    if (name) snprintf(s->name, sizeof s->name, "%s", name);
    else s->name[0] = 0;
    s->frame = frame;
}

bool sound_anim_at_frame(const void *tram, uint16_t frame)
{
    if (!tram) return false;
    const TramSound *s = tram_sound(tram);
    if (!s || !s->name[0]) return frame == 0;
    return frame == s->frame;
}

/* The tables of names (0x5579d8, 0x5579a0): the value of the name, folded; a name not there gives the
   last entry's value for the kinds (its search ends on it), 0 for the modifiers (theirs keeps its 0). */
static int32_t name_value(uint32_t table, const char *name, bool end_value)
{
    uint32_t at = table;
    for (uint32_t s; (s = exe_u32(at)) != 0; at += 8) {
        const char *n = exe_str(s);
        if (n && !nocase(n, name)) return (int32_t)exe_u32(at + 4);
    }
    return end_value ? (int32_t)exe_u32(at + 4) : 0;
}

static Entry *entry_of(const void *variant)
{
    for (uint32_t i = 0; i < nentries; i++)
        if (entries[i].variant == variant) return &entries[i];
    return NULL;
}

/* OSrVariant_SoundAnimation_Add 0x515a70. A record of kind 1 names an animation: one that already has a
   sound that is an impulse keeps it, else the record gives it its sound and frame. Of the other kinds a
   record whose modifier is taken by one with an impulse that exists is left out. */
static void add_record(Entry *e, uint32_t kind, uint32_t modifier, const char *anim, uint16_t frame, const char *impulse)
{
    const void *tram = NULL;
    if (kind == KIND_ANIMATION) {
        tram = tm_instance_get(TRAM_TAG, anim);         /* TMrInstance_GetDataPtr 0x4232e0 */
        const char *has = tram ? tram_sound_name(tram) : NULL;
        if (has && sound_impulse_find(has)) return;
    } else {
        for (uint32_t i = 0; i < e->n[kind]; i++)
            if (e->rec[kind][i].modifier == modifier) {
                if (sound_impulse_find(e->rec[kind][i].impulse_name)) return;
                break;
            }
    }
    if (e->n[kind] == e->cap[kind]) {
        uint32_t nc = e->cap[kind] ? e->cap[kind] * 2 : 4;
        Record *r = realloc(e->rec[kind], nc * sizeof *r);
        if (!r) return;
        e->rec[kind] = r, e->cap[kind] = nc;
    }
    Record *r = &e->rec[kind][e->n[kind]++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->modifier = modifier;
    r->impulse = sound_impulse_find(impulse);          /* OSrImpulse_GetByName 0x512440 */
    r->frame = frame;
    r->tram = tram;
    snprintf(r->anim, sizeof r->anim, "%s", kind == KIND_ANIMATION ? anim : "");
    snprintf(r->impulse_name, sizeof r->impulse_name, "%s", impulse);
    if (tram) tram_set_sound(tram, r->impulse_name, frame);
}

/* 0x47efe0, the binary data's chunk search: the first chunk of the tag in the len bytes from p, the
   tag's four bytes compared at every byte; its size (the u32 after the tag) no more than len. Its data,
   NULL if none. */
static const uint8_t *chunk(const uint8_t *p, size_t len, const char tag[4], uint32_t *size)
{
    for (size_t i = 0; i + 8 <= len; i++) {
        if (memcmp(p + i, tag, 4)) continue;
        uint32_t s = rd32(p + i + 4);
        if (s > len) return NULL;
        *size = s;
        return p + i + 8;
    }
    return NULL;
}

/* OSiSABinaryData_Load 0x5161d0: one blob. SAFT (a version) begins it; SAVT names the variant, whose
   records the blob replaces (the kind 1 ones taking the animations' sounds back); then a SASA for each
   record: the frame (u32), the modifier's name, the kind's, the animation's and the impulse's (32 bytes
   each). The next SASA is looked for from the last one's data, up to the blob's end. */
static void load_blob(const uint8_t *blob, uint32_t len)
{
    uint32_t size;
    if (!chunk(blob, len, "SAFT", &size)) return;
    const uint8_t *name = chunk(blob, len, "SAVT", &size);
    if (!name) return;
    char variant[NAME + 1];
    copy_name(variant, name);
    Entry *e = NULL;
    for (uint32_t i = 0; i < nentries && !e; i++)
        if (!nocase((const char *)entries[i].variant + 4, variant)) e = &entries[i];
    if (e) {
        for (uint32_t i = 0; i < e->n[KIND_ANIMATION]; i++)
            if (e->rec[KIND_ANIMATION][i].tram) tram_set_sound(e->rec[KIND_ANIMATION][i].tram, NULL, 0);
        for (int k = 0; k < KINDS; k++) e->n[k] = 0;
    }
    const uint8_t *end = blob + len, *at = name + NAME;
    for (const uint8_t *r; at < end && (r = chunk(at, (size_t)(end - at), "SASA", &size)) != NULL; at = r) {
        if (size < 4 + 4 * NAME || r + 4 + 4 * NAME > end) break;
        char mod[NAME + 1], kind[NAME + 1], anim[NAME + 1], impulse[NAME + 1];
        copy_name(mod, r + 4);
        copy_name(kind, r + 4 + NAME);
        copy_name(anim, r + 4 + 2 * NAME);
        copy_name(impulse, r + 4 + 3 * NAME);
        int32_t k = name_value(KIND_NAMES, kind, true), m = name_value(MOD_NAMES, mod, false);
        /* (a variant not in the list, or a kind not in the table, -1: the original writes through the
           null entry or the entry's variant; the CD has neither) */
        if (!e || k < 0 || k >= KINDS) continue;
        add_record(e, (uint32_t)k, (uint32_t)m, anim, (uint16_t)rd32(r), impulse);
    }
}

void sound_anim_unload(void)
{
    for (uint32_t i = 0; i < nentries; i++)
        for (int k = 0; k < KINDS; k++) free(entries[i].rec[k]);
    free(entries);
    entries = NULL;
    nentries = 0;
    free(tram_sounds);
    tram_sounds = NULL;
    ntram_sounds = cap_tram_sounds = 0;
}

void sound_anim_load(void)
{
    sound_anim_unload();
    /* OSrVariantList_Initialize 0x515c90: an entry for each variant of the list, in its order (the
       original sorts them by address for its bsearch) */
    const uint8_t *list = tm_instance_get(TM_TAG("ONVL"), "variant_list");
    if (!list) return;
    uint32_t n = rd32(list + 0x14);
    entries = calloc(n ? n : 1, sizeof *entries);
    if (!entries) return;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *v = tm_link(list + 0x18 + 4 * i);
        if (v) entries[nentries++].variant = v;
    }
    for (int s = 0; s < tm_file_slots(); s++) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (!in->data || !in->name || strncmp(in->name, "BINADBAS", 8)) continue;
            size_t rs;
            const uint8_t *raw = tm_raw(in->data, &rs);
            uint32_t size = rd32(in->data), off = rd32(in->data + 4);
            if (!raw || (uint64_t)off + size > rs || size < 8 || rd32(raw + off) != 0x53414244) continue;   /* SABD */
            uint32_t len = rd32(raw + off + 4);
            if ((uint64_t)len + 8 > size) continue;
            load_blob(raw + off + 8, len);
        }
    }
}

/* TRrAnimation_GetMaximumDamage 0x428900: the largest damage among the attacks. */
static uint32_t max_damage(const void *tram)
{
    uint32_t d = 0;
    const uint8_t *a;
    for (unsigned i = 0; (a = tr_attack(tram, i)) != NULL; i++)
        if (rd16(a + 0xc) > d) d = rd16(a + 0xc);
    return d;
}

/* One kind's records for the modifier: those of the modifier or of any (0); the first of the modifier
   ends the search, else the last of any's impulse stands (a missing one too). */
static const void *search(const Entry *e, uint32_t kind, uint32_t modifier, bool check_kind)
{
    const void *found = NULL;
    for (uint32_t i = 0; i < e->n[kind]; i++) {
        const Record *r = &e->rec[kind][i];
        if (check_kind && r->kind != kind) continue;
        if (r->modifier && r->modifier != modifier) continue;
        found = sound_impulse_find(r->impulse_name);
        if (r->modifier == modifier) break;
    }
    return found;
}

const void *sound_anim_impulse(const void *variant, const void *tram)
{
    if (!tram || !nentries) return NULL;
    uint16_t type = tr_type(tram);
    uint32_t row = TYPE_TABLE + 12u * (type < TYPES ? type : 0);
    int32_t kind = (int32_t)exe_u32(row + 4);
    uint32_t modifier = exe_u32(row + 8);
    const char *own = tram_sound_name(tram);
    if (kind == -1 && !own) return NULL;
    if (!modifier) {
        uint32_t d = max_damage(tram);
        modifier = d >= 15 ? MOD_HEAVY : d >= 10 ? MOD_MEDIUM : d ? MOD_LIGHT : 0;
    }
    const Entry *e = entry_of(variant);
    while (e) {
        const void *impulse = NULL;
        if (own) {
            impulse = sound_impulse_find(own);
            if (!impulse) {
                const char *f = exe_str(0x5587a0);  /* COrConsole_Printf: "failed to find impulse sound" */
                char m[160];
                const TmInstance *in = tm_handle_instance(tm_handle_of(tram));
                if (f && in && in->name && sound_anim_log) {
                    snprintf(m, sizeof m, f, own, in->name);
                    sound_anim_log(m);
                }
            }
        } else if (kind != KIND_ANIMATION) {
            impulse = search(e, (uint32_t)kind, modifier, true);
            if (!impulse) impulse = search(e, 0, modifier, false);
        }
        if (impulse) return impulse;
        /* none: the variant's parent's (ONCV +0) */
        const void *parent = tm_link(e->variant);
        e = parent ? entry_of(parent) : NULL;
    }
    return NULL;
}

void sound_anim_play(const void *variant, const void *tram, const float pos[3])
{
    const void *impulse = sound_anim_impulse(variant, tram);
    if (!impulse) return;
    if (sound_anim_trace) {
        const TmInstance *in = tm_handle_instance(tm_handle_of(tram));
        char m[128];
        snprintf(m, sizeof m, "OpenOni: animation sound %s (%s)", sound_impulse_name(impulse), in && in->name ? in->name : "?");
        sound_anim_trace(m);
    }
    sound_impulse_play_at(impulse, pos, NULL);                  /* OSrImpulse_Play 0x512480 */
}
