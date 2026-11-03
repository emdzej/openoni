/* The sound system: SS2 (channels, groups, impulses, ambients) and the game's layer over it (music, dialog,
   the script commands). See sound.h; the data: docs/formats/osbd.md.

   Names in the comments are the Ghidra project's where it has one. Where names.tsv has none (or a Mac name
   that doesn't fit the function's role), the role is given with the address. The DirectSound buffer calls
   of the original (SetVolume, SetPan, SetFrequency, Play, Stop) are kept as the ds_* functions below, in
   DirectSound's units, so the volume and pan curves are the original's. */
#include "sound.h"
#include "sl.h"
#include "sndd.h"
#include "tm.h"
#include "trac.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* The two-argument clamps the original inlines everywhere: volumes to 0..2 (channels), 0..1 (scripts). */
static float clamp02(float v) { return v >= 0.0f ? (v > 2.0f ? 2.0f : v) : 0.0f; }
static float clamp01(float v) { return v >= 0.0f ? (v > 1.0f ? 1.0f : v) : 0.0f; }
/* The x87 conversions (0x4264e0 rounds to nearest, ties to even). */
static int32_t fround(double v) { return (int32_t)nearbyint(v); }

static SoundLogFn log_fn;
void sound_set_log(SoundLogFn fn) { log_fn = fn; }
static void logf_(const char *fmt, const char *a, const char *b, const char *c, const char *d)
{
    if (!log_fn) return;
    char m[256];
    snprintf(m, sizeof m, fmt, a ? a : "", b ? b : "", c ? c : "", d ? d : "");
    log_fn(m);
}

/* ---- the databases (SSrGroup_New 0x47fd90, SSrAmbient_New 0x481e10, SSrImpulse_New 0x480280) ---- */

enum { NAME = 32 };

typedef struct {
    uint32_t weight;
    float vol_min, vol_max, pitch_min, pitch_max;
    const void *sndd;           /* +0x14: the SNDD, resolved at level load or on first play */
    char name[NAME];            /* +0x18 */
} Perm;

enum { GROUP_NO_REPEAT = 1 };   /* +0x28 bit 0: don't pick the last permutation again */

typedef struct {
    char name[NAME];
    float volume, pitch;        /* +0x20, +0x24 */
    uint16_t flags, last;       /* +0x28, +0x2a: the last permutation picked */
    uint32_t channels;          /* +0x2c: 1 mono, 2 stereo: which channels the ambients take */
    Perm *perms;                /* +0x30 */
    uint32_t nperms;
} Group;

enum {
    AMB_INTERRUPT_ON_STOP = 1,  /* +0x24 bit 0: a stop cuts the sound (else the base track ends first) */
    AMB_PLAY_ONCE = 2,          /* bit 1: the base tracks don't loop */
    AMB_CAN_PAN = 4,            /* bit 2: its channels pan */
};

typedef struct {
    char name[NAME];
    int32_t priority;           /* +0x20 */
    uint32_t flags;             /* +0x24: AMB_* */
    float detail_range;         /* +0x28: details play up to this far (x and z) from the ambient */
    float detail_min, detail_max;   /* +0x2c, +0x30: seconds between details */
    float min_dist, max_dist;   /* +0x34, +0x38: full volume up to min, silent from max */
    uint32_t max_count;         /* +0x3c: how many may play at once */
    float min_occlusion;        /* +0x40 */
    const Group *detail, *base1, *base2, *in, *out;     /* +0x44..+0x54 */
    char group_names[5][NAME];  /* +0x58.. in the same order */
} Ambient;

typedef struct Impulse {
    char name[NAME];
    const Group *group;         /* +0x20 */
    char group_name[NAME];      /* +0x24 */
    int32_t priority;           /* +0x44 */
    float min_dist, max_dist;   /* +0x48, +0x4c */
    float max_angle, min_angle, min_angle_atten;    /* +0x50..+0x58 (cones: not used by the playing code) */
    uint32_t alt_threshold;     /* +0x5c: from this many channels playing the group, play the alternate */
    const struct Impulse *alt;  /* +0x60 */
    char alt_name[NAME];        /* +0x64 */
    float impact_velocity, min_occlusion;   /* +0x84, +0x88 */
} Impulse;

typedef struct { void **v; uint32_t n, cap; } Vec;   /* sorted by name, as the original's arrays */
static Vec groups, ambients, impulses;

/* The binary search of 0x47fcf0 / 0x481be0 / 0x4801f0 (case-sensitive, 0x457a30); *at: where it goes. */
static void *vec_find(const Vec *v, const char *name, uint32_t *at)
{
    uint32_t lo = 0, hi = v->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = strcmp(name, (const char *)v->v[mid]);
        if (c == 0) { if (at) *at = mid; return v->v[mid]; }
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    if (at) *at = lo;
    return NULL;
}

static void vec_insert(Vec *v, uint32_t at, void *e)
{
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 256;
        v->v = realloc(v->v, v->cap * sizeof *v->v);
    }
    memmove(v->v + at + 1, v->v + at, (v->n - at) * sizeof *v->v);
    v->v[at] = e;
    v->n++;
}

static void vec_free(Vec *v, bool groups_too)
{
    for (uint32_t i = 0; i < v->n; i++) {
        if (groups_too) free(((Group *)v->v[i])->perms);
        free(v->v[i]);
    }
    free(v->v);
    memset(v, 0, sizeof *v);
}

static void copy_name(char *dst, const char *src)     /* UUrString_Copy 0x4265f0 with 0x20 */
{
    size_t n = strnlen(src, NAME - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

static const Group *group_get(const char *name) { return name && *name ? vec_find(&groups, name, NULL) : NULL; }

/* The scripts' lookups go through hash tables with a case-insensitive compare (0x511580: AUrHashTable_New
   with 0x4266d0). */
static int nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 0x20;
        if (y >= 'a' && y <= 'z') y -= 0x20;
        if (x != y || !x) return x - y;
    }
}

static void *find_nocase(const Vec *v, const char *name)
{
    if (!name) return NULL;
    void *e = vec_find(v, name, NULL);
    if (e) return e;
    for (uint32_t i = 0; i < v->n; i++) if (!nocase(name, (const char *)v->v[i])) return v->v[i];
    return NULL;
}

/* ---- OSBD: the class loaders (OSiBinaryData_ProcHandler 0x5147b0) ---- */

enum { CLASS_AMBIENT = 0x4f53416d, CLASS_GROUP = 0x4f534772, CLASS_IMPULSE = 0x4f53496d };   /* OSAm OSGr OSIm */

/* Ambient (0x513580): an existing one of the name is reloaded in place, its group pointers cleared. */
static void load_ambient(const char *name, const uint8_t *p, uint32_t size)
{
    uint32_t at;
    Ambient *a = vec_find(&ambients, name, &at);
    if (!a) {
        a = calloc(1, sizeof *a);
        copy_name(a->name, name);
        vec_insert(&ambients, at, a);
    }
    a->detail = a->base1 = a->base2 = a->in = a->out = NULL;
    uint32_t version = rd32(p + 8);
    if (size < 0xc8) return;
    a->priority = (int32_t)rd32(p + 0x0c);
    a->flags = rd32(p + 0x10);
    a->detail_range = rdf(p + 0x14);
    a->detail_min = rdf(p + 0x18);
    a->detail_max = rdf(p + 0x1c);
    a->min_dist = rdf(p + 0x20);
    a->max_dist = rdf(p + 0x24);
    for (int k = 0; k < 5; k++) copy_name(a->group_names[k], (const char *)p + 0x28 + NAME * k);
    a->max_count = version >= 5 && size >= 0xcc ? rd32(p + 0xc8) : 3;
    a->min_occlusion = version >= 6 && size >= 0xd0 ? rdf(p + 0xcc) : 0.0f;
}

