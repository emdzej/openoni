# The cheat codes

Once the game has been won (persist.dat's flags bit 2, `ONrPersist_MarkWonGame` 0x50f620, read by
0x50f660), letters typed on the pause screen switch the game's cheats. Ported in `src/cheat.c`.

## Typing them

`ONiRunGame` 0x4d4640 sets the input layer's event hook (0x55fd48, by 0x404df0) to
`ONrGameState_HandleCheats` 0x4f6110 while the pause screen is up (`ONrPauseScreen_Display` 0x5018c0),
and clears it after. The LI's event loop (0x403eb0) hands the hook every event it takes from the queue.

HandleCheats takes only key events of type 9 whose key (the event's +8, a u16) is a lower-case letter,
'a' to 'z'. It keeps the last 20 at 0x5ecea4 (a 21-byte buffer, the newest at index 19): the 19 older
ones move down a place (0x51f8c0, a memmove) and the new one goes last. Then for each start in the
window, 0 to 19, and each entry of the code table, `UUrString_HasPrefix` 0x4265d0 asks whether the
window from that start begins with the code. On a match the cheat is switched (`ONrCheater` 0x4f5c30),
the entry's message for the cheat's new state is set (0x503030 into 0x5ed00c; none when the entry has
no such message) and the window is emptied.

The message is drawn by the pause screen's dialog in place of its item 0x6f (the sub area), at the
pen (0, the font's ascent). The pause screen clears it as it closes, and `ONiInGameUI_LevelLoad`
0x503eb0 at the level's load.

## The table

At 0x5533f8, 16 bytes an entry, ended by a null code: the code's string, the message when the cheat
goes on, the one when it goes off (null for the one-shot cheats), the cheat's id. 21 entries, ids 0 to
21 without 11 (the developer keys, which `ONrCheater` still knows). The port reads it from the exe.

## The cheats (ONrCheater)

Without a player character, nothing happens and the result is "off". Each toggles, and its result
picks the message:

| Id | Code | What it does | Port |
|---|---|---|---|
| 0 | shapeshifter | 0x5ece6f: debug key 0x29 (LI 0x88 with a modifier) steps the player's class through every ONCC (0x4f6200) | the switch only |
| 1 | liveforever | 0x5ece92, invincible | yes |
| 2 | touchofdeath | 0x5ece93, omnipotent | yes |
| 3 | canttouchthis | 0x5ece91, unstoppable | yes |
| 4 | fatloot | the player's C+0x1a2, 0x1a6, 0x1a4 (clips, cells, hypos) to 6; always "on" | yes |
| 5 | glassworld | 0x5e96b2, `p3_furniture_breakable` | yes |
| 6, 7 | winlevel, loselevel | G+0x1679bc to 1 (won) or 2 (lost) | yes |
| 8 | bighead | 0x5ecb98, `chr_big_head` | yes |
| 9 | minime | the scale C+0x34: under 0.75 back to 1 ("off"), else 0.25 | yes |
| 10 | superammo | C+4 bit 0x40 (no ammo used) | yes |
| 12 | reservoirdogs | 0x5eb848: `AI2rTeam_FriendOrFoe` 0x48b2c0 answers 2 (foes) for any two teams | yes |
| 13 | roughjustice | 0x620133: the player's shots wait the shooter's +0x48 instead of +0x46 (`WPiPulseShooter` 0x519c50, also 0x5192b0 and `WPrOutOfAmmoSound` 0x51d080), the kick scaled by +0x48 / +0x46 | the pulse's delay and kick |
| 14 | chenille | C+0x1664 (the super value) at most 1: to 10 and C+4 bit 0x40000000; else both off | yes |
| 15 | behemoth | 0x5ece90 (unthrowable, and the reactions of 0x4e7740 as for a class with ONCC+0xc8f); the scale 1.8, or back to a random one in the class's range (ONCC+0xc58..0xc5c, `UUrRandom`) | unthrowable and the size, not the reactions |
| 16 | elderrune | 0x620134: with no hypo healing, the player under the maximum gains a point on each game tick that is a multiple of 60 (`WPrInventory_Update` 0x51bad0) | yes |
| 17 | moonshadow | the invisibility C+0x1ba: 0xffff (for good), or 0 | yes |
| 18 | munitionfrenzy | every ONWC (`TMrInstance_GetDataPtr_List`) as a new weapon, the i-th of n at the angle 2π i / n on a circle of 8 about the player, 10 up, facing along it (the angle + π/2) | yes |
| 19 | fistsoflegend | 0x5ece8f: the player's blows land with the reaction 0xd6 (0x4e7740) | yes |
| 20 | killmequick | 0x5eb849: the melee AI chooses again whatever it is doing (`AI2rMelee_Update` 0x4a01f0) and skips a maneuver of type 4 (0x4a6f20) | the switch only |
| 21 | carousel | G+0x104: `ONrGameState_UpdateServerTime` 0x4fbeb0 takes two of the machine's 60ths a tick (as with G+0x108 above 0) | a tick every other frame |

`ONrCharacter_LevelBegin` 0x4d87b0 clears 0x5ece90 to 0x5ece93 and 0x5ece8f at each level's start; the
rest last.
