/* Host-filesystem backends of the file layer (POSIX): directories and disc-image files. Used by the
   tests and tools; a backend without a filesystem (gasm) mounts a VfsSource instead. */
#pragma once
#include "vfs.h"

/* Mounts a data root: a directory (the installed game folder), a disc image of it (.iso, raw .bin), or a
   .cue sheet (its FILE line names the image, its TRACK line the sector format). False if it can't be
   mounted; whether it holds the game is up to the caller (vfs_exists("Oni.exe")). */
bool vfs_mount_path(const char *path);
/* $OPENONI_DATA if set, else ./game (tests and tools run from the project root): the installed game
   folder (Oni.exe + GameDataFolder/), as made by openoni-extract or by the original installer, or the
   CD (its image or its files), read through its installer archive (vfs_vise.h). */
bool vfs_mount_default(void);
