# The AI: knowledge, alert, combat and melee

How an AI character learns of others and what it does with it. The ports are `src/knowledge.c` (with
its test `tests/knowledge_test.c`), `src/combat.c` and `src/melee.c`. The executor, the patrol and the AI's character fields are in
[character.md](character.md) ("The AI's executor") and [objc.md](../formats/objc.md) (PATR). Offsets
are of the character record (C+) unless marked otherwise. Confirmed means read in the Windows exe;
inferred means from the data or the context.

## The parts

| What | Where | Status |
|---|---|---|
| The update, once a tick | `AI2rKnowledge_Update` 0x499e80 (the Mac match had it as `AI2iKnowledge_UpdateProjectileAlertness`: the Windows function holds both) | confirmed |
| The sight check | `AI2iKnowledge_CheckVisible` 0x49adf0 | confirmed |
| A post, queued | `AI2iKnowledge_AddPending` 0x49b300: at most 64, 0x24 bytes each at 0x63a2a0, count 0x63b340 | confirmed |
| A post, applied | `AI2iKnowledge_PostContact` 0x49b3f0 | confirmed |
| The order of contacts | 0x49ba10 | confirmed |
| Removing a contact | 0x49b8e0 | confirmed |
| Hurt | `AI2rKnowledge_HurtBy` 0x49a7f0, from 0x48b480 (TakeDamage, for AI characters) | confirmed |
| The calm states' handler | `AI2rAlert_NotifyKnowledge` 0x48ecc0 | confirmed |
| Raising the alert | `AI2rAlert_UpgradeStatus` 0x48eee0 | confirmed |
| The pursuit's handler | 0x4b1750 | confirmed |
| Teams | `AI2rTeam_FriendOrFoe` 0x48b2c0; enemy test 0x48b1d0 | confirmed |
| Neutrals | `AI2rNeutral_*` 0x4ac870..0x4adb00, speech 0x516940, 0x516a60 | confirmed |

## Contacts

The contacts are a pool of 128 entries of 0x58 bytes (0x5eb850, count 0x63ada0, free list 0x5eb854).
Each AI keeps its own as a doubly linked list, most important first (C+0xf7c, count C+0xf78). Each
state installs its handler at C+0xf80: idle, patrol, neutral and alarm use `AI2rAlert_NotifyKnowledge`,
or 0x4adeb0 for non-combatants (C+0x1f0 bit 4); combat uses 0x490510; pursuit uses 0x4b1750.

| Offset | What |
|---|---|
| 0x00 | Owner |
| 0x04 | The character it is about (0 for a sound with no one) |
| 0x08 | Last told to the state |
| 0x0c | Last posted |
| 0x10 | Place, 3 floats |
| 0x1c | Event type (below) |
| 0x20 | Value: the damage for a hurt; the other character for a sound |
| 0x24 | Strength: 1 forgotten, 2 a hint, 3 heard or hit, 4 seen |
| 0x28 | Strength as last told |
| 0x2c | Hostility: 2 the owner takes the other for an enemy, 1 the other way, 3 forced |
| 0x34 | First posted |
| 0x38 | When the strength next falls |
| 0x3c | Damage taken from it |
| 0x40 | Unimportant sounds in a row |
| 0x44 | Highest strength |
| 0x48 | The last post raised the highest (byte) |
| 0x49, 0x4a | Hurt by it; hurt by it in melee (bytes) |
| 0x4b | Made hostile by its hits (byte), until 0x4c |
| 0x50, 0x54 | Previous, next |

Event types: 0 an unimportant sound, 1 an interesting one, 2 danger, 3 melee, 4 gunfire (2 to 4 are
combat sounds: `AI2rKnowledge_IsCombatSound` 0x49ad70), 5 seen peripherally, 6 seen centrally, 7 hurt,
8 hurt in melee, 9 the alarm.

### A post

Posts queue (`AddPending`) and are applied at the start of the next update. A second post about the
same pair replaces the first unless it is weaker, and types 7 and up are never replaced.

Applying one (`PostContact`) does the following. It needs an AI owner with knowledge on (C+0x1f0
bit 1, which `AI2rInitializeCharacter` sets):

1. **Find or make the entry.** A new entry's hostility is 2 if the owner takes the other for an enemy
   (0x48b1d0, not counting past hits), 1 if the other way round. Its highest strength starts at 1 and
   its timer at −1.
2. **Count repeats.** An unimportant sound repeated within 240 ticks counts up, and the sixth in a row
   counts as interesting (0x545780 is 240, 0x54577c is 6).
3. **Record the event.** The time, place, type and value are stored. Strength only rises: when it
   does, the time it lasts is restarted from the class (ONCC +0x410 for strength 4, +0x414 for 3,
   +0x418 for 2).
4. **Hurts.** A hurt adds the damage. With no hostility yet, the entry becomes hostile (+0x4b) for
   900 ticks and its hostility becomes 2. The exceptions: damage still under half the owner's hit
   points (C+0x3c) while a hostile contact is within C+0x1014 of the owner. The port has no C+0x1014
   yet, so it always makes the entry hostile. With hostility already set, the entry is hostile for
   good.
5. **Re-sort.** The entry moves up past every entry that doesn't rank above it (0x49ba10 ≤ 0).
6. **Notify.** Unless the post is silent, the state's handler runs.

### The order

0x49ba10 ranks two contacts by these rules, the first that differs deciding:

1. Strength over 2 against 2 or less.
2. Higher hostility.
3. The owner taking the character for an enemy, counting past hits.
4. Higher strength.
5. Hurt, then hurt in melee.
6. Higher highest strength.

A debug flag (0x5ec0c3) first orders by the characters' flag 0x10000000; the port leaves that out.

### The update

`AI2rKnowledge_Update` runs once a tick, in this order:

1. **The pending posts** are applied.
2. **The projectiles** heading near an AI are posted as type 3 (not ported).
3. **Every 5 ticks**, the sounds' and projectiles' places are updated (not ported).
4. **Every 60 ticks**, each contact over strength 1 is checked:
   - If its character is dead, the contact drops to 0.
   - Else, if its hostile timer has run out, it stops being hostile and hostility 2 becomes 1.
   - Else, once the strength's time is up (unless the owner has C+0x1f0 bit 0x400), the strength
     drops by one. A cloaked character's strength 4 drops too.

   Changed contacts move down past every entry that doesn't rank below them (0x49ba10 ≥ 0), and the
   handler is told with the "degraded" flag.
5. **Sight.** Every alive AI that isn't passive (bit 2) or blind (bit 0x800) takes a look on its own
   schedule. A look checks every character when the AI has an active record, or just the player when
   it hasn't. The schedule depends on the state:
   - Every 4 ticks for 180 ticks after a startle (C+0xfa4), except in combat.
   - Every 15 ticks in pursuit (state 6), or while C+0x1598 is set.
   - Every 59 ticks otherwise.

   An AI looks when (its index × 13) mod the period equals the time mod the period. A glance
   (C+0xf74, counting down) makes it look as soon as the glance ends.
6. **Re-telling.** The most important contact, if told within the last 60 ticks, is told again. The
   loop is meant to go over every character, but its character variable never advances (it keeps the
   last one from the sight loop). So it tells that one character, as many times as there are
   characters. The port keeps this.

### Sight

`AI2iKnowledge_CheckVisible`, for each other character:

- **Which characters.** It skips the dead and the cloaked (C+0x1ba set with C+0x1bc ≥ 90). It skips
  the player when the global ignore-player cheat (0x5ec0c2) is on or the AI has C+0x1f0 bit 0x2000.
  It skips AIs unless one takes the other for an enemy, counting past hits.
- **The eye and the view.** The eye is the head (0x4e3460: the head part's matrix A+0x247c, or the
  location 15 up). The view direction is the aim (A+0x21a4) while aim tracking is on (A+0x2195), else
  the facing. The port uses the facing.
- **The point looked at** is the position with y raised by the pelvis height (C+0x18c). With d the
  vector from the eye to it:
  - It must be within the central distance (ONCC +0x3f4).
  - Beyond 20 units, it must be within the vertical cosine (+0x3fc), measured against up.
  - c is the cosine to the view, the dot product over the full 3D distance. Up close, a character
    below the eye therefore falls into the peripheral cone.
- **Strength.**
  - A contact already at strength 4 among the top contacts stays seen at strength 4 while c ≥ the
    peripheral minimum (+0x40c) and it is within the central distance.
  - Otherwise, the central range is the full central distance at c ≥ the central maximum (+0x400),
    falls off linearly to 0 at the central minimum (+0x404), and is 0 below that.
  - The peripheral range is the full peripheral distance (+0x3f8) at c ≥ the peripheral maximum
    (+0x408), and falls off linearly to 0 at the peripheral minimum (+0x40c).
  - Within the central range it is strength 4, type 6. Within the peripheral range it is strength 2,
    type 5.
- **The sight lines.** A line runs from the eye to the other's pelvis (0x4e34f0), then its head, then
  its bottom (0x4e3590: the pelvis's x and z, the body's bottom C+0x190 + 1). Each line is cast with
  `AKrCollision_Point` 0x4412d0 through 0x441c80, which wants the first hit only and lets through
  quads with flags 0x42080. The first clear line's point is what gets posted.

The thugs of level 1 (`thug_wh_1`) have:

| Field | Value |
|---|---|
| Central distance | 475 |
| Peripheral distance | 150 |
| Vertical cosine | 0.766 (50°) |
| Central maximum / minimum | 0.819 (35°) / 0.574 (55°) |
| Peripheral maximum / minimum | 0.174 (80°) / −0.423 (115°) |
| Time at strength 4, 3, 2 | 180, 900, 3600 ticks |

### Hurt

TakeDamage (0x4e65f0) calls 0x48b480 for an AI character it hurts but doesn't kill. Unless the AI is
passive, 0x48b480 clears the vocalization delay (C+0xf8c). It then resolves the damage owner and calls
`HurtBy`. The owner is a character for kinds 0x1000000 (its weapon) and 0x2000000 (melee:
`WPrOwner_MakeMeleeDamageFromCharacter` 0x51ce00); 0x3000000 is another kind.

`HurtBy` posts on the victim, about the attacker, at the attacker's position, with the damage. It
posts only when the two aren't friends. Melee posts type 8 at strength 4; other damage posts type 7
at strength 3. Non-melee damage also makes a sound of 120 (`AI2rKnowledge_Sound`, not ported).

