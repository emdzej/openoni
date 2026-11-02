/* MindVision Installer VISE archives (docs/formats/vise.md): the Oni CD holds the whole game in one,
   ONISETUP.EXE (the setup program; its overlay is segment 1: the installer's support files and the item
   list) plus ONISETUP.002 (segment 2: the compressed files). Read through the file layer, so the CD can be
   a folder or an ISO image. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char path[512];          /* '/'-separated, relative to the install folder ("GameDataFolder/IGMD/...") */
    bool dir;
    uint32_t size, csize;    /* uncompressed, compressed */
    uint32_t segment;        /* 2 = <stem>.002 */
    uint32_t offset;         /* in the segment file */
    uint32_t crc;            /* CRC-32 of the uncompressed bytes */
    uint64_t mtime;          /* Windows FILETIME (100 ns since 1601), UTC */
} ViseEntry;

typedef struct ViseArchive ViseArchive;

/* Opens the archive whose setup program is `setup` (a file-layer path, e.g. "ONISETUP.EXE"); segment n is
   the file next to it named <stem>.00n. NULL (with a message in err) if it isn't a VISE archive this reader
   understands. */
ViseArchive *vise_open(const char *setup, char *err, size_t errcap);
void vise_close(ViseArchive *a);

int vise_count(const ViseArchive *a);
const ViseEntry *vise_entry(const ViseArchive *a, int i);
int vise_find(const ViseArchive *a, const char *path);   /* case-insensitive; -1 if missing */

/* A file's bytes, malloc'd, size and CRC checked. NULL (with a message in err) on failure. */
uint8_t *vise_extract(ViseArchive *a, int i, char *err, size_t errcap);
