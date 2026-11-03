/* The materials: see material.h. */
#include "material.h"

#include "akev.h"
#include "tm.h"

#include <stdlib.h>
#include <string.h>

enum {
    MTRL_PARENT = 0x08,         /* the material it is a kind of (a link), none for the root */
    TMBD_COUNT = 0x0c,          /* from the block's tag */
    TMBD_ENTRIES = 0x10,        /* 0x40 each: the material's name, the texture's (32 bytes each) */
    TMBD_ENTRY = 0x40,
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The bindings in force (the texture's +0x98 in the original). */
typedef struct { const void *txmp, *mtrl; } Binding;
static Binding *bindings;
static uint32_t nbindings, cap;

static const char *instance_name(const void *data)
{
    const TmInstance *in = data ? tm_handle_instance(tm_handle_of(data)) : NULL;
    return in && in->name ? in->name + 4 : NULL;
}

bool material_breakable(const void *mtrl)
{
    /* the traversal from the root down: "Glass" sets it, "Unbreak_Glass" clears it, the others take
       their parent's; walked up here, the nearest of the two decides */
    for (int guard = 0; mtrl && guard < 64; guard++) {
        const char *n = instance_name(mtrl);
        if (n && !strcmp(n, "Glass")) return true;
        if (n && !strcmp(n, "Unbreak_Glass")) return false;
        mtrl = tm_link((const uint8_t *)mtrl + MTRL_PARENT);
    }
    return false;
}

static void bind(const void *txmp, const void *mtrl)
{
    for (uint32_t i = 0; i < nbindings; i++)
        if (bindings[i].txmp == txmp) { bindings[i].mtrl = mtrl; return; }
    if (nbindings == cap) {
        uint32_t nc = cap ? cap * 2 : 1024;
        Binding *b = realloc(bindings, nc * sizeof *b);
        if (!b) return;
        bindings = b, cap = nc;
    }
    bindings[nbindings++] = (Binding){ txmp, mtrl };
}

void material_level_load(void)
{
    nbindings = 0;
    /* the BINA class keeps the last block loaded (0x517660); level 0 has the one list */
    const uint8_t *list = NULL;
    for (int s = 0; s < tm_file_slots(); s++) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (!in->data || !in->name || strncmp(in->name, "BINADBMT", 8)) continue;
            size_t rs;
            const uint8_t *raw = tm_raw(in->data, &rs);
            uint32_t size = rd32(in->data), off = rd32(in->data + 4);
            if (!raw || (uint64_t)off + size > rs || size < TMBD_ENTRIES || memcmp(raw + off, "DBMT", 4)) continue;
            uint32_t n = rd32(raw + off + TMBD_COUNT);
            if ((uint64_t)TMBD_ENTRIES + (uint64_t)n * TMBD_ENTRY > size) continue;
            list = raw + off;
        }
    }
    if (!list) return;
    char name[33];
    for (uint32_t k = 0, n = rd32(list + TMBD_COUNT); k < n; k++) {
        const uint8_t *e = list + TMBD_ENTRIES + TMBD_ENTRY * k;
        memcpy(name, e, 32), name[32] = 0;
        const void *mtrl = tm_instance_get(TM_TAG("Mtrl"), name);   /* MArMaterialType_GetByName 0x4871d0 */
        memcpy(name, e + 32, 32), name[32] = 0;
        const void *txmp = tm_instance_get(TM_TAG("TXMP"), name);
        if (txmp) bind(txmp, mtrl);     /* an unknown material is material 0 */
    }
}

const void *material_of_texture(const void *txmp)
{
    for (uint32_t i = 0; i < nbindings; i++)
        if (bindings[i].txmp == txmp) return bindings[i].mtrl;
    return NULL;
}

void material_mark_breakable(void *akev)
{
    AkevQuad q;
    for (uint32_t i = 0, n = akev_quad_count(akev); i < n; i++) {
        if (!akev_quad(akev, i, &q) || (q.texture & 0xffff) == 0xffff) continue;
        const void *t = akev_texture(akev, q.texture & 0xffff);
        if (t && material_breakable(material_of_texture(t))) akev_quad_set_flags(akev, i, 0x200000, 0);
    }
}