/* Group (0x513950). Before version 6 a group of more than 3 permutations doesn't repeat itself. */
static void load_group(const char *name, const uint8_t *p, uint32_t size)
{
    uint32_t at;
    Group *g = vec_find(&groups, name, &at);
    if (!g) {
        g = calloc(1, sizeof *g);
        copy_name(g->name, name);
        g->volume = g->pitch = 1.0f;
        vec_insert(&groups, at, g);
    } else {
        free(g->perms);
        g->perms = NULL;
        g->nperms = 0;
    }
    uint32_t version = rd32(p + 8), o = 0x0c;
    g->volume = g->pitch = 1.0f;
    g->flags = g->last = 0;
    if (version >= 2) g->volume = rdf(p + o), o += 4;
    if (version >= 3) g->pitch = rdf(p + o), o += 4;
    if (version >= 6) g->flags = rd16(p + o), g->last = rd16(p + o + 2), o += 4;
    if (o + 8 > size) return;
    g->channels = rd32(p + o);
    uint32_t n = rd32(p + o + 4);
    o += 8;
    if (version < 6 && n > 3) g->flags |= GROUP_NO_REPEAT;
    if (n > (size - o) / 0x34) n = (size - o) / 0x34;
    g->perms = calloc(n ? n : 1, sizeof *g->perms);
    g->nperms = n;
    for (uint32_t i = 0; i < n; i++, o += 0x34) {
        Perm *m = &g->perms[i];
        m->weight = rd32(p + o);
        m->vol_min = rdf(p + o + 4);
        m->vol_max = rdf(p + o + 8);
        m->pitch_min = rdf(p + o + 12);
        m->pitch_max = rdf(p + o + 16);
        copy_name(m->name, (const char *)p + o + 20);
        m->sndd = NULL;
    }
}

/* Impulse (0x513d90). */
static void load_impulse(const char *name, const uint8_t *p, uint32_t size)
{
    uint32_t at;
    Impulse *m = vec_find(&impulses, name, &at);
    if (!m) {
        m = calloc(1, sizeof *m);
        copy_name(m->name, name);
        vec_insert(&impulses, at, m);
    }
    m->group = NULL;
    m->alt = NULL;
    uint32_t version = rd32(p + 8);
    if (size < 0x44) return;
    copy_name(m->group_name, (const char *)p + 0x0c);
    m->priority = (int32_t)rd32(p + 0x2c);
    m->min_dist = rdf(p + 0x30);
    m->max_dist = rdf(p + 0x34);
    m->max_angle = rdf(p + 0x38);
    m->min_angle = rdf(p + 0x3c);
    m->min_angle_atten = rdf(p + 0x40);
    uint32_t o = 0x44;
    m->alt_threshold = 0;
    m->alt_name[0] = 0;
    if (version >= 4 && size >= 0x68) {
        m->alt_threshold = rd32(p + 0x44);
        copy_name(m->alt_name, (const char *)p + 0x48);
        o = 0x68;
    }
    m->impact_velocity = version >= 5 && size >= o + 4 ? rdf(p + o) : 0.0f;
    if (version >= 5) o += 4;
    m->min_occlusion = version >= 6 && size >= o + 4 ? rdf(p + o) : 0.0f;
}

/* The proc handler's name: the instance's without the tag and the extension (0x426690), spaces made '_'. */
static void osbd_name(const char *inst, char out[NAME])
{
    copy_name(out, inst + 4);
    char *dot = strchr(out, '.');
    if (dot) {
        char *last = strrchr(dot, '.');
        *last = 0;
    }
    for (char *c = out; *c; c++) if (*c == ' ') *c = '_';
}

static const void *seen_files[64];
static int nseen;

static void load_file(TmFile *f)
{
    for (int i = 0; i < nseen; i++) if (seen_files[i] == f) return;
    if (nseen < 64) seen_files[nseen++] = f;
    for (uint32_t i = 0; i < tm_file_instance_count(f); i++) {
        const TmInstance *in = tm_file_instance(f, i);
        if (in->tmpl->tag != TM_TAG("OSBD") || !in->data || !in->name) continue;
        size_t rawsz;
        const uint8_t *raw = tm_raw(in->data, &rawsz);
        uint32_t size = rd32(in->data), off = rd32(in->data + 4);
        if (!raw || off > rawsz || size > rawsz - off || size < 12) continue;
        const uint8_t *p = raw + off;
        char name[NAME];
        osbd_name(in->name, name);
        switch (rd32(p)) {
        case CLASS_AMBIENT: load_ambient(name, p, size); break;
        case CLASS_GROUP: load_group(name, p, size); break;
        case CLASS_IMPULSE: load_impulse(name, p, size); break;
        }
    }
}

/* 0x480140: every permutation's SNDD, by name. */
static void resolve_sounds(void)
{
    for (uint32_t i = 0; i < groups.n; i++) {
        Group *g = groups.v[i];
        for (uint32_t k = 0; k < g->nperms; k++) g->perms[k].sndd = tm_instance_get(SNDD_TAG, g->perms[k].name);
    }
}

/* 0x482670 (the ambients' groups) and 0x480ab0 (the impulses' group and alternate). */
static void resolve_pointers(void)
{
    for (uint32_t i = 0; i < ambients.n; i++) {
        Ambient *a = ambients.v[i];
        a->detail = group_get(a->group_names[0]);
        a->base1 = group_get(a->group_names[1]);
        a->base2 = group_get(a->group_names[2]);
        a->in = group_get(a->group_names[3]);
        a->out = group_get(a->group_names[4]);
    }
    for (uint32_t i = 0; i < impulses.n; i++) {
        Impulse *m = impulses.v[i];
        m->group = group_get(m->group_name);
        if (m->alt_threshold) m->alt = m->alt_name[0] ? vec_find(&impulses, m->alt_name, NULL) : NULL;
    }
}

/* ---- decoded samples: the ACM's job in the original (0x484b70, 0x484ef0) ---- */

typedef struct { const void *sndd; int16_t *pcm; uint32_t samples; bool bad; } Pcm;
static Pcm *pcms;
static uint32_t npcm, cappcm;

static const Pcm *pcm_get(const void *sndd)
{
    for (uint32_t i = 0; i < npcm; i++) if (pcms[i].sndd == sndd) return pcms[i].bad ? NULL : &pcms[i];
    if (npcm == cappcm) {
        cappcm = cappcm ? cappcm * 2 : 64;
        pcms = realloc(pcms, cappcm * sizeof *pcms);
    }
    Pcm *p = &pcms[npcm++];
    memset(p, 0, sizeof *p);
    p->sndd = sndd;
    SnddInfo info;
    sndd_info(sndd, &info);
    int64_t frames = sndd_decode(sndd, NULL, 0);
    if (frames < 0 || !info.channels) { p->bad = true; return NULL; }
    p->samples = (uint32_t)frames * info.channels;
    p->pcm = malloc(((size_t)p->samples + 1) * sizeof *p->pcm);
    if (!p->pcm || sndd_decode(sndd, p->pcm, (uint32_t)frames) < 0) { free(p->pcm); p->pcm = NULL; p->bad = true; return NULL; }
    return p;
}

static void pcm_flush(void)
{
    for (uint32_t i = 0; i < npcm; i++) free(pcms[i].pcm);
    free(pcms);
    pcms = NULL;
    npcm = cappcm = 0;
}

/* ---- the channels (SSiSoundChannels_Initialize 0x4842a0; SS2rInitializeBasic 0x4830c0) ---- */