### Teams

`AI2rTeam_FriendOrFoe` 0x48b2c0 (a, b) returns 0 for friends, 1 for neutral and 2 for enemies. If the
global "everyone hostile" cheat (0x5eb848) is on, it always returns 2. Otherwise these rules apply:

| a | Result |
|---|---|
| Same team, or either is 6 (Switzerland) | 0 |
| 0 Konoko | 0 with 1, 2 with 2, else 1 |
| 1 TCTF | 0 with 0, 2 with 2 and 5, else 1 |
| 4 security | 2 with 2 and 5, else 1 |
| 2 Syndicate | 0 with 7, else 2 |
| 5 rogue Konoko | 2 with 2 and 1, else 1 |
| 7 Syndicate accessory | 0 with 2, else 1 |
| 3 neutral, others | 1 |

0x48b1d0 (a, b, known) tells whether a takes b for an enemy. It is false when either is missing, when
they are the same character, and when a is passive. Teams at 2 make it true and teams at 0 make it
false. For neutral teams:

- If a is an AI, it is false when a is non-aggressive (bit 4) or when known isn't asked. Otherwise it
  is true if a's contact of b is hostile (+0x4b) or has hostility over 1.
- With known, it is also true if b is an AI whose contact of a is like that.

## The alert

`AI2rAlert_NotifyKnowledge` (character, contact, degraded) ignores a degraded notice. It also ignores
everything while a patrol has waypoint 0x16 (ignore the player) in force: the patrol state's byte at
+0x540. And it ignores a contact that isn't first while the first is at strength 4. Otherwise it
works by the contact's type t:

- **Not a combat sound, with no hostility.** The AI glances at the contact for 0x52a64c[t] ticks if
  its strength rose (0x4a8150, C+0xf74). The table is 30, 60, 90, 120, 120, 180, 180, 600, 600, 300.
- **Otherwise, for the first contact over strength 1.** The new level is 0x52a624[t]: 0, 1, 2, 2, 3,
  1, 4, 4, 4, 3. If the alert (C+0xf94) is not above it, it is raised to that level with
  `UpgradeStatus`. When the alert went up and the AI hasn't vocalized (C+0x1f0 bit 0x8000):
  - A startle or the combat level vocalizes now and sets the bit.
  - Otherwise, if no vocalization is waiting, one is set for 60 + random(30) ticks.

  With any alert, a character that isn't in combat, pursuit or alarm goes to pursuit (state 6, the
  contact at C+0x8c0). A neutral (state 8) first checks the contact is dangerous.

`AI2rAlert_UpgradeStatus` (character, level, contact) does the following:

