# Decompilation layer

The port's first step from "recompiled 6502" towards readable C. It has three parts, all in the
port's own files and all switched off by default:

- **The game API** (`include/ducktales/`): typed names for the game's RAM (objects, Scrooge, HUD,
  game-wide state) and RAM transactions to read and change it.
- **Game events** (`include/ducktales/events.h`, `src/game/events.c`): verified code sites where mod
  code can react to, or cancel, something the game does. The enhancements are handlers of these
  events.
- **Decompiled routines** (`src/decomp/`): Scrooge's per-frame code, the object system (the per-frame
  dispatch, the shared object helpers and 21 object kinds) and the motion and sound primitives they
  use, written as C on the API. Each replaces the recompiled routine at its entry, and
  each is checked against the original on every call of every route.

The routines are checked in the cycle host (`build/headless`, and `ducktales_cycle.exe` of a window
build). The game as players run it, `ducktales.exe` of `build/windows`, is the native runtime's
player by default (`DUCKTALES_PLAYER=native`); there Settings → Gameplay → Decompiled code (off by
default, with No slowdown only) runs them alone in place of the recompiled ones ("On the native
runtime" below, [native-runtime.md](native-runtime.md#the-player-build)).

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
off|check|replace` in the cycle host, or from the environment variable `DUCKTALES_DECOMP` when the
option is not given; the native runtime has `off` and `replace` only ("On the native runtime" below).

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
instructions outside the decompiled ranges that jump to an entry. Of the 67 routines in the player,
motion, sound, level and collision tables, 27 have
one: `Player_Update` (the frame loop's `JSR` at `$DB6C`), `Player_PitDeath` (the original
`Player_Update` jumps to it in mode 8), `Player_RowChange` (the original `$E878` falls into
it), `Player_Message` and `Player_Treasure` (the original `Player_Update`'s mode table jumps to
them; the C calls all three, and these sites serve replace mode's original call past VBlank), the four motion primitives and the two sound requests
(object and game code everywhere), `Player_TakeDamage`, kept for replace mode's measuring call
above, and 15 of the 19 camera, scrolling, level and background routines (all but
`Level_ScreenPointer`, `Level_DecodeBlock`, `Level_MetatileAt` and `Bg_ProbePoint`, which only decompiled code
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

### Task code

The game runs its main flow as a task: the title, Land Select, the land load and the frame loop
wait for frames (`task_yield` `$FFAC`), switch banks and drive the PPU, none of which an isolated
call can do. `flow.c` decompiles that code as **task code** (`DecompRoutine.task`):

- A task routine's C ends in one of two ways: it returns (RTS), or it hands off to original code
  that runs for real: `Decomp_Handoff(routine, resume, uses)` is a JSR (resume 0: a JMP), and
  `Decomp_Push`/`Decomp_Pull`/`Decomp_PushReturn` keep the task's stack as the original keeps it
  (frame counters, the land and row around a redraw). `Decomp_SelectBank` and `Decomp_Store` make
  the device writes the C needs (the mapper, `$2000`), for real in replace mode, and so do the
  `Decomp_BusWrite` writes of the routines a task routine calls in C (the probe's bank switches).
  A task routine whose C another task routine's C reaches has no check of its own: the caller's
  covers it, and its calls count as compared with the caller's.
- Its C starts at a site (original code reaches it: `$D4E6` from the scheduler's `JMP ($93)`, `$D79B`
  from Land Select, `$DB36` after each frame, `$DD52` from the scenes, `$DFED` from the message
  box), from another task routine's C (a plain C call), or at a **resume point** (`DecompRoutine.resume`): the address an RTS returns
  to after the original a hand-off reached. Resume points have no site: the RTS observer
  (`cyc_mod_set_return_hook`) sees every RTS and starts the C when it lands on one (one in the
  switchable window, bank 4's take-off scene, only with its bank mapped). On the routes
  no interrupt is pending at any of them.
- Check mode runs the original from the same point in an isolated call with hooks allowed. It
  enters through an RTS (`$FFAB`) whose return the observer moves to the start, at the routine's
  true S, with the isolated call's return address in place of the routine's own (at S + locals
  + 1). When the C hands off, the original stops where it reaches the same routine at the same
  depth: at that routine's site, or at a **stop site** registered for it (`gFlowStops`, 2 of
  them: the scheduler `$FF35`, which `task_yield`'s C hands off to, and the reset `$FEE0`). Check compares RAM, the stack down to that depth, the registers the hand-off uses and the
  number of device writes.
- Replace mode stores the C's results, makes its device writes, spends the original's cycles up to
  the stop (without the entry RTS) and jumps there. A call that would run into VBlank or past the
  frame end goes to the original, as for the other routines: the NMI steps state the task code
  uses (the random generator, the scratch bytes).
- A task routine called from another's C is not checked on its own: the caller's comparison covers
  it up to the hand-off. Its calls are counted.

### Interrupt routines

The NMI handler (`$C000`) is decompiled as **interrupt routines** (`DecompRoutine.interrupt`,
`src/decomp/nmi.c`): task code whose C ends in RTI instead of RTS. When the C returns, P and the
return address come off the stack above its last pull (S + 1 to S + 3), as is.

- On the cycle host the original runs in an isolated call as task code's does, entered through an RTS
  whose return the observer moves to the start; for an interrupt routine it also moves the return
  address the call left on the stack one on, since an RTI returns to the address itself, so that the
  original's RTI ends the call. Check compares as for task code, P after the RTI included.
- Replace mode spends, with the original's cycles, the cycles a replaced routine still owed when the
  NMI came (Timing in replace mode). The `$C138` site (`nmi-exit`) still spends them when the
  original's RTI runs, as when the C's call goes to the original.
- The native runtime ends the NMI at the fetch of the RTI at `$C138` (its lag and NMI-audit
  accounting); a C that makes the RTI tells it with `native_nmi_return()`.

The whole handler is C: `Nmi_Handler` (`$C000`, a site), and for an NMI whose body went to the
original its exit alone, `Nmi_Return` (`$C132`, `PLA TAY PLA TAX PLA PLP RTI`, a site after the bank
select `$FFDE` returns). A second sound request for the speed or fade id (they read the Y the driver
left) gives `Nmi_Handler`'s call to the original.

- The sprite-0 wait is a bus operation of its own, `Decomp_BusWait(addr, mask, want)`: reads of `addr`
  until `(value & mask) == want`, a failure after 60,000.
- `Nmi_Handler` cannot run in an isolated call: that has no clock, so the sprite-0 hit never comes and
  a sprite DMA or a `$2005` write never lands. Check mode runs its C at the NMI and makes its bus
  operations on the machine from a snapshot (`cyc_snapshot_take`), with the clock running and the wait
  real; it keeps what they left and restores the snapshot. Then the original runs for real, and at its
  RTI (the `$C138` site) check compares RAM (the stack below the handler's entry S left out), A, X, Y,
  S and P's N, V, Z and C, CIRAM, palette RAM, CHR RAM, OAM, the PRG bank, the PPU's control and mask
  bits, t, fine X and write latch, the APU's written registers (`$4000`-`$400F` as written, the DMC's
  settings, the `$4015` enables, the frame counter's mode), and the writes to `$2006`, `$2007` and
  `$4000`-`$4017` (not `$4014` or `$4016`) in order: the C's from its bus operations, the original's
  from the trace's cycle observer (`cyc_trace_cycle_hook`; the trace's hash and cycle count are put
  back after). v, OAMADDR and the APU's clocked counters move with time and are not compared.
- Replace mode on the cycle host gives `Nmi_Handler`'s call to the original until its bus operations
  are made at the original's cycles; the native runtime runs the C.

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
| `Player_Update` | X | none, or the hand-off of mode 15, 16 or the row change | task code: X goes into the power-up music request; the probe's bank switches are its device writes; modes 15 and 16 go on in `Player_Message` and `Player_Treasure` (A = the handler's high byte, X = the mode, Y kept), a row change in `Player_RowChange` |
| `Player_Walk`, `Player_Knockback` | X | none | X is the slot the push of `$73` moves when no direction is held, and what the sound request keeps in `$00` |
| `Player_MoveRight`, `Player_MoveLeft`, `Player_Fall`, `Player_Rise`, `Player_ProbeVertical` | X (moves only) | X | X is what the probe or contact routine returned; callers pass it on |
| `Player_ContactH` | none | X, C | X is the slot touched (0 for none, carry set); the cane swing reads its kind |
| `Player_ContactV` | none | C | carry clear for a contact |
| `Player_SelectRefill` | none | X | X goes on to `Player_Walk` |
| `Player_RowChange` | A = row, X = 0 or 2, Y = camera screen, `$00` | hand-off | task code: pops `Player_Update`'s return address and hands off to the row scroll `$C976`, returning to `$E8DF` |
| `Player_Message`, `Player_Treasure` and their resume points | the registers at the site or resume point | X and Y at each hand-off; `Message_Scrolled` and `Message_LetterNext` X and Y at their RTS | task code: they wait for frames (`$FFAC`, `$FFA5`), the pause text `$B580` and the scene draw `$DFED`; X and Y go on the stack in `task_yield`, and the wait `$ED60` and `$ED73` that the RTS returns to are C that keeps them |
| every other `Player_*` | none | none | |
| `Camera_Update` | X | X, hardware | the frame loop keeps X past `$CF93` into the code after it: X is the entry X when the camera stays, else what the column streaming leaves |
| `Scroll_StreamBlocks` | none | X, hardware | X is the last step's collision map index (or the screen id >> 5 when its screen is missing), or where the upload stopped when the screen is blanked |
| `Vram_FlushQueue` | none | X, hardware | X is the queue index where the upload stopped |
| `Level_ScreenPointer` | Y = grid cell | X | X = screen id >> 5 |
| `Level_DecodeBlock` | none | X | X is the block's collision map index |
| `Level_MetatileAt` | X = probe slot | A, X | |
| `Map_LoadHeader` | X = map | X | X is kept |
| `Bg_Probe` | X = slot | X, hardware | X is kept |
| `Bg_ProbePoint` | X = slot, Y = probe point | A, C | `Player_ContactV` falls into it with its hitbox in Y and reads the carry: the page or row sum's, or the last bit the class shifts dropped |
| `Bg_SnapRight`, `Bg_SnapLeft`, `Bg_SnapHead`, `Bg_SnapFeet` | X = slot | X | X is kept |
| `Level_TileEdit` | X = slot | hardware | |
| `Level_DrawStartColumns`, `Level_DrawScreen`, `Vram_FlushTileEdits` | none | hardware | |
| `Chr_StreamRow` | X | X, hardware | X is the entry X when the stream is done, 4 after the palette, else the queue index; the scene draw `$DFED` of message `$0B` pushes it in `task_yield` |
| `Boss_CheckTrigger` | none | none | the arena start runs as the original (`Decomp_RunOriginal`): it scrolls the row |
| `Bank_Select`, `Bank_SelectTemp` | A = bank | A, X, P, hardware | the bank is written to the bus-conflict table at `$FFE5` |
| `Oam_Build`, `Random_Next` | none | X, Y | the frame loop yields right after them, and `task_yield` keeps X and Y on the stack |
| `Records_MarkTaken` | none (bank 5 mapped) | A, X, Y | `$00` ends at `$FF` and `$01` holds the last record's bit; the message box goes on with Y |
| `Ppu_Blank`, `Ppu_Unblank`, `Ppu_FillNametable`, `Chr_LoadList`, `Text_DrawStrings`, `Input_ReadControllers`, `Sound_Update`, `Sound_Play` | as the original | hardware | the controller reads use `Decomp_BusLoad`, made at once; check also compares OAM and the APU, PPU and mapper registers |
| `Hud_Reset` | none | none | |
| `Land_ApplyTables`, `Land_LoadPalette` | X = palette set (palette) | X, Y | Land Select and the title read X and Y after them |
| `Timer_Tick`, `Hud_RefreshMoney`, `Hud_RefreshTotal` | none | none | called from the frame loop's C |
| task code (`flow.c`) | the registers at the site or resume point | at each hand-off, the registers its target reads | see Task code |
| `Task_Create` | A = slot | none | reset reads nothing after it |
| `Task_CurrentSlot`, `Task_SlotOffset` | none; A = slot | X | called from the task primitives' C |
| `Task_Yield`, `Task_Wait` (task code) | X, Y (kept on the stack); X = frames | none: they hand off to the scheduler `$FF35` | `Task_WaitNext` (`$FFA8`, the resume point) returns A, X, Y and P as its RTS leaves them |
| `Nmi_Handler` (interrupt routine) | the interrupted A, X, Y and P | A, X, Y and P after the RTI; hardware | see Interrupt routines |
| `Nmi_Return` (interrupt routine) | none | A, X, Y and P after the RTI | see Interrupt routines |

### On the native runtime

`src/decomp/decomp.c` is the core every host shares: the routine tables, the state of the routine
whose C runs and the calls every backend makes the same way (`decomp_backend.h`). The cycle host
links `decomp_cycle.c`, its check and replace modes above. The native runtime links
`decomp_native.c`, which has replace mode only, and there the C runs alone:

- At a routine's site its C runs on the image of RAM, as on the cycle host, and its results are
  stored, its bus operations made and its registers set; no original runs to measure cycles and
  nothing is compared.
- An original routine the C calls (`Decomp_Call`) runs nested in an isolated call on the native
  bus (`native_isolated_call` in `src/native/native_bus.c`): the machine is saved, takes the C's
  image and its bus operations so far, runs the original until it returns, and is restored; the
  image takes the RAM back, as in the cycle host.
- `Decomp_RunOriginal` still gives a call to the original: on the 12 routes, 14 calls of
  `Player_Update` (its row change and modes 15 and 16, which wait for frames). The controller read
  (`$C1AB`), which the cycle host's replace mode gives to the original because an isolated call
  cannot time it, runs as C here (`Decomp_ConsoleTiming` is false).

The C takes no CPU cycles, so the console's timing is not kept: the work of a frame ends sooner,
and a land load behind a blank screen ends in fewer frames. With the console's lag that would
remove lag the console has, so the mode runs only with No slowdown, where a logic step's result
does not depend on how long it takes ([native-runtime.md](native-runtime.md#lag)). In the player,
Settings → Gameplay → Decompiled code (`[Game] DecompiledCode`, off by default) is disabled while
No slowdown is off, and its (?) says why; `ducktales_native --decomp replace` exits with code 2
unless `--lag none` is given.

`tests/native_decomp_test.py` runs each route off and on, with the same buttons strobe by strobe
(`--latch-log`, `--latch-input`), and compares RAM after every logic step. On the 12 routes all
81,922 logic steps of the off runs are equal (811,676 decompiled calls) except:

| RAM | Why it differs |
|---|---|
| `$0100-$01FF` | the stack (pushes at different points of the frame) |
| bytes the task never reads (`--nmi-audit`) | NMI-only state |
| `$19`, `$90`, `$C7-$CF`, `$0370-$03EF` | the NMI-facing bytes `tests/lag_modes.py` allows (`ALLOWED`) |
| `$92`, `$D0-$E5` | the frame counter and the sound driver's zero page, run by the NMI: it runs a different number of times before a step when loads are shorter |
| the yielding slot's state and wait (`$80,X`, `$81,X`, X = 4 × `$91`), at one step | (4, 0) on where off has (1, 1): the C's zero cost moves `task_yield` (`$FFAC`) before the VBlank NMI, which then counts the yield's one-frame wait out between the C's stores and the scheduler's pass at `$FF38`, where the step is taken (the screen is off, so VBlank is not held back). Only that pattern is excepted, the slot must be equal on the next step, and the test prints the number of such steps per route ("NMI-counted yields") |

No other byte of task state differs.

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
| `Player_Update` | `$E354` | checked (task code) | 12 | invulnerable-held | none |
| `Player_Ground` | `$E3B8` | checked with `Player_Update` | 12 | select-refill, select-poor, select-full | none |
| `Player_StartJump` | `$E3F2` | checked with `Player_Update` | 12 | - | none |
| `Player_Walk` | `$E482` | checked | 12 | slippery | the push of `$73` with no direction held; reversing on slippery floor; momentum speed at `$0300` or more |
| `Player_MoveRight` | `$E593` | checked | 11 | - | an object contact deeper than 12 px (the clamp) |
| `Player_MoveLeft` | `$E5C3` | checked | 12 | - | an object contact deeper than 12 px (the clamp) |
| `Player_Air` | `$E63B` | checked with `Player_Update` | 12 | - | B in the push pose (cane from the air); landing in the push pose or in cane mode; a pogo landing below the pogo ceiling; controller 2 Up; and in the row exit `$E878`/`$E719` it calls: the final arena drop, the early returns (no row edge, no map cell, last row), the camera page step and stream direction 0 |
| `Player_GrabClimb` | `$E740` | checked | 12 | - | the climb probes hitting a solid tile; a grab found by probe 0 or 2 |
| `Player_Pogo` | `$E7EA` | checked with `Player_Update` | 11 | - | none |
| `Player_Climb` | `$E806` | checked with `Player_Update` | 5 | - | holding still without A; climbing off the end of the vine, up or down |
| `Player_Cane` | `$E8EB` | checked | 7 | - | a swing that touches an object of kind `$15` or `$42` |
| `Player_Fall` | `$E970` | checked | 12 | - | none |
| `Player_Rise` | `$E9BA` | checked | 12 | - | none |
| `Player_ProbeVertical` | `$EA5E` | checked | 12 | - | none |
| `Player_Hurt` | `$EAB9` | checked (pokes only) | 0 | hurt-wait, hurt-now, hurt-last-dot | none |
| `Player_Knockback` | `$EB00` | checked | 10 | - | none |
| `Player_TakeDamage` | `$EB5E` | checked | 10 | hurt-now, hurt-last-dot | a hit with no full HP dot left; the cancel (replace mode with a handler only: events check) |
| `Player_Stuck` | `$EB82` | checked with `Player_Update` | 2 | - | none |
| `Player_Pit` | `$EBA7` | checked (pokes only) | 0 | pit-falling, pit-death | none |
| `Player_PitDeath` | `$EBB4` | checked | 8 | pit-death | none |
| `Player_Mode9` | `$EBC9` | checked | 1 | - | none |
| `Player_Carried` | `$EBD9` | checked with `Player_Update` (pokes only) | 0 | carried-10, carried-11 | a wall at the left probes (the knock-back); Down held when A is pressed in mode 10 |
| `Player_Mode12` | `$EC2D` | checked (pokes only) | 0 | mode12, mode12-page7 | no vertical direction with Y below `$18` |
| `Player_Death` | `$EC9B` | checked | 8 | hurt-last-dot | none |
| `Player_Mode14` | `$ECFF` | checked | 1 | - | none |
| `Player_ContactH` | `$D202` | checked | 12 | - | not measured |
| `Player_ContactV` | `$D291` | checked | 12 | power-contact | not measured |
| `Player_SelectRefill` | `$E2FE` | checked (pokes only) | 0 | select-refill, select-poor, select-full | not measured |
| `Player_RowChange` | `$E8C0` | checked | 6 | - | not measured |
| `Player_Message` | `$ED05` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Opened` | `$ED0E` | checked (pokes only) | 0 | message-scene | not measured |
| `Message_Shown` | `$ED33` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Drawn` | `$ED49` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Records_MarkTaken` | bank 5 `$B76A` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Wait` | `$ED60` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_WaitNext` | `$ED69` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Paged` | `$ED73` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Cleared` | `$ED85` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Closed` | `$EDAA` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_Scrolled` | `$EEA6` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Message_LetterNext` | `$EF01` | checked | 5 | message-pay, message-pay-poor, message-life, message-block, message-launchpad, message-scene | not measured |
| `Player_Treasure` | `$EF62` | checked | 5 | treasure-blanks, treasure-max | not measured |
| `Treasure_Waited` | `$EF6B` | checked | 5 | - | not measured |
| `Treasure_Done` | `$EFDF` | checked | 5 | treasure-blanks, treasure-max | not measured |
| `Message_Scroll`, `Message_Text` | `$EE8B`, `$EEAE` | checked in their callers | - | - | not measured; task code only C calls, compared up to the hand-off of the routine that calls it |
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
| `Bg_ProbePoint` | `$D350` | checked (pokes only) | 0 | power-contact | not measured; one call, so one of its four carry exits |
| `Bg_SnapRight` | `$D49A` | checked | 10 | 2 of 11 | not measured |
| `Bg_SnapLeft` | `$D4B2` | checked | 11 | - | not measured |
| `Bg_SnapHead` | `$D4C8` | checked | 9 | - | not measured |
| `Bg_SnapFeet` | `$D4D6` | checked | 12 | 11 of 11 | not measured |
| `Level_TileEdit` | `$F574` | checked | 9 | - | not measured |
| `Chr_StreamRow` | `$C42C` | checked | 12 | 11 of 11 | not measured |
| `Row_Scroll` (task code) | `$C976` | checked | 6 | - | not measured |
| `Row_ScrollFrame` (task code) | `$C99D` | checked | 6 | - | not measured |
| `Row_ScrollStepped` (task code) | `$C9FB` | checked | 6 | - | not measured |
| `Row_ScrollNext` (task code) | `$CA09` | checked | 6 | - | not measured |
| `Row_ScrollEnd` (task code) | `$CA22` | checked | 6 | - | not measured |
| `Row_DrawStep` | `$CA4A` | checked | 6 | - | not measured |
| `Row_KeptStep` | `$CA4E` | checked | 6 | - | not measured |
| `Row_LastStep` | `$CA8B` | checked | 4 | - | not measured |
| `Row_Draw` | `$CAA1` | checked | 6 | - | not measured |
| `Row_QueueHeaders` | `$CB1E` | checked | 6 | - | not measured |
| `Row_RightStep` | `$CB5A` | checked | 6 | - | not measured |
| `Row_LeftStep` | `$CB6E` | checked | 6 | - | not measured |
| `Row_DrawHalf` | `$CB94` | checked | 5 | - | not measured |
| `Block_Draw` | `$D068` | checked | 1 | message-block | not measured |
| `Block_Debris` | bank 4 `$BD80` | checked | 1 | message-block | not measured |
| `Tile_EditByAction` | `$F4D8` | checked | 6 | - | not measured |
| `Tile_EditBySlot` | `$F506` | checked | 1 | - | not measured |
| `Spawn_Tables` | bank 6 `$AE20` | checked | 12 | - | not measured |
| `Spawn_TablesOf` | bank 6 `$AE22` | checked | 12 | - | not measured |
| `Spawn_Reset` | bank 6 `$AE60` | checked | 12 | - | not measured |
| `Spawn_Update` | bank 6 `$AEA0` | checked | 12 | - | not measured |
| `Spawn_TimedAtEdge` | bank 6 `$B0AB` | checked | 12 | - | not measured |
| `Nmi_Handler` | `$C000` | claimed (decomp/system): the NMI has no site form yet | - | - | - |
| `Ppu_NmiOff` | `$C183` | claimed (decomp/system): no JSR, JMP or pointer in the ROM reaches it | - | - | - |
| `Ppu_NmiOn` | `$C18D` | claimed (decomp/system): no JSR, JMP or pointer in the ROM reaches it | - | - | - |
| `Ppu_Blank` | `$C197` | checked | 12 | - | not measured |
| `Ppu_Unblank` | `$C1A1` | checked | 12 | - | not measured |
| `Input_ReadControllers` | `$C1AB` | checked | 12 | - | not measured |
| `Ppu_FillNametable` | `$C203` | checked | 12 | - | not measured |
| `Mem_FillPages` | `$C24F` | claimed (decomp/system): no JSR, JMP or pointer in the ROM reaches it | - | - | - |
| `Oam_Clear` | `$C267` | checked | 12 | - | not measured |
| `Palette_FadeIn` (task code) | `$C279` | checked | 12 | - | not measured |
| `Palette_FadeInKeep` (task code) | `$C27C` | checked | 0 | ending | not measured |
| `Palette_FadeOut` (task code) | `$C282` | checked | 12 | - | not measured |
| `Palette_FadeWait` (resume point after `task_yield`) | `$C2B2` | checked | 12 | - | not measured |
| `Random_Next` | `$C2E6` | checked | 12 | - | not measured |
| `Chr_LoadList` | `$C301` | checked | 12 | - | not measured |
| `Palette_LoadScene` | `$F76D` | checked (pokes only) | 0 | ending | not measured |
| `Bank_Select` | `$FFDE` | checked | 12 | - | not measured |
| `Bank_SelectTemp` | `$FFE0` | checked | 12 | - | not measured |
| `Task_Create` | `$FF7D` | checked | 12 | - | not measured |
| `Task_CurrentSlot` | `$FF9F` | checked | 12 | - | not measured |
| `Task_SlotOffset` | `$FFA1` | checked | 12 | - | not measured |
| `Task_Wait` (task code) | `$FFA5` | checked | 8 | - | not measured |
| `Task_WaitNext` (resume point after `task_yield`) | `$FFA8` | checked | 8 | - | not measured |
| `Task_Yield` (task code) | `$FFAC` | checked | 12 | - | not measured |
| `Nmi_Handler` (interrupt routine) | `$C000` | checked | 13 | - | not measured |
| `Nmi_Return` (interrupt routine) | `$C132` | checked | 13 | - | not measured |
| `Oam_Build` | bank 5 `$8000` | checked | 12 | - | not measured |
| `Oam_DrawSlot` | bank 5 `$80BE` | checked | 12 | - | not measured |
| `Text_DrawStrings` | bank 5 `$BAB0` | checked | 12 | - | not measured |
| `Sound_Update` | bank 3 `$8000` | checked | 12 | - | not measured |
| `Sound_Play` | bank 3 `$8003` | checked | 12 | - | not measured |
| `Hud_Reset` | `$D60A` | checked | 12 | - | not measured |
| `Land_ApplyTables` | `$D9CA` | checked | 12 | - | not measured |
| `Land_LoadPalette` | `$D9F5` | checked | 12 | - | not measured |
| `Timer_Tick` | `$E28C` | checked | 12 | - | not measured |
| `Hud_RefreshMoney` | `$DE6C` | checked | 10 | - | not measured |
| `Hud_RefreshTotal` | `$DEA3` | checked | 0 | hud-total | not measured |
| `MainTask` and `Title_*` (13 resume points) | `$D4E6-$D609` | checked | 12 | - | not measured |
| `BossTrigger` (2 resume points) | `$CF93-$D04C` | checked | 12 (the arena scroll in 4) | - | not measured; the Transylvania redirect to Land Select |
| `LandLoad` (12 resume points) | `$D79B-$D8A5` | checked | 12 | - | not measured |
| `LandSelect` (16 resume points) | `$D630-$D79A` | checked | 12 (the return from a land in 5) | final-trip, all-treasures | not measured |
| `FinalTrip` (3 resume points), `SpritesFrame` (1 resume point) | bank 4 `$BE00-$BEAA`, `$F855-$F868` | checked | 0 | final-trip | not measured |
| `PauseText` (1 resume point), `Text_Lines` (1 resume point) | bank 5 `$B580-$B61B` | checked | 12 | pause, message-scene, message-pay, message-block, message-life | not measured; `FrameLoop_Paused` (`$DB65`) after it |
| `Select_ResultMessage`, `Select_Message` | bank 5 `$B800-$B86B` | checked | 12 | final-trip, all-treasures | not measured |
| `Records_Consumed` (then `Records_MarkTaken`, player.c) | bank 5 `$B760-$B769` | checked | 12 | - | not measured |
| `BankTransfer` (1 resume point) | `$D8D9-$D9BD` | checked | 5 | all-treasures (no money), total-max (TOTAL at 99999) | not measured |
| `Scene` (6 resume points) | `$DF27-$DFEC` | checked | 0 | scene-1, scene-2 | not measured |
| `Mode15_Redraw` (5 resume points) | `$DFED-$E053` | checked | 0 | mode15-redraw, message-scene | not measured |
| `Ending`, `Ending_Split`, `Ending_BankTransfer` (`$FFC7`) and their resume points | `$E1AF-$E28B` | checked; the raster wait and write at `$E229-$E23B` run in the original | 0 | ending, ending-0, ending-2 | not measured |
| `EndingScreen` (11 resume points) | `$E054-$E11A` | checked | 0 | ending, ending-0, ending-2 | not measured |
| `EndingObjects`, `EndingObjects_Update` and the five scene handlers | bank 5 `$B380-$B3CB`, `$B400-$B50C` | checked with the ending | 0 | ending-0 (hop), ending (walk), ending-2 (wait); all three exit and reach the credits | not measured |
| `WaitA` (1 resume point) | `$D9BE` | checked | 5 | final-trip | not measured |
| `FrameLoop` and its resume points | `$DB36-$DC3D` | checked | 12 | - | not measured; the scene request (`$DBC5`) |
| `DeathOrWarp`, `Death_*` and `Respawn_*` | `$DC3E`, `$DD4E`, `$DD52` | checked | 10 | - | not measured |
| `DeathPause` | `$DD23` | checked | 10 | - | not measured |
| `GameOver` | `$DEB9` | checked | 4 | - | not measured |
| `LandExit` | `$DF08` | checked | 5 | - | not measured; the exit after a scene |

**Game flow** (branch `decomp/flow`, `src/decomp/flow.c`): the main task's flow in the fixed bank,
`$D4E6-$E353`, and the boss trigger `$CF93-$D04C`. Decompiled: the title and difficulty select
(`$D4E6-$D620`), Land Select with its tables, the bank transfer of money and the wait for A
(`$D630-$D79A`, `$D8A6-$D9C9`), the land load (`$D79B-$D8A5`), the per-land tables and palettes
(`$D9CA-$DA0F`), the frame loop with pause, the boss checkpoint, death, warps, respawn, game over and
land exit (`$DB36-$DF26`), the special scenes with their tables (`$DF27-$DFEC`), the scene set-up
of message `$0B` that Scrooge's message box calls (`$DFED-$E053`), the ending's screens and its
frames with their tables (`$E054-$E126`, `$E133-$E142`, `$E1AF-$E28B`), the HUD timer
(`$E28C-$E2FD`) and the boss trigger with the arena scroll (`$CF93-$D04C`). Outside these ranges the flow also has the final trip's
take-off scene (bank 4 `$BE00-$BEAA`) with the sprites frame it waits with (`$F855-$F868`), and
in bank 5 the pause and message box text (`$B580-$B61B`), the Land Select messages
(`$B800-$B86B`), the clearing of the consumed-record bits (`$B760`) and the ending's scene
objects (`$B380-$B50C`, with tables). The credits yield to the original raster split at
`$E229-$E23B`: it watches the sprite-0 flag on `$2002`, writes `$2000` at the split and
resumes the C via the internal hook at `$E23C`. The Select refill `$E2FE` is in `player.c`;
the scene palette tables `$E127-$E132` and `$E143-$E1AE` are read by `Palette_LoadScene` (system.c).
The flow has 14 sites: 12 entries that original code reaches and 2 stop sites (the scheduler
`$FF35` and reset `$FEE0`).

Not decompiled yet, and still run as the original from the C:

- Called as C: the background probe `$D120`, its snaps `$D49A`/`$D4B2`/`$D4C8`/`$D4D6` and the probe point
  `$D350` (where `Player_ContactV` falls through to) are the C of `collision.c`, the walls move `$F0F8` and
  the climbing steps `$F192`/`$F1A8` the C of `objcommon.c` and the HP refill `$F812` the C of `objects.c`, and
  `player.c` calls that C directly. So does the message box with the OAM clear `$C267` (`system.c`), the
  sprite builder (bank 5 `$8000`, `sprites.c`) and the consumed-record bits (bank 5 `$B76A`,
  `Records_MarkTaken` in `player.c`).
- No call goes to the original: `Player_Update` is task code and calls the C of `Player_Message`,
  `Player_Treasure` and `Player_RowChange`, which wait for frames or leave it without returning. The
  handlers on the way to the row change (`Player_Ground`, `Player_StartJump`, `Player_Air`,
  `Player_Pogo`, `Player_Climb`, `Player_Stuck`, `Player_Carried`) are task code too, checked with
  `Player_Update`: their original does not return from a row change.

**Coverage of Scrooge's code.** His own code in the fixed bank is `$E2FE-$EFE1` (3,300 bytes).
The decompiled routines cover all of it, modes 15 and 16 included. With the collision and movement
helpers he shares with other objects (`$D120-$D4E5` and `$F0F8-$F233`, 1,281 bytes, of which the
probe, its snaps and the contact tests `$D120-$D4E5` and the climbing steps `$F192-$F1BD` are
decompiled) it is 4,310 of 4,581 bytes, 94%. By routine, counting each of modes 15 and 16 as one,
48 of the 49 routines his code runs are C (98%): the 37 `Player_*`, motion and sound rows above,
the two modes, the probe with its four snaps, the probe point, the two climbing steps and the HP
refill, against the walls move `$F0F8`.
The motion (`$F234-$F2C6` but `$F298`) and sound (`$F752`) primitives are decompiled too, and they
also serve every other object.

**Level routines** (`level.c`, `collision.c`): the camera `$EFE2-$F0E0`, the column streaming
`$C7C5-$C969`, the map header `$C7B0`, the start and Land Select draws `$C71D-$C7AF`, the block
decoding `$CCBD-$CE56`, the queue uploads `$C139-$C182`, the row's sprite CHR stream `$C42C-$C507`,
the tile edit `$F574` and the background probe with its snaps
`$D120-$D4E5`. Branch coverage of these two files was not measured. Not decompiled yet: the row
scroll (`$E9`), the tile-edit metatile helper `$F697` and the room transitions, which the flow's
task code runs.

**Next routines:** `player.c` calling the walls move `$F0F8` as C once `objcommon.c` exports it.

### Objects

`src/decomp/objects.c` (bank 6) and `src/decomp/objcommon.c` (fixed bank) decompile the object
system. The frame loop maps bank 6 and calls `Obj_UpdateAll` (`$8000`) at `$DB78`, its only
caller. It loads the objects' gravity (`$EF = $40`), then walks slots 1-15 with the cursor in `$4A`
and, for every slot whose status `$0700` has bit 7 set, jumps to the handler of its kind (`$0710`)
through the 67-entry table at `$802C`/`$806F` with the return address `$8022` pushed. The C keeps the
cursor in RAM because a handler could change it, writes the jump vector into `$00/$01` as the
original does, and enters an original handler with A = `$22`, X = slot, Y = kind and the loop's
flags (`Decomp_CallP`): a handler that adds without `CLC` (the Amazon boss's random step at
`$A664`) sees the carry of the caller for slot 1 and of the loop's `CPX #$10` after it.

Object layout used by the handlers (all slots, `objects.h`): `$0700` bit 7 live, bit 5 world X, bit 4
hidden, bits 0-3 the handler's step; `$0600` bit 7 on screen (set by the sprite builder, required by
every contact test), bit 6 facing right, bit 3 freed at the end of the animation, bit 1 blink;
`$06B0`/`$06C0` per-kind timers; `$07D0` hitbox (bits 0-6 index the half-height table `$F498` and
half-width table `$F4B8`, bits 4-6 harmful); `$07E0` direction (bit 0 right); `$07F0` the land record
the slot came from. A hit by Scrooge's cane or a thrown block counts in `$0690`; a handler that sees
it non-zero is beaten.