/* 0x485be0 reports 16 channels; 0x4830c0 makes round(16 * 0.4 + 0.5) = 7 of them stereo, the rest mono.
   Every buffer is 22,050 Hz, 16 bits (0x4842a0). */
enum { CHANNELS = 16, BUFFER_RATE = 22050 };

enum {
    CH_PLAYING = 1 << 0,        /* SSiSoundChannel_SetPlaying (0x47f890) */
    CH_LOOPING = 1 << 1,        /* at the end the group plays again (a new permutation) */
    CH_LOCKED = 1 << 2,         /* held by a playing ambient: GetChannelForPlay passes it over */
    CH_UPDATING = 1 << 3,       /* set while the end of its sound is reported (0x47f8b0) */
    CH_PAUSED = 1 << 4,
    CH_CAN_PAN = 1 << 6,
    CH_AMBIENT = 1 << 7,        /* owned by a playing ambient */
};

typedef struct {
    uint16_t index;
    uint8_t kind;               /* +0x1a: 1 mono, 2 stereo */
    uint8_t flags;              /* +0x1b: CH_* */
    int32_t priority;           /* +0x1c: 1, and nothing in the exe writes it again */
    const Group *group;         /* +0x20 */
    const void *sndd;           /* +0x24 */
    float pos[3], dist, prev_dist;                  /* +0x28..+0x38 */
    float eff;                  /* +0x3c: what went to the buffer */
    float pitch;                /* +0x40 */
    float volume, dist_vol, perm_vol;               /* +0x44, +0x48, +0x4c */
    float pan_l, pan_r;         /* +0x50, +0x54 */
    /* The DirectSound buffer, in its units: volume and pan in hundredths of a dB (pan < 0 cuts the right),
       frequency in Hz. */
    int32_t ds_volume, ds_pan;
    uint32_t ds_freq;
    bool ds_playing;
    const int16_t *pcm;
    uint32_t frames;            /* of the buffer's format (kind channels): the stream's length */
    double cursor;
    uint32_t gen;               /* bumped by every new sound on the channel */
} Chan;

static Chan stereo[7], mono[CHANNELS - 7];
static bool enabled;            /* 0x5eb757 */
static float master = 1.0f, master_target = 1.0f;      /* 0x5eb744, 0x5eb71c */
static float listener[3], facing[3] = { 1, 0, 0 };     /* 0x5eb700, 0x5eb738 */
static uint32_t now;            /* the sixtieths clock (UUrMachineTime_Sixtieths 0x4263e0): the tick count */

static void ds_volume(Chan *c, float v)     /* SS2rPlatform_SoundChannel_SetVolume 0x485480 */
{
    if (v > 0.05f) {
        int32_t att = fround(fabs((double)v * -3333.0)) - 3333;
        if (att <= 0) c->ds_volume = att;   /* above 1 the value is out of DirectSound's range: the call fails */
    } else
        c->ds_volume = -10000;
}

static void ds_pitch(Chan *c, float p)      /* 0x485460 */
{
    int32_t f = fround((double)p * 22050.0);
    if (f >= 100 && f <= 100000) c->ds_freq = (uint32_t)f;     /* SetFrequency refuses the rest */
}

static void ds_pan(Chan *c, int side, float v)  /* 0x4853b0: side 1 left, 2 right, else the centre */
{
    if (side == 1) c->ds_pan = v > 0.05f ? -3333 - fround((double)v * -3333.0) : -10000;
    else if (side == 2) c->ds_pan = v > 0.05f ? 3333 - fround((double)v * 3333.0) : 10000;
    else c->ds_pan = 0;
}

/* The channel's volume from its parts (0x481730) into the buffer. */
static void chan_apply(Chan *c)
{
    c->eff = c->perm_vol * c->dist_vol * c->volume * master;
    if (c->group) c->eff *= c->group->volume;
    ds_volume(c, c->eff);
}

static void chan_set_volume(Chan *c, float v) { c->volume = clamp02(v); chan_apply(c); }

/* 0x4816f0: the pitch, times the group's. */
static void chan_set_pitch(Chan *c, float p)
{
    float old = c->pitch;
    c->pitch = c->group ? p * c->group->pitch : p;
    if (old != c->pitch) ds_pitch(c, c->pitch);
}

/* 0x485530 (names.tsv: SSiPlayingAmbient_Update; the Mac build's SS2rPlatform_SoundChannel_Stop): the buffer
   stops, the stream is reset. */
static void chan_stop(Chan *c)
{
    c->flags &= (uint8_t)~CH_PLAYING;
    c->ds_playing = false;
    c->cursor = 0;
}

/* 0x484b70 (SS2rPlatform_SoundChannel_SetSoundData): the channel streams this sound from its start. The
   decoded data go to the buffer as they are: a stereo sound on a mono channel would play as twice the
   samples, and every buffer plays at 22,050 Hz times the pitch, also the 44.1 kHz sounds (the ACM keeps the
   source rate). */
static bool chan_set_sound(Chan *c, const void *sndd)
{
    const Pcm *p = pcm_get(sndd);
    if (!p) return false;
    c->sndd = sndd;
    c->pcm = p->pcm;
    c->frames = p->samples / c->kind;
    c->cursor = 0;
    c->gen++;
    return true;
}

static void chan_play(Chan *c) { c->ds_playing = true; c->flags |= CH_PLAYING; }     /* 0x4847e0 */

/* ---- playing ambients (0x4821f0 and the state machine 0x480c20) ---- */

enum { PLAYING_MAX = CHANNELS };    /* 0x483310: one per channel */

enum {
    PA_FREE, PA_RELEASE, PA_START, PA_IN, PA_IN_WAIT, PA_BASE, PA_BODY, PA_END_WAIT, PA_OUT, PA_OUT_WAIT,
    PA_TOO_FAR, PA_DETAIL_ONLY,
};

typedef struct {
    uint32_t id, state;         /* [0], [1] */
    const Ambient *amb;         /* [2] */
    Chan *a, *b;                /* [3] base track 1 (or the in/out sound), [4] base track 2 */
    uint32_t detail_time;       /* [5] */
    bool has_pos;               /* [6] */
    float pos[3], vel[3], dir[3];   /* [7..15] */
    float min_dist, max_dist;   /* [0x10], [0x11] */
    float volume;               /* [0x12] */
    uint32_t fade_time;         /* [0x13] */
    float fade_rate, target;    /* [0x14], [0x15] */
} Playing;

static Playing playing[PLAYING_MAX];
static uint32_t next_id;        /* 0x5eb70c */
static bool details_can_play = true;    /* 0x53d878 (SSrDetails_SetCanPlay 0x4830b0) */

static Playing *playing_of(uint32_t id)
{
    if (id == UINT32_MAX) return NULL;
    for (int i = 0; i < PLAYING_MAX; i++) if (playing[i].state != PA_FREE && playing[i].id == id) return &playing[i];
    return NULL;
}

/* A channel given back by its ambient: no longer owned, stopped, unlocked. */
static void release(Chan **slot)
{
    Chan *c = *slot;
    if (!c) return;
    c->flags &= 0x7f;
    chan_stop(c);
    c->flags &= (uint8_t)~CH_LOCKED;
    *slot = NULL;
}

static void machine(Playing *pa);

/* SSiSoundChannels_GetChannelForPlay 0x47fab0: a free channel of the kind, else one of lower priority,
   else a nearly silent one (0.1), else the quietest under full volume, taken from its ambient if it has
   one. All channels have priority 1, so a priority above 1 (the scripts' ambients have 3) takes any
   unlocked channel, and 0 (ambient details) only a free one. */
