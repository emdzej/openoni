/* The environment's script commands (AIiScript_Env*): parts of the level's geometry turned on and off by
   the ids the level's editor gave them. */
#pragma once

/* The level's environment the commands act on (AKEV data, tm.h), NULL for none. */
void env_set(void *akev);
/* env_show. */
void env_register_commands(void);
/* The glass broken since env_set (for the traces and tests). */
int env_glass_broken(void);