1. **Set the level.** It does nothing more if the level is unchanged. Otherwise it sets the level,
   tells the movement, and records C+0xf90 (the contact's time) on first going above low (1).
2. **Startle.** Above medium (level ≥ 3), the AI startles unless any of these holds:
   - the class can't (ONCC+0x120 bit 0);
   - the contact is forced hostile;
   - the contact was already at strength 4 without a rise;
   - it is over 60 ticks since C+0xf90.

   The animation type is by the contact's side, from the facing + π/4:
   - 0xc3 in the first quarter (front);
   - 0xc0 in the second (left);
   - 0xc2 in the third (behind);
   - 0xc1 in the fourth (right).

   The startle first puts the AI into fight mode (0x4eb0b0) and clears C+4 bit 0x2000 (0x4ecf80). Then
   it plays the animation from the from-state with 8 frames of interpolation (0x4eb340) and keeps its
   length (C+0xfa8) and the facing (C+0xfac). It also keeps the turn left after the animation's own
   rotation (C+0xfb0, not ported) and the startle time (C+0xfa4).
3. **Combat.** At level 4, for a contact with a character, the AI leaves its state for combat (state
   5): the character at C+0x8c0, the contact at C+0x8c8.

The pursuit's handler (0x4b1750) runs the calm handler first, then the pursuit's own reaction: see
"The pursuit".

## The AI's character fields

| Offset | What |
|---|---|
| C+0x1e8 | 2 for an AI, 0 for the player |
| C+0x1f0 | AI flags: 1 knowledge on, 2 passive, 4 non-aggressive (CHAR flag 0x08), 8 the combat script due, 0x10 the alarm script due, 0x20 the end script due, 0x40 the out-of-ammo script due, 0x80 the no-path script due, 0x400 never forgets (CHAR flag 0x100), 0x800 blind, 0x1000 deaf, 0x2000 ignores the player, 0x8000 vocalized |
| C+0x1f4 | The job: the state it goes back to (`ai2_setjobstate`, `AI2iScript_MakeCurrentStateJob` 0x4b4b00, copies C+0x1f8 here and the state's block, C+0xf70, to C+0x200) |
| C+0x1f8 | State: 1 idle, 3 patrol, 5 combat, 6 pursuit, 7 alarm, 8 neutral, 9 panic |
| C+0xf70 | The state's block: C+0x200 for the job, else C+0x8b8 |
| C+0xf74 | Glance ticks |
| C+0xf8c | Vocalization delay |
| C+0xf90 | When the alert went above low |
| C+0xf94 | Alert: 0 lull, 1 low, 2 medium, 3 high, 4 combat |
| C+0xfa4, 0xfa8, 0xfac, 0xfb0 | The startle: time, length, facing, turn |

`AI2rInitializeCharacter` 0x48a6b0 sets the flags to 1 (3 when the AI is off globally), with bit 4
for CHAR flag 0x08 and bit 0x400 for CHAR flag 0x100. It reads the alert levels from the CHAR object:
initial, minimal, job start and investigating (C+0xf94, C+0xf98, C+0xf9c, C+0xfa0). In the data those
are 1, 1, 1, 2 at +0x1d4..+0x1e0 for the thugs and 0, 0, 1, 2 for Konoko; the reader 0x4bf290 puts them
at D+0x204..D+0x210 (confirmed: docs/formats/objc.md, CHAR).

### The character's scripts

`AI2rUpdateAI` 0x489d40 starts each live AI character's tick with its scripts. Each flag due is
cleared and, if the character has the script and hasn't run it yet, the script is deferred (0x5100b0)
with the character's name and its bit is set in C+0x44. The order is:

| Flag | Script | C+0x44 bit | Who sets the flag |
|---|---|---|---|
| 0x10 | alarm (C+0xa8) | 8 | the event list's alarm, 0x49ac30 |
| 8 | combat (C+0x68) | 2 | `AI2rCombat_Enter` 0x48f290, at its end |
| 0x40 | out of ammo (C+0x108) | 0x40 | the trigger, 0x4e9040: an AI's empty weapon with nothing to reload |
| 0x80 | no path (C+0x128) | 0x80 | none found |
| 0x20 | the Neutral's end script (C+0x1018) | none | the conversation's end |

So each of these scripts runs once in a character's life. Level 13 depends on this: the first
tanker's combat script spawns the second tanker.

## Leaving combat

The combat state (`AI2rCombat_Enter` 0x48f290) points its class block (state +0x28) at ONCC+0x138 and
sets C+0xf80, the knowledge handler, to 0x490510. Its block (C+0x8b8; +0x08 the target, +0x0c lost,
+0x10 the target's contact, +0x14 its strength last told):

- **The handler** (0x490510), for a degraded contact of the target: strength 0 loses the target
  (0x4909a0); above 3 clears the pursue delay (+0x80); a drop from 4 sets +0x80 to ONCC+0x294 (180 for
  the warehouse's classes) and, if that is 0, pursues at once (state 6, the contact). For a new post of
  the target's contact it keeps the strength.
- **Losing the target** (0x4909a0): +0x0c set; +0x7c (the wait) from ONCC+0x290 (240); +0x78 from
  ONCC+0x28c (90) only with a weapon to reload (0x51a990); a target no longer in use (C+0x04 bit 0x8000)
  is cleared.
- **The tick** (`AI2rCombat_Update` 0x48f3e0) starts with it. A target gone or dead (C+0x04 bit 1) is
  lost. Otherwise the contact's place follows the target's pelvis, and any pursue delay left pursues at
  once (counting it down by one; no contact: back to the job). Once lost, +0x78 counts down, then +0x7c,
  but only while the target's body (C+0x1644) is gone. A taunt over the body is possible on the way
  (+0x75, +0x76, the class's chance at ONCC+0x298, type 0x5e); the port has nothing that sets +0x75.
  At 0, 0x48af80.
- **Back to the job** (0x48af80, called with both flags): the state is left (`AI2rExitState` 0x48ae80);
  with the alert at or above the job start level (C+0xf9c) the job returns (C+0x1f4, its block at
  C+0x200), else idle.

A body (C+0x1644) stays ONCC+0xc8a ticks; `ONrGameState_UpdateCharacters` counts it down and deletes an
AI's character at 0 (`ONrGameState_DeleteCharacter` 0x4dc480). Of the CD's classes only `mad_bomber` has
a time (190); every other has 0, and its bodies stay.

`ai2_passive` (`AI2iScript_Passive` 0x4b4be0 → `AI2rPassive` 0x48b160) sets or clears bit 2. Setting
it leaves the state and halts the path; clearing it re-enters the state.

## Combat

What the combat state does with its target once it has one (`src/combat.c`). The state's block is at
C+0x8b8 (B+ below); the behaviours get B+8.

### The settings

`AI2rInitializeCharacter` 0x48a6b0 looks up the CHAR object's combat id (+0x1ae) among the level's
Combat objects (`ONrLevel_Combat_ID_To_Combat` 0x506b30; OBJcCombat, version 39) and copies:

| Object | Character | What | Default |
|---|---|---|---|
| +0x40 (u16) | C+0x1028 | the id | 0 |
| +0x44 (5 u32) | C+0x1034 | the behaviour of each range, 1 to 5 | 2 (Hold And Fire) |
| +0x58 | C+0x102c | the medium range | 200 |
| +0x5c | C+0x104c | when an unarmed one fights (below) | 3 |
| +0x60 | C+0x1050 | what else it does unarmed: 0 melee, 1 retreat, 2 the alarm | 0 |
| +0x64 | C+0x1030 | the short range | 40 |
| +0x68.. | C+0x1054.., C+0xfec.. | the pursuit's and the alarm's settings | 200, 600, ... |

Level 1 has 19 of them (`Stand_and_Fire` id 0: Hold And Fire everywhere, medium 120, short 40;
`Assault` id 4: Firing Charge at long and short range; ...).

### The range and the behaviour

`AI2rCombat_GetRange` 0x4901f0, from the targeting's point (none: long): under the short range 3;
under the medium 2, or 4 coming from 3 or more; else 1, or 5 coming from 2 or more. A change switches the
behaviour (0x490290): the old one gets message 2, the new one (C+0x1034 by the range) message 0. The
behaviours are a table of 14 (names at 0x53eb98, handlers at 0x52a6c4): 0 None, 1 Stare (0x490f20), 2
Hold And Fire (0x490f90), 3 Firing Charge (0x4911e0), 4 Melee (0x4912d0), 5 to 13 the bosses' and Run For
Alarm. A handler takes (message, &handled, args); unhandled goes to `AI2rBehavior_Default` 0x493120.

| Message | Hold And Fire | Firing Charge | Melee | Stare | Default |
|---|---|---|---|---|---|
| 1, the weights | hold 1 | advance 1 (20..80), hold 0.2 | melee 1 | hold 1 | - |
| 4 | may melee | may melee | may melee unless meleeing | may melee | may melee |
| 5, the flags | all on | all on | targeting only | targeting, the environment's test | targeting only |

The flags (B+0x5c..0x5f) are the line of fire's environment test, its friends test, the aim and the fire;
B+0x8c turns the targeting on. Hold And Fire and Firing Charge without a usable gun (none, or empty with
nothing to reload: 0x4edc00) go to 0x491060: it fights hand to hand (melee 1) when C+0x104c is 5, or 1
and hit in the last 240 ticks, or 3 to 5 and the range as close as the table at 0x52a688; else it wants a
gun (weight 1) and, by C+0x1050, melee 0.8, a retreat 0.8 (100..1000) or the alarm.

Message 7 (from the tick, when the target is within the weapon's closest distance, ONWC+0xd0+0x5c) scales
hold and advance down and fights, steps in or backs off; message 9 keeps the aim while firing or within
the maneuver's distance (0x52a6a0: 80, 120, 140, 80, 60, 140, 160; else 500).

The other messages and their arguments: 0 entered (the behaviour left), 2 left (the one entered), 6 lying,
8 the trigger (from 0x490a50 each time it sets or lets go: on, the alternate's), 10 a hit
(`AI2rCombat_NotifyKnowledge` 0x490510, for a hit contact: the damage, the contact; the default keeps the
hit's time at B+0x84 when the target hit it, and at B+0x88 hand to hand), 15 a change of range (the new
one: a handler returning true keeps the range and the behaviour it has).

### The bosses

The Combat profiles in level0 that use them (*confirmed* from the data): Barabbas_TCTF (id 9: 6, 5, 7, 5,
6), SuperNinja (11: 8, 9, 10, 9, 8) and Mutant_Muro (13: 13, 12, 12, 12, 13), for Barabus on levels 3 and
8, OutroNinja on level 12, and MutantMuro on level 19. Run For Alarm (11) is used by none. Their handlers
were only reached through the table: `tools/ghidra/entries.tsv` creates them; their names come from the
Mac build, in the table's order. Ported: Barabbas, the super ninja and Mutant Muro (`src/boss.c`,
`tests/boss_test.c`).

**Barabbas** (5 Shoot 0x491470, 6 Advance 0x491b10, 7 Melee 0x491be0, each choosing a mode, then
`AI2iBehavior_Barabbas` 0x491540). His block: B+0x24 the mode, +0x28 the last, +0x2c the shot's wait,
+0x30 the damage taken while healing, +0x34 the wait before healing again, +0x38 the retreat's time. The
modes: 1 shoot, 2 hold, 3 advance, 4 advance firing the alternate, 5 hand to hand or a gun, 6 heal, 7 hand
to hand, 8 back off.

- Shoot: 1, or 2 for 180 ticks after a primary shot (message 8). Advance: 4 for 360 to 450 ticks after a
  primary shot, else 1. Melee: 7, or 2 while his gun fires. Shoot walks aiming unless `ai2_barabbas_run`
  (0x545770, true; the movement mode 5).
- The weights (every 5 ticks): the timers count down by 5 (the retreat only while free to act, 0x4e3b10;
  its end bars healing for 600 ticks). His gun firing its primary: hold. Healing: unkillable (C+0x04 bit
  0x20) with as much healed as his maximum (C+0x1024), no more (wait 10,000); 5% of his maximum taken
  while healing stops it (wait from his health: 90 + 90 × hp/max); above 80% it stops (240); 20 or more
  from the target he goes on; closer, under 15% he backs off. Not healing: at 40% or less, with no wait,
  he backs off while the target is within 40 (80 once backing off), else heals if free to act. Without a
  gun, or one out of ammo for good (0x4edc00): mode 5. The movement mode: run aiming with the flag or in
  modes 5, 6 and 8, else walk aiming. Leaving the healing plays type 6 (flags 4).
- The maneuvers: 1 and 2 hold 1; 3 and 4 hold 0.3, advance 1; 5 a gun (100 to 350) beyond 30, melee 0.8;
  6 hold 1, fight mode and the animation 0xdb (flags 1), and at each tick of its loop (its animation's from
  state 0x44, 0x4e3ab0) max × 8 / 1200 hit points; 7 melee 1; 8 hold 0.3, retreat 1 (30..100).
- The flags: in 6, 7 and 8 targeting only; else targeting, the tests and the aim, firing in 1 (primary) and
  4 (alternate). Message 4: no melee of opportunity. Message 6 restarts the healing's wait (then the
  default). 7: handled in 2 and 5 to 8. 9: keep aiming in 1, 2 and 4. 10: the damage counts while healing,
  else the healing's wait restarts.

**Mutant Muro** (12 Melee 0x492f90, 13 Thunderbolt 0x492ac0, each after `AI2iBehavior_MutantMuro`
0x492d50). His block: B+0x24 engaged (720, counted down and set again each time), +0x28 the
thunderbolt's chance, +0x2c its cooldown, +0x30/+0x34 the shield's drop (length, left), +0x38 the ticks
it stays down, +0x3c/+0x40 its rise (left, length), +0x44 the ticks it has been full, +0x48 the damage
taken lately, +0x4c the ticks before it is forgotten (75 after a hit, message 10).

- **The shield** is C+0x1668 (the shown super value; *confirmed*). Every weights message: dropping, the
  part of the drop left; then 0 while down, and at its end a rise of 240; rising, the part done; else 1
  (and its full time counts). `DoCharacterFrame` meanwhile moves C+0x1668 toward the super value C+0x1664
  (or, for a class with ONCC+0xc8d, its health over the maximum, if more) by 0.06 a tick down and 0.03 up
  (0x52b2d8, 0x52b2f0), so between his messages it sinks to 0.7. A class with ONCC+0xc8e above 0.5 is
  shielded: `HandleAttackMask` takes a blow as blocked and unstoppable, with the shield's material.
- **The drop** (0x492cd0, length and down time): from nothing, the full length; during a rise, the
  rise's ticks left scaled to the new length (so it starts from the part left to rise, not the level: kept
  as the exe has it); during a drop or a stay, the drop rescaled and the longer stay kept. An animation of
  his with TRAM flag 22 (message 12, `AI2rMelee_NotifyAnimation` 0x49fd10) drops it for 30, down 360.
- **Melee** (12): engaged, no chance. Shielded: the damage forgotten, melee 1. Unshielded with 12 or
  more damage lately and the target within 30: lying, get up; else hold 1, and standing still (0x4ed3c0:
  type 6 in state 7) his pauses cleared, fight mode and the animation 0x91, the damage forgotten. Else
  melee 1. The flags: targeting only. Message 9: keep aiming.
- **Thunderbolt** (13): lying, get up. More than 35 damage lately or the target within 40: a cooldown of
  300; else with the target beyond 60 or in his line of fire, the chance (120). Engaged with the chance
  and no cooldown: hold 1, and (not already queued; standing still or in it) fight mode and the animation
  0xe7, its shield dropping for 90 and down 600 (refused: cooldown 300). Else, not engaged with a chance,
  the cooldown 300; then engaged, the chance and the damage reset, advance 1. So he waits 300 ticks after
  entering the behaviour before the first. The flags: targeting and the tests, no aim (the fire as it
  was). Message 9: keep aiming; 15: keep the range while he has the chance.
- Message 16 answers whether the shield has been full less than 360 ticks (its sender isn't ported).

**The super ninja** (8 `AI2iBehavior_SuperNinjaFireball` 0x491c50, 9 `...Advance` 0x492a20, 10
`...Melee` 0x492a70, each setting the mode on entering and at every weights message, then
`AI2iBehavior_SuperNinja` 0x491cc0). The Mac build splits the common part into helpers (ContinueTeleport,
CheckTeleportFlee, CheckTeleportAdvance, CheckInvis, FindRetreatFlag, FindEdgeFlag); the Windows exe has
them inline. His block: B+0x24 the mode, +0x28 the last, +0x2c/+0x30/+0x34 the cooldowns of the
fireball, the alternate and the teleport, the bytes +0x3c..+0x3e their animations under way, +0x3f/+0x40
an even chance rolled and its outcome, +0x44 the messages to the damage's decay, +0x48 the ticks before it
decays, +0x4c the damage taken lately, +0x50 the ticks below the level. The modes: 1 melee, 2 fireball, 3
advance, 4 the alternate fireball, 5 teleport to a flag, 6 teleport behind the target. Fireball: 2 on
entering; then 2 with its cooldown over and free to act (0x4e3b10), else 3. Advance: 3; Melee: 1.

- Entering (message 0): B+0x5d8 = 1 and B+0x5dc = 550 (the fall's watch, below); from another than 8 to
  10 the twelve dwords from B+0x24 are cleared, the mode just set with them.
- The weights: the cooldowns count down by 5; every 6 messages the damage falls by 3 (once its hold of
  240 after a hit, message 10, has run out by 30 each time). Outside a teleport (or lying, or knocked,
  0x4e37b0) and not invisible (C+0x1ba): 35 damage within 50 and free to act, mode 5; advancing at 80 or
  more and free to act, the even chance (rolled once, `UUrRandom` < 0x7fff) takes 6; else the alternate's
  cooldown over, free to act, not in 2 and 40 or more away, the chance's other side takes 4. In a teleport
  the mode stays the last. A new mode ends the old one's animation flag; 5 picks a flag (fails: 1), 6 a
  point (fails: 3).
- Mode 5: of the flags from 9500 on, the best by distance from the target (1 − 0.008 × |r − 200| within
  100 of 200, else 0.2) times its angle off the target's facing (`ONrCharacter_GetFacing` 0x4e33a0; within
  15°, 1 − 3.0558 × |angle|, else 0.2); A+0x178 is set to whether one was found, A+0x17c the flag, A+0x188
  the facing toward the target. Mode 6: `AI2rFindNearbyPoint` from the target's location, 35 away, at its
  facing + π (min 15, steps of 15°, 6 each way, weight 4, tolerance 3); A+0x178 set only on success.
- The animations: 1 melee 1 (the chance forgotten); 2 and 4 hold 1, and standing still, fight mode and
  0xe0 or 0xe1 (flags 1) once; lying or a victim instead, that one's cooldown 300. 3 advance 1. 5 and 6
  hold 1 and 0xde once; lying or a victim, the teleport's cooldown 300, mode 1, the last 0, melee 1.2.
- Message 11 (`DoCharacterFrame` 0x4df571 through 0x48b770, at the animation's action frame, TRAM +0x174,
  for an AI in combat): 0xe0 the fireball's cooldown 1200; 0xe1 invisibility 600 (`WPrInventory`
  0x51ca30, forced) and its cooldown 1200. Message 12 (a new animation): another than the fireball's or
  the alternate's while it is under way, its cooldown 300; else a teleport under way, or 0xdf, ends it
  (mode and last 0; 0xdf also forgets the damage and the chance, the others give the teleport a cooldown
  of 300). The flags: targeting and the tests, no aim. Message 9: keep aiming in 2. Message 4: melee.
- **The teleport** is in `ONrCharacter_NewAnimationHook` 0x4e97a0: a new animation of type 0xdf with
  A+0x178 set puts the character at A+0x17c (position, last position, location + 0.5 up, the physics',
  the ground's), facing A+0x188, its room found again (0x4e91c0) and an AI's path made anew (0x4ae3d0).
  0xde is always followed by 0xdf. A+0x178 is never cleared.
- **The fall** (message 13, from `AI2rCombat_Update` after the targeting while B+0x5d8 is set and the
  position is below B+0x5dc; taken, the tick ends there): for 150 ticks he is held at 550; then the flags
  from 9000 on make a closed loop, and the point of it nearest him (across) is where he comes back, facing
  square to it (atan2(dz, −dx) of the segment). `AI2rFindNearbyPoint` there (40, at that facing turned by
  a random −30°..30°, steps of 11.25°, 4 each way, weight 4, tolerance 3), or 80 straight out; then 0xdf
  from state 7 with the varient's 0x200 bit (0x4eb340), seen (C+0x1ba = 0), on the ground (A+0xf6, the air
  velocity 0). No flag 9000, or no animation: not taken.

**`AI2rFindNearbyPoint`** 0x49cec0 (from, distance, min, angle, step, count, weight, tolerance): the
start's room and grid (none: fails) and its cell clamped into the grid; tries at the angle, then +step,
−step, +2 steps, ...; each a straight line by `AI2rDoLocalMovement` to the point `distance` along it.
One that gets there is the answer. Else the farthest point reached, if the *last* try's (not the
farthest's) squared distance is over min² (kept as the exe has it).

**`AI2rDoLocalMovement`** 0x49d700: from the start cell to the point's cell, clipped at the grid's edge
(the crossing interpolated, rounded); `PHrLocalPathWeight` 0x50dc60 along it (the weight its threshold,
the tolerance its blocked cells at the start). Blocked: the last passable cell's middle. Clipped: the edge
cell's middle, stepped 7.5 plus half a cell (0x52a5bc) beyond the edge into the room there (none, or one
with flag 8: fails at the edge cell); the line goes on from there on that room's grid. The first room
crossed in a call of `AI2rFindNearbyPoint` is kept (0x5eb89c), and a later try's first crossing that falls
inside it is taken straight, without the room's grid being loaded: the line goes on over the old grid from
the new cell (an original bug: kept). The grids' run-time obstacles (0x50d660, the visited bits) aren't
ported.

### The tick

After the lost-target part (above), `AI2rCombat_Update`:

1. A new weapon (or firing mode) goes to the targeting (0x4bb700) with the weapon's matrix (the body's
   node 19).
2. With the targeting on, `AI2rTargeting_Update`; else the target's pelvis is the point and the aim stops.
   Then message 13, the fall (the super ninja's, above), while B+0x5d8 is set and the character is
   below B+0x5dc.
3. The getting up (B+0x1f8) is set to 4, none. Lying (the character's state, A+0x1afc, 0x12 or 0x32:
   0x4ecdf0) with a behaviour, message 6 (**the daze**, below); if it answers 1 the path halts, the aim
   stops, the trigger is let go and the tick ends.
4. Every fifth tick, by the character's index (C+0) against the game time: the range; the line of fire
   (beyond the weapon's farthest, +0x60, none; else 0x490c50 from the muzzle to the point, or the arc of
   a ballistic weapon in steps); the weights cleared (0x49dbc0), message 1, the lost target's and the
   no-line-of-fire's weights (advance 1.2 to 12..30), message 7; the maneuver (0x49dbf0); messages 4
   and 5. A getting up that isn't 4 weighs 1.5.
5. The melee maneuver runs `AI2rMelee_Update`.
6. The trigger: unless meleeing, in an animation of the types 0xc0..0xc3, backing off too close, in an
   attack, with an attack override, or against a player whose input is off (`AI2rAllowedToAttack`
   0x48bc30), the fire decision; else the aim stops and the trigger is let go (0x490a50).

**The maneuvers** (0x49dbf0, B+0x194, seven weights): the heaviest is tried, with the next heaviest
weight as the bar it must still clear once its own test has scaled it down; one that can't be done
loses its weight and the next is tried. 0 hold (the path halted); 1 advance (`AI2iManeuver_CheckAdvance`
0x49df80: by how far beyond the near distance, at least half, then a path to the point); 2 retreat (a
local path away, 15 long, by the distance between the near and far ones and the path's quality,
0x52a944); 3 a gun to pick up; 4 the alarm; 5 melee (0x49e910: within 80 of the target, else an advance
to 15..20); 6 getting up (lying, the forward key for one tick, `AI2rExecutor_MoveOverride` 0x498b60;
standing, it fails).

**The gun** (`AI2iManeuver_CheckGun` 0x49e390; the no-gun behaviour 0x491060 gives it weight 1 with
its distances, B+0x1cc 30 and B+0x1d0 200). The block's fields: B+0x71/0x72 the chance to look drawn
and its outcome, B+0x73/0x74 the same for a running pickup, B+0x1d4 running, B+0x1d8 the gun, B+0x1dc
and B+0x1e0 two counters, B+0x1e4 and B+0x1e8 a list of four guns out of reach. `AI2rCombat_Enter`
clears them all (0x49db70); a range change from none or short, or farther from long or medium, or
closer from the retreats, draws the chance again.

- **Looking for one** (no gun, or it is gone, owned or flag 4: 0x51b240). Once, `UUrRandom` below the
  class's percentage (ONCC+0x138 +0x164; level 1's thugs 30) times 0xffff over 100; failed, no gun.
  Then `WPrLocateNearbyWeapons` 0x51b290 lists the free weapons whose handle (the place + ONWC+0x704)
  is within the far distance, nearest first. Skipped: a class that can't be holstered (ONWC+0xcc 2);
  one without ammo when the character has no clip or cell for it (0x51cce0, 0x51c730); with unlimited
  ammo (C+0x04 0x40), any but those that take none (+0xcc 0x800); one in the list out of reach. For
  each, `AI2iManeuver_TryPathToGun` 0x49e810: the rooms' A* from the character's room to the handle's
  no longer than the best so far (`PHrPathDistance` 0x50edb0 into 0x50bdb0, which keeps no room past
  the bound and gives the way's length: g plus the last link's middle to the point), then
  `AI2rPath_GoToPoint` within 8. The weight is scaled by 1 − (length − near) / (far − near), never
  above 1; under the bar the path halts. The last that passes is the gun.
- **Going for it.** Lying, it gets up (the forward key, `AI2rExecutor_MoveOverride`). With a running
  pickup and an animation with TRAM flag 20 but not 23, the path halts and it looks at the handle.
  Within 3 of the handle (its feet, C+0x148): the pickup animation (0x9e, 0x9f for a rifle: +0xcc 8,
  `ONrCharacter_DoAnimation` 1), the gun at C+0x1d8 for its action frame, and the hand's weapon
  dropped (0x4e4ab0). Else, when the path has arrived (C+0x112c; or A+0x9f0, read here only) or it is
  within 25 across but more than 0.8 of the distance up or down, B+0x1dc counts up, else it clears and
  B+0x1e0 counts the maneuvers within 25 across (else clears). Under 40 and 180 the path to it is
  made again; else the gun is out of reach: it goes to the front of the list, which is shifted with a
  forward copy (every entry becomes the first; the original's), and the count is at least 4.
- **After the maneuvers** (`AI2rCombat_Update`), going for a gun, once the class's other percentage
  (+0x168) for a running pickup (`AI2rManeuver_TryRunningGunPickup` 0x49f020); with another
  maneuver, that chance is forgotten and B+0x1dc cleared.

The port (`src/combat.c`, its test `tests/gun_test.c`: a THUG_Wh 50 from a pistol picks it up)
has no running pickup. Its movement layer is a stand-in that walks in combat (the default modes of
0x52af2c) and stops at the path's 8, so an AI may stand 4 or 5 from the handle until another maneuver
carries it closer, or give the gun up after 40 maneuvers.

**The daze** (the default behaviour's message 6, 0x493120; the combat block's dazed flag at B+0x189,
the character's C+0x101c, this fall's daze begun, and C+0x1020, the ticks left). Not dazed: with no
daze begun yet this fall, both are set, `AI2rStartDazeTimer` 0x48b9f0 draws the ticks (the class's
ONCC+0x128 to ONCC+0x12a, the span times `UUrRandom & 0xffff` over 0x10000), and the answer is 1;
with one done, the getting up is set to 0 (so it weighs) and the answer 0. Dazed: with no ticks left
the flag goes and the answer is 0; else the targeting and the line-of-fire checks and the aim are off
and the answer 1. `AI2rUpdateAI` 0x489d40 counts the ticks down while the character lies. A shot
(damage kind 0x1000000 to `AI2rNotifyDamage` 0x48b480) cuts them to 1, and so does a block's
decision (0x48b9d0, not ported yet). `ONrCharacter_Knockdown` 0x4e4e10, for an AI's character knocked down (the types 0x4f..0x5c that
lie, 0x8d, 0xd6, 0xd7) while not lying, calls 0x48b530, which clears C+0x101c: a new fall, a new
daze.

**The keys' override** (A+0x1ab9, the keys at A+0x1ac0, flags at A+0x1ac4; cleared after each
`AI2rExecutor_Update` 0x498550): the keys instead of the move's, the mode running (6, 7 aiming), 3
with the crouch key, 4 (5 aiming) with flag 1.

### The targeting

The targeting (B+0x90, `AI2rTargeting_*`) works for its owner through callbacks (0x53ebf8): the fire, the
aiming speed, the shooter's place (its pelvis), and the movement's looks (along a direction, at a point,
at a character). Its class block is ONCC+0x138: +0 and +4 the first shots' error (by the distance, at
most), +8 how much of the target's motion is led, +0xc the lead's delay, +0x10, +0x14 and +0x18 the
motion's windows and the memory of places (60), +0x1c 13 shooting skills of 0x18 bytes (+4 the error
close up, +0x14..+0x16 the delay's range). A weapon's two AI firing modes (ONWC+0xd0, +0x150, 0x80
bytes): +0 flags, +4 a matrix to the barrel's space, +0x34 the barrel, +0x40 the muzzle, +0x4c the shots'
speed, +0x50 and +0x54 the tolerances, +0x5c the closest it fires at, +0x60 the farthest, +0x66 the
skill, +0x6c and +0x70 a ballistic shot's speed and gravity.

