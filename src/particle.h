/* The particle system (P3, Particle3.c): the classes (the BINA instances "3RAP<name>", format
   docs/formats/par3.md), the particles, their events and actions, their emitters, their collisions with
   the walls and the characters, and their damage. Weapons' shooters, projectiles, sparks, glass and
   smoke are all particles; so are the animations' own (docs/re/character.md, "The animation's particles").

   Not ported yet (stand-ins marked in the code): the decals (bullet holes), the attractors (homing),
   the impact effects (ONrImpactEffect: their sounds and particles), the particles' sounds, the AI's
   knowledge of dangerous projectiles and the flyby sounds (the callbacks of ONrParticle3_Initialize),
   the physics objects other than the characters, the lens flares and the detail levels. */
#pragma once
#include "character.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct P3Class P3Class;
typedef struct P3Particle P3Particle;

/* The events (the definition's 16 action lists). */
enum {
    P3_EV_UPDATE, P3_EV_PULSE, P3_EV_START, P3_EV_STOP, P3_EV_BG_START, P3_EV_BG_STOP, P3_EV_HIT_WALL,
    P3_EV_HIT_CHAR, P3_EV_LIFETIME, P3_EV_EXPLODE, P3_EV_BROKEN_LINK, P3_EV_CREATE, P3_EV_DIE,
    P3_EV_NEW_ATTRACTOR, P3_EV_DELAY_START, P3_EV_DELAY_STOP,
};

/* The per-particle blocks a class may ask for (flags1 bits 12 to 24), in the order of their offsets. */
enum {
    P3_X_VELOCITY, P3_X_ORIENTATION, P3_X_OFFSET, P3_X_ATTACH, P3_X_PREV_POS, P3_X_DECAL, P3_X_TEX_INDEX,
    P3_X_TEX_TIME, P3_X_OWNER, P3_X_CONTRAIL, P3_X_LENSFLARE, P3_X_ATTRACTOR, P3_X_CACHE, P3_XS,
};

/* P3rInitialize 0x455a50 and the level's end (0x455b20): no classes, no particles. */
void p3_reset(void);
/* P3rLoadParticleDefinition 0x459f20 for every BINA "3RAP" instance of the loaded files, then the
   classes' setup (0x4574e0) and the level's start (0x457e50). */
void p3_load(void);
/* P3rGetParticleClass 0x45a030: the class of that name, NULL if none. */
P3Class *p3_class(const char *name);
/* P3rCreateParticle 0x455e40: a particle of the class at the tick, NULL if there is no room. */
P3Particle *p3_create(P3Class *c, uint32_t tick);
/* P3rSendEvent 0x45b2c0: the event's actions at the time (seconds). True if the particle died. */
bool p3_send_event(P3Class *c, P3Particle *p, int event, float time);
/* P3rKillParticle (the inline kill of 0x456ce0 and 0x456e20): the die event, then the particle goes. */
void p3_kill(P3Class *c, P3Particle *p);
/* P3rUpdate 0x45c830: every particle up to the tick (the gameplay classes: P3rUpdate(time + 1, true) in
   ONrGameState_ProcessHeartbeat; the decorative ones once a frame, with false). chars: the characters
   the particles may hit. */
void p3_update(uint32_t tick, bool gameplay, Character **chars, int nchars);
/* p3_removedangerous (0x50aaf0, 0x458310; also ONrGameState_BeginCutscene without "keepparticles"): the
   dangerous particles go at the next update. */
void p3_remove_dangerous(void);
/* P3rCreateEffect 0x456030's arguments. The specification (0x14 bytes, the impact effects' particles
   from +0x40): the class, how the effect is turned and where it is put. */
