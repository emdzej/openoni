# The AI: knowledge, alert and melee

How an AI character learns of others and what it does with it. The port is `src/knowledge.c`, with
its test `tests/knowledge_test.c`. The executor, the patrol and the AI's character fields are in
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

The pursuit's handler (0x4b1750) runs the calm handler first, then follows the most important
contact if it is hostile. Its modes, `ChangeMode` 0x4b1260 and `DesirePursuit` 0x4b1620, are not
ported.

## The AI's character fields

| Offset | What |
|---|---|
| C+0x1e8 | 2 for an AI, 0 for the player |
| C+0x1f0 | AI flags: 1 knowledge on, 2 passive, 4 non-aggressive (CHAR flag 0x08), 0x400 never forgets (CHAR flag 0x100), 0x800 blind, 0x1000 deaf, 0x2000 ignores the player, 0x8000 vocalized |
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
  distances. +0x98c, +0x990, +0x994 and +0x998 are the forward, back, left and right steps of the
  class's animations of types 3, 0x10, 0x17 and 0x19 (fight variant 0x200). +0x988..+0x998 are at
  least half of ONCC +0x08.
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