- **The prediction** (0x4bacc0): the target's pelvis kept in a ring; its motion over the short and the
  long windows (per second); with both steady (their agreement over 0.5) the point is led by the shots'
  travel time and the delay.
- **The aim** (0x4bb370): within 25 the muzzle draws back along the barrel (up to 8, fully within 10);
  the vector from the muzzle to the point (a ballistic weapon's arc solved for) is what the movement aims
  along.
- **The fire decision** (0x4ba830): the vector in the barrel's space; not even toward it (the ratio
  ahead/across under 0.5 + 1.5 × closeness), faster aiming and no shot. Else the tolerance (the firing
  mode's, the wide one after the first shots; grown by the grouping, the first shots' error, a tenth per
  tick over 30 off target; close up the skill's; else up to 4 times at range): within it, fire; outside
  it, a delay from the skill's range, and far off (5 times), faster aiming. Under 10 away for 10 ticks it
  fires point blank.

### The executor's aim

The movement's look (`AI2iMovementState_Calculate_Movement` 0x4aa250; +0x1584: 1 a point, 2 a
direction, 3 a character) sets the executor's aim (0x498dc0, 0x498e10; stop 0x498da0) and, without a
path's facing, the facing. `AI2rExecutor_UpdateAim` 0x498f50 turns the aim point (A+0x2198), from 15 over
the feet, toward it at the aiming speed (A+0x1ab4 × 6° a tick, by the weapon's +0x7c in the combat
states, 0x52a4d8); more than 130° off the facing it aims ahead. `DoCharacterFrame` turns the point into
the character's aim (A+0x21bc, 0x21c0) for an AI's character not under a film.

