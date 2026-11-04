/* The CD's installer archive as a layer of the file layer: the Oni CD (an ISO image or a folder with its
   files) holds the whole game in its Installer VISE archive (ONISETUP.EXE + ONISETUP.002, vise.h), so the
   port reads the installed files from there and needs no install or extract step. Layout:
   docs/formats/vise.md.

   vfs_vise_layer puts the archive's files on top of the current mount at their installed paths
   ("Oni.exe", "GameDataFolder/level1_Final.dat"): a path is looked up on the medium first, then in the
   archive. A file is inflated whole when opened (the largest is 41 MB) and kept while it is open; opens
   of the same file share it. */
#pragma once
#include <stdbool.h>

/* If the mounted medium has ONISETUP.EXE and no Oni.exe (the CD or its image, not an installed game),
   layers the archive over it. False only if an archive is there but can't be read; true when it was
   layered or not needed. */
bool vfs_vise_layer(void);
