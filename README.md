# OpenOni

A from-scratch reimplementation of **Oni** (Bungie West, 2001) for [gasm](https://gasm.emdzej.pl), the
WebAssembly game runtime: one `openoni.wasm` for macOS, Linux, Windows and the browser. It is ported
function by function from the original Windows executable and needs the original CD; no game data is
included.

**State:** early. The game files can already be taken from the CD (below); the engine port has not
started yet. The plan: [docs/re/port-plan.md](docs/re/port-plan.md).

## Getting the game files from the CD

Oni's CD keeps everything inside its installer. `openoni-extract` reads the installer's archive directly
(the CD as an ISO image or as a folder with its files) and writes the installed game folder. It runs
without Windows or Wine and checks every file against the archive's CRC-32.

```sh
cmake -S . -B build && cmake --build build
./build/openoni-extract "Oni (USA).iso" game
```

## License

GPL-3.0 (see [LICENSE](LICENSE)). Oni is a trademark of its owners. This project is not affiliated with
them and distributes none of their data.