static Chan *channel_for_play(int kind, int32_t prio)
{
    Chan *arr = kind == 2 ? stereo : mono;
    int n = kind == 2 ? 7 : CHANNELS - 7;
    Chan *c = NULL;
    for (int i = 0; i < n && !c; i++)
        if (!(arr[i].flags & (CH_LOCKED | CH_PLAYING))) c = &arr[i];
    if (c) goto done;
    for (int i = 0; i < n && !c; i++)
        if (!(arr[i].flags & CH_LOCKED) && arr[i].priority < prio) c = &arr[i];
    for (int i = 0; i < n && !c; i++)
        if (!(arr[i].flags & CH_LOCKED) && arr[i].priority <= prio && arr[i].eff <= 0.1f) c = &arr[i];
    if (c) {
        if (c->flags & CH_PLAYING) chan_stop(c);
        goto done;
    }
    float least = 1.0f;
    for (int i = 0; i < n; i++)
        if (arr[i].priority <= prio && arr[i].eff < least) least = arr[i].eff, c = &arr[i];
    if (!c) return NULL;
    if (c->flags & CH_AMBIENT) {
        for (int i = 0; i < PLAYING_MAX; i++) {
            if (playing[i].state == PA_FREE) continue;
            if (playing[i].a == c) { release(&playing[i].a); break; }
            if (playing[i].b == c) { release(&playing[i].b); break; }
        }
    } else if (c->flags & CH_PLAYING)
        chan_stop(c);
done:
    c->dist = c->prev_dist = 0;
    c->pan_l = c->pan_r = 1.0f;
    return c;
}

/* ---- groups and permutations ---- */

/* SSiGroup_SelectPermutation 0x480040: by weight, with the second LCG. A group that doesn't repeat itself
   draws again up to 10 times; once it has drawn the last one again it keeps drawing all 10 (the flag is
   never set back), so the tenth draw is taken whatever it is. */
static Perm *select_permutation(Group *g)
{
    if (!g->nperms) return NULL;
    if (g->nperms == 1) return &g->perms[0];
    uint32_t total = 0;
    for (uint32_t i = 0; i < g->nperms; i++) total += g->perms[i].weight;
    bool ok = true;
    Perm *pick = NULL;
    uint32_t idx = 0;
    for (int tries = 0;;) {
        uint32_t r = total ? (uint32_t)(trac_local_random() * (uint64_t)total >> 16) : 0, sum = 0;
        pick = NULL;
        uint32_t i;
        for (i = 0; i < g->nperms; i++) {
            sum += g->perms[i].weight;
            if (r <= sum) { pick = &g->perms[i]; idx = i; break; }
        }
        if ((g->flags & GROUP_NO_REPEAT) && i == g->last) ok = false;
        if (ok || ++tries >= 10) break;
    }
    g->last = (uint16_t)idx;
    return pick;
}

/* SSrPermutation_Play 0x47f8d0: a random volume and pitch within the permutation's, the sound started on the
   channel (one of the sound's kind at priority 1 if none). */
static void permutation_play(Perm *m, Chan *c, const char *what, const char *who)
{
    if (!enabled) return;
    if (!m->sndd) {
        m->sndd = tm_instance_get(SNDD_TAG, m->name);
        if (!m->sndd) { logf_("SOUND NOT FOUND: %s %s - sound %s%s", what, who, m->name, ""); return; }
    }
    if (!c) {
        uint16_t ch = rd16((const uint8_t *)m->sndd + SNDD_CHANNELS);
        if (ch != 1 && ch != 2) return;
        c = channel_for_play(ch, 1);
        if (!c) return;
    }
    uint16_t r = trac_random();     /* UUrRandom 0x426e10 */
    float vol = (float)((double)(m->vol_max - m->vol_min) * r * (double)(1.0f / 65535.0f) + m->vol_min);
    r = trac_random();
    float pitch = (float)((double)(m->pitch_max - m->pitch_min) * r * (double)(1.0f / 65535.0f) + m->pitch_min);
    c->perm_vol = clamp02(vol);
    chan_apply(c);
    if (chan_set_sound(c, m->sndd)) chan_play(c);
    chan_set_pitch(c, pitch);
}

/* SSrGroup_Play 0x47ff50: a permutation on the channel, or on one of priority 1. */
static void group_play(const Group *gc, Chan *c, const char *what, const char *who)
{
    Group *g = (Group *)gc;
    if (!enabled || !g || !g->nperms) return;
    Perm *m = select_permutation(g);
    if (!m) return;
    if (!m->sndd) { logf_("SOUND NOT FOUND: %s %s - sound %s%s", what, who, m->name, ""); return; }
    if (!c) {
        uint16_t ch = rd16((const uint8_t *)m->sndd + SNDD_CHANNELS);
        if (ch != 1 && ch != 2) return;
        c = channel_for_play(ch, 1);
        if (!c) return;
    }
    c->group = g;
    permutation_play(m, c, what, who);
}

/* SSiSoundChannel_Spatialize 0x480710: the distance's volume, and the pan for channels that may pan. pos
   NULL: at the listener. Returns false if it is out of hearing. */
static bool spatialize(Chan *c, const float *pos, float min, float max)
{
    if (!pos) {
        memcpy(c->pos, listener, sizeof c->pos);
        c->dist = c->prev_dist = 0;
    } else {
        memcpy(c->pos, pos, sizeof c->pos);
        c->prev_dist = c->dist;
        float dx = pos[0] - listener[0], dy = pos[1] - listener[1], dz = pos[2] - listener[2];
        c->dist = sqrtf(dx * dx + dy * dy + dz * dz) * 0.33333334f;
    }
    float a;
    if (min <= c->dist) {
        if (max - min <= 0.0f || (a = (max - c->dist) / (max - min)) < 0.0f) a = 0.0f;
        else if (a > 2.0f) a = 2.0f;
    } else
        a = 1.0f;
    c->dist_vol = a;
    chan_apply(c);
    if (a <= 0.0f) return false;
    c->pan_l = c->pan_r = 1.0f;
    if (!(c->flags & CH_CAN_PAN)) return true;     /* and the buffer's pan stays as it was */
    int side = 0;
    float v = 1.0f;
    if (pos) {
        float ex = pos[0] - listener[0], ez = pos[2] - listener[2];
        float d2 = ex * ex + ez * ez;
        if (!(pos[0] == listener[0] && pos[2] == listener[2]) && d2 > 49.0f) {
            float dir[3] = { pos[0] - listener[0], pos[1] - listener[1], pos[2] - listener[2] };
            /* The side: the facing turned a quarter round the vertical, (-fz, fy, fx). */
            float dot = -facing[2] * dir[0] + facing[1] * dir[1] + facing[0] * dir[2];
            /* MUrAngleBetweenVectors 0x433140 in the horizontal plane, 0..pi */
            float lf = sqrtf(facing[0] * facing[0] + facing[2] * facing[2]), ld = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
            float cs = lf > 0 && ld > 0 ? (facing[0] * dir[0] + facing[2] * dir[2]) / (lf * ld) : 0.0f;
            float ang = acosf(cs < -1.0f ? -1.0f : cs > 1.0f ? 1.0f : cs);
            if (dot < 0.0f) side = 1;
            else if (dot > 0.0f) side = 2;
            if (side) v = fabsf((ang - 1.5707964f) * 0.31830990f) + 0.5f;
        }
    }
    if (side == 1) c->pan_r = v;
    else if (side == 2) c->pan_l = v;
    ds_pan(c, side, v);
    return true;
}

