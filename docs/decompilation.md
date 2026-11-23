# Decompilation layer

The port's first step from "recompiled 6502" towards readable C. It has three parts, all in the
port's own files and all switched off by default:

- **The game API** (`include/ducktales/`): typed names for the game's RAM (objects, Scrooge, HUD,
  game-wide state) and RAM transactions to read and change it.
- **Game events** (`include/ducktales/events.h`, `src/game/events.c`): verified code sites where mod
  code can react to, or cancel, something the game does. The enhancements are handlers of these
  events.
- **Decompiled routines** (`src/decomp/`): Scrooge's per-frame code, shared motion and sound
  primitives, and parts of the frame loop and HUD, written as C. The cycle host can check them
  against the original or run them in its place. Flow tasks can hand control back to recompiled
  code and resume in C when the task resumes, without a hook at each resume point.

Addresses are named only in `src/game/ducktales_symbols.h`. Status words (verified, code,
hypothesis) are used as in [memory-map.md](memory-map.md). Code addresses are in bank 7 (the fixed
bank).

## The game API

`#include "ducktales/ducktales.h"` gives all of it:

| Header | Contents |
|---|---|
| `ram.h` | `gRam`, `RAM_U8`, `Ram_GetU16`/`Ram_SetU16`, the transactions, and `Game_ReadRom` for tables the game reads at run time |
| `objects.h` | `gObjects`: the 16-slot object tables at `$0600-$07FF` as a structure of arrays (`gObjects->y[slot]` is `$0640,X`), with 16-bit helpers for speed, velocity and world X |
| `player.h` | `PLAYER_SLOT`, `PlayerMode` (`$39`), `PlayerAction` (`$0620`) and Scrooge's zero-page variables |
| `hud.h` | HP dots (sprite records at `$0340`), lives, money and time digits |
| `game.h` | difficulty, land, map and camera, timer, bosses, progress flags, controller bytes |
| `events.h` | the events below |

Every accessor is an lvalue in `gRam`, the RAM image of the open transaction:

- `GameRam_Begin()`/`GameRam_End()` open a transaction on the running machine. The image starts as
  a copy of CPU RAM, and at the end every byte that changed is stored with `cyc_mod_poke`, through
  the compiled-code write watch, as a CPU store would. A nested `Begin` joins the open transaction.
- `GameRam_Enter(image)`/`GameRam_Leave()` point `gRam` at a private image (a decompiled routine
  in progress) and store nothing.

Outside a transaction `gRam` is `NULL`, so a stray access fails at once instead of reading stale
memory. Rows and variables whose meaning is a hypothesis say so in their comment, and unknown object
rows keep their address as their name (`unk_0690`).

## Events

Each event owns one `[[mod_function_hook]]` site (`config/game.toml`, with a `length`/`crc32`
content key). A handler registered with `Events_On(EVENT_..., fn, user)` runs inside a RAM
transaction at that site, before the site's instruction. **While an event has no handler its site's
hook is disabled**, so the generated code runs the original instruction with nothing changed. A
cancelable event is cancelled when any handler returns `EVENT_CANCEL`. All handlers run, in
registration order, even after one cancels.

| Event | Site | When | Cancel | Used by |
|---|---|---|---|---|
| `EVENT_PLAYER_DAMAGED` | `$EB5E`, entry of `damage` | a hit is about to cost an HP dot | the routine returns as its RTS would: no dot is lost and the Easy hit counter `$5F` stays; the knock-back still happens | NoDamage |
| `EVENT_LIFE_LOST` | `$DCB0`, in `death_or_warp` (`$DC3E`) | a death is about to cost a life | one life is added before the decrement | InfiniteLives |
| `EVENT_TIMER_TICK` | `$E2A6`, in `timer_tick` (`$E28C`) | a frame of the HUD timer passes | `$B0` is reloaded with 60 before the decrement, so the HUD second never passes | InfiniteTime |
| `EVENT_BOSS_HIT` | `$F6DD`, in the boss hit routine `$F6D7` | a hit is about to count against a boss | one hit is added to `$B1` before the decrement (the boss still flashes) | none yet |
| `EVENT_NEW_GAME` | `$D59C`, title set-up | the HUD template was copied (lives = 2) | not cancelable | StartLives |
| `EVENT_JUMP_START` | `$E3FE`, ground handler | the take-off velocity was copied into Scrooge's | not cancelable | JumpScale |

