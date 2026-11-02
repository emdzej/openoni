/* Installer VISE reader (see vise.h and docs/formats/vise.md). The layout was worked out from the Oni CD
   and the setup program (ONISETUP.EXE: WinMain-side setup 0x401a80, the support-file loop 0x4016a0, the
   decoder 0x401ee0) and checked by the CRC-32 every file record carries. */
#include "vise.h"
#include "crc32.h"
#include "inflate.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum {
    MAGIC = 0x56495345,          /* "ESIV" */
    ANCHOR = 32,                 /* 0xff bytes before every item's name */
    HDR_TO_ANCHOR = 0x46,        /* item header (type, id, parent) to the 0xff run */
    TYPE_FOLDER = 1,
    TYPE_FILE = 2,
    FILE_BODY = 0x4d,            /* file fields after the name, up to and including the CRC */
};

struct ViseArchive {
    char stem[512];              /* setup path without ".EXE" */
    ViseEntry *e;
    int n;
};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

typedef struct {
    uint32_t id;
    int entry;                   /* index into e, -1 for the root */
} Node;

static const char *parent_path(const ViseArchive *a, const Node *nodes, int nn, uint32_t parent)
{
    if (parent == 0) return "";
    for (int i = nn - 1; i >= 0; i--)
        if (nodes[i].id == parent && nodes[i].entry >= 0 && a->e[nodes[i].entry].dir) return a->e[nodes[i].entry].path;
    return NULL;
}

/* The item list: every item starts with (u32 type, u32 id, u32 parent); 0x46 bytes on, 32 bytes of 0xff,
   a 0, a u16 length and the name; then a body that depends on the type. Items are found by their 0xff
   runs (the bodies have no common length), in the order the installer lists them, parents first. */
static bool parse_items(ViseArchive *a, const uint8_t *d, size_t lo, size_t hi, char *err, size_t errcap)
{
    int cap = 256, nn = 0;
    a->e = calloc((size_t)cap, sizeof *a->e);
    Node *nodes = calloc((size_t)cap, sizeof *nodes);
    if (!a->e || !nodes) { free(nodes); snprintf(err, errcap, "out of memory"); return false; }
    for (size_t p = lo + HDR_TO_ANCHOR; p + ANCHOR + 3 < hi; p++) {
        if (d[p] != 0xff || (p > lo && d[p - 1] == 0xff)) continue;
        size_t q = p;
        while (q < hi && d[q] == 0xff) q++;
        if (q - p != ANCHOR || q + 3 > hi || d[q] != 0) continue;
        size_t len = rd16(d + q + 1), name = q + 3;
        if (len == 0 || len >= 256 || name + len > hi) continue;
        bool printable = true;
        for (size_t i = 0; i < len; i++) printable &= d[name + i] >= 0x20 && d[name + i] < 0x7f;
        if (!printable) continue;
        const uint8_t *h = d + p - HDR_TO_ANCHOR;
        uint32_t type = rd32(h), id = rd32(h + 4), parent = rd32(h + 8);
        if (type != TYPE_FOLDER && type != TYPE_FILE) continue;   /* shortcuts, registry keys, actions */
        const char *pp = parent_path(a, nodes, nn, parent);
        if (!pp) { snprintf(err, errcap, "item %.*s: unknown parent %u", (int)len, d + name, parent); goto fail; }
        if (a->n == cap || nn == cap) {
            cap *= 2;
            ViseEntry *ne = realloc(a->e, (size_t)cap * sizeof *ne);
            Node *nn2 = realloc(nodes, (size_t)cap * sizeof *nn2);
            if (ne) a->e = ne;
            if (nn2) nodes = nn2;
            if (!ne || !nn2) { snprintf(err, errcap, "out of memory"); goto fail; }
        }
        ViseEntry *e = &a->e[a->n];
        memset(e, 0, sizeof *e);
        snprintf(e->path, sizeof e->path, "%s%s%.*s", pp, *pp ? "/" : "", (int)len, d + name);
        if (type == TYPE_FILE) {
            const uint8_t *b = d + name + len;
            if (name + len + FILE_BODY > hi) { snprintf(err, errcap, "%s: record cut short", e->path); goto fail; }
            e->size = rd32(b + 0x04);
            e->csize = rd32(b + 0x08);
            e->segment = rd32(b + 0x0c);
            e->offset = rd32(b + 0x10);
            e->mtime = rd32(b + 0x28) | (uint64_t)rd32(b + 0x2c) << 32;
            e->crc = rd32(b + 0x49);
        } else {
            e->dir = true;
        }
        nodes[nn].id = id;
        nodes[nn].entry = a->n;
        nn++;
        a->n++;
        p = name + len - 1;
    }
    free(nodes);
    if (!a->n) { snprintf(err, errcap, "no items in the archive"); return false; }
    return true;
fail:
    free(nodes);
    return false;
}