/* SSiGroup_PlaySpatialized 0x480560: a permutation on a channel of the priority, at the position. */
static bool group_play_spatial(const Group *gc, const float *pos, float min, float max, float vol, int32_t prio,
                               const char *what, const char *who)
{
    Group *g = (Group *)gc;
    if (!enabled || !g || !g->nperms) return false;
    Perm *m = select_permutation(g);
    if (!m) return false;
    if (!m->sndd) { logf_("SOUND NOT FOUND: %s %s - group %s - sound %s", what, who, g->name, m->name); return false; }
    uint16_t ch = rd16((const uint8_t *)m->sndd + SNDD_CHANNELS);
    if (ch != 1 && ch != 2) return false;
    Chan *c = channel_for_play(ch, prio);
    if (!c) return false;
    c->flags &= (uint8_t)~CH_LOOPING;
    chan_set_volume(c, vol);
    c->flags |= CH_CAN_PAN;
    bool ok = spatialize(c, pos, min, max);
    if (ok) {
        c->group = g;
        permutation_play(m, c, what, who);
    }
    return ok;
}

/* SSrImpulse_Play 0x480440: with the alternate impulse once the group plays on `alt_threshold` channels. */
static void impulse_play(const Impulse *m, const float *pos, const float *vol)
{
    while (enabled && m && m->group) {
        float v = vol ? *vol : 1.0f;
        if (m->alt_threshold) {
            Chan *arr = m->group->channels == 2 ? stereo : mono;
            int n = m->group->channels == 2 ? 7 : CHANNELS - 7;
            uint32_t count = 0;
            for (int i = 0; i < n; i++)
                if ((arr[i].flags & CH_PLAYING) && arr[i].group == m->group && ++count >= m->alt_threshold) break;
            if (count >= m->alt_threshold) { m = m->alt; continue; }
        }
        group_play_spatial(m->group, pos, m->min_dist, m->max_dist, v, m->priority, "impulse", m->name);
        return;
    }
}

/* ---- the playing ambient's states (0x480c20, names.tsv: SSiPAUpdate_InSoundStart) ---- */

/* 0x481000: a channel for one of the ambient's tracks, owned and locked, at the volume. */
static Chan *ambient_channel(uint32_t kind, int32_t prio, float vol, bool can_pan)
{
    Chan *c = channel_for_play(kind == 2 ? 2 : 1, prio);
    if (!c) return NULL;
    c->flags = (uint8_t)((c->flags & 0xf5) | 0x84);
    c->flags = can_pan ? (uint8_t)(c->flags | CH_CAN_PAN) : (uint8_t)(c->flags & ~CH_CAN_PAN);
    chan_set_volume(c, vol);
    c->dist_vol = 1.0f;
    chan_apply(c);
    chan_set_pitch(c, 1.0f);
    c->pan_l = c->pan_r = 1.0f;
    ds_pan(c, 0, 1.0f);
    return c;
}

/* 0x480fa0: the next detail in detail_min..detail_max seconds. */
static void detail_schedule(Playing *pa)
{
    if (!pa->amb->detail) return;
    uint16_t r = trac_random();
    float t = (float)((double)r * (double)(1.0f / 65535.0f) * (pa->amb->detail_max - pa->amb->detail_min) + pa->amb->detail_min);
    pa->detail_time = now - (uint32_t)fround((double)t * -60.0);
}

/* A detail (0x481140, and in 0x4812a0): somewhere within detail_range of the ambient (or the listener) in x
   and z, at priority 0. */
static void detail_play(Playing *pa)
{
    const Ambient *amb = pa->amb;
    float pos[3];
    memcpy(pos, pa->has_pos ? pa->pos : listener, sizeof pos);
    float lo = -amb->detail_range;
    uint16_t r = trac_random();
    float dx = (float)((double)r * (double)(1.0f / 65535.0f) * (amb->detail_range - lo) + lo);
    r = trac_random();
    float dz = (float)((double)r * (double)(1.0f / 65535.0f) * (amb->detail_range - lo) + lo);
    pos[0] += dx;
    pos[2] += dz;
    group_play_spatial(amb->detail, pos, amb->min_dist, amb->max_dist, pa->volume, 0, "ambient detail", amb->name);
    detail_schedule(pa);
}

static void ambient_set_volume(uint32_t id, float vol, float seconds);

/* 0x4812a0: the base tracks playing. A track whose channel was taken gets a new one at volume 0 and a fade
   to the ambient's volume over half a second, which changes nothing unless a fade is under way (0x482000
   compares with the target), so it stays silent: kept. */
static bool body(Playing *pa)
{
    const Ambient *amb = pa->amb;
    bool pa_ = pa->a && (pa->a->flags & CH_PLAYING), pb = pa->b && (pa->b->flags & CH_PLAYING);
    bool pan = (amb->flags & AMB_CAN_PAN) != 0;
    for (int k = 0; k < 2; k++) {
        const Group *g = k ? amb->base2 : amb->base1;
        Chan **slot = k ? &pa->b : &pa->a;
        if (!g || *slot) continue;
        Chan *c = channel_for_play(g->channels == 2 ? 2 : 1, amb->priority);
        if (c) {
            c->flags = (uint8_t)((c->flags & 0xf5) | 0x84);
            c->flags = pan ? (uint8_t)(c->flags | CH_CAN_PAN) : (uint8_t)(c->flags & ~CH_CAN_PAN);
            c->volume = 0;
            chan_apply(c);
            c->dist_vol = 1.0f;
            chan_apply(c);
            chan_set_pitch(c, 1.0f);
            c->pan_l = c->pan_r = 1.0f;
            ds_pan(c, 0, 1.0f);
        }
        *slot = c;
        ambient_set_volume(pa->id, pa->volume, 0.5f);
    }
    if (amb->base1 && pa->a && !pa_ && !(amb->flags & AMB_PLAY_ONCE)) group_play(amb->base1, pa->a, "ambient base1", amb->name);
    if (amb->base2 && pa->b && !pb && !(amb->flags & AMB_PLAY_ONCE)) {
        if (pa->b->flags & CH_LOOPING)
            group_play(amb->base2, pa->b, "ambient base2", amb->name);
        else if (pa->a && (pa->a->flags & CH_LOOPING)) {
            pa->a->flags &= (uint8_t)~CH_LOOPING;
            pa->state = PA_END_WAIT;
            return true;
        }
    }
    if (!pa_ && !pb) { pa->state = PA_END_WAIT; return true; }
    if ((amb->flags & AMB_PLAY_ONCE) && pa->a && (pa->a->flags & CH_UPDATING)) { pa->state = PA_END_WAIT; return true; }
    if (amb->detail && pa->detail_time < now && details_can_play) detail_play(pa);
    return false;
}

/* 0x481860: the volume fade, then onto both channels. */
static void fade(Playing *pa)
{
    if (pa->target == pa->volume || fabsf(pa->fade_rate) < 0.001f) return;
    uint32_t t = now, dt = t - pa->fade_time;
    pa->fade_time = t;
    float v = (float)dt * pa->fade_rate + pa->volume;
    if (pa->fade_rate <= 0.0f) v = v < pa->target ? pa->target : v > 1.0f ? 1.0f : v;
    else v = v < 0.0f ? 0.0f : v > pa->target ? pa->target : v;
    pa->volume = v;
    if (pa->a) chan_set_volume(pa->a, v);
    if (pa->b) chan_set_volume(pa->b, v);
}

