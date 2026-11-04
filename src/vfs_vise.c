/* The installer archive as a file layer: see vfs_vise.h. */
#include "vfs_vise.h"
#include "vfs.h"
#include "vise.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char SETUP[] = "ONISETUP.EXE";

/* An inflated file, shared by its opens. */
typedef struct Open {
    struct Open *next;
    int entry;
    unsigned refs;
    uint8_t *data;
    uint32_t size;
} Open;

typedef struct {
    VfsBackend base;        /* the medium under the archive */
    ViseArchive *a;
    Open *open;
} Layer;

/* A handle: the medium's file, or one of ours (told apart by the tag). */
typedef struct {
    bool ours;
    void *h;
} Handle;

static void *layer_open(void *ctx, const char *rel, uint64_t *size)
{
    Layer *l = ctx;
    Handle *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    if ((h->h = l->base.open(l->base.ctx, rel, size))) return h;
    int i = vise_find(l->a, rel);
    const ViseEntry *e = i >= 0 ? vise_entry(l->a, i) : NULL;
    if (!e || e->dir) {
        free(h);
        return NULL;
    }
    Open *o = l->open;
    while (o && o->entry != i) o = o->next;
    if (!o) {
        char err[256];
        /* vise_extract reads ONISETUP.002 through the file layer: this layer, then the medium */
        uint8_t *d = vise_extract(l->a, i, err, sizeof err);
        if (!d && e->size) {
            free(h);
            return NULL;
        }
        o = calloc(1, sizeof *o);
        if (!o) {
            free(d);
            free(h);
            return NULL;
        }
        o->entry = i;
        o->data = d;
        o->size = e->size;
        o->next = l->open;
        l->open = o;
    }
    o->refs++;
    h->ours = true;
    h->h = o;
    *size = o->size;
    return h;
}

static int64_t layer_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    Layer *l = ctx;
    Handle *h = file;
    if (!h->ours) return l->base.read_at(l->base.ctx, h->h, off, dst, len);
    const Open *o = h->h;
    if (off >= o->size) return 0;
    if (len > o->size - off) len = (size_t)(o->size - off);
    memcpy(dst, o->data + off, len);
    return (int64_t)len;
}

static void layer_close(void *ctx, void *file)
{
    Layer *l = ctx;
    Handle *h = file;
    if (!h->ours) {
        l->base.close(l->base.ctx, h->h);
    } else {
        Open *o = h->h;
        if (--o->refs == 0) {
            Open **p = &l->open;
            while (*p != o) p = &(*p)->next;
            *p = o->next;
            free(o->data);
            free(o);
        }
    }
    free(h);
}

/* Directory listings: the medium's entries, then the archive's under dir that the medium lacks. */
typedef struct {
    VfsListFn fn;
    void *user;
    char seen[256][64];
    int nseen;
} ListCtx;

static bool seen(ListCtx *c, const char *name)
{
    for (int i = 0; i < c->nseen; i++)
        if (!strcasecmp(c->seen[i], name)) return true;
    if (c->nseen < 256) snprintf(c->seen[c->nseen++], sizeof c->seen[0], "%s", name);
    return false;
}

static void list_base(const char *name, bool is_dir, void *user)
{
    ListCtx *c = user;
    seen(c, name);
    c->fn(name, is_dir, c->user);
}

static bool layer_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    Layer *l = ctx;
    ListCtx *c = calloc(1, sizeof *c);
    if (!c) return false;
    c->fn = fn;
    c->user = user;
    char d[512];
    snprintf(d, sizeof d, "%s", dir);
    for (char *s = d; *s; s++)
        if (*s == '\\') *s = '/';
    size_t dl = strlen(d);
    while (dl && d[dl - 1] == '/') d[--dl] = 0;
    bool any = l->base.list && l->base.list(l->base.ctx, d, list_base, c);
    for (int i = 0; i < vise_count(l->a); i++) {
        const ViseEntry *e = vise_entry(l->a, i);
        const char *p = e->path;
        if (dl && (strncasecmp(p, d, dl) || p[dl] != '/')) continue;
        const char *rest = p + (dl ? dl + 1 : 0);
        const char *slash = strchr(rest, '/');
        size_t el = slash ? (size_t)(slash - rest) : strlen(rest);
        char name[64];
        if (!el || el >= sizeof name) continue;
        memcpy(name, rest, el);
        name[el] = 0;
        any = true;
        if (!seen(c, name)) fn(name, slash != NULL || e->dir, user);
    }
    free(c);
    return any;
}

static void layer_unmount(void *ctx)
{
    Layer *l = ctx;
    while (l->open) {
        Open *o = l->open;
        l->open = o->next;
        free(o->data);
        free(o);
    }
    vise_close(l->a);
    if (l->base.unmount) l->base.unmount(l->base.ctx);
    free(l);
}

bool vfs_vise_layer(void)
{
    if (!vfs_exists(SETUP) || vfs_exists("Oni.exe")) return true;
    char err[256], desc[512], d[600];
    ViseArchive *a = vise_open(SETUP, err, sizeof err);
    if (!a) return false;
    Layer *l = calloc(1, sizeof *l);
    if (!l || !vfs_detach(&l->base, desc, sizeof desc)) {
        free(l);
        vise_close(a);
        return false;
    }
    l->a = a;
    snprintf(d, sizeof d, "%s + %s", desc, SETUP);
    VfsBackend b = { l, layer_open, layer_read_at, layer_close, layer_list, layer_unmount };
    vfs_mount(&b, d);
    return true;
}
