# Status

Under way, and playable from the intro movie to the outro: all 14 levels play through to the end of
the game, checked by scripted routes after every change, with the game's front end, HUD, cutscenes,
films, movies, saves and options. The routes play with the game's own cheats on (invincible, mostly
omnipotent): the test bot's fighting is blind, and without the cheats it loses its fights, which is the
bot's skill and not a known fault of the port. Play by hand is being tested now; what isn't ported yet
is small and marked in the code. Level 1's training plays through to its end, with the level's own scripts driving the lessons:
movement, combat, the shooting range, the rifle and the two training drones. Then the cutscene after the
training plays, its actors moved by the game's own recorded films, and the warehouse mission plays
through: the consoles open the way, the thugs come at Konoko, she finds the LSI, frees the hostage who
opens the warehouse, beats the timer to the last console, and the outro plays before level 2 begins.

Level 2, the TCTF's Manufacturing Plant, plays through as well: the consoles' chain of locks, the
Chase after the hall's guards fall, the flyway, Griffin's call, the lasers and wall turrets, the
hidden lock and Shinatama's talk, the guard room and the brain lock, then the four rounds at the
brain's consoles and the outro, and level 3 begins. (The unattended run behind this check is made
invincible from the third save point on: its driver can't time the sweeping lasers that wake the
corridor's turret, which a player jumps.)