static void machine(Playing *pa)
{
    for (;;) {
        bool again = false;
        const Ambient *amb = pa->amb;
        switch (pa->state) {
        case PA_FREE:
            return;
        case PA_RELEASE:
            release(&pa->a);
            release(&pa->b);
            pa->state = PA_FREE;
            again = true;
            break;
        case PA_START: {
            const Group *g = amb->base1 ? amb->base1 : amb->in ? amb->in : amb->out;
            bool pan = (amb->flags & AMB_CAN_PAN) != 0;
            if (g) pa->a = ambient_channel(g->channels, amb->priority, pa->volume, pan);
            if (amb->base2) pa->b = ambient_channel(amb->base2->channels, amb->priority, pa->volume, pan);
            pa->state = !pa->a && !pa->b ? (amb->detail ? PA_DETAIL_ONLY : PA_RELEASE) : PA_IN;
            again = true;
            break;
        }
        case PA_IN:
            pa->state = PA_IN_WAIT;
            if (amb->in && pa->a) group_play(amb->in, pa->a, "ambient in", amb->name);
            again = true;
            break;
        case PA_IN_WAIT:
            if (!pa->a || !(pa->a->flags & CH_PLAYING) || (pa->a->flags & CH_UPDATING)) pa->state = PA_BASE, again = true;
            break;
        case PA_BASE:
            if ((!pa->a || !(pa->a->flags & CH_UPDATING)) && (!pa->b || !(pa->b->flags & CH_UPDATING))) {
                uint8_t loop = (amb->flags & AMB_PLAY_ONCE) ? 0 : CH_LOOPING;
                if (amb->base1 && pa->a) {
                    pa->a->flags = (uint8_t)((pa->a->flags & ~CH_LOOPING) | loop);
                    group_play(amb->base1, pa->a, "ambient base1", amb->name);
                }
                if (amb->base2 && pa->b) {
                    pa->b->flags = (uint8_t)((pa->b->flags & ~CH_LOOPING) | loop);
                    group_play(amb->base2, pa->b, "ambient base2", amb->name);
                }
                detail_schedule(pa);
                pa->state = !pa->a && !pa->b ? PA_END_WAIT : PA_BODY;
                again = true;
            }
            break;
        case PA_BODY:
            again = body(pa);
            break;
        case PA_END_WAIT: {
            if (pa->a && (pa->a->flags & CH_LOOPING)) pa->a->flags &= (uint8_t)~CH_LOOPING;
            if (pa->b && (pa->b->flags & CH_LOOPING)) pa->b->flags &= (uint8_t)~CH_LOOPING;
            bool a = pa->a && (pa->a->flags & CH_PLAYING), b = pa->b && (pa->b->flags & CH_PLAYING);
            if (!a) {
                if (!b || (pa->b->flags & CH_UPDATING)) pa->state = PA_OUT, again = true;
            } else if (!b && (pa->a->flags & CH_UPDATING))
                pa->state = PA_OUT, again = true;
            break;
        }
        case PA_OUT:        /* SSiPAUpdate_OutSoundStart 0x481760 */
            pa->state = PA_OUT_WAIT;
            if (amb->out && pa->a) {
                pa->a->flags &= (uint8_t)~CH_LOOPING;
                group_play(amb->out, pa->a, "ambient out", amb->name);
            }
            release(&pa->b);
            break;
        case PA_OUT_WAIT:   /* 0x4817d0 */
            if (!pa->a || !(pa->a->flags & (CH_PLAYING | CH_UPDATING))) pa->state = PA_RELEASE, again = true;
            break;
        case PA_TOO_FAR:    /* 0x481a80 */
            if (pa->a) chan_stop(pa->a);
            if (pa->b) chan_stop(pa->b);
            pa->state = PA_RELEASE;
            again = true;
            break;
        case PA_DETAIL_ONLY:    /* 0x481140 */
            if (amb->detail && pa->detail_time < now) detail_play(pa);
            break;
        }
        if (again) continue;
        fade(pa);
        if (pa->has_pos) {      /* 0x481810 */
            if (pa->a) spatialize(pa->a, pa->pos, pa->min_dist, pa->max_dist);
            if (pa->b) spatialize(pa->b, pa->pos, pa->min_dist, pa->max_dist);
        }
        /* 0x481a00: a positional ambient gone silent beyond 300 and moving away stops. */
        Chan *c = pa->a ? pa->a : pa->b;
        if (pa->has_pos && pa->state > PA_IN && c && c->eff <= 0.0f && c->dist >= 300.0f && c->prev_dist < c->dist)
            pa->state = PA_TOO_FAR;
        return;
    }
}

/* SSrAmbient_Start 0x4821f0 (the script's form 0x4823a0: no position). Returns the id, UINT32_MAX if the
   ambient plays its maximum already or every slot is taken. */
static uint32_t ambient_start(const Ambient *amb, const float *pos, float min, float max, const float *vol)
{
    if (!enabled) return UINT32_MAX;
    Playing *pa = NULL;
    uint32_t count = 0;
    for (int i = 0; i < PLAYING_MAX; i++) {
        if (playing[i].state == PA_FREE) { if (!pa) pa = &playing[i]; }
        else if (playing[i].amb == amb && playing[i].state < PA_OUT) count++;
    }
    if (count >= amb->max_count || !pa) return UINT32_MAX;
    if (++next_id == UINT32_MAX) next_id = 0;
    memset(pa, 0, sizeof *pa);
    pa->id = next_id;
    pa->state = PA_START;
    pa->amb = amb;
    pa->volume = vol ? *vol : 1.0f;
    pa->fade_time = now;
    pa->target = pa->volume;
    if (!pos) {
        pa->min_dist = amb->min_dist;
        pa->max_dist = amb->max_dist;
    } else {
        memcpy(pa->pos, pos, sizeof pa->pos);
        pa->has_pos = true;
        pa->min_dist = min;
        pa->max_dist = max;
    }
    machine(pa);
    return pa->id;
}

/* 0x4823c0 (SSrAmbient_Stop): the base tracks stop looping and the out sound follows; an ambient that
   interrupts stops its channels at once. With base track 2 still playing only its loop ends (the state
   stays). */
static void ambient_stop(uint32_t id)
{
    if (!enabled) return;
    Playing *pa = NULL;
    for (int i = 0; i < PLAYING_MAX && !pa; i++) if (playing[i].id == id) pa = &playing[i];
    if (!pa || pa->state == PA_FREE || (pa->state >= PA_END_WAIT && pa->state <= PA_OUT_WAIT)) return;
    if (!(pa->amb->flags & AMB_INTERRUPT_ON_STOP)) {
        if (pa->b && (pa->b->flags & CH_PLAYING)) { pa->b->flags &= (uint8_t)~CH_LOOPING; return; }
        if (pa->a) pa->a->flags &= (uint8_t)~CH_LOOPING;
    } else {
        if (pa->a) chan_stop(pa->a);
        if (pa->b) chan_stop(pa->b);
    }
    pa->state = PA_END_WAIT;
}

/* 0x481d90 (SSrAmbient_Halt): everything stops now. */
static void ambient_halt(uint32_t id)
{
    Playing *pa = playing_of(id);
    if (!pa) return;
    if (pa->a) chan_stop(pa->a);
    if (pa->b) chan_stop(pa->b);
    pa->state = PA_RELEASE;
    machine(pa);
}

/* 0x482000 (SSrAmbient_SetVolume): to vol (0..1) over the time, at once if none. */
static void ambient_set_volume(uint32_t id, float vol, float seconds)
{
    if (!enabled) return;
    vol = vol >= 0.0f ? (vol > 1.0f ? 1.0f : vol) : 0.0f;
    Playing *pa = playing_of(id);
    if (!pa || pa->target == vol) return;
    if (seconds * 60.0f > 0.0f) {
        pa->fade_rate = (vol - pa->volume) / (seconds * 60.0f);
        pa->target = vol;
        pa->fade_time = now;
        return;
    }
    pa->volume = pa->target = vol;
    if (pa->a) chan_set_volume(pa->a, vol);
    if (pa->b) chan_set_volume(pa->b, vol);
}

