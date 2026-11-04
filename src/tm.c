/* TM, the instance manager: see tm.h, docs/formats/dat.md. */
#include "tm.h"

#include "crc32.h"
#include "exe.h"
#include "vfs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The template table (built by 0x51d1c0, read by TMrTemplate_Register 0x4200f0 and 0x420330). */
enum {
    TEMPLATE_COUNT_VA = 0x558cf0,
    TEMPLATE_SETUP_VA = 0x51d1c0,   /* mov [entry + 0x10], layout for each template; then the base */
    TEMPLATE_TABLE_PTR = 0x620138,
    TEMPLATE_STRIDE = 0x30,
    MAX_TEMPLATES = 256,
};

/* Level file header (checked by TMiGame_InstanceFile_LoadHeader 0x420ff0). */
enum {
    DAT_VERSION = 0x56523331,       /* "VR31" (big-endian files have it swapped: not shipped) */
    DAT_HEADER = 0x40, DAT_DESC = 0x14, DAT_TEMPLATE = 0x10, DAT_NAME = 0x08,
    MAX_LEVEL = 0x7f,
    MAX_SLOTS = 64,
    FIRST_LEVEL_SLOT = 2,           /* slots 0 and 1: the dynamic instance files (TMrGame_Initialize) */
};

/* Layout opcodes, walked by TMiGame_Instance_PrepareForMemory 0x421c50. */
enum {
    OP_8 = 1, OP_4, OP_2, OP_1,
    OP_ARRAY,           /* count (u8), the element's layout, OP_ARRAY_END */
    OP_ARRAY_END,       /* also ends the instance's layout */
    OP_VARARRAY,        /* count field kind (u8), the element's layout, OP_VARARRAY_END */
    OP_VARARRAY_END,
    OP_LINK,            /* the target's tag (u32, 0 = any) */
};

struct TmFile {
    char name[64];          /* "level0_Final" */
    int level;
    uint32_t id;            /* (suffix hash | level << 24) << 1 | 1 (TMrUtility_LevelInfo_Get) */
    uint8_t *buf;
    size_t size;
    TmInstance *inst;
    uint32_t n_inst;
    const uint8_t *names;   /* the name directory: (instance index, name offset) sorted by name */
    uint32_t n_names;
    bool resolved;
    VfsFile *raw;
    uint8_t *raw_all;       /* the .raw whole, once tm_raw asked for it */
    size_t raw_size;
};

static TmTemplate templates[MAX_TEMPLATES];
static int n_templates;
static struct { char name[64]; int level; uint32_t id; } levels[MAX_SLOTS];  /* the valid level files */
static int n_levels;
static TmFile *slots[MAX_SLOTS];
static int n_slots;
static TmLoadStats stats;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static bool fail(char *err, size_t cap, const char *fmt, ...)
{
    if (err && cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, cap, fmt, ap);
        va_end(ap);
    }
    return false;
}

static void tag_str(uint32_t tag, char out[5])
{
    for (int i = 0; i < 4; i++) out[i] = (char)(tag >> (24 - 8 * i));
    out[4] = 0;
}

/* ---- templates ---- */

/* The table is static data in the exe; its layout pointers are filled in at start-up by 0x51d1c0, a
   run of mov dword [mem], imm32 (c7 05 addr imm) ending with the store of the table's base. */
static bool load_templates(char *err, size_t cap)
{
    uint32_t at[MAX_TEMPLATES], layout[MAX_TEMPLATES];
    int n_at = 0;
    uint32_t base = 0;
    for (uint32_t pc = TEMPLATE_SETUP_VA;; pc += 10) {
        const uint8_t *ins = exe_data(pc, 10);
        if (!ins || ins[0] != 0xc7 || ins[1] != 0x05) return fail(err, cap, "template setup: unexpected code at 0x%x", (unsigned)pc);
        uint32_t addr = rd32(ins + 2), imm = rd32(ins + 6);
        if (addr == TEMPLATE_TABLE_PTR) { base = imm; break; }
        if (n_at == MAX_TEMPLATES) return fail(err, cap, "template setup: too long");
        at[n_at] = addr;
        layout[n_at++] = imm;
    }
    uint32_t count = exe_u32(TEMPLATE_COUNT_VA);
    if (!count || count > MAX_TEMPLATES) return fail(err, cap, "template count %u", (unsigned)count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = base + i * TEMPLATE_STRIDE;
        const uint8_t *p = exe_data(e, TEMPLATE_STRIDE);
        if (!p) return fail(err, cap, "template table at 0x%x", (unsigned)e);
        TmTemplate *t = &templates[i];
        t->checksum = (uint64_t)rd32(p + 4) << 32 | rd32(p);
        t->tag = rd32(p + 8);
        t->name = exe_str(rd32(p + 0xc));
        t->flags = rd32(p + 0x14);
        t->layout = 0;
        for (int k = 0; k < n_at; k++)
            if (at[k] == e + 0x10) t->layout = layout[k];
        if (!t->layout) return fail(err, cap, "template %u has no layout", (unsigned)i);
    }
    n_templates = (int)count;
    return true;
}

