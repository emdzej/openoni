/* TRAC, animation collections: a character class's moves, looked up by type (what the character does:
   run, punch, ...), the state it is in and the variant (its fighting mode, weapon). Each collection may
   fall back to a parent (Konoko's to the common moves). Format: docs/formats/tram.md#collections. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TRAC_TAG = 0x54524143, TRAC_ANY_STATE = 1 };

/* iAnimationCollection_DiskToMemory 0x428250: once per collection at load (it rewrites the entries in
   place: drops empty ones, sorts, turns the weights into running sums, and adds the shortcuts'
   collection). */
bool trac_prepare(void *trac);
/* Frees what the loads made (the shortcuts' collections), when the level goes. */
void trac_forget(void);
/* TRrCollection_Lookup 0x427f00: the animation of the type from the state, NULL if none. */
const void *trac_lookup(const void *trac, uint16_t type, uint16_t state, uint16_t varient);
/* Every animation of the type from the state (not from any state), the collection's then its
   fallbacks' in turn (FUN_00427dd0's run of the key, the parent at +0x10: AttemptThrow 0x4eaa10's
   walk). Returns how many, at most max. */
int trac_list(const void *trac, uint16_t type, uint16_t state, const void **out, int max);
/* ONrLevel_Load 0x5059d0 stores the level number the lookups compare with (TRAM_LEVEL). */
void trac_set_level(uint16_t level);
/* UUrRandom 0x426e10. */
uint16_t trac_random(void);
void trac_random_seed(uint32_t seed);
/* UUrLocalRandom 0x426e30, the second generator (sounds, particles). */
uint16_t trac_local_random(void);
