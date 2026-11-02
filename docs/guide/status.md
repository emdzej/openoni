# Status

Early. What works today:

- **The game files** come out of the CD's installer without running it ([how](./game-data)).
- **The reverse engineering** has started: the executable is in Ghidra, and 1,055 of its functions carry
  Bungie's own names, matched from the Intel Mac build 1.2.1. Together they cover 45% of the code.
- **The skeleton** runs on gasm: it checks `Oni.exe` and draws a test screen, one bar per level file.
  Nothing of the game yet, but the loop, the file layer, input and audio are in place.

Not started yet: the engine itself. The order of work, from the file layer and the level data through the
renderer, front end, characters, scripts, combat and AI to sound and movies, is in the repository's port
plan. Each step ends with something that can be checked against the original.