**Contact with Scrooge.** `Obj_TouchScrooge` (`$F436`) is the test every kind uses: a slot on
screen touches him when the vertical distance is below its half-height (8 px more when he crouches)
and the world-X distance at most its half-width. A harmful hitbox then starts his knock-back
(mode 6) unless `$7D` is running; during the power-up (`$75`) the object takes a hit instead.
Scrooge's own side, `$D202`/`$D291`, is still original (see above).

**Contracts.** The registers original callers use after a routine returns (the C callers use its
return value instead). Every handler is entered with A = `$22`, X = slot and Y = kind and returns
nothing the dispatch uses.

| Routine | In | Out | Notes |
|---|---|---|---|
| `Obj_UpdateAll` | P | X | X = 16 when it returns; the timer after it passes X to a sound request, which keeps it in `$00` |
| `Obj_TouchScrooge` | X = slot | X, C | C clear = contact; Y = hitbox index when the slot is on screen |
| `Obj_WalkRight`, `Obj_WalkLeft`, `Obj_Fall`, `Obj_Rise`, `Obj_ClimbDown`, `Obj_ClimbUp` | A = first probe, Y = probe count - 1, X = slot | X | X is what the probe returned |
| `Obj_TouchBlockH`, `Obj_TouchBlockV`, `Obj_HitOthers` | X = slot | C | C clear = a hit |
| `Obj_FindFreeSlot`, `Obj_FindFreeSlotY` | - | X or Y, C | C set = no free slot |
| `Obj_DistanceX`, `Obj_DistanceY`, `Obj_FaceScrooge` | X = slot | A | |
| `Obj_SetSpeedIndex`, `Obj_MarkConsumed`, `Hud_RefillHp`, `Money_Max` | A = index (speed), X = slot | A, Y | what the loops leave; original callers may use them |

