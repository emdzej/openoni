/* TM: the template and instance manager (TMrInitialize 0x41ff40, TMrLevel_Load 0x4217d0). Every piece of
   game data is an instance of a template (a struct type named by a four-character tag: TXMP texture,
   M3GM geometry, ONCC character, ...), stored in the level files GameDataFolder/levelN_Final.dat (the
   instances) and .raw (bulk data the instances point into). Format: docs/formats/dat.md.

   The templates' layouts (which fields are links to other instances) are compiled into Oni.exe; they
   are read from the user's exe at run time (exe.h).

   Loading a level reads its .dat whole and resolves every link in place, as the original does. The
   original stores a pointer in a link field; here a link field holds a 32-bit handle (the port is also
   built 64-bit, for tests), read with tm_link(). An instance's data is preceded by 8 bytes: its id
   (index << 8 | 1) and, once loaded, its file's slot (the original: the file's pointer). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TM_TAG(s) ((uint32_t)(s)[0] << 24 | (uint32_t)(s)[1] << 16 | (uint32_t)(s)[2] << 8 | (uint32_t)(s)[3])

typedef struct {
    uint64_t checksum;      /* of the layout: a level file's template table must agree */
    uint32_t tag;
    const char *name;       /* "Texture Map", from the exe */
    uint32_t layout;        /* VA of the layout code in the exe */
    uint32_t flags;         /* TM_TEMPLATE_* */
} TmTemplate;

enum {
    TM_TEMPLATE_NO_LINKS = 1 << 1,          /* no link anywhere: the loader doesn't walk it */
    TM_TEMPLATE_NO_ARRAY_LINKS = 1 << 2,    /* none in the variable array: the walk stops there */
};

enum {
    TM_INSTANCE_UNNAMED = 1 << 0,
    TM_INSTANCE_PLACEHOLDER = 1 << 1,       /* no data: a named reference to an instance of another file */
    TM_INSTANCE_SKIP = 1 << 2,              /* not walked at load (never set in the shipped files) */
};

typedef struct {
    const TmTemplate *tmpl;
    uint8_t *data;          /* past the 8-byte header; NULL for a placeholder */
    const char *name;       /* with the tag in front ("TXMPNONE"); NULL if unnamed */
    uint32_t size;          /* including the header, padded to 32 bytes (some are bigger) */
    uint32_t flags;         /* TM_INSTANCE_* (the descriptor's low 16 bits) */
} TmInstance;

typedef struct TmFile TmFile;

/* TMrInitialize 0x41ff40 + TMrGame_Initialize 0x4209e0: the template table from the exe, and the level
   files in GameDataFolder that pass the checks (header, template checksums). Needs exe_init. */
bool tm_initialize(char *err, size_t errcap);
void tm_terminate(void);
const TmTemplate *tm_template(uint32_t tag);           /* NULL if the exe has no such template */
int tm_template_count(void);
const TmTemplate *tm_template_at(int i);
/* Whether a level has a valid file (TMrLevel_Load refuses the others). */
bool tm_level_valid(int level);

/* TMrLevel_Load 0x4217d0: loads the level's files and resolves the links of every file loaded so far
   that isn't resolved yet. Level 0 (the shared data) stays loaded under the others. */
bool tm_level_load(int level, char *err, size_t errcap);
/* TMrLevel_Unload 0x423260: drops the level's files. */
void tm_level_unload(int level);

/* The loaded files, in load order (slots 0 and 1 are the original's dynamic instance files, not used
   yet: NULL). */
int tm_file_slots(void);
TmFile *tm_file_at(int slot);
const char *tm_file_name(const TmFile *f);
uint32_t tm_file_instance_count(const TmFile *f);
const TmInstance *tm_file_instance(const TmFile *f, uint32_t index);

/* TMrInstance_GetDataPtr 0x4232e0: the data of the named instance (name without the tag), first loaded
   file first; NULL if none has it with data. */
void *tm_instance_get(uint32_t tag, const char *name);
/* An instance's handle (what a link field holds) and back. */
uint32_t tm_handle_of(const void *data);
void *tm_handle_data(uint32_t handle);
const TmInstance *tm_handle_instance(uint32_t handle);
/* The instance a link field points at, NULL for none. */
static inline void *tm_link(const void *field)
{
    const uint8_t *p = field;
    return tm_handle_data((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
/* Reads from the .raw of the file an instance comes from (offsets in instance fields are into it).
   Returns the byte count, -1 on error. */
int64_t tm_raw_read(const void *data, uint32_t off, void *dst, size_t len);

/* What the loader did, over all loads since tm_initialize. */
typedef struct {
    uint32_t links;         /* link fields walked that weren't 0 */
    uint32_t cross;         /* of them, resolved by name to another file (level 0's copy) */
    uint32_t unresolved;    /* named targets no loaded file has data for: the field becomes 0 */
} TmLoadStats;
TmLoadStats tm_load_stats(void);

/* The reference check against tools/onidat.py: walks every instance of one .dat by its layout without
   loading it, and hashes it (see the tool's docstring). False (err says why) on a layout mismatch. */
typedef struct {
    uint32_t instances, placeholders, links, slack;
    uint32_t hash;
} TmSummary;
bool tm_file_summary(const char *dat_path, TmSummary *out, char *err, size_t errcap);