ViseArchive *vise_open(const char *setup, char *err, size_t errcap)
{
    size_t size;
    uint8_t *d = vfs_read_all(setup, &size);
    if (!d) { snprintf(err, errcap, "%s: not found", setup); return NULL; }
    ViseArchive *a = calloc(1, sizeof *a);
    if (!a) { free(d); snprintf(err, errcap, "out of memory"); return NULL; }
    /* The setup program ends with "ESIV" and the offset of segment 1 (its overlay). */
    uint32_t seg = size >= 8 && rd32(d + size - 8) == MAGIC ? rd32(d + size - 4) : 0;
    if (!seg || seg + 16 > size || rd32(d + seg) != MAGIC || rd32(d + seg + 8) != 1) {
        snprintf(err, errcap, "%s: not an Installer VISE setup program", setup);
        goto fail;
    }
    uint32_t seglen = rd32(d + seg + 12);
    size_t hi = (size_t)seg + seglen < size ? (size_t)seg + seglen : size;
    /* Segment 1: three length-prefixed strings (the second's length byte keeps 6 bits), then the support
       files the setup program unpacks for itself (u16 count; each: name, DOS date and time, two u32s, the
       compressed size, the data), then the installer's data with the item list. */
    size_t p = seg + 16;
    p += 1 + d[p];
    p += 1 + (d[p] & 0x3f);
    p += 1 + d[p];
    unsigned nsupport = rd16(d + p);
    p += 2;
    for (unsigned i = 0; i < nsupport && p < hi; i++) {
        p += 1 + d[p];
        p += 12;
        p += 4 + rd32(d + p);
    }
    if (p >= hi) { snprintf(err, errcap, "%s: segment 1 cut short", setup); goto fail; }
    if (!parse_items(a, d, p, hi, err, errcap)) goto fail;
    snprintf(a->stem, sizeof a->stem, "%s", setup);
    char *dot = strrchr(a->stem, '.');
    if (dot && !strchr(dot, '/')) *dot = 0;
    free(d);
    return a;
fail:
    free(d);
    vise_close(a);
    return NULL;
}

void vise_close(ViseArchive *a)
{
    if (!a) return;
    free(a->e);
    free(a);
}

int vise_count(const ViseArchive *a) { return a->n; }
const ViseEntry *vise_entry(const ViseArchive *a, int i) { return i >= 0 && i < a->n ? &a->e[i] : NULL; }

int vise_find(const ViseArchive *a, const char *path)
{
    for (int i = 0; i < a->n; i++)
        if (!strcasecmp(a->e[i].path, path)) return i;
    return -1;
}

uint8_t *vise_extract(ViseArchive *a, int i, char *err, size_t errcap)
{
    const ViseEntry *e = vise_entry(a, i);
    if (!e || e->dir) { snprintf(err, errcap, "not a file"); return NULL; }
    uint8_t *out = malloc(e->size ? e->size : 1);
    if (!out) { snprintf(err, errcap, "%s: out of memory", e->path); return NULL; }
    if (e->size) {
        if (e->segment < 2) { snprintf(err, errcap, "%s: files in segment 1 are not supported", e->path); goto fail; }
        char seg[600];
        snprintf(seg, sizeof seg, "%s.%03u", a->stem, e->segment);
        VfsFile *f = vfs_open(seg);
        if (!f) { snprintf(err, errcap, "%s: segment %s missing", e->path, seg); goto fail; }
        uint8_t *c = malloc(e->csize ? e->csize : 1);
        int64_t got = c ? vfs_read_at(f, e->offset, c, e->csize) : -1;
        vfs_close(f);
        if (got != (int64_t)e->csize) { free(c); snprintf(err, errcap, "%s: %s cut short", e->path, seg); goto fail; }
        int64_t n = inflate_vise(c, e->csize, out, e->size);
        free(c);
        if (n != (int64_t)e->size) { snprintf(err, errcap, "%s: damaged compressed data", e->path); goto fail; }
    }
    if (crc32(out, e->size) != e->crc) { snprintf(err, errcap, "%s: CRC mismatch", e->path); goto fail; }
    return out;
fail:
    free(out);
    return NULL;
}