Not ported yet: the running gun pickup, Run For Alarm, the getting up, the attack of
opportunity (0x49f2e0), the melee wait of an empty weapon (B+0x64), the objects in the line of fire,
the arc's steps; the local paths of the advance and the retreat are the port's stand-ins.

## The alarm

The alarm state (C+0x1f8 7; `src/alarm.c`, its test `tests/alarm_test.c`: level 1's Bay4_Comguy_1 runs
upstairs to console 6 and uses it). Its settings are the Combat object's last five fields, copied by
`AI2rInitializeCharacter` 0x48a6b0:

| Object | Character | What | Default |
|---|---|---|---|
| +0x7c | C+0xfec | how far (by way) an alarm console is looked for | 300 |
| +0x80 | C+0xff0 | enemies beyond this are ignored | 80 |
| +0x84 | C+0xff4 | enemies within this are fought | 0 |
| +0x88 | C+0xff8 | the damage from an enemy that makes it fight | 30 |
| +0x8c | C+0xffc | the ticks a fight lasts before the alarm again | 360 |

The run's own fields: C+0xfdc the console's action marker, C+0xfe0 the marker its tries count for,
C+0xfe4 the tries, C+0xfe8 the fight time left. The CHAR object's +0x1e4 is C+0xfd8, the alarm groups.

- **The console** (`AI2rAlarm_FindAlarmConsole` 0x48eb70, each console through 0x48ebc0): an alarm
  console (flag 0x40), active, not used, with the shortest way to its marker within the find distance
  (`PHrPathDistance`, bounded by the best so far).
- **Setup** (`AI2rAlarm_Setup` 0x48e810, marker or none for the current one): another marker ends the
  current run first (0x48e950); the fight time is cleared; a new marker restarts the tries; the
  alarm state is entered. **Enter** (0x48e360): the calm states' knowledge handler, every contact's
  damage (+0x3c) cleared, the goal 5 back from the marker along its facing, movement mode 6 (running),
  no aim, glance or held facing, then `AI2rPath_GoToPoint` within 5 facing the marker's way.
  **Exit** (0x48e7f0): the path halted, movement mode 0.
- **The tick** (0x48e410). Using the console (C+0x1680): the path halted. No marker: the pursuit of
  the best hostile contact (hostility and strength above 1; the list is by importance, the nearest of
  the strongest), else back to the job. An enemy contact (strength above 3) within the ignore distance
  and either within the chase distance or with the damage threshold done: the fight time, then the
  pursuit of it (the original does so for each such contact and goes on; the port takes the last and
  stops). On the way, lying, it gets up (forward). There: not standing still (0x4ed280), it waits 60
  ticks; standing, if the marker can be used with the AI's wider limits (`ONrGameState_TryActionMarker`
  0x4f5840) the console action starts (0x4ee1b0, which holsters the weapon in hand, for the player
  too). Else, lying, the back key; after three tries the run ends (0x48e950: the pursuit of the best
  contact, else the job); else the path again and a try counted.
- **The fight on the way.** In combat with a run pending, a target within the chase distance renews
  the fight time and one beyond the ignore distance sends it back (`AI2rAlarm_Setup`).
  `AI2rUpdateAI` 0x489d40 counts the fight time down out of the alarm state and resumes the run at 0.
- **Into the alarm**: the combat maneuver 4 (the no-gun behaviour's fallback 2; the console set
  beforehand, B+0x1fc, is never written, so the nearest is looked for), then Setup after the
  selection; `ai2_doalarm char [console]` (`AI2iScript_SetState_Alarm` 0x4b4db0: by id, active and
  unused, else the nearest); the pursuit's forget mode with C+0xfc4 2 (0x4b1350, "The pursuit") and the
  knockdown (0x48ea40; not ported).
- **The alarm event** (event type 6, its id the group: 0x49ac30): every AI not passive whose groups
  have that bit, but the setter, gets a pending alarm contact (`AI2iKnowledge_AddPending`, type 9,
  strength 3, the setter's place, value the group) and flag 0x10; a setter that is the player only if
  the AI doesn't ignore it (0x2000), an AI setter only if one of the two is hostile to the other.
  Event 7 (0x4cae10) does nothing.

## The pursuit

The pursuit state (6, `AI2_Pursuit.c`, ported in `src/pursuit.c`, tested by `tests/pursuit_test.c`) is
what a calm character does with a contact it has noticed short of combat. All of it is *confirmed* in
the Windows exe unless marked.

| What | Where |
|---|---|
| Enter, exit | `AI2rPursuit_Enter` 0x4b0ea0, `AI2rPursuit_Exit` 0x4b0f50 |
| The tick | 0x4b0fb0 (from `AI2rUpdateAI` 0x489d40) |
| Hurrying | 0x4b0f90, through `AI2rIsHurrying` 0x48b020 |
| The mode for the contact | `AI2iPursuit_ChangeMode` 0x4b1260 |
| A mode begins, ends | 0x4b1350, 0x4b1570 |
| Whether to go | `AI2iPursuit_DesirePursuit` 0x4b1620 |
| The knowledge handler, the contact forgotten | 0x4b1750, 0x4b1830 (from 0x49b8e0 when `AI2rKnowledgeState_Forget` removes it) |
| The look around | `AI2rGuard_UpdateScan` 0x499b50, with 0x499970 (a new scan), 0x499990 (the next look), 0x499d00 (the sweep) |

### The settings

From the CHAR object (file offsets; the reader 0x4bf290 moves them):

| CHAR | C+ | What | Default |
|---|---|---|---|
| +0x1e0 | C+0xfa0 | The least alert that pursues | 2 |
| +0x1e8 | C+0xfb4 | The mode for a strong contact (strength 3, 4) below medium alert | 5 |
| +0x1ec | C+0xfb8 | The same for a weak one (strength 2) | 1 |
| +0x1f0 | C+0xfbc | A strong one at medium alert or above | 6 |
| +0x1f4 | C+0xfc0 | A weak one at medium or above | 4 |
| +0x1f8 | C+0xfc4 | A lost contact: 0 the job, 1 keep looking, 2 the alarm | 0 |

The modes: 0 none, 1 forget, 2 go to the place, 3 wait, 4 look around, 5 move, 6 hunt, 7 glance. The
original prints "pursuit mode %s not yet implemented" for 5 and 6 and does nothing else: levels 2
and 4 have characters with them (inferred from the data: on levels 1, 3, 9 every character has
4, 1, 4, 4, 0). The Combat object's +0x68 (C+0x1054, default 200) is the pursuit's distance, 0 none.
C+0xfc8 and C+0xfcc keep the job's place: `AI2rUpdateAI` writes the location there each tick the job's
patrol is running and its block's +0x53a is set and +0x53b clear (the port: each tick of the job's
patrol); `ai2_setjobstate` and the initialization clear it.