/* TMrTemplate_GetFromTag 0x420330: a linear search, as the original. */
const TmTemplate *tm_template(uint32_t tag)
{
    for (int i = 0; i < n_templates; i++)
        if (templates[i].tag == tag) return &templates[i];
    return NULL;
}

int tm_template_count(void) { return n_templates; }
const TmTemplate *tm_template_at(int i) { return i >= 0 && i < n_templates ? &templates[i] : NULL; }

/* ---- level files ---- */

/* TMrUtility_LevelInfo_Get 0x420590: "level<N>_<suffix>.dat". The suffix "Final" is the shipped
   game; any other one is hashed into the file id. */
static bool level_info(const char *fname, int *level, bool *final, uint32_t *id)
{
    char base[32];
    snprintf(base, sizeof base, "%s", fname);
    char *us = strchr(base, '_');
    if (!us || strncmp(base, "level", 5)) return false;
    *us = 0;
    int n = atoi(base + 5);
    char *suffix = us + 1, *dot = strchr(suffix, '.');
    if (!dot) return false;
    *dot = 0;
    uint32_t hash = 0;
    *final = strcmp(suffix, "Final") == 0;
    if (!*final) {
        int w = 1;
        for (const char *c = suffix; *c; c++, w++) {
            int u = *c >= 'a' && *c <= 'z' ? *c - 32 : *c;
            hash += (uint32_t)((u - 0x40) * w);
        }
    }
    if (n < 0 || n > MAX_LEVEL) return false;
    *level = n;
    *id = ((hash & 0xffffff) | (uint32_t)n << 24) << 1 | 1;
    return true;
}

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char a = s[n - m + i], b = suffix[i];
        if ((a | 32) != (b | 32)) return false;
    }
    return true;
}

/* TMiGame_InstanceFile_LoadHeader 0x420ff0: the header's version and record sizes. */
static bool check_header(const uint8_t *h, size_t size, char *err, size_t cap)
{
    if (size < DAT_HEADER) return fail(err, cap, "too short");
    if (rd32(h + 8) != DAT_VERSION) return fail(err, cap, "not an instance file (or big-endian)");
    if (rd16(h + 0xc) != DAT_HEADER || rd16(h + 0xe) != DAT_DESC || rd16(h + 0x10) != DAT_TEMPLATE || rd16(h + 0x12) != DAT_NAME)
        return fail(err, cap, "record sizes");
    return true;
}

static void list_level(const char *name, bool is_dir, void *user)
{
    (void)user;
    int level;
    bool final;
    uint32_t id;
    if (is_dir || !ends_with_ci(name, ".dat") || n_levels == MAX_SLOTS) return;
    if (ends_with_ci(name, "tools.dat")) return;        /* "skipping tool file" */
    if (!level_info(name, &level, &final, &id) || !final) return;
    snprintf(levels[n_levels].name, sizeof levels[0].name, "%.*s", (int)(strlen(name) - 4), name);
    levels[n_levels].level = level;
    levels[n_levels].id = id;
    n_levels++;
}

/* TMrGame_Initialize 0x4209e0: a level file is valid if its header is, and every template it uses
   exists with the same checksum. Only _Final files: the original records whether a file is final and
   then loads by level number; the CD has nothing else. */