**Hook sites: only the frontier.** A routine has a site only when original code calls it
(`tools/decomp_callers.py`); everything the C calls is a plain C call that starts with
`Decomp_Enter`, so check mode compares it on its own at every call. The 16 sites are the dispatch
`$8000`, `Obj_TouchScrooge`, the four movers and the two climbing steps (called by the 24 handlers that are still
original), and eight fixed-bank helpers that original code also calls: `$F312`, `$F6BD`, `$F7BE`,
`$F7CC`, `$F7DA`, `$F7F0`, `$F812` (also the flow's respawn) and `$F83F`. The flow's stop at
`$8000` (bank 6) is this site: `Obj_UpdateAll`'s hook ends an isolated run of the frame loop there
too, so the flow has no separate stop for it. Routines with a site that the C calls are compared
where original code calls them, not from the C.

**Routines** (90: 42 kind handlers, the dispatch and 47 inner routines). "Routes" is the number of
the 12 routes where check mode compared the routine, "Calls" the calls compared on them, "Pokes"
the `tests/decomp_test.py` cases that reach it from a poked state. All were compared equal on every
call and are RAM-identical in replace mode.

| Routine | Address | Site | Out | Status | Routes | Calls | Pokes |
|---|---|---|---|---|---|---|---|
| `Obj_UpdateAll` | `$8000` (bank 6) | yes | X | checked | 12 | 51,750 | - |
| `Obj_FindFreeSlotY` | `$80B3` (bank 6) | - | Y, C | checked | 3 | 32 | - |
| `Obj_HitOthers` | `$80C1` (bank 6) | - | C | checked | 4 | 170 | - |
| `Obj_SetMotionIndex` | `$818A` (bank 6) | - | - | checked | 5 | 971 | - |
| `Obj_Hopper` | `$8225` (bank 6) | - | - | checked | 2 | 1,566 | - |
| `Obj_Walker` | `$82AF` (bank 6) | - | - | checked | 1 | 625 | - |
| `Enemy_Walk` | `$82F9` (bank 6) | - | X | checked | 2 | 2,180 | - |
| `Obj_Thrower` | `$830E` (bank 6) | - | - | checked | 1 | 106 | - |
| `Obj_Shot` | `$8462` (bank 6) | - | - | checked | 1 | 24 | - |
| `Obj_Watcher` | `$8491` (bank 6) | - | - | checked | 2 | 515 | - |
| `Obj_Dangler` | `$84E5` (bank 6) | - | - | checked | 2 | 770 | - |
| `Obj_Snapper` | `$8567` (bank 6) | - | - | checked | 2 | 3,321 | - |
| `Obj_Block` | `$8668` (bank 6) | - | - | checked | 11 | 96,973 | - |
| `Obj_LaunchBlock` | `$86B5` (bank 6) | - | - | checked | 5 | 18 | - |
| `Obj_HitBlock` | `$86F7` (bank 6) | - | - | checked | 9 | 45 | - |
| `Obj_ResolveBlock` | `$870D` (bank 6) | - | - | checked | 9 | 45 | - |
| `Obj_Thrown` | `$8722` (bank 6) | - | - | checked | 5 | 186 | - |
| `Obj_Dropper` | `$8845` (bank 6) | - | - | checked | 1 | 1,263 | - |
| `Obj_ThrownFall` | `$88BC` (bank 6) | - | X | checked | 3 | 104 | - |
| `Obj_ThrownWalk` | `$88C8` (bank 6) | - | X | checked | 5 | 183 | - |
| `Obj_Break` | `$88DF` (bank 6) | - | - | checked | 8 | 50 | - |
| `Obj_Shatter` | `$88E4` (bank 6) | - | - | checked | 8 | 52 | - |
| `Obj_Defeat` | `$8932` (bank 6) | - | - | checked | 8 | 61 | - |
| `Obj_Defeated` | `$897B` (bank 6) | - | - | checked | 7 | 1,720 | - |
| `Obj_OpenBlock` | `$8A11` (bank 6) | - | - | checked | 6 | 11 | - |
| `Obj_BecomePickupNow` | `$8A32` (bank 6) | - | - | checked | 6 | 11 | - |
| `Obj_Pickup` | `$8A59` (bank 6) | - | - | checked | 11 | 5,458 | extra-dot, money-max |
| `Pickup_Take` | `$8B21` (bank 6) | - | - | checked | 10 | 23 | extra-dot, money-max |
| `Pickup_ExtraDot` | `$8B38` (bank 6) | - | - | checked (pokes only) | 0 | 0 | extra-dot |
| `Hud_RestoreDot` | `$8B82` (bank 6) | - | - | checked | 1 | 1 | - |
| `Money_Add` | `$8BBF` (bank 6) | - | - | checked | 10 | 19 | money-max |
| `Pickup_Treasure` | `$8C13` (bank 6) | - | - | checked | 5 | 6 | - |
| `Obj_HiddenItem` | `$8C45` (bank 6) | - | - | checked | 12 | 26,083 | - |
| `Obj_ShowItem` | `$8CAC` (bank 6) | - | - | checked | 11 | 79 | - |
| `Obj_BecomeBlock` | `$8CE1` (bank 6) | - | - | checked | 5 | 10 | - |
| `Obj_Chest` | `$8D19` (bank 6) | - | - | checked | 7 | 8,242 | - |
| `Obj_Door` | `$9065` (bank 6) | - | - | checked | 5 | 3,442 | - |
| `Obj_Lift` | `$9093` (bank 6) | - | - | checked (pokes only) | 0 | 0 | lift |
| `Obj_Crawler` | `$90EA` (bank 6) | - | - | checked | 1 | 788 | - |
| `Obj_Flyer` | `$916C` (bank 6) | - | - | checked | 1 | 1,763 | - |
| `Obj_Swing` | `$9225` (bank 6) | - | - | checked | 1 | 1,492 | - |
| `Obj_Charger` | `$9313` (bank 6) | - | - | checked | 4 | 5,882 | - |
| `Obj_Swooper` | `$9430` (bank 6) | - | - | checked | 4 | 2,370 | - |
| `Obj_BlockDweller` | `$9552` (bank 6) | - | - | checked | 2 | 2,556 | - |
| `Obj_DwellerLinkBlock` | `$9649` (bank 6) | - | - | checked | 2 | 12 | - |
| `Obj_Patroller` | `$96D5` (bank 6) | - | - | checked | 3 | 9,163 | - |
| `Obj_Pouncer` | `$973C` (bank 6) | - | - | checked | 1 | 792 | - |
| `Obj_PitJumper` | `$9956` (bank 6) | - | - | checked | 1 | 334 | - |
| `Obj_PitSplashOut` | `$9A1E` (bank 6) | - | - | checked (pokes only) | 0 | 0 | - |
| `Obj_PitSplashIn` | `$9A2A` (bank 6) | - | - | checked (pokes only) | 0 | 0 | - |
| `Obj_Bouncer` | `$9A81` (bank 6) | - | - | checked | 1 | 306 | - |
| `Obj_Leaper` | `$9B10` (bank 6) | - | - | checked | 2 | 4,279 | - |
| `Obj_SpawnDust` | `$9C0A` (bank 6) | - | - | checked | 2 | 31 | - |
| `Obj_LedgeWalker` | `$9D8A` (bank 6) | - | - | checked | 1 | 810 | - |
| `Obj_Pacer` | `$9DF7` (bank 6) | - | - | checked | 1 | 1,083 | - |
| `Obj_Bobber` | `$9E51` (bank 6) | - | - | checked | 2 | 4,271 | - |
| `Obj_Roller` | `$9FEC` (bank 6) | - | - | checked | 1 | 91 | - |
| `Obj_Boulder` | `$A024` (bank 6) | - | - | checked | 1 | 301 | - |
| `Obj_CeilingHopper` | `$A065` (bank 6) | - | - | checked | 2 | 2,772 | - |
| `Obj_CeilingWalk` | `$A1D4` (bank 6) | - | - | checked | 2 | 1,240 | - |
| `Obj_TreatSpout` | `$A1E9` (bank 6) | - | - | checked | 1 | 212 | - |
| `Obj_Treat` | `$A2BA` (bank 6) | - | - | checked | 1 | 196 | - |
| `Obj_RisingShot` | `$A5BE` (bank 6) | - | - | checked | 2 | 3,898 | - |
| `Obj_Dropping` | `$AAC8` (bank 6) | - | - | checked | 1 | 1,547 | - |
| `Obj_Thief` | `$AC3D` (bank 6) | - | - | checked | 1 | 198 | - |
| `Obj_Pickpocket` | `$ACBE` (bank 6) | - | - | checked | 1 | 408 | - |
| `Obj_PickpocketWalk` | `$AD56` (bank 6) | - | - | checked | 1 | 405 | - |
| `Obj_StealMoney` | `$AD6B` (bank 6) | - | - | checked (pokes only) | 0 | 0 | - |
| `Obj_WalkRight` | `$F0F8` | yes | X | checked | 11 | 10,787 | - |
| `Obj_WalkLeft` | `$F13B` | yes | X | checked | 11 | 13,317 | - |
| `Obj_Despawn` | `$F179` | - | - | checked | 3 | 27 | - |
| `Obj_ClimbDown` | `$F192` | yes | X | checked | 6 | 3,178 | - |
| `Obj_ClimbUp` | `$F1A8` | yes | X | checked | 7 | 2,944 | - |
| `Obj_Fall` | `$F1BE` | yes | X | checked | 11 | 19,854 | - |
| `Obj_Rise` | `$F1F9` | yes | X | checked | 10 | 2,157 | - |
| `Obj_SetSpeedIndex` | `$F312` | yes | A, Y | checked | 2 | 6 | - |
| `Obj_TouchBlockH` | `$F332` | - | C | checked | 11 | 41,904 | - |
| `Obj_TouchBlockV` | `$F3B7` | - | C | checked | 11 | 8,544 | - |
| `Obj_TouchScrooge` | `$F436` | yes | X, C | checked | 12 | 62,474 | - |
| `Obj_FaceScrooge` | `$F6BD` | yes | A | checked | 9 | 154 | - |
| `Obj_FindFreeSlot` | `$F7BE` | yes | X, C | checked | 12 | 970 | - |
| `Obj_DistanceY` | `$F7CC` | yes | A | checked | 2 | 158 | - |
| `Obj_DistanceX` | `$F7DA` | yes | A | checked | 10 | 7,864 | - |
| `Obj_MarkConsumed` | `$F7F0` | yes | A, Y | checked | 11 | 74 | - |
| `Hud_RefillHp` | `$F812` | yes | A, Y | checked | 12 | 28 | extra-dot |
| `Obj_DifficultAction` | `$F822` | - | - | checked | 11 | 90 | - |
| `Money_Max` | `$F83F` | yes | A, Y | checked (pokes only) | 0 | 0 | money-max |
| `Obj_Talker` | `$FDF5` | - | - | checked | 7 | 3,599 | - |
| `Obj_KeySpot` | `$FE50` | - | - | checked | 3 | 768 | - |
| `Obj_MovingPlatform` | `$FE69` | - | - | checked | 1 | 401 | - |

**Object kinds decompiled: 43 of the 67 entries of the handler table (64%).**

| Kind | Handler | Name | What it is |
|---|---|---|---|
| 1 | `$8225` | `Obj_Hopper` | enemy: walks and hops |
| 2 | `$82AF` | `Obj_Walker` | enemy: walks, turns at walls |
| 3 | `$830E` | `Obj_Thrower` | enemy: walks, hops, throws a shot |
| 4 | `$8462` | `Obj_Shot` | kind 3's shot |
| 5 | `$8491` | `Obj_Watcher` | enemy: stays, faces Scrooge |
| 6 | `$84E5` | `Obj_Dangler` | enemy: climbs to the ceiling, drops on Scrooge |
| 7 | `$8567` | `Obj_Snapper` | plant that catches Scrooge |
| 8 | `$8668` | `Obj_Block` | block: solid to other objects, launched or opened by the cane |
| 9 | `$8722` | `Obj_Thrown` | a launched block |
| `$0A` | `$8845` | `Obj_Dropper` | block that falls when Scrooge comes near |
| `$0B` | `$897B` | `Obj_Defeated` | a beaten enemy flying off |
| `$0C` | `$90EA` | `Obj_Crawler` | enemy: waits for Scrooge to pass, walks |
| `$0D` | `$916C` | `Obj_Flyer` | enemy: weaving flyer |
| `$0E` | `$9225` | `Obj_Swing` | swinging hold Scrooge grabs with Up |
| `$0F` | `$9093` | `Obj_Lift` | platform that rises and sinks |
| `$10` | `$9313` | `Obj_Charger` | enemy: charges at Scrooge, bounces off walls and blocks |
| `$11` | `$9430` | `Obj_Swooper` | enemy: drops to Scrooge's height and weaves |
| `$12` | `$9552` | `Obj_BlockDweller` | an enemy that lives in a block |
| `$13` | `$8A59` | `Obj_Pickup` | an item Scrooge takes by touching it |
| `$14` | `$8C45` | `Obj_HiddenItem` | an item that appears when touched |
| `$15` | `$8D19` | `Obj_Chest` | a chest the cane opens |
| `$18` | `$96D5` | `Obj_Patroller` | enemy: walks a ledge, turns at its edges and at walls |
| `$19` | `$973C` | `Obj_Pouncer` | enemy: flies in arcs, pounces on Scrooge |
| `$1A` | `$9956` | `Obj_PitJumper` | enemy: jumps out of a pit with a splash |
| `$1B` | `$9A81` | `Obj_Bouncer` | enemy: bounces towards Scrooge |
| `$1C` | `$9B10` | `Obj_Leaper` | enemy: walks a ledge, turns and leaps with a puff of dust |
| `$1E` | `$9D8A` | `Obj_LedgeWalker` | enemy: walks a ledge, turns at its edges |
| `$1F` | `$9DF7` | `Obj_Pacer` | enemy: paces left and right |
| `$20` | `$9E51` | `Obj_Bobber` | enemy: bobs up and down in place |
| `$23` | `$9FEC` | `Obj_Roller` | something that rolls left on the floor |
| `$24` | `$A024` | `Obj_Boulder` | a boulder that rolls right and pushes Scrooge |
| `$25` | `$A065` | `Obj_CeilingHopper` | an enemy that drops from the ceiling and hops at Scrooge |
| `$27` | `$A5BE` | `Obj_RisingShot` | a shot that flies up and sideways |
| `$2B` | `$AAC8` | `Obj_Dropping` | something that drops and breaks on the floor |
| `$2C` | `$A1E9` | `Obj_TreatSpout` | throws out four treats when Scrooge is near |
| `$2D` | `$A2BA` | `Obj_Treat` | a treat that gives an HP dot back |
| `$2F` | `$9065` | `Obj_Door` | a door: warp request |
| `$30` | `$AC3D` | `Obj_Thief` | enemy: paces, takes money on contact |
| `$31` | `$AC3D` | `Obj_Thief` | kind $30 with a shorter span |
| `$32` | `$ACBE` | `Obj_Pickpocket` | enemy: walks at Scrooge, takes money on contact |
| `$35` | `$FDF5` | `Obj_Talker` | a character that shows a message |
| `$39` | `$FE69` | `Obj_MovingPlatform` | a platform that carries Scrooge |
| `$41` | `$FE50` | `Obj_KeySpot` | a spot that needs the land's key item |

Written as C inside the routines above but without an entry of their own: the drop of a beaten
enemy (`$89DA`, entered with the caller's carry, which `Decomp_Enter` cannot pass; it is compared
through `Obj_Defeat`), the vertical moves `$F270`/`$F284`/`$F298` and the seven item actions at
`$8B71-$8BE7`.

Not decompiled yet: the other 24 kinds, among them the bosses (their hit routine `$F6D7` raises
`EVENT_BOSS_HIT`, so a C boss has to raise it itself). The spawner that calls `Obj_SpawnRecord`
(bank 6 `$AE20-$B163`) is in `level.c`. The movers and the climbing steps test the
background through the C of the probe `$D120` and its snaps (`collision.c`).

### Progress

`tools/decomp_progress.py` gives one number: the PRG bytes the routes and the poke cases
execute on the original (quick_verify's golden coverage, and `decomp_pokes.cover` beside it, which
`--record-pokes` writes), counted once with each instruction's length, and the share of them
inside routines with C. A `DecompRoutine` has no end field: the script derives each routine's code
from the ROM by following fall-through, branches and `JMP` from its entry up to `RTS`, `RTI`, an
indirect jump or another routine's entry, and counts as C the routines only C reaches; the
script's docstring gives every rule. `--rev` reads the C of another branch against the same
coverage.

On this tree (`py -3 -I tools/decomp_progress.py --golden DIR`): **99.3%, 26,789 of 26,968 executed
bytes**, in 375 routines (13 routes and 64 poke cases).

| Group | Files | Routines | Executed bytes | Share |
|---|---|---|---|---|
| player | `player.c`, `motion.c`, `sound.c` | 54 | 3,624 | 13.4% |
| objects | `objects.c`, `objcommon.c` | 112 | 12,073 | 44.8% |
| flow | `flow.c` | 142 | 3,794 | 14.1% |
| level | `level.c`, `collision.c` | 40 | 3,602 | 13.4% |
| system | `system.c`, `sprites.c`, `audio.c` | 25 | 3,385 | 12.6% |

No executed byte is shared with an original routine. The original code still reached is the reset
`$FEE0` (85 bytes), scheduler `$FF35` (72), credits' raster split `$E229` (19) and the frame loop's
return jump `$DC29` (3).

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
| `decomp_test --modes replace` | the check run (stock without check mode; check keeps the original's result, so its RAM is stock's) against `--decomp replace` (`--dt-ram-log`): CPU RAM outside page 1 equal after every frame; the main task's live stack equal on every frame where both runs are idle in the scheduler |
| `decomp_test --modes pokes` | 11 poked states (modes 5, 8, 10, 11 and 12, the last HP dot, slippery floor, held timers) in check mode and stock against replace, each from the latest idle golden save state before its first poke |
| `decomp_test --modes events` | NoDamage and JumpScale with the decompiled routines raising the events: check mode, and off against replace with the option on |
| `native_decomp_test` | the native runtime's replace mode against the recompiled program: RAM equal after every logic step on the 12 routes, with the exceptions in "On the native runtime" |
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

- Replace mode in long real play. One owner session on the window build's cycle host covered one
  land, about 8,400 frames (docs/validation.md). The native player's Decompiled code has been run
  only on the routes and hidden (`tests/native_decomp_test.py`, `tests/window_smoke.py`), not in real
  play.
- Paths no route or poke case reaches (table above). They are written from the code, and check mode
  will stop on any of them that is wrong, the first time a route reaches it.
