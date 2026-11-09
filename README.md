# OpenOni

A from-scratch reimplementation of **Oni** (Bungie West, 2001) for [gasm](https://gasm.emdzej.pl), the
WebAssembly game runtime: one `openoni.wasm` for macOS, Linux, Windows and the browser. It is ported
function by function from the original Windows executable and needs the original CD; no game data is
included.

**State:** all 14 levels play from the intro movie to the outro; play testing by hand is under way. See
the [status](https://openoni.emdzej.pl/guide/status), the [downloads](https://openoni.emdzej.pl/guide/install)
and the <a href="https://openoni.emdzej.pl/play/">browser player</a>.

## Getting the game files from the CD

Oni's CD keeps everything inside its installer. `openoni-extract` reads the installer's archive directly
(the CD as an ISO image or as a folder with its files) and writes the installed game folder. It runs
without Windows or Wine and checks every file against the archive's CRC-32.

```sh
cmake -S . -B build && cmake --build build
./build/openoni-extract "Oni (USA).iso" game
```

## Playing from the source

```sh
./play                                   # builds openoni.wasm and opens it in a gasm-run window
./play ~/Downloads/"Oni (USA).iso"       # the data: a CD image, the CD's files or an installed game
./play --param play=1 --param level=4    # gasm-run options and the game's parameters after it
```

Without an argument it plays `$OPENONI_DATA`, else `./game`, else `~/Downloads/Oni (USA).iso`. The first run
fetches the gasm SDK and runner into `.deps/`; `GASM_RUN=<gasm-run>` uses another runner.

## License

GPL-3.0 (see [LICENSE](LICENSE)). Oni is a trademark of its owners. This project is not affiliated with
them and distributes none of their data.