Level 3, the Syndicate's chemical plant, plays through too: Barabas beaten and his rocket escape, the
three floors' locks, the roof and the tower, the bomber's fall, the three passage consoles, the vats
and the end door, the last ambush and the outro, and level 4 begins. (Its unattended run uses the
game's `invincible` and `omnipotent` switches: the driver fights badly.)

Level 4, the airport, plays through as well: the motorbike ride in, the tarmac's strikers and the
mad bomber, the four consoles with their cutscenes, the final ambush, and level 6, the airport's
hangars, begins. (Its unattended run adds the `unstoppable` switch.)

Level 6, the airport's second part, plays through too: the two security locks and the tarmac, Muro's
escape, the rat door, the hangar's three locks and the roof, the lowroad to the far compound, the
lock that opens the office, the LSI on its crate, the ambush, the three strikers in the hangar and the
rappel, and level 8 begins. The original's scripts are kept as they are, their bug too: the trigger
past the hangar's door 20 turns off every console (`console_deactivate` without an id), so a player who
goes that way can't open the office; the run takes the upper way.

Level 8, the TCTF's regional headquarters, plays through as well: the van through the lobby's wall and
the blast doors, the attack and the floors' locks, Shinatama's kidnap, the atrium's broken stairs
(crossed with a running jump, then a sprinting one), the LSI from the civilian who has it, the leap onto
the elevator and its fall, the lock to the stairs, the roof, the helicopter and Barabas, and level 9
begins.

Level 9, the power plant, plays through too: the upper floor's locks and the first lift with Muro's
cutscene, the middle floor's locks and the second lift, the consoles that discharge the two power
conduits and the runs through them before they recharge, the drop to the lowest floor, the last
chambers' fight, and level 10 begins.

Level 10, the power plant's second part, plays through as well: the guards who come through the blast
doors before they lock (the AI opens the doors on its way, manual ones too), Shinatama's cutscene and the
run from the bomb, the two pipes run while their discharge lasts, the vat room's walkways and the zaps,
the control room and the elevator, the last pipe, the fan and the outro, and level 11 begins.

Level 11, the TCTF's secure building, plays through too: the floors' consoles and their locks, the
basement and the ramp, the ninja at the window, whose blast breaks the glass, the jump from the sill to
the roof, the ninjas there, the outro, and level 12 begins.

Level 12, the TCTF's roof, plays through as well: the gaps and the belt, the glass elevator and its
door, the decks and the stairs, the flip (a crouch in the air tucks her up) from the vent onto the
catwalk with the LSI, the zip line, the flips up the ledges, the guards by the elevator who keep its door
locked, the boss in the elevator's room, the outro, and level 13 begins.

Level 13, the dream lab, plays through too. It has the ghosts, Muro's wall, the mini strikers who fall
through the floor, the tankers (the first one's combat script brings the second), the red ninjas and
Griffin's squad. Then come the acid, the laser corridor with its ninja, Shinatama floating, and the evil
Konoko. Her death plays the outro, and level 14 begins.

Level 14, the neural lab, plays through as well: the pods and their consoles, Kerr's cutscene and the
hypos, the three lab consoles that open the probe room, the guards who come after the scan, the security
wing's ambush in two waves, the drop to the vats, the crane consoles, the guards lost to the acid, the
outro on the crane, and level 18 begins.

Level 18, the compound, plays through too: the consoles' rounds against the zombie pit, the base's
cutscene, Griffin, and level 19 begins. Level 19, the final level, plays through to the end: the
parking lot and the truck, the LSI, the dish that lifts Konoko to the arena, Griffin's squad and Muro,
whose death ends the game (the game won is kept in persist.dat), and the outro movie plays.

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
  power-ups. Hypos (Tab) heal her, past her maximum too, invisibility fades her out of sight and out
  of the enemies' view, and the force shield, glowing round her where she is hit, takes the shots until it
  is spent (not the blows). Her condition is heard: the heartbeat of low health, the rush of health over
  her maximum and the hum of invisibility, each louder as it grows.
- **The other characters**: they spawn from the level's objects and scripts, follow their patrol paths,
  and see, hear and react to the player: they hear the shots, the hits, the throws, the slides and
  the doors within each sound's reach, where it carries through the rooms (only gunfire through a
  shut door). In combat they pick a behaviour for the range to their target
  from the level's combat settings, hold, advance or back off, fight hand to hand (their melee profile's
  techniques weighed as the original does: whether the target could block, which side they stand on,
  how the moves' transitions and timing reach it, the attack predicted; stepping into reach as
  the original's positions do, and stopped by the bodies they run into), and with a gun lead
  their target, aim from the weapon's muzzle and fire within their class's shooting skill. Knocked
  down, they lie dazed for their class's time and then get up. They start
  with the weapons and the inventory the level gives them. When they die their gun flies from their
  hand and what they were to drop (clips, cells, hypos, a shield, invisibility, the LSI) tumbles out;
  a dropped weapon or power-up falls, lands with the sound of the floor's material, slides to rest
  and, a weapon, fades away a minute and a half later; both lie on their glows. Their paths through
  a room go round the other characters, the crates and the open doors' panels in it, which the
  original marks in the room's grid every 20 ticks; one spawned onto another or a crate steps aside,
  and a patrol starts from its nearest waypoint where the path asks for it. The characters
  stop at the level's crates and boxes, a falling weapon stops on a character, and the guards kick
  the weapons on the ground aside (but not the one they are going for). Without a gun they may go for one lying
  nearby, by their class's chance. They run to alarm consoles when their scripts or combat settings say so, and an
  alarm alerts the guards of its group. A guard that hears or glimpses Konoko pursues her: it goes
  to where she was, glances or looks around there, and gives up as the trail goes cold, by the
  character's own pursuit settings. Barabbas fights as his own behaviours say: he shoots, closes in firing his gun's second mode, fights
  hand to hand, and backs off to heal when hurt. Mutant Muro fights behind his shield, which drops when he casts his thunderbolt. The super
  ninja throws his fireballs, turns invisible, teleports behind his target or away when hit hard, and
  comes back when he falls off the level. The fighters advance on their targets by a path or a straight line, back away from them, and sidestep out of a gun's line of fire (the firing spreads of the pistol, the SMG and the plasma rifle) as the original's maneuvers do. Startled guards turn toward what startled them while the startle plays; the characters cry out as their classes say (alerted, startled, pursuing, cowering, the super moves' yells), and a fighter that fells an enemy hand to hand may taunt over the body.
- **Neutral characters**: the civilians call Konoko over or answer the action key, talk with her line
  by line, with their animations and speech, hand over weapons and power-ups, and run their scripts
  when the talk is over; firing, a blow, walking off or a fight nearby breaks it off.
- **The scripts**: the BSL engine runs the level's own scripts, with their trigger volumes, doors,
  consoles, animated objects and a growing share of the game's script commands, among them those that
  set the AI's awareness, health and movement modes, and `lock_keys`, with which the training lets
  the player's keys through one lesson at a time.
- **The level chain**: a level's `win` loads the next level and its `lose` plays the same level again,
  as the original's game loop does, from the last save point the level's scripts made (`save_game`,
  `restore_game`: health, place, inventory and weapon put back). The mission timer counts down and runs its script at zero.
- **The HUD**: the health ring, its loss shown fading down to the new health and the healing to come
  ahead of it, the shield's arc and the arcs on the side each hit came from; the left ring with the
  ballistic clips, the energy cells and the compass toward the objective's flag (`target_set`), its
  arrow up or down, and in its middle the weapon in hand with its ammo's icon and ring; the right
  ring's invisibility and hypos and the LSI's icon; the scripts show, hide and flash its elements.
- **Saved games**: each save point is kept in `persist.dat` (the original's own layout), with the levels
  reached; the main menu's Load Game lists them and starts the level from the one picked, in a later
  session too.
- **The pause screen** (F1): the objectives the level has set, the items and weapons the player has had,
  the diary's move pages with their key icons, and the controls' help, in the game's own dialog.
- **The main menu**: the game starts on Oni's own menu (New Game, Load Game, Options, Quit; Escape in a
  level brings it back over the level, with Resume), its dialogs built by the game's window manager from
  its data.
- **The controls** are the game's own: `key_config.txt` (written with Oni's defaults when there is
  none, or the one in the game folder) binds the keys and the mouse to the actions, with the second key
  set (P L ; ' and the rest) and any rebinding a player writes in it.
- **Options**: the volume, the difficulty, the subtitles (shown under the dialogue) and the inverted
  mouse, with the game's own sliders, popup menus and check boxes, kept in `persist.dat`. The gamma
  brightens or darkens the picture as the original's display ramp did; the graphics quality sets the
  characters' levels of detail (a triangle budget, nearer characters more detailed), the textures'
  sharpness and filtering, the shadows and which particle effects play. The resolution lists only the
  one screen mode the port has.
- **The difficulty**: the characters' health, the enemies' aim (theirs and the turrets'), and on hard
  the stronger classes the levels' characters are upgraded to; their costumes picked at random from
  their class's variants.
- **The cheat codes**: typed on the pause screen once the game is won, with the exe's own codes and
  messages; most of them work (invincibility, fat loot, glass furniture, last man standing, gatling
  guns, regeneration, the weapons locker, slow motion and the rest).
- **The text consoles**: the documents Konoko reads at the level's consoles, page by page, in the
  game's own window manager, its dialogs built from the game's data, over the level as the original
  draws them.
- **The splash screens**: each level's chapter title as it loads, the ones between the parts of a level
  ("Chapter 00 . Combat Training"), and the screens of a level won and a level lost, with their music,
  faded in and out as the original does.
- **The main menu's music**, faded in over the level's sounds, which are paused while the menu is up.
- **The cutscenes**: the films (a character's input recorded tick by tick and played back), the
  characters carried by object animations (Konoko's motorbike ride into the airport), the camera's
  moves, the letterbox, and the AI held still while they play.
- **The turrets** (seven levels have them): switched on by the event lists and the scripts, they take
  the nearest character of their target teams, turn their heads and barrels within their limits, aim
  with the AI's targeting and fire their shooters' particles; their time over, they turn back to rest.
  They hum while active.
- **The laser triggers**: the beams sweep, blink, hum while active and set off their alarms and scripts
  when the player crosses them.
- **The particle system**, the engine under every shot, spark and shard of glass. It covers the particle
  classes, emitters, events and actions, plus collisions with the walls and characters, and damage.
  Projectiles fly and stop on what they hit, and break the glass made of breakable materials. The
  particles are drawn as the original draws them: sprites with their orientations, fades and animated
  textures, geometry such as the glass shards and the shell casings, vectors (lines), contrails (the
  ribbons of the kicks' trails, the sparks and the lightning) and lens flares, which fade in and out
  as walls come between them and the camera. The level's own emitters (smoke, sparks, steam) run and
  answer the scripts' `particle` command. The characters' animations make theirs too, hanging from the
  bones: the dust and trails of the moves, the ninja's fireballs and
  teleports. Homing shots seek their targets as the original's do: the Scram Cannon's and the Screaming
  Cannon's rockets, the sniper's rounds and the Van de Graaff's bolts, aimed at each foe in reach, find
  the nearest one in front of them and turn towards it at their own rate; the power-ups' and the
  overpower's glows are drawn to what they hang from.
- **The impact effects**: what a hit makes, picked by the game's own table from the kind of blow or
  shot and the material it meets (stone, metal, glass, flesh, the shields). The bullets' sparks,
  dust and sounds on the walls and on the characters come from it, and the blows' flashes and sounds,
  by the attack and the body part that lands it, and the footsteps' dust and sounds by the floor's
  material.
- **Decals**: the bullet holes, scorch marks and the characters' shadows, laid on the walls, cut to their shapes and folded
  round their corners; they fade, and the oldest give way to new ones as the original's do.
- **Animated textures**: the frames of an animated texture (the helicopter's rotor, the shields) follow
  the game's clock, as the draw engine picks them.
- **The lighting** of the characters, weapons and objects: the game's ambient and its directional
  lights, one or two by the graphics quality, and the reflection maps on the shiny surfaces. The dead
  stay where they fell, as corpses, and so do the bodies each level sets into its scenes.
- **What is seen**: rays from the camera through the level find the walls, floors and things in view,
  and only those are drawn, as the original's are, with its pop-in. The sky shows when a ray gets
  out: the level's skybox round the camera, afternoon, sunset or night.
- **The fog**: the far parts of a level fade into the level's fog colour, as far out as its scripts
  set (a grey from about 80 units by default), and particles fade into it.
- **Sound** from the game's own sound groups: the music, the dialog and speech, the impacts, and the
  particles' sounds (the gunshots, the rockets' roar, the level's hissing steam and sparks), placed
  where they play and following the particles that carry them, and the level's own sound places: the
  hums and room tones around the machines and lights, and the rooms' ambiences fading in as she
  enters and out as she leaves; the energy bolts whoosh past her. The characters' moves sound as each
  variant's records say: the punches' and kicks' whooshes by their damage, the reloads, holsters,
  jumps, rolls and slides, Konoko's yawns and the zombie's steps. They are heard from Konoko's ears (from the camera when it isn't
  following her), muffled by the walls between, and not at all from three times their reach.
- **The movies**: the intro before the main menu and the outro at the game's end, decoded from the
  game's own Bink files the way RAD's player shipped with the game does, with their sound.

## Not in this version of the game

The developer console and the developer keys (the console variables typed in, the debug keys, the
recording and camera tools) are in the executable, but v1.0 gives no way to switch them on: the cheat
that would grant developer access has no code in its table. The port keeps them out, as the game does.

The order of work is in the repository's port plan. Each step ends with something that can be checked
against the original.