static bool level_file_valid(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, "GameDataFolder/%s.dat", name);
    VfsFile *f = vfs_open(path);
    if (!f) return false;
    uint8_t h[DAT_HEADER];
    bool ok = vfs_read_at(f, 0, h, sizeof h) == (int64_t)sizeof h && check_header(h, sizeof h, NULL, 0);
    if (ok) {
        uint32_t n_inst = rd32(h + 0x14), n_names = rd32(h + 0x18), n_tmpl = rd32(h + 0x1c);
        uint64_t at = DAT_HEADER + (uint64_t)n_inst * DAT_DESC + (uint64_t)n_names * DAT_NAME;
        uint8_t *t = malloc((size_t)n_tmpl * DAT_TEMPLATE + 1);
        ok = t && vfs_read_at(f, at, t, (size_t)n_tmpl * DAT_TEMPLATE) == (int64_t)n_tmpl * DAT_TEMPLATE;
        for (uint32_t i = 0; ok && i < n_tmpl; i++) {
            const uint8_t *e = t + i * DAT_TEMPLATE;
            const TmTemplate *tt = tm_template(rd32(e + 8));
            ok = tt && tt->checksum == ((uint64_t)rd32(e + 4) << 32 | rd32(e));
        }
        free(t);
    }
    vfs_close(f);
    return ok;
}

bool tm_initialize(char *err, size_t cap)
{
    tm_terminate();
    if (!exe_loaded()) return fail(err, cap, "Oni.exe not loaded");
    if (!load_templates(err, cap)) return false;
    if (!vfs_list("GameDataFolder", list_level, NULL)) return fail(err, cap, "no GameDataFolder");
    int kept = 0;
    for (int i = 0; i < n_levels; i++)
        if (level_file_valid(levels[i].name)) levels[kept++] = levels[i];
    n_levels = kept;
    n_slots = FIRST_LEVEL_SLOT;
    return true;
}

bool tm_level_valid(int level)
{
    for (int i = 0; i < n_levels; i++)
        if (levels[i].level == level) return true;
    return false;
}

static void file_free(TmFile *f)
{
    if (!f) return;
    if (f->raw) vfs_close(f->raw);
    free(f->raw_all);
    free(f->inst);
    free(f->buf);
    free(f);
}

/* TMiGame_InstanceFile_New_FromFileRef 0x422590: reads the file whole and sets up the descriptors:
   data offsets are into the data block, name offsets into the name block. */
static TmFile *file_open(const char *name, char *err, size_t cap)
{
    char path[96];
    snprintf(path, sizeof path, "GameDataFolder/%s.dat", name);
    TmFile *f = calloc(1, sizeof *f);
    if (!f) { fail(err, cap, "out of memory"); return NULL; }
    snprintf(f->name, sizeof f->name, "%s", name);
    bool final;
    char fname[80];
    snprintf(fname, sizeof fname, "%s.dat", name);
    if (!level_info(fname, &f->level, &final, &f->id)) { fail(err, cap, "%s: not a level file name", name); goto bad; }
    f->buf = vfs_read_all(path, &f->size);
    if (!f->buf) { fail(err, cap, "%s: can't read", path); goto bad; }
    if (!check_header(f->buf, f->size, err, cap)) goto bad;
    const uint8_t *h = f->buf;
    f->n_inst = rd32(h + 0x14);
    f->n_names = rd32(h + 0x18);
    uint32_t data_off = rd32(h + 0x20), names_off = rd32(h + 0x28);
    uint64_t dir_end = DAT_HEADER + (uint64_t)f->n_inst * DAT_DESC + (uint64_t)f->n_names * DAT_NAME;
    if (dir_end > f->size || data_off > f->size || names_off > f->size) { fail(err, cap, "%s: tables outside the file", name); goto bad; }
    f->names = f->buf + DAT_HEADER + (size_t)f->n_inst * DAT_DESC;
    f->inst = calloc(f->n_inst ? f->n_inst : 1, sizeof *f->inst);
    if (!f->inst) { fail(err, cap, "out of memory"); goto bad; }
    for (uint32_t i = 0; i < f->n_inst; i++) {
        const uint8_t *d = f->buf + DAT_HEADER + (size_t)i * DAT_DESC;
        TmInstance *in = &f->inst[i];
        char t[5];
        tag_str(rd32(d), t);
        in->tmpl = tm_template(rd32(d));
        in->size = rd32(d + 0xc);
        in->flags = rd32(d + 0x10) & 0xffff;
        if (!in->tmpl) { fail(err, cap, "%s: instance %u: no template %s", name, (unsigned)i, t); goto bad; }
        if (!(in->flags & TM_INSTANCE_PLACEHOLDER)) {
            uint64_t off = (uint64_t)data_off + rd32(d + 4);
            if (off < 8 || off - 8 + in->size > f->size) { fail(err, cap, "%s: instance %u: data outside the file", name, (unsigned)i); goto bad; }
            in->data = f->buf + off;
        }
        if (!(in->flags & TM_INSTANCE_UNNAMED)) {
            uint64_t off = (uint64_t)names_off + rd32(d + 8);
            if (off >= f->size || !memchr(f->buf + off, 0, f->size - off)) { fail(err, cap, "%s: instance %u: name outside the file", name, (unsigned)i); goto bad; }
            in->name = (const char *)f->buf + off;
        }
    }
    for (uint32_t i = 0; i < f->n_names; i++)
        if (rd32(f->names + i * DAT_NAME) >= f->n_inst) { fail(err, cap, "%s: name directory entry %u", name, (unsigned)i); goto bad; }
    return f;
bad:
    file_free(f);
    return NULL;
}