/* SSrAmbient_Update 0x4824a0 without the position: whether the id still plays. */
static bool ambient_alive(uint32_t id) { return enabled && playing_of(id) != NULL; }

/* ---- the mixer: the buffers' streaming and the end of a sound (0x484ef0, 0x4851f0, 0x486880) ---- */

/* The end of a channel's data. Looping: the group plays again on it (a new permutation). Otherwise the
   owning ambient is told (0x480bb0) with the channel marked as updating, and the channel stops unless that
   started something new on it. The original does this when the last data go into the buffer; the port
   when they have played. */
static void chan_end(Chan *c)
{
    uint32_t gen = c->gen;
    if (c->flags & CH_LOOPING) {
        group_play(c->group, c, "sound channel", NULL);
        if (c->gen == gen) chan_stop(c);
        return;
    }
    if (c->flags & CH_AMBIENT) {
        c->flags |= CH_UPDATING;
        for (int i = 0; i < PLAYING_MAX; i++)
            if (playing[i].state != PA_FREE && (playing[i].a == c || playing[i].b == c)) { machine(&playing[i]); break; }
        c->flags &= (uint8_t)~CH_UPDATING;
    }
    if (c->gen == gen) chan_stop(c);
}

static float db_gain(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : powf(10.0f, (float)hundredths / 2000.0f); }

static void mix_channel(Chan *c, float *out, unsigned frames)
{
    for (unsigned f = 0; f < frames && c->ds_playing; f++) {
        for (int ends = 0; c->ds_playing && c->cursor >= c->frames; ends++) {
            double over = c->cursor - c->frames;
            uint32_t gen = c->gen;
            if (ends == 4) { chan_stop(c); break; }     /* sounds of no samples, again and again */
            chan_end(c);
            if (c->gen == gen) break;
            if (over < c->frames) c->cursor = over;     /* the next sound goes on where the last ended */
        }
        if (!c->ds_playing) break;
        float g = db_gain(c->ds_volume);
        float gl = g * (c->ds_pan > 0 ? db_gain(-c->ds_pan) : 1.0f), gr = g * (c->ds_pan < 0 ? db_gain(c->ds_pan) : 1.0f);
        uint32_t k = (uint32_t)c->cursor, k2 = k + 1 < c->frames ? k + 1 : k;
        float t = (float)(c->cursor - k), l, r;
        const int16_t *s = c->pcm;
        if (c->kind == 2) {
            l = (s[2 * k] + (s[2 * k2] - s[2 * k]) * t) * (1.0f / 32768.0f);
            r = (s[2 * k + 1] + (s[2 * k2 + 1] - s[2 * k + 1]) * t) * (1.0f / 32768.0f);
        } else
            l = r = (s[k] + (s[k2] - s[k]) * t) * (1.0f / 32768.0f);
        out[2 * f] += l * gl;
        out[2 * f + 1] += r * gr;
        c->cursor += (double)c->ds_freq / SOUND_RATE;
    }
}

