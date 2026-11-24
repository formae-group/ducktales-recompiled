# Native runtime: the shipped build without a cycle-level host

Status as of 2026-11-22: the player build. `ducktales.exe` in a window build runs the recompiled
program on the native runtime, draws it with the native renderer and plays it through the native synth
([The player build](#the-player-build)); the cycle host build is kept as `ducktales_cycle.exe` for
verification. `ducktales_native` (`-DDUCKTALES_NATIVE=ON`) is the headless prototype the route
comparison below runs.

In its faithful lag mode the native runtime matches the cycle build on every frame of every route:
CPU RAM, the cycle count and all CPU registers, 82,621 frames over the 12 routes in `tests/routes/`
(`tests/native_routes.py`). It runs 1,200-2,900 frames a second headless (the cycle host: about 140
on the same machine), with no instruction left
to NESRecomp's interpreter and no register read it does not model. Its default mode removes the
console's slowdown; `tests/lag_modes.py` measures that lag and shows the game's logic is unchanged
by its removal.

```
src/native/native_runtime.h   the machine side: bus, frame loop, lag modes, verification hooks
src/native/native_bus.c       NESRecomp's hw.h for the generated code: RAM, UNROM, controllers,
                              sprite DMA, APU write capture, cycle count, scheduler watch
src/native/native_video.c     PPU registers and memories, VBlank/NMI/$2002/sprite-0 timing
src/native/native_state.h     the interface for a renderer and a synth
src/native/native_audio.c     the synth: a frame's APU writes in, its samples out (audio.md)
src/native/native_hash.h      the per-frame state line both builds write (--ram-hash-out)
src/native/native_main.c      ducktales_native: input routes, logs, dumps
src/native/native_hooks.c     game.toml hook sites: the enhancements' callbacks
src/native/native_route.c     input routes (tests/routes) for both native programs
src/native/native_window.c    the player build's window (ducktales.exe)
src/native/native_present.c   the frame record drawn and offered as cyc_render.h
src/native/native_audio.c     the native synth: each frame's APU writes to samples
src/host/ducktales_settings.c config.ini [Game] and the game options, shared by both hosts
src/host/ducktales_ramhash.c  --ram-hash-out and --lag-log for the cycle host (headless only)
tools/native_compare.py       compares two per-frame state files field by field
tests/native_routes.py        native (faithful) vs cycle build on every route
tests/lag_modes.py            lag measurements and the faithful vs no-slowdown logic comparison
tests/native_player_test.py   the player's presented picture vs the cycle build's, off screen
```

## The seam: what the generated code needs

The shipped build's generated C (`build/*/cycle-ducktales/*/generated/game_cyc*.c`) reaches the
machine only through NESRecomp's `runner/cyc/hw.h` (NESRecomp 77ad5b8), via the inline CPU bus
helpers in `runner/cyc/cpu6502.h`:

| hw.h | Line | Called for | Native implementation |
|---|---|---|---|
| `hw_cycle_start(addr, kind)` | 61 | the start of every CPU bus cycle (`cpu_read` 102, `cpu_read_rom` 116, `cpu_write` 128, `cpu_fetch_rom_path` 181) | pending sprite DMA; scheduler watch |
| `hw_read`, `hw_read_rom`, `hw_read_code_ram` | 62-68 | the access | RAM, registers, PRG bytes, open bus |
| `hw_write` | 69 | the access | RAM, registers, DMA, controller strobe, bank select |
| `hw_cycle_finish(done)` | 70 | the end of the cycle | count the cycle, advance video time, sample /NMI |
| `hw_irq_line()` | 166 | IRQ polls | always false (UNROM has no IRQ, and no IRQ is taken on any route in the cycle build either) |
| `hw_frame_done` | 173 | checked at every instruction boundary | set when VBlank begins |
| `hw_prg_bank4(addr)` | 192 | dispatch to the compiled copy of the mapped bank | UNROM: switchable `$8000`, fixed `$C000` |
| globals | 97 and others | `hw_native_interval`, `hw_code_watch`, `cyc_hooks_armed`, `cyc_trace_*` | defined, inert (intervals and PRG guards compiled out) |

So the runtime has to provide a bus, not a PPU or an APU. `ducktales_native` links the generated
C, `cpu6502.c` (interrupt and power-on sequences) and `cpu6502_interp.c` (instruction templates for
code the recompiler did not reach), and nothing else from NESRecomp. It reports
`fallback_instructions=0` on every route: the interpreter is linked but never runs.

The cost of counting cycles stays in the generated code (each bus helper calls `hw_cycle_start` and
`hw_cycle_finish`), which is acceptable: counting cycles is not emulating hardware. Nothing in the
runtime clocks a dot or an APU tick; time-dependent answers (`$2002`, the NMI line, the frame end)
are computed from the count when the program asks.

### The function-level runtime (`runner/src`)

NESRecomp also has a non-cycle runtime: `runner/src/runtime.c` (`maybe_trigger_vblank`, emitted
at every instruction boundary, line 891 and 1025), `main_runner.c` (NMI from a VBlank callback),
`ppu_renderer.c` and `mapper.c`, which already handles mapper 2 (line 258 and 389). It was read, not
run, and not adopted:

- it runs the recompiler's function-level output, not the cycle-level C the shipped build
  compiles, so adopting it means a second recompilation of DuckTales (with its own seeds and its
  own coverage work) and a second program to verify;
- its frame is a CPU-cycle budget (`s_frame_budget = OPS_PER_FRAME`, line 603, or a dot clock),
  checked between instructions, not VBlank at the console's dot, so the sprite-0 wait, `$2002`
  polls and lag frames would not line up with the cycle build frame by frame, which is the
  verification this design rests on.

For UNROM it would still need DuckTales's `$2002` and sprite-0 behaviour (below), the OAM DMA stall
and a way to compare it with the cycle build. The bus approach reuses the program that is already
verified against the TriCNES oracle (`docs/validation.md`).

## What DuckTales needs from the hardware

All of this is read from the generated code, the disassembly (`tools/disasm6502.py`) and the
traces; the NMI audit (`ducktales_native --nmi-audit`) confirmed the RAM side.

- **NMI handshake.** The NMI (`$C000`) skips the sprite DMA while `$98` is non-zero (`$C034`; the
  task sets it around OAM building, `$DBC5-$DF19`), skips the whole PPU update block while `$E8` is
  non-zero (`$C01E`, `$C0CA`; set at `$C197-$C1A1`), serves the update requests in `$18-$1A`,
  increments `$92`, sets `$90 = $FF` and moves waiting task slots (`$80,X` state 1) to runnable
  when their counter `$81,X` reaches 0 (`$C0AE-$C0C8`). Then it runs the sound engine in bank 3
  (`$C10B`: `JSR $FFE0`, `$8003` per queued request, `$8000`).
- **Sprite-0 split.** With `$EA` set the NMI forces OAM entry 0 to the HUD sprite (`$C006-$C01B`),
  waits for the hit flag to clear and then to set (`BIT $2002` loops at `$C0D4`/`$C0D9`), and then
  writes `$2006`/`$2005` for the playfield below the HUD. A task does the same wait at
  `$E22B`/`$E230`.
- **`$2002` reads** at `$BABC` (bank 6), `$C02C`, `$C056`, `$C07B`, `$C0D4`, `$C0D9`, `$C0E2`,
  `$C209`, `$C330`, `$E22B`, `$E230`, `$FEEE`, `$FEF3`: latch resets before `$2006`, the two
  sprite-0 waits, and the reset handler's two VBlank waits.
- **`$2006`/`$2007`** writes (nametables, palette, CHR RAM uploads), `$2000`, `$2001`, `$2003`,
  `$2005`. No `$2004` or `$2007` reads (`unmodeled_reads=0`).
- **OAM DMA** from page 2 every NMI unless `$98`; the CPU stalls 513 or 514 cycles.
- **Controllers** `$4016` strobe and serial reads, read once per scheduler dispatch to slot 0
  (`$C1AB`).
- **APU**: `$4000-$4013` writes (the DMC is never used), `$4015` (always `$0F`), `$4017`. Never read.
- **UNROM** bank writes to `$8000-$FFFF`; CHR RAM; vertical mirroring.
- **Lag frames**: the game is a cooperative task scheduler (bank 7 `$FF35`; [functions.md](functions.md)).
  A frame whose task has not yielded when VBlank begins is a lag frame (below).

## Design

**Time.** The count n of CPU cycles is the clock. Cycle n spans master clocks (12n, 12n+12],
NESRecomp's alignment 0; the PPU's dots are at every fourth clock, so 3n dots have passed when cycle
n begins. A frame has four events at known dots: VBlank (line 241 dot 1), the odd-frame decision
(line 260 dot 340: an odd frame with rendering on is one dot shorter), the flag clear (line 261
dot 1) and the wrap (line 262). `video_advance()` runs the events whose dot has passed; it is called
at the end of every cycle and before every register access.

**Frame end.** VBlank sets `hw_frame_done`; the generated code stops at the next instruction
boundary, as with the cycle host. A `$2002`, `$2004` or `$2007` access completes seven clocks
into the following cycle, so on the console the next dot has already happened when that
instruction ends: if VBlank falls on that dot, the frame ends at this instruction rather than the
next (`notice_early_vblank`).

**`$2002`.** VBlank is read as the access starts and the sprite-0 flag as it ends; reading clears
VBlank on the next dot, so a read on the dot VBlank is set (or one dot before) loses that NMI, as on
the console. The flags clear at line 261 dot 1.

**Sprite-0 hit.** Worked out once per frame at line 0 from OAM entry 0, CHR RAM, the nametable and
the scroll the program set: the first pixel where sprite 0 and the background are both opaque
(left-column clipping, x = 255 and 8x16 sprites handled), then the dot that pixel is drawn on, plus
`NATIVE_S0_DELAY` (3) dots until a `$2002` sample sees it. On every route DuckTales sets the scroll,
`$2000` and OAM during VBlank and leaves them alone until the hit, so one evaluation per frame is
exact for it.

**`$2001`.** A write takes effect one dot after the access (`mask_at`), which decides the odd-frame
dot and whether a frame counts as rendered.

**Sprite DMA.** On the next read cycle the CPU stalls 514 cycles when the DMA starts on an even
cycle and 513 on an odd one; the 256 bytes land in OAM at once (nothing can observe them arrive).

**Memories.** CHR RAM, CIRAM (vertical mirroring), palette RAM (the `$3F10` aliases) and OAM are kept
exactly; `$2007` honours the +1/+32 increment.

**Not modelled** (not used by DuckTales, each checked): `$2004`/`$2007` reads (counted as
`unmodeled_reads`, zero on every route), `$4015` reads, sprite overflow, the DMC, IRQs, open-bus
decay, mid-frame scroll changes before the sprite-0 hit, PAL.

## Verification and every divergence point

`ducktales_native --ram-hash-out` and the cycle host's `--ram-hash-out` (headless builds,
`src/host/ducktales_ramhash.c`) write the same line per frame: hashes of CPU RAM, CIRAM, palette,
OAM and CHR RAM, the cycle count and PC/A/X/Y/S/P as VBlank ended the frame
(`src/native/native_hash.h`). `tests/native_routes.py` runs both on every route (the native one with
`--lag faithful`) and requires RAM, cycles and registers to be equal on every frame; both builds also
take `--trace-frame N --trace-out FILE` and write the same per-instruction line, so a mismatch is
found by diffing two traces.

```
py -3 -I tests/native_routes.py --build build/headless [--routes tests/routes/R.txt ...] [--reuse]
```

| Route | Frames | RAM, cycles, registers | PPU memory hashes |
|---|---|---|---|
| `african_mines_easy` | 7,398 | all frames | differ on 42 frames |
| `african_mines_intro` | 5,132 | all frames | 2 |
| `amazon_intro` | 5,092 | all frames | 14 |
| `full_african_mines` | 13,520 | all frames | 12 |
| `full_amazon` | 9,600 | all frames | 24 |
| `full_himalayas` | 6,200 | all frames | 24 |
| `full_moon` | 11,000 | all frames | 13 |
| `full_transylvania` | 7,120 | all frames | 4 |
| `himalayas_intro` | 5,152 | all frames | 5 |
| `moon_intro` | 5,172 | all frames | 8 |
| `re_amazon_moves` | 2,128 | all frames | 3 |
| `transylvania_intro` | 5,112 | all frames | 2 |

The PPU memory hashes are not part of the pass: the console performs a `$2007` write a few dots after
the store instruction, so a store that is the last instruction of a frame lands in the next frame
on the console and in this one here. The next frame agrees again, RAM is never affected, and a
renderer only reads the memories at line 0 of the following frame, after the write in both.

`ducktales_native --save-state F:FILE` (repeatable) writes `native_state_save` after frame F with
the frame to continue at, and `--load-state FILE` continues from it: the route's input from that
frame on, every log's frame numbers continuing the saved run's. The lag mode is not in the state, so
a run is loaded with the same `--lag`. `tests/quick_verify.py --record` saves one every 500 frames
of every route and replays each segment from its state against the golden frames (161 states, all
equal), and `--from-state` starts the native runtime from them.

Each place the native runtime can diverge, and what catches it:

| Divergence point | What goes wrong | How it shows | Found |
|---|---|---|---|
| sprite-0 hit pixel or delay | the NMI's `BIT $2002` loop exits one iteration early or late | cycles and PC at the frame end, then RAM | with delay 4 the read at cycle 31,036,408 of `african_mines_easy` frame 1042 missed a hit the cycle build saw (by one dot); 3 passes every route, 1-2 fail `amazon_intro` at frame 881, 5 at 1143 |
| frame end after a `$2002`/`$2004`/`$2007` access | one instruction more or less in the frame | cycles/PC at the frame end | `moon_intro` frame 946 (VBlank on the dot after a `$2007` write); a second frame end from the real VBlank event, fixed with `frame_ended` |
| DMA stall parity | 513/514 swapped | cycles drift by one from the first DMA | fixed first: with the parity reversed the routes diverged at the first sprite DMAs |
| `$2001` write latency and the odd-frame dot | frames one dot long or short | cycles drift where rendering is switched | covered by the transitions in every route |
| VBlank read suppression | an NMI lost or kept | RAM within a frame | exercised by the reset handler's `$FEEE` waits on every route |
| `$2007` timing at the frame edge | memories differ for one frame | PPU memory hash only | 2-42 frames per route, never RAM |
| code the recompiler missed | interpreter runs | `fallback_instructions` | 0 |
| a register read not modelled | wrong value | `unmodeled_reads` | 0 |

The generated code itself is the same file in both builds, so anything the program computes is
identical once timing and the bus agree; the table above is the whole list of places where the two
builds differ.

## The renderer and synth interface (`src/native/native_state.h`)

The runtime hands out, per machine frame (VBlank to VBlank):

- `NativeVideoMemory`: CHR RAM, both nametables, palette RAM and OAM, as the console holds them;
- `NativeVideoRegs`: `$2000`, `$2001`, `$2003`, and the scroll/address latch (t, v, fine X, w);
- `NativeFrame`: the memories and registers at line 0 (`start`, `start_regs`), whether rendering was
  on, the sprite-0 split (`split_line`, `split_x`), every PPU register write made while lines 0-239
  were drawn with its scanline and dot (already in raster order), every APU register write with its
  CPU cycle in the frame, the frame's length in cycles and `held_cycles` (below).

A renderer draws frame N from `start`, applying `ppu_writes[]` in order; one that only wants the HUD
split uses `split_line`. A synth plays `apu_writes[]` at their cycle offsets (1,789,773 Hz); when the
no-slowdown mode has stretched a frame it plays it in `cycles - held_cycles` and scales the offsets
to match, since every frame is still presented for one 60 Hz period. The synth exists:
`native_audio_frame()` in `src/native/native_audio.h` ([audio.md](audio.md#native-synth)), and
`ducktales_native --wav-out FILE` records it. Nothing in the interface is read back by the game, so a
renderer or synth cannot change the machine.

## Lag

The game runs as cooperative tasks: a task runs from the scheduler's dispatch (`$FF6A` starts one,
`$FF7C` resumes one) until it yields (`task_yield` `$FFAC`) and the scheduler starts its next pass at
`$FF38`; the NMI wakes the waiting slots. On every measured route only slot 0, the main task, is
alive. A **lag frame** is a VBlank that arrives while the task is still running: the NMI interrupts
it, and the picture repeats because the task had not finished building it.

### Measured on the full_* routes (faithful mode)

`tests/lag_modes.py` (`ducktales_native --lag faithful --lag-log`). "Over" is the CPU cycles of task
work that ran after the missed VBlank, the NMI handler excluded; a frame has 29,781 cycles. Lag
"behind a blank screen" is the same thing with rendering off: land loads and transitions that run
for several frames on purpose.

| Route | Gameplay lag frames | Over: mean / max | Behind a blank screen |
|---|---|---|---|
| `full_amazon` | 40 | 1,118 / 4,508 | 55 frames in 6 stretches |
| `full_transylvania` | 2 | 321 / 465 | 48 in 5 |
| `full_african_mines` (incl. Transylvania) | 13 | 486 / 1,160 | 133 in 11 |
| `full_himalayas` | 16 | 748 / 3,423 | 40 in 4 |
| `full_moon` | 15 | 896 / 3,224 | 46 in 5 |

Where (land, map row `$F1`, screen `$2D`), frames, and cycles over (max / mean):

| Land | Row | Screen | Frames | Over max / mean |
|---|---|---|---|---|
| Amazon | 5 | 28 | 1 (446) | 489 |
| Amazon | 5 | 29 | 7 (676-713) | 1,154 / 658 |
| Amazon | 5 | 2A | 3 (905-913) | 527 / 338 |
| Amazon | 5 | 2D | 4 (1705-1742) | 1,041 / 807 |
| Amazon | 5 | 2E | 3 (1789-1805) | 499 / 255 |
| Amazon | 3 | 1B | 1 (3239) | 788 |
| Amazon | 3 | 1C | 1 (3638) | 1,947 |
| Amazon | 3 | 1A | 8 (4145-4211) | 4,508 / 1,919 |
| Amazon | 3 | 19 | 11 (4215-4477) | 4,170 / 1,477 |
| Amazon | 2 | 13 | 1 (5843) | 290 |
| Transylvania | 5 | 2C | 1 (1778) | 465 |
| Transylvania | 4 | 25 | 1 (2222) | 177 |
| African Mines | 0 | 02 | 1 (9649) | 320 |
| African Mines | 2 | 16 | 1 (10821) | 1,037 |
| African Mines | 2 | 15 | 9 (10869-10995) | 1,160 / 481 |
| Himalayas | 0 | 03 | 4 (1206-1217) | 3,423 / 1,640 |
| Himalayas | 4 | 23 | 11 (2129-2280) | 1,032 / 460 |
| Himalayas | 2 | 13 | 1 (3989) | 347 |
| Moon | 4 | 20 | 2 (1298, 6320) | 366 / 242 |
| Moon | 2 | 11 | 11 (2577-3668) | 3,224 / 971 |
| Moon | 2 | 10 | 1 (2588) | 1,068 |
| Moon | 5 | 2D | 1 (8397) | 1,212 |

Every gameplay lag is a single frame: the worst overrun, 4,508 cycles (15% of a frame), never
reaches the next VBlank. Frame numbers are route frames (the African Mines rows come from
`full_african_mines`, which plays Transylvania first).

### The two modes

`native_set_lag_mode()` (`native_runtime.h`), `ducktales_native --lag faithful|none`:

1. **Faithful** (`NATIVE_LAG_FAITHFUL`): VBlank and the NMI where the cycle count puts them, lag
   frames as on the console. Verification uses it; RAM equals the cycle build on every frame.
2. **No slowdown** (`NATIVE_LAG_NONE`, the default): when VBlank is due while the task is still
   running and the picture is on (NMI enabled, background or sprites shown), VBlank is held back,
   the PPU staying in post-render line 240, until the task yields at `$FF38`. Then VBlank, the NMI
   and the frame handoff happen as usual. The hold is released early if the program polls `$2002`
   twice during it (it would wait on a PPU that is not moving) or after one frame of extra time
   (`NATIVE_DEFER_CAP_CYCLES`); neither happened on any route.

The hold applies only with the picture on. The long tasks behind a blank screen (land loads,
transitions) keep the console's frame count, so their length and the music that plays under them
do not change; they are not slowdown anyone sees.

Result on the full_* routes (same input per logic step as the faithful run, below):

| Route | Gameplay lag left | Frames held | Longest hold (cycles) |
|---|---|---|---|
| `full_amazon` | 0 | 50 | 3,216 |
| `full_transylvania` | 0 | 1 | 460 |
| `full_african_mines` | 0 | 15 | 1,575 |
| `full_himalayas` | 0 | 17 | 3,375 |
| `full_moon` | 0 | 17 | 1,564 |

Holds can land on other frames than the faithful run's lags: once the first lag is gone, every
later logic step happens at a different point of its frame.

### What changes without lag, and why the game logic does not

What a lag frame does on the console, read from the NMI and the scheduler:

- the NMI runs in the middle of the task's logic step: `$92` is incremented, the sound engine
  advances one tick, the sprite DMA and the PPU update are skipped if the task holds `$98` or `$E8`
  (the picture keeps the previous step's sprites), and the task's own slot is not touched because
  it is running, not waiting;
- the logic step therefore spans two NMIs: the game advances one step in two frames, which is the
  visible slowdown, and the controller is read once for those two frames (`$C1AB` runs per
  dispatch to slot 0), so one frame's input is never seen.

Without lag every logic step has exactly one NMI, which is what the console does on every frame it
does not lag. So the changes, relative to the console, are:

- **enemy and boss timing**: they move in the task, once per logic step; per step nothing changes.
  Relative to frames they no longer lose a frame where the console lagged.
- **frame counters**: `$92` is written by the NMI and read by nothing (the NMI audit counts zero task
  reads after the first NMI on every route), so it cannot affect the game. `$95` is the sprite
  builder's counter (bank 5 `$8069`), incremented once per logic step; it drives sprite flicker
  order (`$806B`, `$812B`), animation phases (bank 6 `$A67E`, `$AA21`, `$EF70`), the low-health beep
  every 16 steps (`$E2F2`, sound `$36`) and a reset at `$B4` (`$D5E8`). It counts logic steps in
  both modes, so all of these keep their per-step timing.
- **RNG**: no random number generator is documented or found. The NMI writes only the bytes in its
  audit (scheduler `$80/$81/$90`, `$92`, the update flags `$18-$1A`, the sound queue `$C7-$CF`, sound
  engine RAM `$D0-$E5` and `$0370-$03ED`, OAM entry 0, the stack); nothing random advances in the NMI,
  so anything random would advance in the task, per logic step, and is covered by the comparison
  below.
- **music** runs in the NMI, one tick per frame in both modes. Relative to the logic it keeps the
  console's no-lag relation.
- **input**: every frame's buttons are read; none is dropped.

The comparison (`tests/lag_modes.py`): the faithful run records the buttons latched at every
controller strobe and dumps RAM each time the task yields; the no-slowdown run is fed the same
buttons strobe by strobe and dumps RAM the same way, so step k of both saw the same input. Over
every step of the five routes, the only RAM that differs is:

| Bytes | What | Why it may differ |
|---|---|---|
| `$19` | PPU update request | cleared by the NMI that performs it; a lag frame's NMI comes mid-step |
| `$90` | scheduler NMI flag | set by the NMI |
| `$C8`, `$CB`, `$CE`, `$CF` | sound request queue slots | drained by the NMI |
| `$0372-$0377` | sound engine state read by the task | advanced by the NMI |

plus bytes only the NMI uses (160-169 per route, never read by the task) and the stack. Positions,
HP, lives, money, timers, enemies, bosses, `$95` and the OAM buffer are equal on every one of the
47,001 logic steps compared. Removing the lag removes the slowdown and nothing else.

A route's per-frame input cannot be replayed in the no-slowdown mode as it is: each lag frame the
console had shifts every later button press by one logic step, and the route soon goes somewhere
else. That is why the comparison replays per strobe.

### The same thing on the cycle host, without changing NESRecomp

NESRecomp's cycle host already has an enhancement for this: `cyc_set_extra_scanlines(N)`
(`cyc_core.h:183`, `--extra-scanlines N`). It adds N blank lines after line 240 of every frame,
before VBlank (`hw_ppu.c:985`), and pauses the APU across them (`hw_machine.c:57`); the window keeps
60 frames a second. It is supported for UxROM (`hw_machine.c:864`: mapper 1, 2 and 4). Each line is
113.67 CPU cycles; the worst measured overrun, 4,508 cycles, is 40 lines.

The port exposes it as **No slowdown**: a Settings menu row (On/Off), config.ini `[Game] NoSlowdown =
on|off`, 128 lines (`DT_NO_SLOWDOWN_LINES`, 14,549 cycles, three times the worst overrun). It is on
by default in the window build and off in the headless build, which every verification runs, so
`tests/verify_routes.py` is unaffected. `--extra-scanlines N` still overrides it for one run.

How it behaves (`ducktales.exe --lag-log`, which classifies a frame as lag when the instruction
VBlank lands on is outside the scheduler's `$FF38-$FF7C`, with `$FF`/`$FE` as the NMI/picture state;
on stock timing it agrees with the native runtime's exact lag flags on 9,594 of 9,600 frames of
`full_amazon`: the other six are power-on frames 0-4, before the shadows are set, and frame 910,
where the task overran by a single cycle):

| `full_amazon` | Gameplay lag frames | Lag frames behind a blank screen |
|---|---|---|
| stock | 39 (the native runtime counts 40: frame 910 above) | 55 |
| 128 extra scanlines | 0 (frames 400-2000 cross the same screens 28-2E that lag 18 times at stock) | 31 |

Power-on frames 0-4 and the reset handler's VBlank wait (`$FEEE`, a reset the route triggers in the
128-line run) are left out of both counts.

With extra scanlines the route desyncs after about frame 2000 for the same reason as above, so
only the stretch it still follows is comparable. Differences from the native no-slowdown mode:

- the extra lines are added to every frame, whether it needs them or not: 49% more CPU cycles per
  frame, most of them in the scheduler's idle loop. Headless, `full_amazon` ran at about 140
  frames a second stock and 110 with 128 lines (measured while another verification ran, so only
  the ratio means much); the window needs 60;
- they are also added behind blank screens, so land loads and transitions take fewer frames than
  on the console (31 lag frames instead of 55 on `full_amazon`), and the music under them plays for
  correspondingly less time;
- the APU is paused across the lines, so a sound effect started in the task during them starts at
  the end of the frame's extra time instead of mid-frame (by ear: not verified).

## The player build

`-DDUCKTALES_PLAYER=native` (the default) makes the window build's `ducktales.exe` the
`ducktales_player` target, and the cycle host target `ducktales_cycle.exe`;
`-DDUCKTALES_PLAYER=cycle` swaps the names back (the native one is then `ducktales_player.exe`).
The install rules and the CPack ZIP package whichever is `ducktales.exe`. Headless builds are
unchanged: `build/headless/Release/ducktales.exe` is the cycle host every verification runs.

The player links the generated C, `src/native/`, `src/game`, `src/host/ducktales_sprites.c`,
`src/host/ducktales_settings.c`, `src/host/ducktales_main.c`, `src/ui` and Dear ImGui. From
NESRecomp it takes the CPU sequences and instruction templates, the hook registry
(`runner/src/mod_function_hooks.c`), the settings file and bindings (`cyc_settings.c`,
`cyc_input.c`), the PNG writer and the toast font, and no part of the cycle host (no `cyc_host.c`,
`cyc_sdl.c`, PPU, APU or mapper).

| Part | How it works on the native runtime |
|---|---|
| Frame loop | `native_window.c`: one `native_run_frame()` per 60.0988 Hz period, at its deadline; VSync as `[Display] VSync` (resolved from FramePacing at launch); fast-forward (Tab, held) and `--uncapped` run unpaced and present at most 60 pictures a second |
| Picture | `native_present.c` replays the frame's register writes over the memories at line 0 (`NativeFrame`), with the PPU's scroll counters, into the per-line record the cycle host keeps (`cyc_frame_lines`), fills `NvVideo` from it (`nv_cycle_fill`) and draws it with `nv_render`. It serves the `cyc_render.h` calls `ui/app_present.c` and `host/ducktales_sprites.c` make, so the HUD split, overscan, scaling, 8:7 and No sprite flicker are the same code as in the cycle build |
| Menu, settings, input | `app_ui_event`, `app_ui_input`, `app_ui_frame_begin`, `app_ui_audio` and `app_ui_present` called where the cycle host's extras call them; config.ini read and written with `cyc_settings` (on F11 and at exit, reconciled by `app_settings.c`); bindings evaluated with `cyc_input_eval` |
| No slowdown | `[Game] NoSlowdown`: on (default) is `NATIVE_LAG_NONE`, off is `NATIVE_LAG_FAITHFUL` |
| Enhancements | `native_hooks.c` binds the plugins `src/game` registers (`mod_function_hooks.h`) to the `game.toml` sites, arms `cyc_hooks_armed` while one is enabled, and runs the callbacks at the instruction boundary when the content key holds, returning from a handled routine as its RTS would: the cycle host's rules (`cyc_hooks.c`) |
| Decompiled routines | Settings → Gameplay → Decompiled code (`[Game] DecompiledCode`, off by default): with No slowdown on, the routines decompiled to C run alone in place of the recompiled ones (`src/decomp/decomp_native.c`, the native backend of the decompilation layer; docs/decompilation.md "On the native runtime"). With No slowdown off the toggle is disabled and its (?) says why: the C does not take the console's cycles, so with the console's lag the game would lag where the console does not. `ducktales_native --decomp replace` does the same headless, with `--lag none` only. A window session that ran the C writes `decomp-session-<time>.txt` at exit beside the cycle host's replays (`NESRECOMP_REPLAY_DIR`, `NESRECOMP_DIAGNOSTICS_DIR` or `<exe dir>/diagnostics`): the frames played and how many ran the C, the decomp error that stopped the game if any, and each routine's `decomp:` line as the cycle host prints it |
| Save states | F8 / F9: `<exe dir>/saves/DuckTales.native-state`, CPU registers and interrupt lines, CPU RAM, the bus (bank, cycle count, DMA, controllers, lag state) and the video state (memories, registers, frame timing), with the generated program's PRG hash; a toast confirms |
| Sound | `native_audio.h`: `native_audio_frame(frame, out, cap)` per frame, queued at 48 kHz with at most 100 ms waiting (the native synth, docs/audio.md "Native synth") |

Off screen (`--frames N [--input ROUTE] [--lag none|faithful] [--present-out FILE --present-every K]
[--state-check F]`) the player writes the picture the window would show at 1x, named as the cycle
host's `--present-out` names it, and can check a save state round trip.

### Its verification

- `tests/native_player_test.py --build build/windows`: `ducktales.exe --lag faithful` and
  `ducktales_cycle.exe --extra-scanlines 0` off screen on the same route, the presented pictures
  compared pixel for pixel (results in [validation.md](validation.md)).
- `tests/window_smoke.py --build build/windows` on `ducktales.exe` (and `--exe ducktales_cycle.exe`).
- `tests/native_routes.py`, so any change to the bus is held to the cycle build frame by frame.
- `tests/native_decomp_test.py --build build/headless`: the decompiled code against the recompiled
  program, RAM after every logic step on the 12 routes (docs/decompilation.md "On the native
  runtime"); `window_smoke` checks that the setting runs the C with No slowdown on and not with it off.

## Next steps

1. Listen to the native synth in the player's window (it is compared only by `tests/audio_compare.py`).
2. The split column from the write's dot (`NvSegment.split_column`), so the original HUD split line
   jitters as on the console; with the default clean split it is not shown.
3. Keep `tests/native_routes.py` and `tests/native_player_test.py` in the verification set;
   `tests/quick_verify.py` runs the same comparison against recorded golden hashes in seconds.

## Not verified

- Sound by ear: `--wav-out` is compared with the cycle host APU by `tests/audio_compare.py`,
  but nobody has listened to it in a window.
- The player's window as a person sees it (pacing on the 144 Hz monitor, VSync, full screen, gamepads,
  F8/F9 by hand): only hidden and off-screen runs were made.
- `HudSplit = original` on the native player: the split line is drawn at a line boundary
  (no split column), so it differs from the cycle build's on that one line; not compared.
- Save state files from one build loaded in another build (the format is the build's own structures,
  checked by size and PRG hash only).
- Real-time pacing and audio of the No slowdown setting in the window (144 Hz monitor, by ear):
  needs a person at the screen. The window build was only run hidden (`tests/window_smoke.py`).
- Lag outside the five full_* routes (other screens, Easy/Difficult, more enemies on screen).
- The sprite-0 delay of 3 dots is fitted to the cycle build on these routes, not derived from first
  principles; a hit at a different dot phase than any route produces could still disagree, and
  `tests/native_routes.py` would show it.
