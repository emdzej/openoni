# Status

Under way, not yet playable from start to finish. Level 1's training plays through to its end, with
the level's own scripts driving the lessons: movement, combat, the shooting range, the rifle and the
two training drones. Then the cutscene after the training plays, its actors moved by the game's own
recorded films, and the warehouse mission begins.

It can be tried in the browser on the <a href="/play/" target="_self">play page</a>, with your own copy of the game.

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
  and see, hear and react to the player. In combat they pick a behaviour for the range to their target
  from the level's combat settings, hold, advance or back off, fight hand to hand, and with a gun lead
  their target, aim from the weapon's muzzle and fire within their class's shooting skill. They start
  with the weapons the level gives them. The bosses' behaviours, the gun pickup and the alarms are
  still missing, and the paths they take in a fight are stand-ins.
- **The scripts**: the BSL engine runs the level's own scripts, with their trigger volumes, doors,
  consoles, animated objects and a growing share of the game's script commands, among them those that
  set the AI's awareness, health and movement modes.
- **The level chain**: a level's `win` loads the next level and its `lose` plays the same level again,
  as the original's game loop does, from the last save point the level's scripts made (`save_game`,
  `restore_game`: health, place, inventory and weapon put back). The mission timer counts down and runs its script at zero.
- **The cutscenes**: the films (a character's input recorded tick by tick and played back), the
  characters carried by object animations (Konoko's motorbike ride into the airport), the camera's
  moves, the letterbox, and the AI held still while they play.
- **The laser triggers**: the beams sweep, blink and set off their alarms and scripts when the player
  crosses them.
- **The particle system**, the engine under every shot, spark and shard of glass. It covers the particle
  classes, emitters, events and actions, plus collisions with the walls and characters, and damage.
  Projectiles fly and stop on what they hit, and break the glass made of breakable materials. The
  particles are drawn as the original draws them: sprites with their orientations, fades and animated
  textures, and geometry such as the glass shards and the shell casings. The level's own emitters
  (smoke, sparks, steam) run and answer the scripts' `particle` command.
- **Sound** from the game's own sound groups.

## Not yet

- The particles' contrails, vectors, lens flares and decals (bullet holes), homing, and their sounds.
- The AI's alarms, the gun pickups and the bosses' fighting. The power-ups' physics and glow, and the
  HUD.
- The front end (the menus), saved games kept between sessions, and the Bink movies.
- The other 13 levels as a playthrough.

The order of work is in the repository's port plan. Each step ends with something that can be checked
against the original.
