/* The impact effects: see impact.h. */
#include "impact.h"

#include "character.h"
#include "sound.h"
#include "tm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the trees (MArImpacts_PreProcess 0x4874a0 with MAiImpacts_Traverse 0x4879f0; the materials'
   0x487aa0 and 0x487f50) ---- */

enum { TYPES_MAX = 512, PARENT = 0x08 };

typedef struct {
    const void *data[TYPES_MAX];        /* by index (0x5eb800, 0x5eb7f4) */
    int parent[TYPES_MAX], depth[TYPES_MAX];
    bool mark[TYPES_MAX];               /* +0x04: blunt (the impact types), glass (the materials) */
    int n;                              /* 0x5eb7fc, 0x5eb7f8 */
} Tree;
static Tree impacts, materials;

static const char *instance_name(const void *data)
{
    const TmInstance *in = data ? tm_handle_instance(tm_handle_of(data)) : NULL;
    return in && in->name ? in->name + 4 : "";
}

typedef struct { const void *data; int child, next; } Node;

static void walk(Tree *t, const Node *nodes, int k, int parent, int depth, bool mark, bool materials_tree)
{
    for (; k >= 0; k = nodes[k].next) {
        const char *name = instance_name(nodes[k].data);
        bool m;
        if (!materials_tree) m = mark || !strcmp(name, "Blunt");    /* 0x53e598 */
        else m = !strcmp(name, "Glass") || (strcmp(name, "Unbreak_Glass") && mark);     /* 0x53e7bc, 0x53e7ac */
        int i = t->n++;
        t->data[i] = nodes[k].data, t->parent[i] = parent, t->depth[i] = depth, t->mark[i] = m;
        walk(t, nodes, nodes[k].child, i, depth + 1, m, materials_tree);
    }
}

/* The instances by their parent links: the root (one, "multiple base types" else) and each node's
   children in their names' order, then the walk from the root numbers them. */
static void build(Tree *t, uint32_t tag, bool materials_tree)
{
    memset(t, 0, sizeof *t);
    static void *list[TYPES_MAX];
    static Node nodes[TYPES_MAX];
    int n = tm_instance_list(tag, list, TYPES_MAX);
    int root = -1;
    for (int i = 0; i < n; i++) nodes[i] = (Node){ list[i], -1, -1 };
    for (int i = 0; i < n; i++) {
        const void *up = tm_link((const uint8_t *)list[i] + PARENT);
        int p = -1;
        for (int j = 0; up && j < n; j++)
            if (list[j] == up) p = j;
        int *head;
        if (p < 0) {
            if (up) continue;           /* a parent not among them: left out ("there were types ...") */
            if (root >= 0) continue;    /* "multiple base types" */
            root = i;
            continue;
        }
        head = &nodes[p].child;
        const char *name = instance_name(list[i]);
        while (*head >= 0 && strcmp(name, instance_name(nodes[*head].data)) >= 0) head = &nodes[*head].next;
        nodes[i].next = *head;
        *head = i;
    }
    if (root >= 0) {
        nodes[root].next = -1;
        walk(t, nodes, root, -1, 0, false, materials_tree);
    }
}

void impact_types_load(void)
{
    build(&impacts, TM_TAG("Impt"), false);
    build(&materials, TM_TAG("Mtrl"), true);
}

static int index_of(const Tree *t, const void *data)
{
    for (int i = 0; data && i < t->n; i++)
        if (t->data[i] == data) return i;
    return -1;
}

int impact_type_by_name(const char *name) { return name ? index_of(&impacts, tm_instance_get(TM_TAG("Impt"), name)) : -1; }
int impact_material_index(const void *mtrl) { return index_of(&materials, mtrl); }
int impact_material_by_name(const char *name) { return name ? index_of(&materials, tm_instance_get(TM_TAG("Mtrl"), name)) : -1; }
int impact_type_count(void) { return impacts.n; }
int impact_material_count(void) { return materials.n; }
int impact_type_parent(int type) { return type >= 0 && type < impacts.n ? impacts.parent[type] : -1; }
bool impact_type_blunt(int type) { return type >= 0 && type < impacts.n && impacts.mark[type]; }
int impact_material_parent(int m) { return m >= 0 && m < materials.n ? materials.parent[m] : -1; }
int impact_material_depth(int m) { return m >= 0 && m < materials.n ? materials.depth[m] : 0; }
bool impact_material_glass(int m) { return m >= 0 && m < materials.n && materials.mark[m]; }

