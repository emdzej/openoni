/* The materials (MTRL instances, MArMaterials): a tree under "Default" that tells what a surface is made
   of, and the textures' materials (the BINA "TMBD" list, ONrTextureMaterials). A surface is breakable if
   its material is "Glass" or under it, unless it is (or is under) "Unbreak_Glass". */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ONrTextureMaterials_PreProcess 0x5177e0 and _LevelLoad 0x517790: each listed texture of the loaded
   files takes its material (later entries win). Call after a level's files load. */
void material_level_load(void);
/* The texture's material (MTRL data), NULL for one not listed (material 0, "Default"). */
const void *material_of_texture(const void *txmp);
/* MAiMaterials_Traverse 0x487f50's breakable flag (MTRL +4), read through 0x488060. */
bool material_breakable(const void *mtrl);
/* AKrLevel_Begin 0x43ffe0's end: every quad of the environment whose texture's material is breakable
   takes the breakable flag (0x200000). */
void material_mark_breakable(void *akev);
