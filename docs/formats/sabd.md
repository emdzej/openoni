# Animation sounds (SABD)

The sounds a character's animations make: a punch's whoosh, a kick's, a reload's click, a jump or a roll, and
a few idles' yawns. They are records of the binary data's `SABD` class, one blob for each character
variant, all in level 0's `.raw`. Each record gives an [impulse](./osbd) for a kind of animation, or for
one animation by name. The variant's own records come first, then its parent's: the `ONCV` link at +0
leads from `Konoko` to `Any`, and from `striker_hard` to `striker` and then to `Any`.

OpenOni reads them in `src/sound_anim.c`. Worked out from `Oni.exe` v1.0: `OSiSABinaryData_Load`
0x5161d0, `OSrVariant_SoundAnimation_Add` 0x515a70 and `OSiVariant_Impulse_Get` 0x516670 (from
`Oni_Sound_Animation.c`). The field roles are confirmed by their use.

## The blobs

There are 54 `BINA` instances named `BINADBAS<variant>`; `DBAS` is the class `SABD` stored as a
little-endian u32. The class is registered by `OSiSABinaryData_Register` 0x516110. The blob's u32 at
+4 is its length, and its chunks follow from +8. Each chunk is a four-character tag in reading order, a
u32 size, then the data. The search (0x47efe0) compares the tag at every byte and rejects a size larger
than the area searched. On the CD, 51 of the 54 blobs have no records (52 bytes: the two chunks);
`Any`'s is 3,132 bytes.

| Chunk | Size | What |
|---|---|---|
| `SAFT` | 4 | A version (6 on the CD) |
| `SAVT` | 0x20 | The variant's name, matched without case to the `ONCV`s' names (+4) |
| `SASA` | 0x84 | A record |

The `SASA`s are looked for from the end of the `SAVT`'s name, and each next one from the data of the
last, up to the blob's end. A variant's blob replaces its records. Those of the kind "Animation" give
their animations' sounds back first.

## A record (SASA)

| Offset | What |
|---|---|
| 0x00 | u32: the frame (for the kind "Animation") |
| 0x04 | The modifier's name (32 bytes): `Any`, `Crouch`, `Jump`, `Heavy Damage`, `Medium Damage`, `Light Damage` (0 to 5; the table at 0x5579a0, an unknown name 0) |
| 0x24 | The kind's name (32 bytes): `Any` (0), `Animation` (1), `Block`, `Draw Weapon`, ... `Roll`, `Falling Flail` (29); the table at 0x5579d8. An unknown name gives -1, which the original uses as an index anyway; the CD has none |
| 0x44 | The animation's name (32 bytes; for the kind "Animation") |
| 0x64 | The impulse's name (32 bytes) |

The CD has 30 records. 22 of them are in `Any`'s blob; Konoko's has 7 (her punch `kon_pnch_lt`, her
kick `kon_kik`, two idles' `kon_snz` and `kon_yawn`); Shinatama_Zombie's has one, her walk.

Adding a record (0x515a70):

- **Kind "Animation"**: the record sets the `TRAM` of its name's sound to the impulse's name, and its
  frame (`TRrAnimation_SetSoundName` 0x428800, TRAM +0x150 and +0x154). An animation that already has a
  sound naming an existing impulse keeps it. These sounds exist only at run time: the files hold 0.
- **The other kinds**: the record is kept in the variant's array for its kind. If the array already has
  a record of the same modifier whose impulse exists, the new record is left out.

## Playing

`DoCharacterFrame` tests the animation at its frame before the overlays step
(`TRrAnimation_IsSoundFrame` 0x428780). An animation with a sound of its own sounds at that sound's
frame; one without sounds at frame 0. The play comes after the overlays, which test and play their own
at their frames, all at the character's location (C+0x160). The lookup (0x516670) goes as follows:

1. The animation's type gives a kind and a modifier from the table at 0x557ad0 (12 bytes for each type
   below 0xe9: the type, the kind, -1 for none, and the modifier). Punches are kind 13 and kicks kind 8.
   No kind and no sound of its own: nothing.
2. A modifier of 0 comes from the animation's largest attack damage (`TRrAnimation_GetMaximumDamage`
   0x428900): 15 and over is heavy (3), 10 and over medium (4), above 0 light (5).
3. The variant is found in the list (a bsearch by address in the original).
4. If the animation has a sound of its own, it is that impulse. If no impulse has that name, it logs
   "failed to find impulse sound" and tries the parent variant.
5. Otherwise, unless the kind is "Animation", the kind's records are searched. A record of the modifier,
   or of any modifier (0), gives its impulse. The first one of the modifier ends the search; failing
   that, the last match of any modifier stands. With no impulse found, the kind "Any"'s records are
   searched the same way.
6. Still none: the parent variant (`ONCV` +0), until there is no parent.

The `TRAM`'s own per-frame sounds are separate. TRAM +0x30 holds records of 0x22 bytes: an impulse's
name (32 bytes) and a frame (u16 +0x20), counted by the u32 at +0x14c. The frame's record
(`TRrAnimation_GetNewSoundForFrame` 0x4287c0) plays by name (`OSrImpulse_PlayByName` 0x512500) after
the variant's sound. On the CD, 8 of Konoko's animations have one: her combination kicks (`BigKick`,
`TripleKick`) and some of her throws (`big_crack`, `small_crack`, `face_med`).