enum {
    P3_ORIENT_PARENT = 0, P3_ORIENT_REVERSE = 1, P3_ORIENT_REFLECT = 2, P3_ORIENT_NORMAL = 3,
    P3_LOC_DEFAULT = 0, P3_LOC_OFFSET = 1, P3_LOC_DECAL = 4, P3_LOC_ATTACHED = 5,
};
typedef struct {
    P3Class *cls;               /* +0x00 */
    uint32_t orientation;       /* +0x04: P3_ORIENT_*, 0..3 */
    uint32_t location;          /* +0x08: P3_LOC_*, 0..5 (2 and 3: the place is left alone) */
    float offset;               /* +0x0c: along the normal, for P3_LOC_OFFSET */
    bool decal_static, decal_random_rotation;   /* +0x0c, +0x0d for a decal (the same bytes) */
} P3EffectSpec;
/* Where (the location, 0x6c bytes): the kinds of what was hit. */
enum { P3_HIT_NONE = 0, P3_HIT_WALL = 1, P3_HIT_CHARACTER = 2, P3_HIT_MELEE = 3 /* a blow (HandleAttackMask) */, P3_HIT_NONE_ATTACHED = 4 };
typedef struct {
    uint32_t tick;              /* +0x00: when */
    uint32_t owner;             /* +0x04: the damage's owner (P3_X_OWNER) */
    float pos[3];               /* +0x08 */
    float vel[3];               /* +0x14 */
    float normal[3];            /* +0x20: the hit's */
    float up[3];                /* +0x2c: a decal's axis (zero: one across the normal) */
    float decal_scale[2];       /* +0x38, +0x3c: a decal's size, times its class's */
    P3Class *parent_cls;        /* +0x40: the particle it comes from */
    P3Particle *parent;         /* +0x44 */
    const float *orientation;   /* +0x48: 3x3, NULL none */
    const float *dir;           /* +0x4c: the parent's velocity, NULL none */
    const float *attach;        /* +0x50: the matrix it hangs from, NULL none */
    uint32_t kind;              /* +0x54: P3_HIT_* */
    uint32_t quad;              /* +0x58: the wall's (P3_HIT_WALL) */
    Character *chr;             /* +0x58: the character's (P3_HIT_CHARACTER) */
} P3EffectLoc;
/* P3rCreateEffect 0x456030: the particle made, NULL if none (a decal's, once made, in its list). */
P3Particle *p3_create_effect(const P3EffectSpec *spec, const P3EffectLoc *loc);
/* P3rCreateEffect 0x456030's common part: a particle at pos with vel and orient (3x3, NULL along the
   velocity), its create event. NULL if none could be made. */
P3Particle *p3_effect(P3Class *c, const float pos[3], const float vel[3], const float *orient, uint32_t tick);
/* P3rCreateEffect with an owner (P3_X_OWNER) and the matrix the particle hangs from (P3_X_ATTACH, NULL
   none; pos then in its space). */
P3Particle *p3_effect_owned(P3Class *c, const float pos[3], const float vel[3], const float *orient,
                            const float *attach_m, uint32_t owner, uint32_t tick);
/* P3rSetGlobalTint 0x456f90 (on) and 0x456fa0 (off): the particles created meanwhile take the tint
   (into their tint variable). */
void p3_global_tint(bool on, uint32_t tint);
/* 0x456fb0: the particle's tint variable set, if its class has one. */
bool p3_set_tint(P3Class *c, P3Particle *p, uint32_t tint);
/* The tick of the last update. */
uint32_t p3_tick(void);
/* The level's environment the particles collide with (AKEV). */
void p3_set_environment(const void *env);

/* A particle's handle (+0x2c) and the particle a handle names while it lives (NULL else). */
uint32_t p3_handle(const P3Particle *p);
P3Particle *p3_from_handle(uint32_t h, P3Class **c);
uint32_t p3_class_def_flags(const P3Class *c, int which);   /* the definition's flags1 (0) or flags2 (1) */
/* The particle's position (P3rGetPositionPtr 0x45abc0), NULL if it died following its link. */
float *p3_position(P3Class *c, P3Particle *p);
/* P3rGetRealPosition 0x45acc0: with its offset and attachment matrix. False if it died. */
bool p3_real_position(P3Class *c, P3Particle *p, float out[3]);
/* ONrCharacter_Collide_Ray 0x4f1630 for anyone: the segment from s along d against the character's
   parts; the nearest point on them. */
