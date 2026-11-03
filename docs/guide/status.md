# Status

Under way, not yet playable from start to finish. On level 1 (the training), Konoko can be played
through the movement and combat lessons and the shooting range's targets, with the level's own scripts
driving the lessons.

## What works

- **The game files** come out of the CD's installer without running it ([how](./game-data)).
- **The level data**: all 15 levels load the way the original loads them, and every kind of asset the
  game draws or plays decodes, each checked against a reference reader:
  - textures
  - geometry and the environment
  - animations and characters
  - level objects and object animations
  - fonts and text
  - sounds
- **The renderer** draws the environment, the characters and the level's objects through a port of the
  original's fixed-function pipeline on gasm's GL.
- **Konoko**, with her movement, jumps, melee moves, throws and being hit, ported from the character
  engine. She can also pick up, carry, aim, fire and reload weapons, and pick up ammo and other
  power-ups.
- **The other characters**: they spawn from the level's objects and scripts, follow their patrol paths,
  and see, hear and react to the player. Their combat is still a stand-in.
- **The scripts**: the BSL engine runs the level's own scripts, with their trigger volumes, doors,
  consoles, animated objects and the first of the game's script commands.
- **The particle system**, the engine under every shot, spark and shard of glass. It covers the particle
  classes, emitters, events and actions, plus collisions with the walls and characters, and damage.
  Projectiles fly and stop on what they hit, and break the glass made of breakable materials. The
  particles are drawn as the original draws them: sprites with their orientations, fades and animated
  textures, and geometry such as the glass shards and the shell casings.
- **Sound** from the game's own sound groups.

## Not yet

- The particles' contrails, vectors, lens flares and decals (bullet holes), homing, and their sounds.
- The AI's guns, laser triggers and alarms. The power-ups' physics and glow, and the HUD.
- The front end (the menus), saving and loading, and the Bink movies.
- The other 13 levels as a playthrough.

The order of work is in the repository's port plan. Each step ends with something that can be checked
against the original.