Evidence for each site:

- `$EB5E`, `$E2A6`, `$DCB0` and `$D59C` are the enhancement sites. Their evidence is under
  "Enhancements" in [architecture.md](architecture.md) (verified).
- `$E3FE` is reached only by falling through from the two velocity stores at `$E3F5`/`$E3FB`.
  See "Jump experiment" in [architecture.md](architecture.md) (verified).
- `$F6DD` is the `DEC $B1` of the boss hit routine `$F6D7`. That routine is called by the boss
  objects of bank 6 and by `$FA44`, and it reaches the decrement only when the boss's cooldown
  `$06F0,X` is 0, that is, for a hit that counts. `$B1` holds the hits left (verified for Amazon and
  Himalayas in [game-state.md](game-state.md)). With `--dt-events-log`, `full_amazon` raises
  `boss-hit` at frames 7093, 7285, 7645, 7933 and 8173, exactly the frames where `$B1` drops from 5
  to 0 (verified). The cancel's effect comes from the code alone: no enhancement cancels it yet.

`--dt-events-log FILE` (developer builds) registers a handler on every event that changes nothing
and writes one line per raised event.

**Events raised by decompiled code.** `$EB5E` is also the entry of the decompiled
`Player_TakeDamage`, and `$E3FE` lies inside the decompiled `Player_StartJump`. When a decompiled
routine runs instead of the original, the original instruction never runs, so the C raises those
two events itself (`Decomp_RaiseEvent`, replace mode only) at the same point in its logic.
`tests/decomp_test.py --modes events` checks NoDamage and JumpScale with the decompiled routines on.

## Decompiled routines

### How a routine replaces the recompiled one

A decompiled routine that original code calls owns a `[[mod_function_hook]]` site at its entry,
with id `ducktales.decomp.XXXX` and a content key over the routine's first 8 bytes. A routine that
only decompiled code calls has no site: its callers' C calls its C directly (below).
`src/decomp/decomp.c` registers the sites and runs the routines in one of three modes. The mode comes from `--decomp
off|check|replace` in both hosts, or from the environment variable `DUCKTALES_DECOMP` when the
option is not given.

| Mode | What runs | Machine |
|---|---|---|
| `off` (default) | no site is enabled; the recompiled code runs | exactly the console's |
| `check` | at every call the original runs alone in an isolated call (`cyc_mod.h`), the C runs on the same state, and their results must agree; then the original runs for real | exactly the console's; the first difference stops the program with exit code 3 and prints it |
| `replace` | the C runs instead of the original | the C's RAM and registers, with the console's timing (below) |

