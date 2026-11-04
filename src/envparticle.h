/* The environment particles (EP, EnvParticle.c): the particles placed in the level (the ENVP instance
   the level's ONLV links at +0x70), each a particle class at a matrix with a tag the scripts address
   with the particle command (ONiParticle3_Command 0x50a6f0: create, reset, kill, do <event>).

   Not ported yet: the decal classes (flags2 0x800000, placed on the wall behind them: 0x453b60) and the
   particles of the animated objects (an ENVP an OBOA links, 0x438d90), which follow their object. */
#pragma once

/* The level's start (FUN_00454f90 on ONLV +0x70, after 0x455500): every particle of the list made,
   unless its flags hold 2 ("not initially created"). */
void envparticle_load(void);
void envparticle_reset(void);
void envparticle_register_commands(void);
