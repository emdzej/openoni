# Getting the game files

**You can skip this page:** OpenOni plays straight from the CD. Give the [player](/play/) (or
`gasm-run openoni.wasm --asset cd="Oni (USA).iso"`) the CD's `.iso` image, or a folder with its files,
and the game reads its files out of the installer's archive itself (`src/vfs_vise.c`): each file is
inflated when the game opens it. Extracting first only makes the levels load faster (a few seconds less).

Oni's Windows CD has no game files on it in the open: the game is packed inside its installer, a
MindVision Installer VISE archive (`ONISETUP.EXE` + `ONISETUP.002`). You don't need to run that
installer, nor Windows or Wine. Two tools read the archive straight from the CD and write the installed
game folder, checking every file against the archive's CRC-32:

- **[visextract](https://github.com/emdzej/visextract)**, a standalone tool for Linux, macOS and Windows
  (download it from its releases):

  ```sh
  visextract extract "Oni (USA).iso" oni
  ```

- **openoni-extract**, the same reader built with OpenOni (`cmake -S . -B build && cmake --build build`):

  ```sh
  ./build/openoni-extract "Oni (USA).iso" oni
  ```

The CD can be an ISO image or a folder with its files. Either way you get:

```
oni/
  Oni.exe
  binkw32.dll
  readme.txt
  GameDataFolder/
    level0_Final.dat, level0_Final.raw, ... (15 levels)
    intro.bik, outro.bik
    IGMD/<level>/*.bsl
```

95 files, 641,025,194 bytes. An Oni folder installed by the original installer works just as well.

How the archive is laid out: [Installer VISE archive](../formats/vise).
