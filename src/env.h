/* The environment's script commands (AIiScript_Env*): parts of the level's geometry turned on and off by
   the ids the level's editor gave them. */
#pragma once

#include "sl.h"

#include <stdint.h>

/* The level's environment the commands act on (AKEV data, tm.h), NULL for none. */
void env_set(void *akev);
/* env_show, env_shade, env_broken and the glass's switches. */
void env_register_commands(void);
/* The glass broken since env_set (for the traces and tests). */
int env_glass_broken(void);
/* The opaque shade of a script's three channels (r, g, b: 0..1, clamped), as env_shade and obj_shade make
   it. */
uint32_t env_script_shade(const SlValue *rgb);
