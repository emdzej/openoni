/* The environment's draw (M3rEnv_DrawGQList 0x41a930 and the GL engine's quad lists): the level's
   gunk quads (akev.h) as one static mesh, in ranges by texture. */
#pragma once
#include <stdbool.h>

bool env_draw_load(const void *env);
void env_draw_unload(void);
/* Solid quads, then the transparent ones (blended, no depth write). */
void env_draw(void);
/* The same, of the quads the visibility marks (akev_vis.h); the game's draw. */
void env_draw_visible(void);
/* The environment loaded (NULL none). */
const void *env_draw_environment(void);
