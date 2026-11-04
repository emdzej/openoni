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
  engine. She can also pick up, carry, aim, fire, reload, holster and draw weapons, and pick up ammo and other
  power-ups.
- **The other characters**: they spawn from the level's objects and scripts, follow their patrol paths,
  and see, hear and react to the player. In combat they pick a behaviour for the range to their target
  from the level's combat settings, hold, advance or back off, fight hand to hand, and with a gun lead
  their target, aim from the weapon's muzzle and fire within their class's shooting skill. Knocked
  down, they lie dazed for their class's time and then get up. They start
  with the weapons the level gives them, and without one they may go for a gun lying nearby, by their
  class's chance. They run to alarm consoles when their scripts or combat settings say so, and an
  alarm alerts the guards of its group. The bosses' behaviours are still missing, and the paths they take in a
  fight are stand-ins.
- **The scripts**: the BSL engine runs the level's own scripts, with their trigger volumes, doors,
  consoles, animated objects and a growing share of the game's script commands, among them those that
  set the AI's awareness, health and movement modes, and `lock_keys`, with which the training lets
  the player's keys through one lesson at a time.
- **The level chain**: a level's `win` loads the next level and its `lose` plays the same level again,
  as the original's game loop does, from the last save point the level's scripts made (`save_game`,
  `restore_game`: health, place, inventory and weapon put back). The mission timer counts down and runs its script at zero.
- **The HUD**: the health ring, and the left ring with the ballistic clips, the energy cells and the
  compass toward the objective's flag (`target_set`), its arrow up or down; the scripts show, hide and
  flash its elements.
- **Saved games**: each save point is kept in `persist.dat` (the original's own layout), with the levels
  reached; the main menu's Load Game lists them and starts the level from the one picked, in a later
  session too.
- **The pause screen** (F1): the objectives the level has set, the items and weapons the player has had,
  the diary's move pages with their key icons, and the controls' help, in the game's own dialog.
- **The main menu**: the game starts on Oni's own menu (New Game, Load Game, Options, Quit; Escape in a
  level brings it back over the level, with Resume), its dialogs built by the game's window manager from
  its data.
- **Options**: the volume, the difficulty, the subtitles (shown under the dialogue) and the inverted
  mouse, with the game's own sliders, popup menus and check boxes, kept in `persist.dat`. The graphics
  quality and the gamma are kept but not yet applied, and the resolution lists only the one screen mode
  the port has.
- **The text consoles**: the documents Konoko reads at the level's consoles, page by page, in the
  game's own window manager, its dialogs built from the game's data, over the level as the original
  draws them.
- **The splash screens** between the parts of a level ("Chapter 00 . Combat Training"), faded in
  and out as the original does.
- **The cutscenes**: the films (a character's input recorded tick by tick and played back), the
  characters carried by object animations (Konoko's motorbike ride into the airport), the camera's
  moves, the letterbox, and the AI held still while they play.
- **The turrets** (seven levels have them): switched on by the event lists and the scripts, they take
  the nearest character of their target teams, turn their heads and barrels within their limits, aim
  with the AI's targeting and fire their shooters' particles; their time over, they turn back to rest.
  Their humming sound is not there yet.
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
- The bosses' fighting, and the AI's pursuit (a stand-in). The power-ups' physics and glow, and the
  HUD's weapon and inventory displays.
- The Bink movies; the graphics quality and gamma settings applied.
- The other 13 levels as a playthrough.

The order of work is in the repository's port plan. Each step ends with something that can be checked
against the original.
