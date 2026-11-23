# Game state

How the game moves between title, Land Select, a land, its exits, game over and the ending,
and which RAM holds that state. Status per entry follows [memory-map.md](memory-map.md):
**verified** = seen in a controlled run or read from unambiguous code; **hypothesis** = a
reading that still needs an experiment. Code addresses are bank 7 (fixed) unless a bank is
named. Map and room structure is in [levels.md](levels.md).

"Poke run" below means a controlled run: a save state taken from a route, patched with
`tools/world_poke.py`, then continued with `--load-state` (commands under
[Reproducing](#reproducing)).

## Control flow

The game runs as cooperative tasks (see [functions.md](functions.md)). Reset creates one task,
`$D4E6`; the scheduler resumes tasks whose wait expired, and the NMI drives the wait counters, so
game logic advances once per frame per task.

## Top-level flow of the main task

1. **Title** (`$D4E6`): draws the title, plays music 7, then loops at `$D5A3` reading new
   presses: Right/Left/Select change `$F9` (difficulty, wraps 0-2), Start leaves to Land Select.
2. **Land Select** (`$D630`): loads the screen by temporarily setting `$F6` to 5 (screen graphics,
   palette and music 5 come from the land-5 entries of the per-land tables), then restores `$F6`.
   - If the player just left a land (`$5D` non-zero) it waits one second and runs the bank
     transfer at `$D8D9`: the HUD money is counted down into TOTAL one unit at a time with a
     sound per step. If all five land treasures are then held (`$A0 & $1F = $1F`) it shows a
     message and goes straight to the final trip (step 6); otherwise it shows the land's result
     and waits for A.
   - The cursor starts on the first land whose treasure bit in `$A0` is clear. Up/Down add -1/+1
     to `$F6` with wrap-around (4 to 0 and 0 to 4) and repeat the step while the target land's
     bit is set, so completed lands cannot be selected. Start loads the land (`$D79B`).
3. **Land load** (`$D79B`): blanks the screen, applies the per-land tables (`$D9CA`, see
   [levels.md](levels.md#land-load)), loads graphics, draws the starting screens, sets up the
   object spawner (bank 6 `$AE20`/`$AE60`), puts Scrooge at his start, resets the HUD, clears
   `$70-$7F`, `$FB`, `$5D`, sets the HUD time from the land's start time and clears the
   "already collected" object bits for the land (bank 5 `$B760`), then enters the frame loop.
4. **Frame loop** (`$DB36`), once per frame: ending check (`$AC`, which replaces the rest of the
   loop); pause toggle; player and object update (bank 6 `$8000`); timer (`$E28C`); then in this
   order land left (`$5D`), boss trigger (`$CF93`, skipped during a boss fight), land redirect
   (`$A4`), death or warp (`$7E` bit 7), HUD money/total refresh (`$74`, `$AB`), special scene
   (`$78` bit 7); finally scrolling and object spawning (bank 6 `$AEA0`) and the boss-checkpoint
   test (`$DC04`).
5. **Exits** back to step 2: land finished or Launchpad ride (`$5D`), `$A4` negative, or a
   special scene with `$78` = `$82`.
6. **Final trip**: after the fifth treasure is banked the game loads Transylvania again
   (`$F6` = 1) without the cursor; the boss trigger in Transylvania then leads to the final
   arena (see [Boss](#boss)).
7. **Ending** (`$E1AF`) when `$AC` becomes non-zero; **game over** (`$DEB9`) ends in a jump to
   the reset vector.

## State variables

| Address | Meaning | Status | Evidence |
|---|---|---|---|
| `$F6` | Current land: 0 Amazon, 1 Transylvania, 2 African Mines, 3 Himalayas, 4 Moon (Land Select order). 5 while the Land Select screen loads and during the boss-arena scroll, 9 during the ending | verified | Values 0-4 at frame 1450 of the five intro routes and stable through play; Land Select code at `$D6EF-$D746` writes it; the African Mines redirect changes it 2 to 1 (frame 1365 of `african_mines_intro`, writer `$DB9D`); 5 seen in poke runs while Land Select loads; ending writes 9 at `$E1C6`. |
| `$C6/$C7` | **Not** the land: sound request ring indices | verified (refutes the candidate) | `$C7` is the NMI's read index (memory-map); both count requests, e.g. 02-06 at frame 500 in the five routes before any land loads, and African Mines read 07 at frame 1450 after its extra redirect. |
| `$C5` | Last music id requested through `$F752` (the land's music is its land number; 5 Land Select, 6 boss, 7 title, 9 game over, `$0A` death) | verified | `$F752` stores A in `$C5` before queueing it; `$C5` equals `$F6` in each land, 5 on Land Select, 6 after the boss trigger, `$0A` after a death in poke runs. |
| `$F1` | Map row of the current screen (0 = top row) | verified | Per-land start rows 5/6/0/0/4 from the land table; changes to the destination row on warps and respawns (poke runs `7E=$80-$86`); row arithmetic at `$E8B6`. See [levels.md](levels.md). |
| `$A0` | Treasures held: bit n (n = 0-4) = treasure of land n, bits 5 and 6 = two further treasures | verified (bits 0-4), hypothesis (meaning of bits 5-6) | Bank 6 `$8C22-$8C2E` ORs bit (object type - `$6F`) for object types `$6F` and above; Land Select skips lands whose bit is set (poke runs `A0=$01`, `$02`, `$10`, `$1D`); `$1F` triggers the final trip; ending variant 2 needs `$7F`. |
| `$A3` | Key items: bit 1 = the item African Mines asks for, bit 2 = item picked up in the Moon | verified (bit 1), hypothesis (bit 2) | Bank 6 `$8BE7` ORs 4 when picked up in land 4, else 2; with bit 1 set African Mines is not redirected (poke run `mines_key`), with it clear it is (`$EE19`, route `african_mines_intro`). |
| `$A4` | Land redirect request: positive = load Transylvania, negative = back to Land Select | verified | `$DB8F-$DBA6`; poke runs `A4=$01` (Transylvania loads, time reset to 500) and `A4=$80` (Land Select). Set by `$EE1F` (African Mines without the item), `$CFC9` and bank 6 `$9090`. |
| `$5D` | Land left: once set, the frame loop returns to Land Select, which banks the money | verified (effect), hypothesis (which events set it besides the Launchpad scene) | Poke run `5D=$01`: Land Select with the land's result and the money moved into TOTAL; set at frame 1367 of poke run `78=$81` at the end of the helicopter scene. Writers `$EFDF` (after a 60-frame wait), `$F8EF`, `$FA3C` (when an object carrying Scrooge has lifted him above Y `$10`); cleared at land load `$D84D`. |
| `$78` | Special scene request: bit 7 = start, low bits = scene (1 = Launchpad ride home, 2 = scene that returns to Land Select without banking) | verified (behaviour), hypothesis (scene names) | `$DF27` loads the scene from respawn-table entry `$10 + n`; poke run `78=$81`: helicopter scene, then Land Select showing the land's money without "clear"; `78=$82`: sky scene, then Land Select directly. `$EE16` sets `$81` unless B is newly pressed (hypothesis: the answer to Launchpad's offer). |
| `$A6` | Lands where the Launchpad ride was taken (bit = land) | hypothesis | `$EE0B-$EE14` sets the land's bit together with `$78=$81`; `$FE12-$FE19` skips the conversation object type `$33` when the bit is set. |
| `$7E` | Respawn/warp request: bit 7 = pending, low 5 bits = entry in the respawn table (`$DDC1`, see [levels.md](levels.md#respawn-and-warp-table)). `$87+` costs a life | verified | Poke runs `7E=$80-$86` move Scrooge to seven different rows/screens without losing a life; `$87` costs a life; pit death writes `$87 + land + $7C` at `$EBB4` (seen as `$87` in Amazon and `$8B` in the Moon routes). |
| `$7C` | Boss checkpoint: 5 while Scrooge is in his land's boss-checkpoint screen, else 0 | verified | `$DC04` sets it when (`$F1`, `$0730`) equals the land's checkpoint; poke run `F1=2 0730=1` in the Amazon sets it on the next frame. |
| `$FB` | Bit 7 set = boss fight (after the boss trigger); suppresses the trigger and makes a respawn replay music 6 | verified | Poke runs at each trigger position set it to `$FF` together with music 6; `$DB85`, `$DCE4`. |
| `$AC` | Ending: non-zero starts it, bit 7 = ending running | verified | Poke run `AC=$01`: the ending sequence plays. `$FDF2` increments it when an object's countdown (`$06B0`) reaches 0. |
| `$AD` | Ending variant 0-2 | verified | Written at `$E1E7`; poke runs below. |
| `$7B` | Final arena flag: 5 when the boss trigger leads to the final arena (all five treasures) | verified | `$CFE6`; poke run `A0=$1F` at Transylvania's trigger sets 5. Cleared by time-up (`$E2A3`). `$E878` reads it. |
| `$E6` | Pause: Start toggles it (sound `$15`); while set the frame loop skips game logic | verified (static) | `$DB46-$DB69`. |
| `$39` | Scrooge's control state (examples: 0 idle, `$0D` dying, `$0F` holding up an item or message, `$10` holding up a land treasure) | hypothesis (beyond the listed values) | `$0D` follows a pit death and time-up in every route; `$0F/$10` written by the pickup code at bank 6 `$8BF9`, `$8C37-$8C42`. |
| `$B0` | Frames until the HUD time drops by one (60) | verified | Reloaded with `$3C` at `$E2AA`; HUD digit changes are 60 frames apart (frames 954, 1014, 1074 in `amazon_intro`). |
| `$0151`, `$0155`, `$0159` | HUD TIME hundreds, tens, ones: tile ids of sprite records in the HUD group at `$0140-$015C` | verified | `$E2AE-$E2CA` decrements them; values 5,0,0 then 4,9,9 in `amazon_intro`. |
| `$0334-$033A` | TOTAL, seven decimal digits as tile ids (`$24` = blank leading zero), part of the update packet `$0330-$033C` (PPU `$206C`, length 8); the HUD shows a fixed trailing 0 (`$033B`) | verified | Poke run `5D=$01`: 4000 in money becomes digits 4,0,0 at `$0338-$033A`; `$D8D9` writes them. |
| `$0120-$013F` | Per-land "consumed object" bits: bit (k & 7) of byte k >> 3 for the land's object record k | verified | Spawner bank 6 `$AF2B` skips records whose bit is set; `$F7F0` sets bits, bank 6 `$8A7E` clears one; bank 5 `$B760` clears all at land load and pre-sets some from persistent flags; bits appear in `$0120`/`$012E` as objects are taken in the Himalayas and Moon routes. |
| `$A1`, `$A8-$AE` | Persistent progress flags read at land load to pre-set consumed-object bits (bank 5 `$B76A` table lists `$A0`, `$A1`, `$A3`, `$A6`, `$A8`, `$AE` per land). `$A8+`: bit per message id already shown (`$FE37-$FE4C`); `$AE`: set when the extra-HP object (type `$2B`) is taken in Transylvania (bank 6 `$8B38`) | hypothesis | Static reading only. |

## Land table values (from `$D9CA` and runs)

| Land | Start row | Start time | Music | Rows in map |
|---|---|---|---|---|
| 0 The Amazon | 5 | 500 | 0 | 7 |
| 1 Transylvania | 6 | 500 | 1 | 8 |
| 2 African Mines | 0 | 600 | 2 | 6 |
| 3 The Himalayas | 0 | 500 | 3 | 6 |
| 4 The Moon | 4 | 600 | 4 | 6 |

Start rows, times and music are what `world_state.py` shows on the first gameplay frames of the
five intro routes; row counts come from `$2A` in the same runs.

## Timer

The HUD time is three sprite tiles (`$0151/$0155/$0159`). `$E28C` runs every frame unless Scrooge
is dying (`$39 = $0D`) or holding a treasure (`$39 = $10`); every 60 frames it subtracts one with
decimal borrow. Below 30 seconds it plays sound `$36` every 16 frames. At 000 it writes the
tiles of the word shown in the time box when time runs out, and on the next frame sets
`$39 = $0D` (death) and clears `$7B`. A respawn after any death resets the time to the land's
start value (`$DCEE-$DD06`; only when the death music was requested and not during a boss
fight, where music 6 is requested instead). Verified by poke run
`timeup` (time set to 001): the time box shows the time-out word, Scrooge dies, a life is lost
and the time restarts at 500.

## Lives, death and game over

Scrooge dies by falling below Y `$D0` (`$EBAA`, writes `$7E = $87 + land + $7C`), by time-out or
by losing his last HP dot. `$DC3E` handles `$7E`: for `$87` and above it requests music `$0A`,
and if `$0361` (lives) is 0 it goes to game over; otherwise it decrements `$0361` and respawns at
the table entry (the land start, or the boss checkpoint when `$7C = 5`). Game over (`$DEB9`)
requests music 9, replaces the HUD time box with the game-over sprites, waits 240 frames and
jumps to the reset vector, so the title screen that follows is a full restart. Verified: poke run
`gameover` (`$0361 = 0`, `$7E = $87`) shows the game-over box and the title about 250 frames later;
`moon_intro` and `african_mines_easy` reach game over by play.

## Treasures, items and pickups

Pickups are objects whose type is in `$0620+n`; bank 6 `$8B21` dispatches on it when Scrooge
touches one:

- types `$23-$29` go through a jump table (bank 6 `$8B5A`, one sound per type): `$23` adds 2,000,
  `$24` restores one lost HP dot, `$25` only refreshes the HUD, `$26` starts a timed state in
  `$75-$77`, `$27` adds a life (capped at 9), `$28` adds 10,000, `$29` is the key-item handler that
  sets `$A3`;
- type `$2B`: adds an HP dot record to the HUD group at `$0340` (max HP up); in Transylvania it
  also sets `$AE`;
- type `$61`: adds 50,000;
- types `$6F-$76`: treasures; `$6F + n` is land n's treasure (Land Select shows the same ids as
  its icons, `$0622 = $6F + cursor`) and sets bit n of `$A0`; types from `$74` set bits 5 and up
  and show a message instead of the treasure pose.

The 2,000 item is verified by the existing money entry (gems add 2 to `$0327`) and the life
cap by the existing `$0361` entry; the other handler meanings come from static reading of
the digit position each handler adds to and are hypotheses until a route collects them.

## Land completion

Taking a land treasure sets its `$A0` bit and puts Scrooge in the treasure pose; one of the exit
routines then increments `$5D`. With `$5D` set the frame loop waits 180 frames and returns to
Land Select (`$DF08`), which banks the money (land money counted into TOTAL), shows the land's
result and, once all five bits are set, starts the final trip. The Land Select message comes from
bank 5 `$B800`: plain heading when nothing happened, "land clear" result when `$5D` is set and
`$78` is 0, a result without "clear" after the Launchpad ride (`$5D` and `$78` both set).
Verified by poke runs `5D=$01` and `78=$81`. The land-select skip of completed lands is verified
by poke runs on `$A0`.

## Boss

Each land has a boss trigger at a fixed row and screen (`$CF93`, table in
[levels.md](levels.md#boss-trigger-and-checkpoint)). When Scrooge reaches it:

- in Transylvania with its own treasure already taken and not all five held, `$A4 = $80` sends
  him back to Land Select;
- otherwise the camera scrolls into the arena while `$F6` is 5 and `$F1` is the land number
  (row 5 instead, and `$7B = 5`, when all five treasures are held: the final arena), then the
  land and row are restored, music 6 starts and `$FB = $FF` marks the fight.

Dying during the fight respawns at the boss checkpoint and replays music 6. Verified by poke
runs placing Scrooge on the trigger (Amazon, Transylvania with `$A0` = 0, `$02` and `$1F`). Boss
hit points and the boss's own state were not located.

## Ending

`$E1AF` runs while `$AC` is non-zero. On the first frame it sets bit 7, moves the money into
TOTAL, waits 120 frames, requests music 5, sets `$F6 = 9` and picks the variant:

| Variant (`$AD`) | Condition | Observed in poke runs |
|---|---|---|
| 2 | `$A0 & $7F = $7F` and TOTAL has 8 significant digits (`$0334` not blank, i.e. 10,000,000 or more) | Scrooge on a large pile of money, then a newspaper page about extra treasures |
| 1 | otherwise, TOTAL is zero (`$033A` blank) | Scrooge alone with no treasure, then a newspaper page about a lost fortune |
| 0 | otherwise | Scrooge beside a treasure chest, then a newspaper page about the richest duck |

After the first scene the sequence continues with scenes 4 and 5 and then stops in an endless
wait (static reading of `$E269-$E289`; the newspaper page seen in each run differs by variant). Poke runs: `AC=1` alone (4000 in money, variant 0), `AC=1 A0=$7F 0334=1` (variant 2), `AC=1`
with every money digit blank (variant 1).

## Reproducing

```sh
E=build/headless/Release/ducktales.exe; ROM=roms/baserom.nes
mkdir -p captures && echo "0 -" > captures/idle.txt
# base states: after the title Start press, and 1100 frames into the Amazon
$E "$ROM" --frames 1101 --input tests/routes/amazon_intro.txt \
   --save-state 201:captures/s_201.state --save-state 1100:captures/s_amazon1100.state
# example: completed-land mask, then land exit and ending variant 2
py -3 -I tools/world_poke.py captures/s_201.state poke captures/a0.state A0=02
$E "$ROM" --load-state captures/a0.state --frames 1000 --input tests/routes/transylvania_intro.txt \
   --frame-log captures/a0.bin --frame-log-frames 300:1000
py -3 -I tools/world_state.py captures/a0.bin --changes   # land goes 0 -> 2 on the first Down
py -3 -I tools/world_poke.py captures/s_amazon1100.state poke captures/end.state AC=01 A0=7F 0334=01
$E "$ROM" --load-state captures/end.state --frames 2600 --input captures/idle.txt \
   --screenshot captures/end.png --shot-every 300
```

The other poke runs use the same pattern with the assignments named above.

## Not yet located

- Boss hit points and boss behaviour state; which object types are bosses.
- The writer of `$5E` (Land Select reads it as a message index and as a "go to the final trip"
  shortcut; no direct writer was found) and the use of `$7B` after the final trip starts (`$D6DF`
  increments it but the land load clears `$70-$7F` right after).
- Names of the items behind `$A3` bit 2, `$A1` and `$A8+`, and of scenes `$78` = 1 and 2 beyond the
  observed screens.
- The ending's variant conditions were tested by poking; no route reaches the ending by play.