int impact_character_material(const Character *c, int part, bool ignore_shield)
{
    /* ONrCharacter_GetMaterialType 0x4e6200: shielded, the shield's material; else the part's, from the
       class's body part materials (CBPM, ONCC+0xc3c, a Mtrl link a part); "Character" without */
    if (!ignore_shield) {
        if (c->flags & 0x40000000u) return impact_material_by_name("Super_Shield");    /* 0x550f28 */
        if (((const uint8_t *)c->oncc)[0xc8e] && c->super_level > 0.5f) return impact_material_by_name("Super_Shield");
        if (c->shield) return impact_material_by_name("Shield");      /* 0x550f20 */
    }
    const uint8_t *cbpm = tm_link((const uint8_t *)c->oncc + 0xc3c);
    const void *m = cbpm && part >= 0 && part < 19 ? tm_link(cbpm + 4 * part) : NULL;
    if (m) return impact_material_index(m);
    return impact_material_by_name("Character");    /* "character %s class %s bodypart %d has no material" */
}

/* ---- the table (ONiImpactEffect_Load 0x4fe9b0) ----

   The block: a header of seven u32s (the version, the impact names', material names', particles',
   sounds', entries' and material indices' counts), then the impact names (0x84 each: the name, then
   its index in the file and the type it is now, u16s), the material names (the same), the lookup (8
   bytes an impact type: the type, the count of its material indices and the first), the particles
   (0x54: the class's name, then a P3rCreateEffect specification), the sounds (0x30, 0x28 before
   version 2: the impulse's name, then at +0x24 the impulse, +0x28 flags, bit 0 an AI sound, +0x2a its
   type, +0x2c its radius), the entries (0x14: the type, the material, the component, the modifier,
   the particles' count, the sound (-1 none) at +0xc, the first particle at +0x10) and the material
   indices (8 bytes: the material, the count of its entries and the first). */

typedef struct { int type, count; uint32_t first; } Lookup;
typedef struct { int material, count; uint32_t first; } MatIndex;
typedef struct { int type, material, component, modifier, nparticles; int32_t sound; uint32_t first; } Entry;
typedef struct { char cls_name[65]; P3EffectSpec spec; } Particle;
typedef struct { char name[33]; const void *impulse; uint16_t flags, ai_type; float ai_radius; } Sound;

static struct {
    bool ok;
    Lookup *lookup;             /* by type index (0x5ecf1c) */
    MatIndex *matidx;           /* 0x5ecf2c */
    Entry *entries;             /* 0x5ecf08 */
    Particle *particles;        /* 0x5eced8 */
    Sound *sounds;              /* 0x5ecf24 */
    uint32_t n[7];
} E;

static void effects_free(void)
{
    free(E.lookup), free(E.matidx), free(E.entries), free(E.particles), free(E.sounds);
    memset(&E, 0, sizeof E);
}

static int cmp_matidx(const void *a, const void *b)
{
    return ((const MatIndex *)a)->material - ((const MatIndex *)b)->material;
}