### The block (C+0x8b8)

+0x08 the contact; +0x0c the mode; +0x10 the contact was seen (its highest strength above 3, or
strength 4 at a mode change); +0x11 the alert above low; +0x12 the place reached; +0x14 the
vocalization's timer; +0x18 ticks held still (only cleared in the exe); +0x1c the mode's timer; +0x20
a time (only cleared: the test against it holds only in the game's first 15 ticks); +0x24 ticks seen
close; +0x28 hurrying; +0x29 the look begun; +0x2c the scan.

The scan (0x54 bytes): +0x00 about a facing, +0x04 that facing (1000: the character's), +0x08 the
range about it, +0x0c the centre of the pick (1000 all round), +0x10 the first look, +0x14 started,
+0x18 the look picked, +0x1c the vector along it (30 long), +0x28 the sweep's offset, +0x2c its range
(0 none), +0x30 the vector looked along, +0x3c/+0x40 how long a look is held (base, random part),
+0x44/+0x48 the ticks between sweeps, +0x4c/+0x50 their timers.

### What it does

- **Enter.** The aiming speed is halved (restored on exit). Alerted, and wanting to go (why 0), it goes
  to the place; else it glances, for the contact type's glance time (0x52a64c, the calm handler's),
  which is also the glance (C+0xf74) when not alerted. The vocalization timer starts at 120. Then the
  mode for the contact, and the mode begins.
- **Wanting to go** (0x4b1620, by a reason 0..3): not for an unhostile contact (but on entering), nor
  below the least alert. With a pursuit distance: for 0 and 1 the contact's place must be within it
  by the rooms' graph (`PHrPathDistance` 0x50edb0; a point in no room fails); for 2 and 3 the job's
  place must be known and the way back to it shorter than the distance (no way found passes).
- **The mode for the contact** (0x4b1260), only when the mode's timer isn't running. Strength 0 (the
  character dead) starts the timer at the class's lost-target wait (ONCC+0x290). Strength 1 forgets,
  unless lost contacts are kept looking for. 2 takes the weak mode, 3 and 4 the strong one (4 marks
  it seen), by the alert. Mode 0, or the mode it has, changes nothing; going to the place keeps going
  unless it forgets.
- **A mode begins** (0x4b1350; held still, only forgetting does). The path halts and the aim stops.
  - *Forget*: the lost setting below 0 does nothing; 2, with the contact seen and an alarm console
    (the one it has, else `AI2rAlarm_FindAlarmConsole`), runs to the alarm; else back to the job
    (0x48af80, both flags: idle below the job's alert).
  - *Go*: the path to the contact's place (within 12), hurrying when alerted.
  - *Look*: a scan all round, 45 degrees of sweep, looks held 100 to 200 ticks, sweeps every 40 to 80.
  - *Glance*: a scan along the way to the contact, held 10,000 ticks, a sweep after 80.
- **A mode ends** (0x4b1570): going ends in a glance (80 ticks) at an unhostile contact seen, else a
  look around (600); the look goes on if it still wants to (why 2), else forgets, then takes the mode
  for the contact; the glance goes to the place if it wants to (why 1), else forgets; any other mode
  waits.
- **The tick** (0x4b0fb0). Lying, the forward key (to get up). The hold, then the mode's timer, count
  down. Waiting, looking, moving and hunting, the vocalization's timer runs (720 to 900 ticks;
  `AI2rVocalize` 4 not ported). Looking and glancing run the scan. Going: the contact's fading is held
  back a tick (its +0x38); then by the squared distance to the path's goal (the height ignored within
  22.5, from the path's 0x4af030):
  - beyond 200: on its way, hurrying when alerted, not looking;
  - within 200, with the way from its head to the contact's place blocked (flags 0x42080): not looking;
  - in sight beyond 60: it looks toward the place (a scan within 70 degrees of its facing, looks held
    40 to 80 ticks), hurrying if alerted and not yet reached;
  - in sight within 60 for 6 ticks, or arrived (within 12): reached, and the mode ends. In sight it
    goes on looking. Arrived, the exe tests a flag that only the in-sight way sets: its value is the
    stack's leftover (the port takes it as clear: the aim stops).
- **Hurrying** (`AI2rIsHurrying`): the AI's movement takes the hurrying table (0x52af18: walk, walk,
  run, run aiming, run aiming by the alert) in the states the table at 0x52a4d8 doesn't mark calm
  (combat, alarm, panic, and 4), and in the pursuit while going and hurrying; else the calm table
  (0x52af2c).
- **The knowledge handler** (0x4b1750). The calm handler first (it may start combat). A fading contact
  that is its own: with an alarm run set and strength below 3, back to the alarm; else the mode for
  the contact. Then the most important contact: its own, and fading, nothing more; another, only if
  hostile, becomes its contact. If it wants to go (why 3), it goes to the place.
- **Forgotten** (0x4b1830): its own contact forgotten sends it to the alarm (as above), else to the
  first other contact it wants to go to, else forgetting.
- **The scan** (0x499b50). When not started or the look's hold is out: the centre (all round when not
  about a facing), the next look (0x499990: first the scan's first look, then random about the centre
  within its range, at least 40 degrees from the last, and 30 along it clear of the environment from
  the head, flags 0x42080; six tries each), the hold and the sweep timers drawn. With a sweep range,
  when its timer is out a new offset is drawn within the range. The look is the pick's vector, or it
  turned by the offset (0x499d00); the movement looks along it (0x4a8090).

Not ported: the vocalizations, the glance's look (0x4a8150) and the facing held, the report
(`AI2rPursuit_Report` 0x4b18e0, debug text). A contact removed without the forget's notice leaves
the exe with a stale pointer; the port goes back to the job.

## Neutral characters

The port is `src/neutral.c`. A neutral is an AI character with a Neutral behaviour
([objc.md](../formats/objc.md), NEUT): `AI2rNeutral_ChangeBehavior` 0x4ac870 (from
`AI2rInitializeCharacter` 0x48a6b0 with the CHAR's id, forced, and from `ai2_neutralbehavior`
0x4b87b0) keeps its id at C+0x100c, the line to start at (C+0x100e, 0), the flags (C+0x1000), the
time of the last check (C+0x1004), and the trigger and enemy ranges (C+0x1010, C+0x1014; without one
the enemy range is 100). C+0x1008 is a wait before it may start by itself again. Confirmed:

- **Starting.** `AI2rUpdateAI` 0x489d40, before the state's update (not passive), asks
  `AI2rNeutral_CheckTrigger` 0x4ac950 (automatic) of every AI not in the neutral state, else counts the
  wait down. The check: an AI with a behaviour, no partner, not panicking, not done (flag 0x10); the
  player not its enemy (0x48b1d0) and the way to the player's pelvis not blocked (0x49ea60).
  Automatic: no wait, within the trigger range, at most every 30 ticks, the player a seen contact
  (strength 4), and with an enemy range, no dangerous contact (0x4ae170: a hurt, or a melee sound,
  gunfire or a sighting of someone hostile, within the enemy range) nor a fighter after it or the player
  (`AI2iNeutral_CheckEnemies` 0x4acbc0: an AI in combat with either as its target, within the range of
  either), else not for 300 ticks. With the action key (`AI2rTryNeutralInteraction` 0x48bb40, from
  `HandleStartTauntOrAction` 0x4f80d0 after the doors, and to show the hint, 0x4fa6a0): within 35 of
  the player, the player facing it within 60 degrees. A neutral already calling the player that the
  player faces (0x4acc90) is taken first; the key starts the state for one that isn't and marks the
  conversation asked (+0x278).