bool p3_character_ray(const Character *c, const float s[3], const float d[3], float point[3]);
/* The particle's block of the kind (P3_X_*), NULL if its class has none. */
void *p3_block(P3Class *c, P3Particle *p, int x);
/* The matrices particles hang from (P3_X_ATTACH holds an id): a weapon's, for its shooters. */
uint32_t p3_attach_id(const float *m);
uint32_t p3_flags(const P3Particle *p);
void p3_set_flags(P3Particle *p, uint32_t f);
void p3_set_life(P3Particle *p, float life);

/* Drawing: every visible particle of the drawn classes, with the camera's place and axes. */
typedef struct {
    const P3Class *cls;
    const P3Particle *p;
    float pos[3];               /* the real position */
    const float *orient;        /* 3x3 by columns, NULL none */
    const float *velocity;      /* NULL none */
    float scale, yscale, alpha, rotation, x_offset, x_shorten;
    float chop;                 /* P_CHOPPED: what of the far end the life has cut (1 - life / fade) */
    uint32_t tint;
    const void *texture;        /* the TXMP or M3GM instance (+0x118), NULL none */
    int mode;                   /* the sprite mode (flags1 bits 5 to 7) */
    bool geometry, vector;      /* flags1 0x100, flags2 0x8000000 */
    uint32_t tex_index, tex_time;
    float edge_min, edge_max;
    bool edge_fade, one_sided;
    bool flare;                 /* a lens flare (p3_each_flare): drawn through walls */
    float end[3];               /* a vector's: the line from pos to it */
} P3Draw;
typedef void (*P3DrawFn)(const P3Draw *d, void *user);
void p3_each_drawn(P3DrawFn fn, void *user);
/* The lens flares (flags2 bit 30), drawn after the rest: those the camera at eye, looking along fwd
   (a unit vector) with its near plane, sees, faded in and out. */
void p3_each_flare(const float eye[3], const float fwd[3], float near, uint32_t now, P3DrawFn fn, void *user);
/* A contrail's segment (P3rDisplayClass_Contrail 0x461d20): from this particle's end to the one it
   follows; each end its place and its half width (a vector), its colour (the tint, the alpha on top)
   and its v across the texture. */
typedef struct {
    float a[3], wa[3], b[3], wb[3];
    uint32_t argb_a, argb_b;
    float va, vb;
    const void *texture;        /* the TXMP */
    uint32_t tex_index, tex_time;
} P3Contrail;
typedef void (*P3ContrailFn)(const P3Contrail *s, void *user);
/* The contrails' segments now, for a camera at eye; brings the particles' contrail blocks up to date
   (the original does it as it draws). */
void p3_each_contrail(const float eye[3], uint32_t now, P3ContrailFn fn, void *user);

/* The owner's inaccuracy, which widens direction 5's cone (WPrParticleInaccuracy 0x51ced0). */
extern float (*p3_inaccuracy_hook)(uint32_t owner);
/* The damage a hit does to a character (the hook into the character module: 0x5179f0 through
   0x5178c0). owner: 0x1000000 | id for a character's, 0 none. */
extern bool (*p3_damage_hook)(Character *c, const float point[3], const float dir[3], int type, float damage,
                              float stun, float knockback, uint32_t owner, uint32_t handle, bool self_immune);
/* The environment's damage at a quad (glass, P3iDamageQuad 0x462bf0 then 0x508f10); point and dir may
   be NULL. */
extern void (*p3_glass_hook)(uint32_t quad, const float point[3], const float dir[3], float force, float radius);
/* A blast's damage to the characters around pos (WPrBlast 0x517f40). */
extern void (*p3_blast_hook)(const void *env, const float pos[3], int type, float damage, float stun, float knockback,
                             float radius, int falloff, uint32_t owner, Character *exclude, uint32_t handle,
                             bool self_immune);

/* The script commands: particle (ONiParticle3_CreateParticle 0x509ff0), particle_kill_all. */
void p3_register_commands(void);
/* Counts, for the tests: classes loaded and particles alive. */
int p3_class_count(void);
int p3_live_count(void);
const char *p3_class_name(const P3Class *c);