void impact_effects_load(void)
{
    effects_free();
    const uint8_t *b = NULL;
    uint32_t size = 0;
    for (int s = 0; s < tm_file_slots() && !b; s++) {
        TmFile *f = tm_file_at(s);
        for (uint32_t i = 0; f && i < tm_file_instance_count(f); i++) {
            const TmInstance *in = tm_file_instance(f, i);
            if (!in->data || !in->name || strncmp(in->name, "BINAEINO", 8)) continue;
            size_t rs;
            const uint8_t *raw = tm_raw(in->data, &rs);
            uint32_t sz = rd32(in->data), off = rd32(in->data + 4);
            if (!raw || (uint64_t)off + sz > rs || sz < 8 + 0x1c || memcmp(raw + off, "EINO", 4)) continue;
            b = raw + off + 8, size = sz - 8;
            break;
        }
    }
    if (!b) return;
    uint32_t version = rd32(b), ni = rd32(b + 4), nm = rd32(b + 8), np = rd32(b + 12), ns = rd32(b + 16);
    uint32_t ne = rd32(b + 20), nmi = rd32(b + 24);
    uint32_t ssz = version < 2 ? 0x28 : 0x30;
    if (ni > 0xfffe || nm > 0xfffe || np > 0xfffe || ns > 0xfffe) return;
    uint64_t want = 0x1c + (uint64_t)ni * 0x8c + (uint64_t)nm * 0x84 + (uint64_t)np * 0x54 + (uint64_t)ns * ssz +
                    (uint64_t)ne * 0x14 + (uint64_t)nmi * 8;
    if (want != size) return;   /* "expected size %d but buffer is %d" */
    const uint8_t *inames = b + 0x1c, *mnames = inames + ni * 0x84, *look = mnames + nm * 0x84;
    const uint8_t *parts = look + ni * 8, *snds = parts + np * 0x54, *ents = snds + ns * ssz, *mis = ents + ne * 0x14;
    char name[129];
    /* the file's impact types and materials as the trees number them now (-1: gone) */
    int *itype = calloc(ni + 1, sizeof *itype), *imat = calloc(nm + 1, sizeof *imat);
    E.lookup = calloc((size_t)impacts.n + 1, sizeof *E.lookup);
    E.matidx = calloc(nmi + 1, sizeof *E.matidx);
    E.entries = calloc(ne + 1, sizeof *E.entries);
    E.particles = calloc(np + 1, sizeof *E.particles);
    E.sounds = calloc(ns + 1, sizeof *E.sounds);
    if (!itype || !imat || !E.lookup || !E.matidx || !E.entries || !E.particles || !E.sounds) {
        free(itype), free(imat), effects_free();
        return;
    }
    for (uint32_t i = 0; i < ni; i++) {
        memcpy(name, inames + i * 0x84, 128), name[128] = 0;
        itype[i] = impact_type_by_name(name);
    }
    for (uint32_t i = 0; i < nm; i++) {
        memcpy(name, mnames + i * 0x84, 128), name[128] = 0;
        imat[i] = impact_material_by_name(name);
    }
    /* the lookup, by the type it is now (ONiIESwap_LookupIndices 0x4ff9d0, then the remap of 0x4fe9b0) */
    for (int t = 0; t < impacts.n; t++) E.lookup[t] = (Lookup){ t, 0, 0 };
    for (uint32_t i = 0; i < ni; i++) {
        const uint8_t *l = look + i * 8;
        uint16_t f = rd16(l);
        int t = f < ni ? itype[f] : -1;
        if (t >= 0) E.lookup[t] = (Lookup){ t, rd16(l + 2), rd32(l + 4) };
    }
    /* the material indices (ONiIESwap_MaterialIndices 0x4ff590): each impact's sorted by the material
       again if a material's number changed */
    for (uint32_t i = 0; i < nmi; i++) {
        const uint8_t *m = mis + i * 8;
        uint16_t f = rd16(m);
        E.matidx[i] = (MatIndex){ f < nm ? imat[f] : -1, rd16(m + 2), rd32(m + 4) };
    }
    for (int t = 0; t < impacts.n; t++)
        if (E.lookup[t].count && E.lookup[t].first + (uint32_t)E.lookup[t].count <= nmi)
            qsort(E.matidx + E.lookup[t].first, (size_t)E.lookup[t].count, sizeof *E.matidx, cmp_matidx);
    /* the entries (ONiIESwap_Entries 0x4ff2e0) */
    for (uint32_t i = 0; i < ne; i++) {
        const uint8_t *e = ents + i * 0x14;
        uint16_t ft = rd16(e), fm = rd16(e + 2);
        E.entries[i] = (Entry){ ft < ni ? itype[ft] : -1, fm < nm ? imat[fm] : -1, rd16(e + 4), rd16(e + 6), rd16(e + 8),
                                (int32_t)rd32(e + 12), rd32(e + 16) };
        if ((uint32_t)E.entries[i].sound != 0xffffffffu && (uint32_t)E.entries[i].sound >= ns) E.entries[i].sound = -1;
        if (E.entries[i].first + (uint32_t)E.entries[i].nparticles > np) E.entries[i].nparticles = 0;
        if (E.entries[i].component > 2) E.entries[i].component = 2;
    }
    /* the particles (ONiIESwap_Particles 0x4ff220: the collision type kept to 0..3, the location to 0..5) */
    for (uint32_t i = 0; i < np; i++) {
        const uint8_t *p = parts + i * 0x54;
        Particle *q = &E.particles[i];
        memcpy(q->cls_name, p, 64), q->cls_name[64] = 0;
        uint32_t o = rd32(p + 0x44), l = rd32(p + 0x48);
        q->spec.orientation = (int32_t)o < 0 ? 0 : o > 3 ? 3 : o;
        q->spec.location = (int32_t)l < 0 ? 0 : l > 5 ? 5 : l;
        memcpy(&q->spec.offset, p + 0x4c, 4);
        q->spec.decal_static = p[0x4c] != 0, q->spec.decal_random_rotation = p[0x4d] != 0;
    }
    /* the sounds (0x4fdf00 makes the old 0x28-byte ones 0x30) */
    for (uint32_t i = 0; i < ns; i++) {
        const uint8_t *s = snds + i * ssz;
        Sound *q = &E.sounds[i];
        memcpy(q->name, s, 32), q->name[32] = 0;
        if (version >= 2) {
            q->flags = rd16(s + 0x28), q->ai_type = rd16(s + 0x2a);
            memcpy(&q->ai_radius, s + 0x2c, 4);
        }
    }
    E.n[0] = ni, E.n[1] = nm, E.n[2] = np, E.n[3] = ns, E.n[4] = ne, E.n[5] = nmi, E.n[6] = version;
    E.ok = true;
    free(itype), free(imat);
}