/* TMiGame_InstanceFile_GetDataPtr 0x423160: binary search of one file's name directory (byte order, as
   strcmp). The instance, NULL if the file doesn't have it or has only a placeholder. */
static TmInstance *file_find(TmFile *f, const char *full)
{
    uint32_t lo = 0, hi = f->n_names;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        TmInstance *in = &f->inst[rd32(f->names + mid * DAT_NAME)];
        int c = in->name ? strcmp(full, in->name) : 1;
        if (c == 0) return in->data ? in : NULL;
        if (c > 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static TmInstance *find_by_name(const char *full, int *slot)
{
    for (int s = FIRST_LEVEL_SLOT; s < n_slots; s++) {
        if (!slots[s]) continue;
        TmInstance *in = file_find(slots[s], full);
        if (in) { *slot = s; return in; }
    }
    return NULL;
}

/* TMrInstance_GetDataPtr_List 0x423540 (0x4235a0's walk): file by file in load order, each file's
   named instances of the tag with data, in its name directory's order (by name). */
int tm_instance_list(uint32_t tag, void **out, int cap)
{
    char t[4] = { (char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag };
    int n = 0;
    for (int s = FIRST_LEVEL_SLOT; s < n_slots && n < cap; s++) {
        TmFile *f = slots[s];
        if (!f) continue;
        for (uint32_t i = 0; i < f->n_names && n < cap; i++) {
            TmInstance *in = &f->inst[rd32(f->names + i * DAT_NAME)];
            if (in->name && !memcmp(in->name, t, 4) && in->data) out[n++] = in->data;
        }
    }
    return n;
}

/* ---- the layout walk ---- */

typedef struct Walk Walk;
struct Walk {
    uint8_t *p, *end;           /* the instance's bytes (header included) */
    bool stop_at_array;         /* TM_TEMPLATE_NO_ARRAY_LINKS */
    bool (*link)(Walk *w, uint8_t *field, uint32_t want);
    TmFile *file;
    uint32_t links;
    char *err;
    size_t cap;
};

static bool walk_take(Walk *w, size_t n)
{
    if ((size_t)(w->end - w->p) < n) return fail(w->err, w->cap, "layout runs past the instance");
    w->p += n;
    return true;
}

/* 0x420390: past an empty array's element layout, to just after its end. */
static bool skip_layout(uint32_t *pc, char *err, size_t cap)
{
    int depth = 0;
    for (;;) {
        const uint8_t *op = exe_data((*pc)++, 1);
        if (!op) return fail(err, cap, "layout outside the exe");
        switch (*op) {
        case OP_LINK: *pc += 4; break;
        case OP_ARRAY: case OP_VARARRAY: *pc += 1; depth++; break;
        case OP_ARRAY_END: case OP_VARARRAY_END:
            if (depth-- == 0) return true;
            break;
        default: break;
        }
    }
}

/* TMiGame_Instance_PrepareForMemory 0x421c50: one layout (an instance's, or an array element's) from
   *pc up to and past its end opcode. */
static bool walk_layout(Walk *w, uint32_t *pc)
{
    for (;;) {
        const uint8_t *op = exe_data(*pc, 6);
        if (!op) return fail(w->err, w->cap, "layout outside the exe");
        (*pc)++;
        switch (op[0]) {
        case OP_8: if (!walk_take(w, 8)) return false; break;
        case OP_4: if (!walk_take(w, 4)) return false; break;
        case OP_2: if (!walk_take(w, 2)) return false; break;
        case OP_1: if (!walk_take(w, 1)) return false; break;
        case OP_ARRAY: {
            unsigned count = op[1];
            uint32_t start = ++*pc;
            for (unsigned i = 0; i < count; i++) {
                *pc = start;
                if (!walk_layout(w, pc)) return false;
            }
            if (!count && !skip_layout(pc, w->err, w->cap)) return false;
            break;
        }
        case OP_ARRAY_END:
        case OP_VARARRAY_END:
            return true;
        case OP_VARARRAY: {
            if (w->stop_at_array) return true;
            unsigned kind = op[1];
            uint32_t count;
            uint8_t *field = w->p;
            if (kind == 1) { if (!walk_take(w, 8)) return false; count = rd32(field); }
            else if (kind == 2) { if (!walk_take(w, 4)) return false; count = rd32(field); }
            else if (kind == 3) { if (!walk_take(w, 2)) return false; count = rd16(field); }
            else return fail(w->err, w->cap, "variable array kind %u", kind);
            uint32_t start = ++*pc;
            for (uint32_t i = 0; i < count; i++) {
                *pc = start;
                if (!walk_layout(w, pc)) return false;
            }
            if (!count && !skip_layout(pc, w->err, w->cap)) return false;
            break;
        }
        case OP_LINK: {
            uint8_t *field = w->p;
            if (!walk_take(w, 4)) return false;
            if (!w->link(w, field, rd32(op + 1))) return false;
            *pc += 4;
            break;
        }
        default:
            return fail(w->err, w->cap, "layout opcode %u at 0x%x", op[0], (unsigned)(*pc - 1));
        }
    }
}

static bool walk_instance(Walk *w, const TmInstance *in)
{
    w->p = in->data - 8;
    w->end = w->p + in->size;
    uint32_t pc = in->tmpl->layout;
    return walk_layout(w, &pc);
}

static TmInstance *link_target(Walk *w, uint32_t id, uint32_t want)
{
    if (!(id & 1) || (id >> 8) >= w->file->n_inst) {
        fail(w->err, w->cap, "link 0x%08x isn't an instance id", (unsigned)id);
        return NULL;
    }
    TmInstance *t = &w->file->inst[id >> 8];
    if (want && t->tmpl->tag != want) {
        char a[5], b[5];
        tag_str(t->tmpl->tag, a);
        tag_str(want, b);
        fail(w->err, w->cap, "link to %s, the layout wants %s", a, b);
        return NULL;
    }
    return t;
}

static uint32_t handle(int slot, const TmFile *f, const TmInstance *in)
{
    return (uint32_t)slot << 24 | (uint32_t)(in - f->inst);
}

/* Case OP_LINK of TMiGame_Instance_PrepareForMemory 0x421c50: a named target is looked up by name in
   every loaded file, first loaded first (so a level's copy of a level 0 instance gives way to level 0's); an unnamed one is the
   instance itself. */
static bool resolve_link(Walk *w, uint8_t *field, uint32_t want)
{
    uint32_t id = rd32(field);
    if (!id) return true;
    TmInstance *t = link_target(w, id, want);
    if (!t) return false;
    stats.links++;
    uint32_t h = 0;
    if (t->name) {
        int slot;
        TmInstance *found = find_by_name(t->name, &slot);
        if (found) {
            h = handle(slot, slots[slot], found);
            if (slots[slot] != w->file) stats.cross++;
        } else {
            stats.unresolved++;
        }
    } else {
        if (!t->data) return fail(w->err, w->cap, "dataPtr is NULL");
        h = tm_handle_of(t->data);
    }
    wr32(field, h);
    return true;
}

/* The link pass of TMrLevel_Load: every instance whose template has links. */
static bool resolve_file(int slot, char *err, size_t cap)
{
    TmFile *f = slots[slot];
    for (uint32_t i = 0; i < f->n_inst; i++) {
        TmInstance *in = &f->inst[i];
        if (in->data) wr32(in->data - 4, (uint32_t)slot);    /* TMiGame_Instance_Callback 0x423b30 */
    }
    Walk w = { .link = resolve_link, .file = f, .err = err, .cap = cap };
    for (uint32_t i = 0; i < f->n_inst; i++) {
        TmInstance *in = &f->inst[i];
        if (!in->data || (in->tmpl->flags & TM_TEMPLATE_NO_LINKS) || (in->flags & TM_INSTANCE_SKIP)) continue;
        w.stop_at_array = (in->tmpl->flags & TM_TEMPLATE_NO_ARRAY_LINKS) != 0;
        if (!walk_instance(&w, in)) {
            char e2[200];
            snprintf(e2, sizeof e2, "%s", err ? err : "");
            return fail(err, cap, "%s: instance %u (%s): %s", f->name, (unsigned)i, in->name ? in->name : in->tmpl->name, e2);
        }
    }
    f->resolved = true;
    return true;
}

bool tm_level_load(int level, char *err, size_t cap)
{
    if (!tm_level_valid(level)) return fail(err, cap, "level %d has no valid file", level);
    for (int i = 0; i < n_levels; i++) {
        if (levels[i].level != level) continue;
        if (n_slots == MAX_SLOTS) return fail(err, cap, "too many files");
        TmFile *f = file_open(levels[i].name, err, cap);
        if (!f) return false;
        slots[n_slots++] = f;
    }
    for (int s = FIRST_LEVEL_SLOT; s < n_slots; s++)
        if (slots[s] && !slots[s]->resolved && !resolve_file(s, err, cap)) return false;
    return true;
}

/* TMrLevel_Unload 0x423260: the level's files go (the ones whose id has the level number). The original
   moves the later slots down; here handles carry the slot, so the slot is left empty instead (trailing
   empty slots are dropped), which keeps the search order the same. Links into the dropped files from
   other files are not undone, in the original either (level 0 doesn't link into a level). */
void tm_level_unload(int level)
{
    for (int s = FIRST_LEVEL_SLOT; s < n_slots; s++)
        if (slots[s] && slots[s]->level == level) file_free(slots[s]), slots[s] = NULL;
    while (n_slots > FIRST_LEVEL_SLOT && !slots[n_slots - 1]) n_slots--;
}

void tm_terminate(void)
{
    for (int s = 0; s < n_slots; s++) file_free(slots[s]), slots[s] = NULL;
    n_slots = 0;
    n_levels = 0;
    n_templates = 0;
    memset(&stats, 0, sizeof stats);
}

int tm_file_slots(void) { return n_slots; }
TmFile *tm_file_at(int slot) { return slot >= 0 && slot < n_slots ? slots[slot] : NULL; }
const char *tm_file_name(const TmFile *f) { return f->name; }
uint32_t tm_file_instance_count(const TmFile *f) { return f->n_inst; }
const TmInstance *tm_file_instance(const TmFile *f, uint32_t i) { return i < f->n_inst ? &f->inst[i] : NULL; }
TmLoadStats tm_load_stats(void) { return stats; }

void *tm_instance_get(uint32_t tag, const char *name)
{
    char full[128];
    char t[5];
    tag_str(tag, t);
    snprintf(full, sizeof full, "%s%s", t, name);
    int slot;
    TmInstance *in = find_by_name(full, &slot);
    return in ? in->data : NULL;
}

uint32_t tm_handle_of(const void *data)
{
    const uint8_t *p = data;
    return rd32(p - 4) << 24 | rd32(p - 8) >> 8;
}

const TmInstance *tm_handle_instance(uint32_t h)
{
    int slot = (int)(h >> 24);
    uint32_t i = h & 0xffffff;
    if (slot < FIRST_LEVEL_SLOT || slot >= n_slots || !slots[slot] || i >= slots[slot]->n_inst) return NULL;
    return &slots[slot]->inst[i];
}

void *tm_handle_data(uint32_t h)
{
    const TmInstance *in = h ? tm_handle_instance(h) : NULL;
    return in ? in->data : NULL;
}

/* TMrInstance_GetRawOffset 0x423e10: the original loads the .raw whole next to the .dat
   (TMiGame_InstanceFile_RawFileRef 0x420720) and returns its base; here it is read where asked. */
int64_t tm_raw_read(const void *data, uint32_t off, void *dst, size_t len)
{
    const uint8_t *p = data;
    int slot = (int)rd32(p - 4);
    TmFile *f = tm_file_at(slot);
    if (!f || slot < FIRST_LEVEL_SLOT) return -1;
    if (!f->raw) {
        char path[96];
        snprintf(path, sizeof path, "GameDataFolder/%s.raw", f->name);
        f->raw = vfs_open(path);
        if (!f->raw) return -1;
    }
    return vfs_read_at(f->raw, off, dst, len);
}

/* TMiGame_InstanceFile_RawFileRef 0x420720 + TMrInstance_GetRawOffset 0x423e10: the .raw of the file an
   instance comes from, loaded whole (as the original loads it), and its size. NULL if it can't be read. */
const uint8_t *tm_raw(const void *data, size_t *size)
{
    const uint8_t *p = data;
    int slot = (int)rd32(p - 4);
    TmFile *f = tm_file_at(slot);
    if (!f || slot < FIRST_LEVEL_SLOT) return NULL;
    if (!f->raw_all) {
        char path[96];
        snprintf(path, sizeof path, "GameDataFolder/%s.raw", f->name);
        f->raw_all = vfs_read_all(path, &f->raw_size);
        if (!f->raw_all) return NULL;
    }
    if (size) *size = f->raw_size;
    return f->raw_all;
}

/* ---- the reference summary (tools/onidat.py) ---- */

static bool summary_link(Walk *w, uint8_t *field, uint32_t want)
{
    uint32_t id = rd32(field);
    uint32_t index = 0xffffffffu;
    if (id) {
        if (!link_target(w, id, want)) return false;
        index = id >> 8;
        w->links++;
    }
    wr32(field, index);
    return true;
}

bool tm_file_summary(const char *dat_path, TmSummary *out, char *err, size_t cap)
{
    memset(out, 0, sizeof *out);
    const char *slash = strrchr(dat_path, '/');
    char name[64];
    snprintf(name, sizeof name, "%s", slash ? slash + 1 : dat_path);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    TmFile *f = file_open(name, err, cap);
    if (!f) return false;
    uint8_t *copy = NULL;
    size_t copy_cap = 0;
    uint32_t h = 0;
    bool ok = true;
    Walk w = { .link = summary_link, .file = f, .err = err, .cap = cap };
    char line[200], t[5];
    for (uint32_t i = 0; ok && i < f->n_inst; i++) {
        TmInstance *in = &f->inst[i];
        tag_str(in->tmpl->tag, t);
        if (!in->data) {
            out->placeholders++;
            snprintf(line, sizeof line, "%u %s - %s\n", (unsigned)i, t, in->name ? in->name : "None");
        } else {
            if (copy_cap < in->size) {
                free(copy);
                copy_cap = in->size;
                copy = malloc(copy_cap);
                if (!copy) { ok = fail(err, cap, "out of memory"); break; }
            }
            memcpy(copy, in->data - 8, in->size);
            w.links = 0;
            w.p = copy;
            w.end = copy + in->size;
            uint32_t pc = in->tmpl->layout;
            if (!walk_layout(&w, &pc)) {
                char e2[200];
                snprintf(e2, sizeof e2, "%s", err ? err : "");
                ok = fail(err, cap, "%s: instance %u: %s", name, (unsigned)i, e2);
                break;
            }
            size_t used = (size_t)(w.p - copy);
            if (((used + 31) & ~(size_t)31) != in->size) out->slack++;
            memset(copy, 0, 8);
            out->links += w.links;
            snprintf(line, sizeof line, "%u %s %u %u %08x\n", (unsigned)i, t, (unsigned)in->size, (unsigned)w.links,
                     (unsigned)crc32(copy, in->size));
        }
        h = crc32_update(h, line, strlen(line));
    }
    out->instances = f->n_inst;
    out->hash = h;
    free(copy);
    file_free(f);
    return ok;
}
