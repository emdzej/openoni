# What OpenOni is

OpenOni is a reimplementation of **Oni**, Bungie West's third-person action game from 2001, in C11.
It is not an emulator or a wrapper. Every part of the game is ported function by function from the
original Windows executable (`Oni.exe`, version 1.0), in the original's own terms: its modules, its data
formats, its timing. Quirks and bugs are kept, because they are part of the game.

The game runs on [gasm](https://gasm.emdzej.pl), a WebAssembly game runtime: one `openoni.wasm` that gasm
runs natively on macOS, Linux and Windows, and in the browser.

OpenOni contains none of the original game's data. It reads everything (levels, textures, sounds, movies
and the tables inside the executable) from your own copy of the game: see
[getting the game files](./game-data).

<img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" height="28">
<img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" height="28">