The C of a routine always runs on a private image of CPU RAM taken at the routine's entry. In check
mode "agree" means: all of CPU RAM except the stack below the routine's return address (dead after
its RTS), plus the registers of the routine's contract. The stack ends at `$0161`
(`DT_ADDR_STACK_FLOOR`): below it page 1 holds the master palette, the consumed-record bits
`$0120-$013F` and the HUD time group, which the game-over box replaces up to its end mark at `$0160`; they
are always compared and stored. (Treating them as dead
stack dropped an object routine's writes to `$0120` in replace mode, and `amazon_intro` diverged at
frame 1630; found on the objects branch. With the floor at `$0160`, replace mode dropped the game-over
box's end mark and the sprites of its next frame differed; found on the flow branch.)

**Timing in replace mode.** A hook that handles a routine returns in zero guest time, and the
NESRecomp API has no call that charges cycles. A replaced routine that takes no time makes the game
faster than the console: on a frame where the console lags (`full_amazon` frame 446, for example)
the game no longer lags, and from then on everything runs one frame early. To keep the console's
timing, replace mode also runs the original routine in an isolated call, only to learn its cycle
count from the same state. After storing the C's results, it spends that many CPU cycles as reads
of the routine's first byte (`cpu_read`, with every device clocked), polling interrupts in the
last one as the RTS would. So interrupts and frame boundaries come at the same points of the game's
work as on the console.

**Bus operations.** A routine that writes the PPU or the mapper does it through
`Decomp_BusWrite(addr, value)`, which records the write (the C's own view of the ROM banks follows
mapper writes at once). Replace mode makes the recorded writes on the machine spread evenly among
those reads, and check mode makes them the same way on an isolated copy of the machine at entry and
compares CIRAM, palette RAM, CHR RAM and the PRG bank with the original's (contract `OUT_HW`). The
spacing matters: the PPU acts on a `$2006` or `$2007` write a few cycles late, so writes made back to
back put every byte of a VRAM queue one address off. That left 84 nametable bytes wrong from
`african_mines_intro` frame 221, which moved the sprite-0 split and showed in RAM only at frame
1383 (found on the level branch). The one difference: an NMI that the original would take in the middle of a
routine is taken right after it, so the bytes it pushes on the stack differ. With this, replace
mode is RAM-identical to the console on every frame of every route (see Verification).

**Calls that would run past the frame end** go to the original in replace mode. The host's
`frame_end` calls `Decomp_FrameEnd()`, which keeps the frame length in cycles; when the original's
measured cycles from the call would reach the next frame end (with a margin of 32), the call is
counted as "original" and the original runs. Otherwise the RAM at that frame end would hold the
C's finished results where the console is still inside the routine, as with the level's blanked
whole-screen draws (found on the level branch).

The measuring call runs with hook sites on, so it takes the C's path through a cancelled event:
an event site raises nothing there, and at `$EB5E` the original returns as its RTS would whenever
the C's `EVENT_PLAYER_DAMAGED` was cancelled. Without this, NoDamage in replace mode was charged
the HP-dot work it skips, and `moon_intro` drifted from the console at frame 1088.

**Sites only where original code calls.** NESRecomp registers at most 128 hook plugins, one address
each (`NES_MOD_MAX_FUNCTION_HOOKS` in `runner/src/mod_function_hooks.c`), so one site per routine
would not last the whole game. A routine has a site (`site_id` in its table entry) only if
original code reaches it; `tools/decomp_callers.py` lists, from a build's generated code, the
instructions outside the decompiled ranges that jump to an entry. Of the 50 routines in the player,
motion, sound, level and collision tables, 25 have
one: `Player_Update` (the frame loop's `JSR` at `$DB6C`), `Player_PitDeath` (the original
mode 15 and 16 code jumps to it at `$EDCC`), `Player_RowChange` (the original `$E878` falls into
it), the four motion primitives and the two sound requests
(object and game code everywhere), `Player_TakeDamage`, kept for replace mode's measuring call
above, and 15 of the 18 camera, scrolling, level and background routines (all but
`Level_ScreenPointer`, `Level_DecodeBlock` and `Level_MetatileAt`, which only decompiled code
calls). As more of the game is decompiled the boundary moves out and sites go away.

Check mode still compares every call of every routine. The C of each routine starts with
`Decomp_Enter(addr, a, x, y)`, giving the registers the original gets (0 for those the C does not
model). When another routine's C calls a routine that has no site, check mode first runs the routine's C on a copy
of the caller's image, with checks of its own calls off (its real run makes them), then the original
in an isolated call on the caller's image, entered as a `JSR` at the caller's entry S would, and
compares the two exactly as a site does. A mismatch names the calling routine. A routine with a
site is checked at its site instead, when the caller's original runs for real after its check. Replace mode and the
site's own call skip it. A routine whose C does not start with `Decomp_Enter` stops the program. When a
call is given to the original (`Decomp_RunOriginal`), the routines it reaches run as the original
too and are not counted.

**Code that is not decompiled yet** stays original in two ways:

- `Decomp_Call(addr, a, x, y)` runs an original routine from the C, in an isolated call on the
  C's image, and returns its registers. The image takes the call's RAM changes back.
- `Decomp_RunOriginal()` gives the whole call to the original routine: the C's changes are dropped
  and the original runs from its entry. It is used for paths into code that waits for the hardware
  (a row scroll, a message box), which an isolated call cannot run. Those calls are counted as
  "original" and are not compared.

### Calling contract

The adapter of each routine (`Run_*` in the C files, used at its site and by the inner check) maps
registers to C arguments and back. "Out" lists the registers that callers read after the routine
returns. Check mode compares exactly those, and replace mode sets them. P means N, V, Z and C, and
C the carry alone; I and D are left as they were at entry. Every routine also leaves its RAM
effects, scratch bytes `$00-$0F` included, exactly as the original does. That includes values
later code uses as pointers: the background probe, for one, must
leave the jump vector in `$00/$01` as the original's loop does.

| Routine | In | Out | Notes |
|---|---|---|---|
| `Obj_ApplyVelocityY`, `Obj_ApplyGravity`, `Obj_MoveRight`, `Obj_MoveLeft`, `Obj_AddVelocityY`, `Obj_SubVelocityY` | X = slot | A, X, Y, P | callers test the flags of the last `ADC`/`SBC` (`$F298` keeps the Y move's carry across the probe) |
| `Music_Request`, `Sound_Request` | A = id, X | A, X, Y, P | the caller's X is left in `$00` and the id in `$01` |
| `Player_Update` | X | none | X goes into the power-up music request; modes 15 and 16 run as the original |
| `Player_Walk`, `Player_Knockback` | X | none | X is the slot the push of `$73` moves when no direction is held, and what the sound request keeps in `$00` |
| `Player_MoveRight`, `Player_MoveLeft`, `Player_Fall`, `Player_Rise`, `Player_ProbeVertical` | X (moves only) | X | X is what the probe or contact routine returned; callers pass it on |
| `Player_ContactH` | none | X, C | X is the slot touched (0 for none, carry set); the cane swing reads its kind |
| `Player_ContactV` | none | C | carry clear for a contact |
| `Player_SelectRefill` | none | X | X goes on to `Player_Walk` |
| `Player_RowChange` | A = row, X = 0 or 2, Y = camera screen, `$00` | hand-off | task code: pops `Player_Update`'s return address and hands off to the row scroll `$C976`, returning to `$E8DF` |
| every other `Player_*` | none | none | |
| `Camera_Update` | X | X, hardware | the frame loop keeps X past `$CF93` into the code after it: X is the entry X when the camera stays, else what the column streaming leaves |
| `Scroll_StreamBlocks` | none | X, hardware | X is the last step's collision map index (or the screen id >> 5 when its screen is missing), or where the upload stopped when the screen is blanked |
| `Vram_FlushQueue` | none | X, hardware | X is the queue index where the upload stopped |
| `Level_ScreenPointer` | Y = grid cell | X | X = screen id >> 5 |
| `Level_DecodeBlock` | none | X | X is the block's collision map index |
| `Level_MetatileAt` | X = probe slot | A, X | |
| `Map_LoadHeader` | X = map | X | X is kept |
| `Bg_Probe` | X = slot | X, hardware | X is kept |
| `Bg_SnapRight`, `Bg_SnapLeft`, `Bg_SnapHead`, `Bg_SnapFeet` | X = slot | X | X is kept |
| `Level_TileEdit` | X = slot | hardware | |
| `Level_DrawStartColumns`, `Level_DrawScreen`, `Vram_FlushTileEdits`, `Chr_StreamRow` | none | hardware | |
| `Boss_CheckTrigger` | none | none | the arena start runs as the original (`Decomp_RunOriginal`): it scrolls the row |

### Routines

"Routes" is the number of the 12 routes in `tests/routes/` where check mode compared the routine at
least once. "Pokes" names the `tests/decomp_test.py` cases that reach it from a poked state. The
last column lists the branches of the C that no route and no poke case executed (measured with a
clang coverage build, below). Status: **checked** means compared equal on every call reached, and
RAM-identical in replace mode.

| Routine | Address | Status | Routes | Pokes | Branches never executed |
|---|---|---|---|---|---|
| `Obj_ApplyVelocityY` | `$F2A1` | checked | 12 | - | none |
| `Obj_ApplyGravity` | `$F2B5` | checked | 12 | - | none |
| `Obj_MoveRight` | `$F234` | checked | 11 | - | none |
| `Obj_MoveLeft` | `$F252` | checked | 12 | - | none |
| `Obj_AddVelocityY` | `$F270` | checked | 9 | - | not measured |
| `Obj_SubVelocityY` | `$F284` | checked | 7 | - | not measured |
| `Music_Request` | `$F752` | checked | 12 | - | none |
| `Sound_Request` | `$F754` | checked | 12 | - | a request dropped because its ring entry is still full |
| `Player_Update` | `$E354` | checked | 12 | invulnerable-held | none (modes 15 and 16 run as the original) |
| `Player_Ground` | `$E3B8` | checked | 12 | select-refill, select-poor, select-full | none |
| `Player_StartJump` | `$E3F2` | checked | 12 | - | none |
| `Player_Walk` | `$E482` | checked | 12 | slippery | the push of `$73` with no direction held; reversing on slippery floor; momentum speed at `$0300` or more |
| `Player_MoveRight` | `$E593` | checked | 11 | - | an object contact deeper than 12 px (the clamp) |
| `Player_MoveLeft` | `$E5C3` | checked | 12 | - | an object contact deeper than 12 px (the clamp) |
| `Player_Air` | `$E63B` | checked | 12 | - | B in the push pose (cane from the air); landing in the push pose or in cane mode; a pogo landing below the pogo ceiling; controller 2 Up; and in the row exit `$E878`/`$E719` it calls: the final arena drop, the early returns (no row edge, no map cell, last row), the camera page step and stream direction 0 |
| `Player_GrabClimb` | `$E740` | checked | 12 | - | the climb probes hitting a solid tile; a grab found by probe 0 or 2 |
| `Player_Pogo` | `$E7EA` | checked | 11 | - | none |
| `Player_Climb` | `$E806` | checked | 5 | - | holding still without A; climbing off the end of the vine, up or down |
| `Player_Cane` | `$E8EB` | checked | 7 | - | a swing that touches an object of kind `$15` or `$42` |
| `Player_Fall` | `$E970` | checked | 12 | - | none |
| `Player_Rise` | `$E9BA` | checked | 12 | - | none |
| `Player_ProbeVertical` | `$EA5E` | checked | 12 | - | none |
| `Player_Hurt` | `$EAB9` | checked (pokes only) | 0 | hurt-wait, hurt-now, hurt-last-dot | none |
| `Player_Knockback` | `$EB00` | checked | 10 | - | none |
| `Player_TakeDamage` | `$EB5E` | checked | 10 | hurt-now, hurt-last-dot | a hit with no full HP dot left; the cancel (replace mode with a handler only: events check) |
| `Player_Stuck` | `$EB82` | checked | 2 | - | none |
| `Player_Pit` | `$EBA7` | checked (pokes only) | 0 | pit-falling, pit-death | none |
| `Player_PitDeath` | `$EBB4` | checked | 8 | pit-death | none |
| `Player_Mode9` | `$EBC9` | checked | 1 | - | none |
| `Player_Carried` | `$EBD9` | checked (pokes only) | 0 | carried-10, carried-11 | a wall at the left probes (the knock-back); Down held when A is pressed in mode 10 |
| `Player_Mode12` | `$EC2D` | checked (pokes only) | 0 | mode12, mode12-page7 | no vertical direction with Y below `$18` |
| `Player_Death` | `$EC9B` | checked | 8 | hurt-last-dot | none |
| `Player_Mode14` | `$ECFF` | checked | 1 | - | none |
| `Player_ContactH` | `$D202` | checked | 12 | - | not measured |
| `Player_ContactV` | `$D291` | checked | 12 | - | not measured (the fall into `$D350` needs `$0690` to wrap under the power-up) |
| `Player_SelectRefill` | `$E2FE` | checked (pokes only) | 0 | select-refill, select-poor, select-full | not measured |
| `Player_RowChange` | `$E8C0` | checked | 6 | - | not measured |
| `Camera_Update` | `$EFE2` | checked | 12 | 11 of 11 | not measured |
| `Map_LoadHeader` | `$C7B0` | checked | 12 | 11 of 11 | not measured |
| `Level_DrawStartColumns` | `$C71D` | checked | 12 | 11 of 11 | not measured |
| `Level_DrawScreen` | `$C78F` | checked | 12 | 11 of 11 | not measured |
| `Scroll_StreamBlocks` | `$C830` | checked | 12 | 11 of 11 | not measured |
| `Level_DecodeBlock` | `$CCBD` | checked | 12 | 11 of 11 | not measured |
| `Level_MetatileAt` | `$CDE2` | checked | 6 | - | not measured |
| `Level_ScreenPointer` | `$CE2C` | checked | 12 | 11 of 11 | not measured |
| `Vram_FlushQueue` | `$C139` | checked | 12 | 11 of 11 | not measured |
| `Vram_FlushTileEdits` | `$C15E` | checked | 9 | - | not measured |
| `Bg_Probe` | `$D120` | checked | 12 | 11 of 11 | not measured |
| `Bg_SnapRight` | `$D49A` | checked | 10 | 2 of 11 | not measured |
| `Bg_SnapLeft` | `$D4B2` | checked | 11 | - | not measured |
| `Bg_SnapHead` | `$D4C8` | checked | 9 | - | not measured |
| `Bg_SnapFeet` | `$D4D6` | checked | 12 | 11 of 11 | not measured |
| `Boss_CheckTrigger` | `$CF93` | checked | 12 | 11 of 11 | not measured; the arena start (5 calls) runs as the original |
| `Level_TileEdit` | `$F574` | checked | 9 | - | not measured |
| `Chr_StreamRow` | `$C42C` | checked | 12 | 11 of 11 | not measured |

**Claimed, in progress** (branch `decomp/flow`, `src/decomp/flow.c`): the game flow of the main task in the fixed
bank, `$D4E6-$E353` and the boss trigger `$CF93-$D04C`: title and difficulty select (`$D4E6`), Land Select and the
banking of money (`$D630`, `$D8D9`), land load (`$D79B`) and the per-land tables (`$D9CA`), the frame loop (`$DB36`)
with pause and the boss checkpoint (`$DC04`), death and warps (`$DC3E`, `$DD23`, `$DD4E`), the HUD refreshes
(`$DE6C`, `$DEA3`), game over (`$DEB9`), land exit and scenes (`$DF08`, `$DF27`), the ending (`$E054`, `$E1AF`) and
the HUD timer `timer_tick` (`$E28C`).

Not decompiled yet, and still run as the original from the C:

- Through `Decomp_Call`: the background probe `$D120` and its snaps `$D49A`/`$D4B2`/`$D4C8`/`$D4D6`
  (decompiled in `collision.c`, but `player.c` still calls their originals), one probe point
  `$D350` (where `Player_ContactV` can fall through to), the climbing steps `$F192`/`$F1A8`, the
  walls move `$F0F8` and the HP refill `$F812`. The last four belong with the object code (`$F179`,
  `$F1BE`, `$F1F9`), so they are decompiled there.
- Through `Decomp_RunOriginal`: `Player_Update` in modes 15 (`$ED05`, message or item) and 16
  (`$EF62`, treasure), and every call that reaches the row change: that call runs as the original,
  which then reaches `Player_RowChange` at its own site.

**Coverage of Scrooge's code.** His own code in the fixed bank is `$E2FE-$EFE1` (3,300 bytes).
The decompiled routines cover `$E2FE-$ED04`: 2,567 bytes or 78%. With the collision and movement
helpers he shares with other objects (`$D120-$D4E5` and `$F0F8-$F233`, 1,281 bytes, of which the
contact tests `$D202-$D34F` are decompiled) it is 2,901 of 4,581 bytes, 63%. By routine, 37 of the
49 routines his code runs are C (76%): the 37 `Player_*`, motion and sound rows above against the
12 listed as not decompiled.
The motion (`$F234-$F2C6` but `$F298`) and sound (`$F752`) primitives are decompiled too, and they
also serve every other object.

**Level routines** (`level.c`, `collision.c`): the camera `$EFE2-$F0E0`, the column streaming
`$C7C5-$C969`, the map header `$C7B0`, the start and Land Select draws `$C71D-$C7AF`, the block
decoding `$CCBD-$CE56`, the queue uploads `$C139-$C182`, the row's sprite CHR stream `$C42C-$C507`,
the boss trigger `$CF93`, the tile edit `$F574` and the background probe with its snaps
`$D120-$D4E5`. Branch coverage of these two files was not measured. Not decompiled yet: the row
scroll (`$E9`), the tile-edit metatile helper `$F697` and the room transitions, which the flow's
task code runs.

**Next routines, in order:** `player.c` calling the C of the background probe and its snaps
instead of their originals, then modes 15 and 16 with the flow's task hand-off.

### Adding a routine

1. Read the routine (`tools/disasm6502.py`) and every caller (`tools/player_xref.py`), and note
   which registers and flags the callers use after it returns: that is its contract.
2. Name every address it touches in `ducktales_symbols.h` and add typed accessors to the API.
3. Write the C in `src/decomp/` and add a `DecompRoutine` entry (address, site id, name, adapter,
   contract) to the file's table.
4. Add the site to `config/game.toml` with the CRC of the routine's first 8 bytes, computed locally
   from your ROM. Never commit the bytes.
5. Run `tests/decomp_test.py` (all modes) and `tests/verify_routes.py` with
   `DUCKTALES_DECOMP=check`. If the routes do not reach a branch, add a poke case.

## Verification

All commands run on the headless builds (`build/headless`: `-DDUCKTALES_HEADLESS=ON
-DDUCKTALES_ORACLE=ON`; `build/headless-dev`: `-DDUCKTALES_HEADLESS=ON
-DDUCKTALES_DEV_DIAGNOSTICS=ON`).

```powershell
py -3 -I tests/decomp_test.py --build build/headless --dev-build build/headless-dev --jobs 3
py -3 -I tests/verify_routes.py --build build/headless --jobs 3
$env:DUCKTALES_DECOMP = 'check'; py -3 -I tests/verify_routes.py --build build/headless --jobs 3
py -3 -I tests/enhancements_test.py --build build/headless --dev-build build/headless-dev
```

While writing a routine, check only the routes that run it, from the golden save state just before
it (`tests/quick_verify.py` golden files, `--golden` when they live in another worktree):

```powershell
py -3 -I tests/decomp_test.py --build build/headless --dev-build build/headless-dev --changed DF08-DF40 --from-state --span 600 --modes check replace
```

A routine every route runs at power-on selects frame 0 and saves nothing; run the full
`decomp_test` before a merge in every case.

| Check | What it shows |
|---|---|
| `decomp_test --modes check` | every call of every decompiled routine on every route agrees with the original |
| `decomp_test --modes replace` | stock against `--decomp replace` (`--dt-ram-log`): CPU RAM outside page 1 equal after every frame; the main task's live stack equal on every frame where both runs are idle in the scheduler |
| `decomp_test --modes pokes` | 11 poked states (modes 5, 8, 10, 11 and 12, the last HP dot, slippery floor, held timers) in check mode and stock against replace |
| `decomp_test --modes events` | NoDamage and JumpScale with the decompiled routines raising the events: check mode, and off against replace with the option on |
| `verify_routes` | native, interpreter and oracle agree on every frame at 4 alignments |
| `verify_routes` with `DUCKTALES_DECOMP=check` | the same, with check mode on in the recompiled runs |
| `enhancements_test` | each enhancement, now an event handler, is identical to stock when off, acts when on, and can be toggled |

**Ideas from other projects** (reviewed 2026-11-23; none of their code is used):

- [DecompPipeline](https://github.com/GeneralAtrox/DecompPipeline): one machine verdict per
  function, which agents cannot argue with, bounded repair attempts, and a job registry that never
  repeats a job on unchanged inputs. We already have the verdict: check mode compares every call
  on the whole RAM. From that project we took the reuse rule (AGENTS.md, "Checks") and the queue
  (`tools/test_slot.py`). We did not take its mutation check. It finds harnesses that do not
  observe a result, and check mode observes all of RAM, so the branches the coverage build lists
  already name the gaps.
- [mesen-for-ai](https://github.com/paulomanrique/mesen-for-ai) (GPL-3.0): Mesen driven headless
  with watches, breakpoints and the code/data logger. The same questions are answered here by
  `tools/whoaccesses.py`, `--miss-log` coverage and the TriCNES oracle, so we did not add it.
- [Ghidra6502](https://github.com/tom-seddon/Ghidra6502): Ghidra's analysis of indexed addressing.
  We did not take it, because `tools/disasm6502.py` and `tools/player_xref.py` cover the routines
  we decompile, without a JVM.
- [NESRecomp](https://github.com/mstan/nesrecomp) upstream is 8 commits past our pin. The pin
  already has `symbol_file` and function-entry mod hooks.
- [awesome-rea](https://github.com/yibie/awesome-rea): "done" is a gate a machine decides. For us
  that is `decomp_test` and `verify_routes`.

A difference in check or replace mode names the RAM address with its `ducktales_symbols.h` name
(`$0011 CONTACT_DEPTH/SCRATCH11`), so a mismatch reads without a symbol lookup.

Why replace mode is not part of `verify_routes`: the oracle compares bus traces, and a replaced
routine's bus activity is `cpu_read`s of one address instead of the original's accesses. Replace
mode is checked on RAM, per frame, against the recompiled original.

**Branch coverage** of the C was measured with a scratch clang-cl build of the developer
configuration, with `-fprofile-instr-generate -fcoverage-mapping` on `src/decomp/*.c` only, running
every route and every poke case in check mode: `player.c` ran 95.4% of its lines and 86.9% of its 366 branches; `motion.c` and `sound.c` ran
every line, and every branch except a full sound ring. The branches never executed are in
the table above. Not reachable by a poke: input-driven paths (Select on the ground, reversing on
slippery floor), map geometry (the row-change early returns, the camera page bump when falling into
the row below, a climb that leaves the vine), and object kinds the routes never hit with the cane.
Of those, `Player_TakeDamage`'s cancel path runs only in replace mode with an event handler, which
the events check covers.

What has not been verified:

- Replace mode with the window build in real play.
- Paths no route or poke case reaches (table above). They are written from the code, and check mode
  will stop on any of them that is wrong, the first time a route reaches it.
