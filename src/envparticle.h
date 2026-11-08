/* The environment particles (EP, EnvParticle.c): the particles placed in the level (the ENVP instance
   the level's ONLV links at +0x70), each a particle class at a matrix with a tag the scripts address
   with the particle command (ONiParticle3_Command 0x50a6f0: create, reset, kill, do <event>).

   A decal class's (flags2 0x800000) is put on the wall behind it (0x453b60). The level's objects have
   their own lists (an ENVP the OBOA element links, OBrSetParticles 0x438d90): their particles hang from
   the object's matrix, their tags led by the object's name. */
#pragma once
#include <stdint.h>

/* The level's start (FUN_00454f90 on ONLV +0x70, after 0x455500): every particle of the list made,
   unless its flags hold 2 ("not initially created"). */
void envparticle_load(void);
void envparticle_reset(void);
void envparticle_register_commands(void);

/* An object's list (EPrArray_New 0x454f90 with the owner): its particles made at their places in
   the matrix's space, hanging from it (kept by the caller), each tag "<prefix>_<tag>". The stop
   (EPrArray_Delete 0x4552d0) kills them and takes the list's tags out. */
void envparticle_owned_start(const void *envp, const float *matrix, const char *prefix, uint32_t time);
void envparticle_owned_stop(const void *envp);