- **The state** (C+0x1f8 8; its block from C+0x8b8, +8 on: the player, the Neutral's 0x26c bytes,
  then the conversation's fields, `NeutralTalk` in src/ai.h). `AI2rNeutral_Enter` 0x4acdb0: its place
  kept (+0x290), no line (+0x27c -1), flag 1, the player, 600 ticks of patience, the first call in
  300 + a random 0..89 ticks, the call's sound said (speech kind 4), running.
- **Before the talk** (`AI2rNeutral_Update` 0x4ad1f0, line -1): in the talk range and not blocked,
  asked: it stays and faces the player; with the player free and itself standing (its animation ending
  in state 7) the conversation begins: flag 2 (with 0x10 for a once-only Neutral), the partners
  (C+0x162c, both), C+0x04 0x800000 on both if it can't be broken off, the player turned to face it,
  the player's move count (C+0x1630) 0, both out of fight mode with the weapon hidden (C+0x04 0x10000,
  0x2000: 0x4ed030, 0x4ecf80), and the line from C+0x100e. Not asked: it faces the player and waits
  (the patience out: the wait 180), every 360 + 0..89 ticks the call's sound and animation 0xe6. Out of
  the follow range from its place: the wait 180, back to its place and job. Within 1.5 times the
  trigger range: after the player (within 25) every 20 ticks. Farther: back to the job. Every 10 ticks
  (not during a give) a fighter near ends it (aborted, the enemy's line).
- **The lines.** Each is said by its speaker (flag 1 the player, else the neutral; speech kind 3) and
  plays its animations (the speaker's, the listener's), waited for if flagged
  (`AI2iNeutral_ConversationAnimation` 0x4adb00); the next follows when they and the speech are done.
  A give line: the neutral walks to the player (within 20), plays 0xcd, 15 ticks later the player too,
  and at its animation's action frame (TRAM+0x174) gives: flag 4 (0x10 for a once-only give); the
  clips, cells and hypos through 0x51ca30 (what can't be carried dropped by the neutral 10 ahead,
  0x51bd80); the shield and invisibility at their default amounts; the LSI (with invisibility's default
  amount, as the exe asks) taken from the neutral (C+0x1b4); the weapon into the player's hand, or
  dropped by the neutral (0x51b790) when the player has one. C+0x100e moves past the give, so a
  conversation broken off and started again resumes after it. After the last line: flags 0x18, back to
  the job.
- **The end.** `AI2rNeutral_Stop` 0x4acf70 (refused during a give unless forced): aborted, the neutral
  waits 180 ticks; else the Neutral's end script is due for the character stopped (C+0x1018, C+0x1f0
  flag 0x20, run by the next `AI2rUpdateAI`). Each side (0x4ad090): out of the conversation's flags; an
  aborted neutral says the enemy's or the abort's line once (none when done), stops looking and moving,
  and goes back to its job when asked. The partners are cleared. The state's exit (0x4acf20) stops it,
  aborted unless every line was said. A death (0x48b370) ends it (aborted, with the enemy's line); a
  deletion (0x48b3e0) only clears the partners.
- **The player's keys** (0x4fa520, every tick): in a conversation, fire, alternate fire or a blow
  ends it (aborted, back to the job); moving (forward, back, sideways, jump, crouch) counts 2 a tick
  (not if it can't be broken off), still counts back 2, and past 14 ends it. While it goes on the
  player's movement, blows, jump, crouch and the action keys are taken (the turn keys and walk stay).

### Speech

Every character has one line at a time (`ONrSpeech_Say` 0x516940, `ONrSpeech_Update` 0x516a60 first
in each character's update 0x4ddba0): C+0x15f0 saying, +0x15f1 begun, +0x15f2 over, +0x15f4 the
sound's id, +0x15f8 its end, +0x15fc the line (kind, ticks before, a byte, a flag set while it waits,
the ambient's name, ticks after), +0x1614 one queued. A line of a kind no higher than the current one
is queued (if higher than the queued one and not interrupting) or refused; kind 7 always plays. The
sound starts at the head (`OSiDialog_Play` 0x5125c0, the ambient at its place, its subtitle as a
dialog's) and the line is over when it ends or its first sound's length (0x78 ticks without) passes;
then the ticks after, then the queued one.

Not ported: the blockage test (0x49ea60, as for the consoles), the action hint, the movement's free
facing and glance, a dropped powerup's fall (it is placed).

## Panic

The port is `src/panic.c`. A non-combatant (CHAR flag 0x08, C+0x1f0 bit 4) has its knowledge handled
by `AI2rNonCombatant_NotifyKnowledge` 0x4adeb0 in the calm states and the neutral one, and keeps it in
the panic state (C+0x1f8 9). Confirmed:

- **News** (not a degrade) from someone hostile (hostility 2) or a combat sound that is dangerous
  (0x4ae070, with "known" from the panic's +0x12) ends a conversation (aborted, the enemy line) and
  panics it: `AI2rCharacter_Panic` 0x4ae1e0. The time is by the contact's kind from the combat
  settings (C+0x1058 a melee sound, +0x105c gunfire, +0x1060 a sighting, +0x1064 a hurt; the Combat
  object's +0x6c..+0x78; defaults 600, 900, 600, 1200), plus a random part of up to half again; other
  kinds don't panic it. The end (+0x0c) only moves later. A hostile contact sets +0x12; a hurt brings
  the cry (+0x14) nearer by 5 a point of damage. The strongest contact (of no less hostility) becomes
  the fear (+0x08), looked at unless cowering.
- **A degrade** of the fear (weak, or no longer hostile by its hits) takes the next dangerous contact,
  else the panic ends 180 ticks after the fear's last post.
- **The state** (`AI2rPanic_Enter` 0x4adbe0): out of fight mode, the weapon hidden and the variant
  0x8000 (0x4ed030, 0x4ecf80, 0x4ecf30), halted, running, the first cry in 45 ticks. The update
  (0x4adc80): past its end, back to the job; a cry (`AI2rVocalize` type 5, not ported) every 1500 +
  0..499 ticks; crouching (mode 3) once crouched or the fear within 20; lying, the class's daze once a
  fall, then up with the move override: back when facing the fear within 90 degrees (or with none),
  else forward, crouched if cowering. The exit (0x4adc50) undoes the flags.

## Melee

The combat state's hand-to-hand fighting. The port is `src/melee.c`, with its test
`tests/melee_test.c` (a THUG_Wh thug fights Konoko on level 1).

| What | Where | Status |
|---|---|---|
| Profiles, read | `OBJiMelee_Read` 0x4c6c10 (OBJ type "Melee Profile") | confirmed |
| The attack moves' keys | `AI2rMelee_Initialize` 0x49fa50, from the input table 0x551c00 | confirmed |
| Ready for a character class | `AI2rMelee_PrepareForUse` 0x4a4650 | confirmed |
| The update | `AI2rMelee_Update` 0x4a01f0 | confirmed |
| Stopping a technique | `AI2rStopMeleeTechnique` 0x48b6d0 | confirmed |
| A new animation (the attacker's, or the victim's) | `AI2rMelee_NotifyAnimation` 0x49fd10 | confirmed |
| Pressing an attack's keys | `AI2rExecutor_AttackOverride` 0x498bb0 | confirmed |
| Blocking | `AI2rBlockFunction` 0x48b870, every AI's C+0x15ec | confirmed |
| Weighting a technique | `AI2iMelee_WeightTechnique` 0x4a2580 | not ported: the base weight stays |

**A new animation** in the melee maneuver (0x49fd10, after the behaviour's message 12): a hit
reaction (0x4e37b0) stops the technique. With the attack's keys pressed and not yet taken
(`AI2rExecutor_HasAttackOverride`, its animation type): the new animation of that type takes them; a
new one after an animation of that type leaves them waiting; any other is "wrong" (`AI2rError`
0x4936b0 with the types), and the override and the technique stop. (Only the first was ported before:
a combo whose keys started its own first attack again waited for good.) The throw override's case
(cleared by an attack or a throw-flagged animation) waits for the throws.

### A profile

On disk: the id (u32), the name (64 bytes), the character class (64), four dodge chances at
+0x84..+0x90, the block chances (%) alone (+0x94) and in a group (+0x98, from version 0x26; before,
the single one), six factors (+0x9c..+0xb0), then the counts at +0xbc (attacks),
+0xc0 (evades), +0xc4 (maneuvers) and +0xc8 (moves). The techniques follow at +0xcc, 0x58 bytes
each: the name (64), flags (1 interruptible, 2 the wide facing), weight, importance, the repeat delay,
the move count and the first move. The moves come after the techniques, 16 bytes each: the type
(the category in the top four bits: 0 attack, 1 position, 2 maneuver, 3 evade, 4 throw) and three
parameters. The profile in memory (C+0x1068) keeps the disk's offsets: `OBJiMelee_Read` copies the
id to the object's +0x2c and each field after it in turn. The CHAR object names its profile by id
(u16 at +0x1b0, inferred). The thugs of level 1 use 0x27, THUG_Wh: 10 attacks, 1 evade, 5 maneuvers,
38 moves, blocking 25% alone and 10% in a group (Konoko's profile: 80 and 80).

### The tables

All read from the exe at run time.

| Table | Layout |
|---|---|
| 0x540a88, the attack moves | 0x38 bytes: name (32), from state (byte +0x20), to state (+0x21), direction (bits 16..19 of +0x20), the animation type (+0x24); ends at +0x20 = −1 |
| 0x551c00, the inputs | 0x18 bytes: keys down (u64), keys held (u64), the required previous type (low u16) and the resulting type (high u16) |
| 0x52a9e8, the categories | 0x48 bytes: name (32) and ten functions; 4 the "can follow" check, 5 the start, 6 the update |
| 0x5435b0, the positions | 0x40 bytes: name (32), direction (+0x20: 1 forward, 2 back, 3 left, 4 right), "has parameters" (+0x24) |
| 0x543a94, the maneuvers | Advance, Retreat, Circle Left, Circle Right, Pause, …, Taunt (7), Random Stop (8), …, Max Range (0xd) |
| 0x52ac50, the melee states | pairs |

### The update

Once a tick while in combat and near the target. If no technique runs, it picks one by weight among
those whose facing and range allow it, and not within their repeat delay. The facing factor is 1
within π/4 of the target and falls linearly to 0 at π/3; with the wide flag it is 1 within 70° and
0 at 110°, and at least 0.35 for direction 1. Each technique then runs its moves in turn, each through
its category's start and update. An attack move presses its keys through the executor's override:
while in combat the executor ORs them into the held and down keys, and with no previous type to follow
it zeroes the combo type. The next attack's keys are pressed within the last 4 frames of the current
attack, so the combo continues. When a technique ends, its memory weight goes to 0 if it was 1000,
else it is multiplied by 0.6.

At most two attackers (0x545774) hold the fight cookie at once; a cookie is held for 180 ticks.

The animation's reach is TRAM +0x50; the near limit is the reach less half of TRAM +0x11c.

### Blocking

`HandleAttackMask` 0x4e7740 asks the character's block function (C+0x15ec) once
`ONrCharacter_CouldBlock` 0x4e62b0 and the attack's high and low flags allow a block; the player has
none and blocks by the stance alone. `AI2rInitializeCharacter` 0x48a6b0 gives every AI
`AI2rBlockFunction`. It refuses with no profile, when passive or non-aggressive (flags 2 and 4), when
every AI is passive (0x5ec0c4, set by 0x48b0d0; not yet ported) or in panic (state 9). The chance is
the profile's single one. In combat, with n attackers on the same target (the fight's count, +8), it is
single + (group − single) × (n − 1) / 3 (the third at 0x52a5ac) below four and the group chance from
four. That times the difficulty's factor, from the game settings' table at +0xabc (0.6, 1, 1.3 for
easy, normal, hard: `ONrPersist_GetDifficulty` 0x50f240, normal at start-up), must exceed a random
0..100 (`(UUrRandom & 0xffff) × 100 / 0xffff`). After the decision come the combat report
(`AI2rNotifyAttackLanded` 0x48b700) and the daze timer's cut to 1 (0x48b9d0); neither is ported yet.

Stand-ins, marked in `src/melee.c`: the weighting's prediction of what an attack does to the target;
the positions' prediction (`TRrIntersectAnimations`), replaced by the reach and the profile distances,
with a 120-tick timeout; evades and the dodge chance; throws; jumps and crouches; and the circling's
check of the other attackers.

### Predicting an attack

The melee's weighting asks whether an attack would land before choosing it. The port is
`src/chr_intersect.c`, with its test `tests/intersect_test.c`. All confirmed in the Windows exe.

**The context** (`ONrSetupAnimIntersectionContext` 0x4eea60, 0x1b0 bytes) holds a flag (+0, set for
the melee), then a half per character (0x4eee20, 0x94 bytes, the attacker's at +4 and the target's
at +0x98), then a 3x4 matrix twice (+0x130, and a working copy at +0x160), the target's motion
(+0x190), its parts' bottom, top and radius (+0x19c..+0x1a4), the result (+0x1a8) and the frames to
the hit (+0x1ac).

- A half: the animation and frame, the character, a "changed" flag, the position and facing, the air
  state (in the air, jumped, jump held, air ticks, the frames of air to come, the air velocity), a
  copy of those, a radius (3 for an AI, 4 for the player), and ONCC +0x00..+0x16 (the gravities, the
  lift while the jump is held, its ticks, the ticks before the fall's gravity).
- The matrix takes the target's space to the attacker's, both as (sideways, forward, height): the
  y-z swap at 0x52b298, turned by the target's facing, placed at the difference of the positions,
  turned back by 2π less the attacker's facing (0x434d70 turns from the left), swapped again.
- The target's motion: the mean of its animation's root motion over the current frame and up to three
  before (`ONrCharacter_GetVelocityEstimate` 0x4eafc0), as (x, z), and its climb this tick (C+0x14c
  less C+0x158). When the character collided this tick (the counts at A+0xb0, +0xb2, +0xb4, which
  `ONrGameState_DoCharacterFrame` clears) it is its move turned into its space instead. Stand-in: the
  counts aren't kept yet, so it is always the estimate.
- The parts: the list at 0x54e1e0, 8 bytes each (the part, "in the melee's set", "not for the
  radius"), ended by −1; built for the melee it stops at the first part out of the set. Each part's
  sphere is its matrix's translation plus its geometry's sphere centre. The radius is the largest
  horizontal reach (a lying target counts every part) and is cut to 0.75 for the melee; the bottom
  and top are the spheres' lowest and highest. A character without an active record gets a radius of
  3 or 4 and heights 0 to 15.

**The quick test** (`TRrCheckAnimationBounds` 0x429940): none without attack points or once past the
last. On the ground: the run of the target from now to the last point (its motion times the frames)
must come within the reach plus its radius; its heights must overlap the attack's (TRAM +0x54,
+0x58). Then the 36 directions from the longest reach's: each reach (less 3 for the melee) as a line
the target's circle must touch at the run's start, at its end, or by crossing it. In the air: where
the two circles meet, from the target's motion less the attacker's air velocity.

**The run** (`TRrIntersectAnimations` 0x42a250), frame by frame from the attacker's: at each attack
point's frame, the point's heights against the target's frame's (TRAM +0x28; a unit of margin each
way), then the line from the attacker at the point's angle against the target's circle, a hit when
inside it or when the line meets it within the point's reach. Then the target moves (its air: lift
while the jump is held, then gravity; else its motion), the attacker moves (its air, or its
animation's root motion), and both frames go on. Unless a point was tested, an attacker inside the
target's circle (and overlapping in height) is put back on it along the attack's direction. Past the
last point the result is the commonest miss: too high (3), too low (4), passing the circle (2),
beyond the reach (1), the target behind the line (5), in that order on a tie.

Quirks kept: the target's lift reads the attacker's "jump held"; the target's air velocity, already in
the attacker's space, is turned again each frame; the height overlap of the push adds each height
twice.

### The profile in memory

What the faithful port of the weighting needs; read in the Windows exe, not yet all ported (the
current `src/melee.c` keeps its own shapes).

- **The profile** keeps the disk's offsets up to the counts: attacks +0xbc, evades +0xc0, maneuvers
  +0xc4, moves +0xc8; then the techniques (+0xcc, 0x7c bytes each, attacks, then evades, then
  maneuvers), the moves (+0xd0, 0x28 bytes each), the class's ONCC (+0xd4), the transitions' count
  (+0xdc) and up to 55 transitions from +0xe0, 0x28 bytes each. `AI2rMelee_PrepareForUse` 0x4a4650
  fills the class's part: +0x978 the jump's air time (2 × ONCC +0x08 / ONCC +0x04), +0x97c the
  longest jump's (that plus 2 × ONCC +0x16 × ONCC +0x10 / ONCC +0x04), and from +0x980 seven
  distances. +0x98c, +0x990, +0x994 and +0x998 are the forward, back, left and right steps: the
  frame-0 velocity of the class's animations of type and state (3, 3), (9, 0x10), (0xa, 0x17) and
  (0xb, 0x19) (fight variant 0x200). +0x988..+0x998 are at least half of ONCC +0x08.
- **A technique** in memory: the name (64), flags (+0x40), weight (+0x44), importance (+0x48), the
  move count (+0x4c), the first move (+0x50), the repeat delay (+0x54). On disk the delay, count and
  first move are at +0x4c, +0x50, +0x54; `OBJiMelee_Read` reorders them. Then what `PrepareForUse`
  works out: the index in its kind (+0x58), the delay slot (+0x5a, at most 32, else −1), the computed
  flags (+0x5c), a sum of the attacks' damage (+0x60), the first attack's reach (+0x64) and that less
  half of TRAM +0x11c (+0x68), its TRAM +0x9c (u16 +0x6c), the first attack's animation (+0x70) and
  move (+0x74), and for a throw its distance (+0x78).
- **The computed flags** (+0x5c): the starting melee state (low byte), the direction (bits 8..11),
  0x1000 an attack, 0x4000 no moves, 0x8000 and 0x10000 the attack's high and low flags
  (`TRrAnimation_TestAttackFlag` 1 and 2), 0x20000 any state, 0x40000 attack, 0x80000 evade, 0x100000
  maneuver, 0x200000 unblockable (attack flag 0), 0x400000 block stun (0x429220), 0x800000 stagger
  (0x429260), 0x1000000 a throw (0x2000000 from the front, 0x4000000 from behind; 0x8000000 and
  0x10000000 by the target animation's variant bits 0x6000 and 0x1800), 0x20000000 "max range".
- **A move** in memory: the type and its three numbers, the state it leaves (u16 +0x10), its
  animation (+0x14) and the motion along the direction it makes (+0x18), a second animation (+0x1c)
  and its motion (+0x20), "invalid" (byte +0x24). `AI2rMelee_MoveIsValid` 0x4a50b0 looks a move up. A
  position takes its direction, its animation and its end state from the position table (0x5435b0,
  0x40 bytes each, the animation type by 0x52ab50 and the state by 0x52ab74). The other categories
  ask their category's function 3 (the table at 0x52a9e8, 0x48 bytes each) and look the animation up
  in the class's collection; a throw goes through `AI2iMelee_FindThrow` 0x4a52d0.
- **A transition** (0x28 bytes, from the exe's table at 0x541d6c, 0x38 bytes each, ended by 0, for
  each whose animation the class has): the from and to states and the count of steps, the steps, the
  animation, its duration, its end state, and its motion along its direction (+0x20) and across
  (+0x24). `AI2iMelee_CheckStateTransition` 0x4a4590 finds one; `AI2iMelee_GetTransitionLength`
  0x4a4300 adds up a path's.

### Toward the original's melee (the port's next step)

The port's melee (`src/melee.c`) is a reduced version: its positions, maneuvers and the technique's
weight are stand-ins. `AI2rMelee_PrepareForUse` 0x4a4650 is ported (the class part, the transitions,
the moves' animations and motions, the techniques' computed flags, damage and reach); the stand-ins
don't use all of it yet. The rest, by size of the decompiled code: `AI2rMelee_Update`
0x4a01f0 (1,200), `AI2iMelee_WeightTechnique` 0x4a2580 (2,000), `AI2iMelee_Position_Update` 0x4a6040
(440, on `AI2iMelee_SetupAttackerState` 0x4a2270, `TRrCheckAnimationBounds`, `TRrIntersectAnimations`,
`AI2iMelee_CheckStateTransition` 0x4a4590, 0x4a4300 and `AI2iMelee_WeightLocalMovement` 0x4a4440),
`AI2iMelee_Maneuver_Update` 0x4a7060 (220). The attack prediction is ported (`src/chr_intersect.c`).

`PrepareForUse`'s class part (in the profile, from +0x978): the jump's air time and the longest jump's
(above); seven distances from +0x980, of which +0x98c..+0x998 are the forward, back, left and right
steps of the fight variant's animations of type and state (3, 3), (9, 0x10), (0xa, 0x17) and (0xb,
0x19) (their frame-0 velocity), and +0x988..+0x998 at least half of ONCC +0x08. The transitions come
from the exe's table at 0x541d68 (0x38 bytes a row; groups of rows ended by an empty one, the table by
an empty group): +0x00 the direction (1..4), +0x04 the state whose animation type (by 0x52ab50) the class
must have, +0x08 "required" (a group none of whose rows the class has is an error), +0x0c the steps'
count and +0x10 the steps (melee states), +0x20 an animation type, +0x28 and +0x2c the keys held and
pressed (0x8000000 jump, 0x200000 forward, ...), +0x30 a float (0.7 for the rolls and jumps, 0.2 for
the crouches). Each row the class has becomes a 0x28-byte transition in the profile (+0xe0, at most
55): the bytes +0x00, +0x01, the count +0x02 and the duration +0x03, the steps +0x04, the float +0x08,
the keys +0x10 and +0x14, the animation +0x18, the row's type +0x1c and the animation's +0x1e, and its
motion along its direction (+0x20: the position point, × 0x5295bc) and across (+0x24: the last five
frames' mean step).

### The weighting's call

`AI2iMelee_WeightTechnique` 0x4a2580 takes: the attacker (ECX), the AI's melee state (EDX: the target
first, the distance at +0x80, the angle at +0x88), then on the stack the profile, the intersection
context, "with the cookie" (1; 0 in the second call), the technique's direction, the melee state,
another of the caller's values, the target's "could block", "high" and "low"
(`ONrCharacter_CouldBlock`), and the technique's record (0x2c bytes in the table at 0x637260: the
technique, the weight, ...). An attack is weighed by the profile's factors: the target couldn't
block ×+0x9c, it blocks the other height ×+0xa0, unblockable ×+0xa4, stagger ×+0xa8, block stun
×+0xac, else blocked ×+0xb0. Then the facing difference, the moves one by one (distances, angles,
positions through the transitions, the attack predicted by `TRrIntersectAnimations`), and the throws.
