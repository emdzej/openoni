/* The particle system (P3, Particle3.c): the classes (the BINA instances "3RAP<name>", format
   docs/formats/par3.md), the particles, their events and actions, their emitters, their collisions with
   the walls and the characters, and their damage. Weapons' shooters, projectiles, sparks, glass and
   smoke are all particles. Notes: docs/re/particles.md.

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
/* P3rCreateEffect 0x456030's common part: a particle at pos with vel and orient (3x3, NULL along the
   velocity), its create event. NULL if none could be made. */
P3Particle *p3_effect(P3Class *c, const float pos[3], const float vel[3], const float *orient, uint32_t tick);
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
} P3Draw;
typedef void (*P3DrawFn)(const P3Draw *d, void *user);
void p3_each_drawn(P3DrawFn fn, void *user);

/* The damage a hit does to a character (the hook into the character module: 0x5179f0 through
   0x5178c0). owner: 0x1000000 | id for a character's, 0 none. */
extern bool (*p3_damage_hook)(Character *c, const float point[3], const float dir[3], int type, float damage,
                              float stun, float knockback, uint32_t owner, uint32_t handle, bool self_immune);
/* The environment's damage at a quad (glass, 0x462bf0 then 0x508f10). */
extern void (*p3_glass_hook)(uint32_t quad, const float point[3], const float dir[3], float force, bool blast);

/* The script commands: particle (ONiParticle3_CreateParticle 0x509ff0), particle_kill_all. */
void p3_register_commands(void);
/* Counts, for the tests: classes loaded and particles alive. */
int p3_class_count(void);
int p3_live_count(void);
const char *p3_class_name(const P3Class *c);