void impact_effects_level_begin(void)
{
    /* the particles' classes (P3rGetParticleClass by name) and the impulses, as the level has them */
    for (uint32_t i = 0; E.ok && i < E.n[2]; i++) E.particles[i].spec.cls = p3_class(E.particles[i].cls_name);
    for (uint32_t i = 0; E.ok && i < E.n[3]; i++) E.sounds[i].impulse = sound_impulse_find(E.sounds[i].name);
}

int impact_effects_unresolved(void)
{
    int n = 0;
    for (uint32_t i = 0; E.ok && i < E.n[2]; i++) n += E.particles[i].cls_name[0] && !E.particles[i].spec.cls;
    return n;
}

bool impact_effects_counts(uint32_t out[5])
{
    for (int k = 0; k < 5; k++) out[k] = E.n[k];
    return E.ok;
}

/* ONiImpactEffect_Lookup 0x4fe7f0: from the type up to the root, and for each from the material up its
   parents, the entries of the pair whose modifier fits (the asked one any: 0, the entry's any, or the
   same). A type that isn't blunt takes for each component the first found (the nearest material, then
   the nearest type); a blunt one takes the entry of the deepest material over the whole climb (the
   first of the deepest). A type's material indices are sorted by the material, whose parents come
   before it, so the climb goes on down the list from where it is. */
void impact_lookup(int type, int material, int modifier, int32_t out[3])
{
    out[0] = out[1] = out[2] = -1;
    if (!E.ok || type < 0 || type >= impacts.n || material < 0 || material >= materials.n) return;
    unsigned want = 7;
    bool blunt = impact_type_blunt(type);      /* 0x4871b0 */
    int depth[3] = { 0, 0, 0 };
    for (int t = type; t >= 0; t = impact_type_parent(t)) {     /* 0x487190 */
        const Lookup *l = &E.lookup[t];
        int k = l->count - 1;
        for (int m = material; m >= 0; m = impact_material_parent(m)) {     /* 0x487240 */
            while (k >= 0 && E.matidx[l->first + (uint32_t)k].material > m) k--;
            if (k < 0) break;
            const MatIndex *mi = &E.matidx[l->first + (uint32_t)k];
            if (mi->material != m) continue;
            for (int j = 0; j < mi->count; j++) {
                uint32_t ei = mi->first + (uint32_t)j;
                if (ei >= E.n[4]) break;
                const Entry *e = &E.entries[ei];
                if (modifier && e->modifier && modifier != e->modifier) continue;
                unsigned c = (unsigned)e->component;
                if (!blunt) {
                    if (want & (1u << c)) {
                        out[c] = (int32_t)ei;
                        want &= ~(1u << c);
                        if (!want) return;
                    }
                } else {
                    int d = impact_material_depth(m);   /* 0x487220 */
                    if (out[c] == -1 || depth[c] < d) out[c] = (int32_t)ei, depth[c] = d;
                }
            }
        }
    }
}

void impact_effect(int type, int material, int modifier, const P3EffectLoc *loc, float ai_scale)
{
    (void)ai_scale;
    if (type < 0 || material < 0) return;
    int32_t idx[3];
    impact_lookup(type, material, modifier, idx);
    for (int k = 0; k < 3; k++) {
        if (idx[k] < 0) continue;
        const Entry *e = &E.entries[idx[k]];
        if (e->sound >= 0) {
            const Sound *s = &E.sounds[e->sound];
            if (s->impulse) {
                /* softer under the impulse's impact velocity: 0.7 |v|^2 / v_i^2 + 0.3 (0x52b2fc, 0x5295a8) */
                float iv = sound_impulse_impact_velocity(s->impulse), vol, *pv = NULL;
                if (iv > 0.0f && loc->dir) {
                    const float *v = loc->dir;
                    float sp = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (sp < iv) vol = sp * sp * 0.7f / (iv * iv) + 0.3f, pv = &vol;
                }
                sound_impulse_play_at(s->impulse, loc->pos, pv);
            }
            /* s->flags & 1: AI2rKnowledge_Sound 0x49a8a0 (the type at +0x2a, the radius +0x2c times
               ai_scale): the AI's hearing is not ported */
        }
        for (int j = 0; j < e->nparticles; j++) {
            const Particle *p = &E.particles[e->first + (uint32_t)j];
            if (p->spec.cls) p3_create_effect(&p->spec, loc);
        }
    }
}
