# Levels

How a land is laid out, how the camera and Scrooge's world position are kept, and how the
game loads, scrolls and leaves rooms. Status words as in [memory-map.md](memory-map.md); code
addresses are bank 7 unless a bank is named. Game-wide state (lands, exits, timer, ending) is in
[game-state.md](game-state.md).

## Verified basics

- Land Select lists five lands in this order: The Amazon, Transylvania, African Mines,
  The Himalayas, The Moon; the land number in `$F6` follows that order (0-4).
- At the start of The Amazon, holding Left moves Scrooge's screen X (`$0650`) from `$80` to
  `$44` without the camera moving.

## Map structure

Each land is a grid of 256-pixel-wide screens: rows top to bottom, 8 columns per row. Row
count, row stride and a pointer to a byte grid (one screen id per cell, `$FF` = no screen) are
loaded by `$C7B0` from tables in the fixed bank, indexed by map number (0-4 = the lands, 5-9 =
arena and scene maps of 1 to 6 screens). `tools/world_maps.py ROM` prints every grid from the
user's ROM.

| Address | Meaning | Status | Evidence |
|---|---|---|---|
| `$2A` | Rows in the current map (Amazon 7, Transylvania 8, the other lands 6) | verified | Values at frame 1000 of the five intro routes equal the `$C7B0` table; `$E8BC` refuses a row change at or past it. |
| `$2B` | Row stride, 8 for every land | verified | Same runs; added to or subtracted from `$2D` on a row change (`$E89D-$E8C8`). |
| `$20/$21` | Pointer to the current map's screen grid | verified | Same runs (pointers into the fixed bank); read as `($20),Y` with Y = screen index at `$E8B0` and `$DDA2`. |
| `$F1` | Current row | verified | See [game-state.md](game-state.md#state-variables). |
| `$2D` | Screen index of the camera: row x 8 + camera page (`$F3`) | verified | Matches `$F1 * 8 + $F3` on every sampled frame of the five intro routes (e.g. Amazon row 5 gives `$28`/`$29`, Moon row 4 gives `$20`); respawn entries store it directly. |
| `$24` | Screen index of the column being streamed in at the screen edge | hypothesis | One more than `$2D` while scrolling right, one less after a left scroll; used as the map index when drawing columns (`$DDA0`). |
| `$26` | Column within that screen, 0-`$3F` | hypothesis | Wraps at `$40` and then advances `$24` (`$DDAF-$DDBB`); advanced by `$20` while the camera moved 129 pixels in `amazon_intro` (so about 4 pixels per step). |
| `$2C` | `$24 & 7` (screen column of the streamed column) | verified (static) | `$DD73`, `$DDBB`. |
| `$2E` | Camera movement this frame in pixels | hypothesis | `$C7C5` computes it from `$F2 - $25`. |
| `$25` | Camera X low byte of the previous frame | verified | Copied from `$F2` every frame at `$DBE4`; equal to `$F2` in the sampled frames. |

## Camera and Scrooge's world position

| Address | Meaning | Status | Evidence |
|---|---|---|---|
| `$F2` | Camera X within the page (low byte of the 16-bit camera X) | verified | In 5,201 of 5,250 gameplay frames of the five intro routes `$0650 = ($0730:$0720) - ($F3:$F2)`; the 49 exceptions are transition frames. Also written to `$2005` after the HUD split (rendering.md). |
| `$F3` | Camera page (high byte of camera X, 0-7); bit 0 also selects the nametable after the split | verified | Same check; `$2D & 7 = $F3` in the same frames. |
| `$0720` | Scrooge's world X, low byte (column `$0720+n` holds every object's) | verified | Same identity; set from the respawn table and land table at load. |
| `$0730` | Scrooge's world X page (column `$0730+n` for objects) | verified | Same identity; compared with the boss-checkpoint and trigger tables. |
| `$0640` | Scrooge's Y on screen (no separate world Y: the row in `$F1` is the vertical position) | verified | memory-map; pit death below `$D0`, row change below `$E0` or above `$39` on a rope. |

The camera keeps Scrooge at screen X `$80` while it can scroll (`$0650 = $80` on most sampled
frames). Vertical movement between rows is a whole-screen scroll, not a camera coordinate.

## Land load

`$D79B` loads a land; `$D9CA` applies the per-land tables indexed by `$F6` (entries 0-4 lands,
5 the Land Select screen, 6-8 scenes):

- music request = land number (`$F752`, so `$C5 = $F6`);
- `$F7` (hypothesis: graphics set, values 1-3 for the lands);
- start row `$F1` and the start screen column in `$2D`, `$F3`, `$2C` and `$0730`;
- Scrooge's start X low (`$0720 = $80` for every land);
- the land's 16 background palette bytes into `$0300` (copy at `$0100`), followed by 16
  sprite palette bytes shared by every land at `$0310` (copy at `$0110`).

Then it loads graphics (`$C301` with X = land), streams the current row's sprite CHR (`$C42C` until `$50`
goes negative; see [rendering.md](rendering.md)), points the object spawner at the land's records (bank 6 `$AE20`: ten bytes `$60-$69`
= five pointers) and finds the first record for the start row (bank 6 `$AE60`), then sets the
HUD, the start time and the consumed-object bits (bank 5 `$B760`).

## Object placement and spawning