void sound_mix(float *out, unsigned frames)
{
    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    if (!enabled) return;
    for (int i = 0; i < 7; i++) mix_channel(&stereo[i], out, frames);
    for (int i = 0; i < CHANNELS - 7; i++) mix_channel(&mono[i], out, frames);
    for (unsigned i = 0; i < frames * 2; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
}

int sound_channels_playing(void)
{
    int n = 0;
    for (int i = 0; i < 7; i++) n += stereo[i].ds_playing;
    for (int i = 0; i < CHANNELS - 7; i++) n += mono[i].ds_playing;
    return n;
}

/* ---- the game's layer: music, dialog, the script commands (OSiScriptCommands_Initialize 0x512cb0) ---- */

/* The scripts' ambients (sound_ambient_* and sound_music_*, the same functions): 16 slots of id, volume and
   name at 0x61fdf8. */
typedef struct { uint32_t id; float volume; char name[NAME]; } Slot;
static Slot slots[16];
/* The dialog: one at a time (0x6200a8, its id 0x61fa30, its end in game ticks 0x620098). */
static bool dialog_on;
static uint32_t dialog_id = UINT32_MAX, dialog_end;
static bool loaded;             /* the hash tables exist (0x61fa28, 0x6200a4) */
static bool script_only;        /* 0x6200a0 (OSrSetScriptOnly 0x514e60): no script impulses while set */
static uint32_t ticks;          /* ONrGameState_GetGameTime 0x4fc880 */

static const Ambient *ambient_named(const char *name) { return loaded ? find_nocase(&ambients, name) : NULL; }

/* 0x514160 (the Mac build's OSiDialog_Start): returns true while another dialog plays and the call doesn't
   interrupt; then nothing starts. The dialog is an ambient at priority 3 at full volume. It counts as
   playing until its ambient ends or, sooner, the length of base track 1's first resolved sound (2 s if the
   ambient couldn't start). */
static bool dialog_start(const char *name, bool interrupt)
{
    if (dialog_on) {
        if (!interrupt) return true;
        ambient_halt(dialog_id);
        dialog_on = false;
        dialog_id = UINT32_MAX;
        dialog_end = 0;
    }
    Ambient *amb = (Ambient *)ambient_named(name);
    if (!amb || !name) return false;
    dialog_on = true;
    amb->priority = 3;
    float vol = 1.0f;
    dialog_id = ambient_start(amb, NULL, 0, 0, &vol);
    uint32_t len = 0;
    if (amb->base1)
        for (uint32_t i = 0; i < amb->base1->nperms; i++)
            if (amb->base1->perms[i].sndd) {
                SnddInfo info;
                sndd_info(amb->base1->perms[i].sndd, &info);
                len = info.duration;    /* SSrSoundData_GetDuration 0x47f790 */
                break;
            }
    if (len) dialog_end = ticks + len;
    else if (dialog_id == UINT32_MAX) dialog_end = ticks + 120;
    else dialog_end = 0;
    /* Here the original shows the subtitle (SSrAmbient_GetSubtitle 0x481c70) for the length + 60 ticks. */
    return false;
}

bool sound_dialog_play(const char *name) { return !dialog_start(name, false); }
void sound_dialog_interrupt(const char *name) { dialog_start(name, true); }
bool sound_dialog_playing(void) { return dialog_on; }

/* 0x514410: a free slot, the ambient at priority 3. Volume 0..1, 1 without. Nothing if all 16 are taken. */
void sound_music_start(const char *name, float volume)
{
    Ambient *amb = (Ambient *)ambient_named(name);
    if (!amb) return;
    Slot *s = NULL;
    for (int i = 0; i < 16 && !s; i++) if (slots[i].id == UINT32_MAX) s = &slots[i];
    if (!s) return;
    copy_name(s->name, name);
    s->volume = clamp01(volume);
    amb->priority = 3;
    s->id = ambient_start(amb, NULL, 0, 0, &s->volume);
}

/* 0x514500: the first playing slot of the name stops. */
void sound_music_stop(const char *name)
{
    for (int i = 0; i < 16; i++)
        if (slots[i].id != UINT32_MAX && !nocase(name, slots[i].name)) { ambient_stop(slots[i].id); return; }
}

/* 0x514550: the first slot of the name, played or not (a freed slot keeps its name), gets the volume as
   given and the ambient the clamped one, over the seconds. */
void sound_music_volume(const char *name, float volume, float seconds)
{
    for (int i = 0; i < 16; i++)
        if (!nocase(name, slots[i].name)) {
            slots[i].volume = volume;
            ambient_set_volume(slots[i].id, clamp01(volume), seconds);
            return;
        }
}

/* 0x5145f0: at the listener, volume 0..1. */
void sound_impulse_play(const char *name, float volume)
{
    const Impulse *m = loaded ? find_nocase(&impulses, name) : NULL;
    if (!m || script_only) return;
    float v = clamp01(volume);
    impulse_play(m, NULL, &v);
}

static const char *arg_name(int argc, const SlValue *argv) { return argc >= 1 && argv[0].type == SL_STRING ? argv[0].s : NULL; }
static float arg_float(const SlValue *v) { return v->type == SL_FLOAT ? v->f : v->type == SL_STRING ? (float)atof(v->s) : (float)v->i; }

static bool cmd_music_start(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    const char *n = arg_name(argc, argv);
    if (n) sound_music_start(n, argc == 2 ? arg_float(&argv[1]) : 1.0f);
    return true;
}

static bool cmd_music_stop(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    const char *n = arg_name(argc, argv);
    if (n) sound_music_stop(n);
    return true;
}

static bool cmd_music_volume(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    const char *n = arg_name(argc, argv);
    if (n && argc >= 2) sound_music_volume(n, arg_float(&argv[1]), argc == 3 ? arg_float(&argv[2]) : 0.0f);
    return true;
}

static bool cmd_dialog_play(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    dialog_start(arg_name(argc, argv), false);
    return true;
}

/* The block form waits (the thread stalls) while a dialog plays, then starts its own and goes on. */
static bool cmd_dialog_play_block(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    return !dialog_start(arg_name(argc, argv), false);
}

static bool cmd_dialog_play_interrupt(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    dialog_start(arg_name(argc, argv), true);
    return true;
}

static bool cmd_impulse_play(SlThread *t, int argc, const SlValue *argv, SlValue *res, void *u)
{
    const char *n = arg_name(argc, argv);
    if (n) sound_impulse_play(n, argc == 2 ? arg_float(&argv[1]) : 1.0f);
    return true;
}

void sound_register_commands(void)
{
    sl_register("sound_music_start", cmd_music_start, NULL);
    sl_register("sound_music_stop", cmd_music_stop, NULL);
    sl_register("sound_music_volume", cmd_music_volume, NULL);
    sl_register("sound_dialog_play", cmd_dialog_play, NULL);
    sl_register("sound_dialog_play_block", cmd_dialog_play_block, NULL);
    sl_register("sound_dialog_play_interrupt", cmd_dialog_play_interrupt, NULL);
    sl_register("sound_ambient_start", cmd_music_start, NULL);
    sl_register("sound_ambient_stop", cmd_music_stop, NULL);
    sl_register("sound_ambient_volume", cmd_music_volume, NULL);
    sl_register("sound_impulse_play", cmd_impulse_play, NULL);
}

/* ---- the system ---- */

static void stop_all(void)      /* 0x483710 */
{
    for (int i = 0; i < PLAYING_MAX; i++) if (playing[i].state != PA_FREE) ambient_halt(playing[i].id);
    for (int i = 0; i < 7; i++) chan_stop(&stereo[i]), stereo[i].sndd = NULL;
    for (int i = 0; i < CHANNELS - 7; i++) chan_stop(&mono[i]), mono[i].sndd = NULL;
}

static void init_channel(Chan *c, uint16_t index, uint8_t kind)     /* 0x483200.. */
{
    memset(c, 0, sizeof *c);
    c->index = index;
    c->kind = kind;
    c->priority = 1;
    c->eff = c->pitch = c->volume = c->dist_vol = c->perm_vol = c->pan_l = c->pan_r = 1.0f;
    c->ds_freq = BUFFER_RATE;
}

void sound_initialize(void)
{
    for (int i = 0; i < 7; i++) init_channel(&stereo[i], (uint16_t)i, 2);
    for (int i = 0; i < CHANNELS - 7; i++) init_channel(&mono[i], (uint16_t)(7 + i), 1);
    memset(playing, 0, sizeof playing);
    next_id = 0;
    master = master_target = 1.0f;
    listener[0] = listener[1] = listener[2] = 0;
    facing[0] = 1, facing[1] = facing[2] = 0;
    for (int i = 0; i < 16; i++) slots[i].id = UINT32_MAX, slots[i].name[0] = 0;
    dialog_on = false;
    dialog_id = UINT32_MAX;
    dialog_end = 0;
    now = ticks = 0;
    enabled = true;
}

void sound_terminate(void)
{
    stop_all();
    vec_free(&groups, true);
    vec_free(&ambients, false);
    vec_free(&impulses, false);
    pcm_flush();
    nseen = 0;
    loaded = false;
    enabled = false;
}

bool sound_level_load(int level)
{
    for (int s = 0; s < tm_file_slots(); s++) if (tm_file_at(s)) load_file(tm_file_at(s));
    resolve_sounds();
    if (level == 0) resolve_pointers();
    loaded = true;
    for (int i = 0; i < 7; i++) if (!(stereo[i].flags & CH_PLAYING)) ds_volume(&stereo[i], 0);    /* 0x483540 */
    for (int i = 0; i < CHANNELS - 7; i++) if (!(mono[i].flags & CH_PLAYING)) ds_volume(&mono[i], 0);
    return ambients.n || groups.n;
}

void sound_level_unload(void)
{
    stop_all();
    for (uint32_t i = 0; i < groups.n; i++) {
        Group *g = groups.v[i];
        for (uint32_t k = 0; k < g->nperms; k++) g->perms[k].sndd = NULL;
    }
    pcm_flush();
    loaded = false;
}

void sound_set_listener(const float pos[3], const float dir[3])
{
    memcpy(listener, pos, sizeof listener);
    memcpy(facing, dir, sizeof facing);
}

void sound_set_volume(float v) { if (enabled) master_target = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* The frame update (0x483bd0): the overall volume slides by up to 0.05 towards its target, then every
   playing ambient takes its step. */
static void ss2_update(void)
{
    if (!enabled) return;
    if (master != master_target) {
        float d = master_target - master;
        if (d > 0.05f) d = 0.05f;
        else if (d < 0.0f && d <= -0.05f) d = -0.05f;
        master = clamp01(master + d);
        for (int i = 0; i < 7; i++) chan_set_volume(&stereo[i], stereo[i].volume);
        for (int i = 0; i < CHANNELS - 7; i++) chan_set_volume(&mono[i], mono[i].volume);
    }
    for (int i = 0; i < PLAYING_MAX; i++) if (playing[i].state != PA_FREE) machine(&playing[i]);
}

/* OSrUpdate 0x514f20 (the scripts' part): slots of ended ambients are freed, the dialog ends. Still to come
   with the level's sound objects: the placed ambients (0x61fa34), the sound spheres and volumes (0x620078)
   and the projectiles' flybys (0x62009c). */
void sound_update(void)
{
    ticks++;
    now++;
    for (int i = 0; i < 16; i++) if (slots[i].id != UINT32_MAX && !ambient_alive(slots[i].id)) slots[i].id = UINT32_MAX;
    if (dialog_on) {
        if (dialog_id == UINT32_MAX || ambient_alive(dialog_id)) {
            if (dialog_end <= ticks) dialog_on = false, dialog_end = 0, dialog_id = UINT32_MAX;
        } else
            dialog_id = UINT32_MAX, dialog_end = 0, dialog_on = false;
    }
    ss2_update();
}

SoundStats sound_stats(void)
{
    SoundStats s = { groups.n, ambients.n, impulses.n, 0, 0 };
    for (uint32_t i = 0; i < groups.n; i++) {
        const Group *g = groups.v[i];
        s.permutations += g->nperms;
        for (uint32_t k = 0; k < g->nperms; k++) s.unresolved += !g->perms[k].sndd;
    }
    return s;
}

bool sound_has_ambient(const char *name) { return ambient_named(name) != NULL; }
bool sound_has_impulse(const char *name) { return loaded && find_nocase(&impulses, name) != NULL; }