Per land, five parallel lists hold the land's object records sorted by world X: X page
(`($60)`), X low (`($62)`), the first record of each row (`($64)`, indexed by `$F1`), Y
(`($66)`) and a type index (`($68)`) into a two-byte template (flags/parameter nibbles into
`$0700/$0600` and `$06B0`, type into `$0620`). Every frame bank 6 `$AEA0` moves two window
indices (`$4C` right edge, `$4D` left edge) across the list as the camera moves and spawns a
record into a free slot (`$F7BE`) unless its bit in `$0120-$013F` is set or a live slot already
holds it (`$07F0+n` = record index of slot n). `$4E` = first record of the row. Status:
hypothesis for the field meanings (static reading of bank 6 `$AE20-$AFB0`), verified that the
pointers change per land (`$60-$69` differ between the five routes and stay constant within a
land).

## Horizontal scrolling

The camera follows Scrooge horizontally inside a row; new map columns are drawn at the edge
from the screen whose index is `$24`. The spawner adds objects as their X enters the window.
Pages run 0-7; where the next cell of the row is `$FF` the row ends (hypothesis: the camera
clamps there; not exercised).

## Row changes (vertical transitions)

`$E88C` changes the row when Scrooge leaves the screen vertically: by falling below Y `$E0`
(`$E711`) or by climbing a rope past Y `$39` (up) or `$E0` (down) (`$E806-$E86A`). It only acts
when the camera sits exactly on a page (`$F2 = 0`) whose number is odd, and only if the map cell
above/below (`$2D -/+ $2B`) is not `$FF` and the new row is inside `$2A`. It then sets
`$F1 -/+ 1` and `$2D`, and runs the vertical screen scroll (`$C976`). Status: static reading;
no route has changed rows yet, and poking Scrooge's position did not trigger it (the collision
map follows the drawn screen). The odd-page rule is consistent with the maps: single-screen
vertical shafts (Transylvania rows 0-1, Amazon row 4, Himalayas row 1) sit in odd columns.

## Respawn and warp table

`$DD4E` places Scrooge from a 9-byte entry of the table at `$DDC1`, index = `$7E & $1F` (or
`$10 + scene` for `$78` scenes). An entry sets row `$F1`, camera `$F2/$F3`, the column count to
draw, `$24`, `$26`, Scrooge's Y and world X, and `$2D`, then draws the visible columns.

| Entries | Used for | Status | Evidence |
|---|---|---|---|
| 0-6 | Warps: `$7E = $80 + n`, written by warp objects (bank 6 `$9065`, parameter in `$06B0`); 5 loads its screens from map 5 and enters the boss state | verified (placement), hypothesis (which objects) | Poke runs `7E=$80`-`$86` move Scrooge to rows 7, 6, 4, 3, 6, 1, 2 at different pages without losing a life; `$85` sets `$FB = $FF` and requests music 6. |
| 7-11 | Land start for lands 0-4 (death respawn) | verified | Pit deaths respawn at the land start (Amazon `$87`, Moon `$8B`); same row/page as the land table. |
| 12-16 | Boss checkpoint for lands 0-4 (death respawn while `$7C = 5`) | verified (selection), hypothesis (in-game location) | `$EBB4` adds `$7C` to the index; `$DC04` sets `$7C` from tables with the same rows and pages. |
| 17, 18 | Scenes `$78` = 1 and 2 | verified | Poke runs `78=$81`, `78=$82`. |

## Boss trigger and checkpoint

| Land | Boss checkpoint (row, page) | Boss trigger (row, page, scroll) |
|---|---|---|
| The Amazon | 2, 1 | 1, 4, left |
| Transylvania | 4, 5 | 3, 5, left |
| African Mines | 3, 5 | 2, 5, left |
| The Himalayas | 4, 7 | 2, 4, right |
| The Moon | 1, 2 | 5, 4, left |

The checkpoint is the screen that sets `$7C = 5` (`$DC04`); the trigger (`$CF93`) fires when
`$F1` and Scrooge's X page (taken 8 pixels ahead or behind, per land) match, then moves the
camera one pixel per frame in the listed direction until it is page-aligned (`$F2 = 0`), drawing
the new columns from map 5 at row = land (row 5 for the final arena). Verified: the trigger
fires in poke runs placing Scrooge on it in the Amazon and Transylvania, where `$F2` falls
from `$80` to 0 over about 130 frames; the checkpoint in the Amazon. The other lands' rows,
pages and directions are read from the same tables.

## African Mines and Transylvania

African Mines checks for the key item (`$A3` bit 1) at `$EE19` after a message; without it
`$A4 = 1` reloads the game in Transylvania (time and position reset; money kept). Verified by
`african_mines_intro` (redirect at frame 1365) and poke run `mines_key` (no redirect with the
bit set). Transylvania is also the land of the final trip (see game-state.md).

## Reproducing

```sh
py -3 -I tools/world_maps.py roms/baserom.nes
build/headless/Release/ducktales.exe roms/baserom.nes --frames 2000 \
   --input tests/routes/african_mines_intro.txt --frame-log captures/am.bin --frame-log-frames 900:2000
py -3 -I tools/world_state.py captures/am.bin --changes
# warp entry 3 from the Amazon
py -3 -I tools/world_poke.py captures/s_amazon1100.state poke captures/w3.state 7E=83
build/headless/Release/ducktales.exe roms/baserom.nes --load-state captures/w3.state \
   --frames 1400 --input captures/idle.txt --frame-log captures/w3.bin
py -3 -I tools/world_state.py captures/w3.bin --changes
```

(`captures/s_amazon1100.state` and `captures/idle.txt` as in
[game-state.md](game-state.md#reproducing).)

## Not yet known

- The screen id format is now documented in [rendering.md](rendering.md) (`$CE2C`, `$C830`,
  `$CCBD`).
- A route that changes rows by play, to verify the vertical transition and the odd-page rule.
- Which in-game objects are warps 0-6 and which carry each entry.
- Horizontal limits of a row and how the camera clamps.
