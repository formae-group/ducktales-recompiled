# Mesen2 as a second reference

Status as of 2026-11-21. Mesen2 is an emulator independent of NESRecomp and of the TriCNES-derived
oracle that `tests/verify_routes.py` uses. `tests/mesen_compare.py` replays every route in
`tests/routes/` in both `ducktales.exe` and Mesen2 and compares the machines frame by frame.

**Result in one paragraph.** At `--align 1`, `2` or `3`, game state (CPU RAM outside the stack
page) matches Mesen2 on more than 99% of frames in five of the seven routes. In two routes
(`himalayas_intro` from frame 4426, `moon_intro` from frame 2148) one emulator takes a lag frame
the other does not, and the runs stay apart after that. At `--align 0`, which is `ducktales.exe`'s
default, the game boots one frame later than in Mesen2, so every route is shifted by one frame from
frame 4 on. Every mismatch we traced comes back to one mechanism: Mesen2 and our machine disagree
about the exact CPU cycle on which the start of VBlank (scanline 241 dot 1) becomes visible,
through a `$2002` read or through the NMI. Neither emulator is proven right; settling that needs
the hardware (see *Unknown*).

## Setup

```text
py -3 -I tools/mesen/fetch_mesen2.py            # Mesen 2.1.1 into build/tools/mesen2 (gitignored)
py -3 -I tests/mesen_compare.py --ignore 0100-01FF --domains ram,oam,palette,ciram
```

- `fetch_mesen2.py` downloads the official Windows release
  (`github.com/SourMesen/Mesen2/releases/download/2.1.1/Mesen_2.1.1_Windows.zip`), checks its
  SHA-256 (`23ccc2bc…5cbe`, recorded in the script), extracts `Mesen.exe` (a single self-contained
  executable; no .NET install needed) and writes an empty `settings.json` beside it. That file
  turns on portable mode, so Mesen keeps its DLLs, `Saves/` and `Debugger/` in that folder and
  not in Documents. It also stops the test runner from opening the first-run setup dialog
  (Mesen2 2.1.1 `UI/Program.cs`, `UI/Config/ConfigManager.cs` `HomeFolder`).
- The ROM is the one in `roms/` with the supported SHA-256, whatever its name; nothing else is downloaded.
- Each `ducktales.exe --frame-log` run first writes about 130 KB per frame (CHR RAM and the
  picture are always included), up to 1 GB for the longest route. The script shrinks each log
  to RAM, OAM, palette and optional CIRAM right after its run. It refuses to start unless
  `--min-free-gb` (default 8) stays free after the temporary logs of `--jobs` (default 2)
  parallel runs. One full run of all routes at four alignments takes about 25 minutes. The
  compacted logs in `captures/mesen/` take about 0.8 GB.

## How the two runs are made comparable

**Mesen side.** `Mesen.exe --testrunner tools/mesen/replay_route.lua ROM` runs headless at
maximum speed until the script calls `emu.stop`. The comparison passes these switches:
`--debug.scriptWindow.allowIoOsAccess=true` (the script needs `io`/`os`),
`--nes.region=Ntsc`, `--nes.randomizeCpuPpuAlignment=false`,
`--nes.randomizeMapperPowerOnState=false`, `--emulation.runAheadFrames=0`,
`--nes.ramPowerOnState=AllZeros|AllOnes` and `--timeout=900`. Parameters reach the script
through environment variables (`DT_MESEN_*`, documented at the top of the script). Mesen changes
its working directory to its home folder, so every path it is given is absolute. Two Mesen
instances sharing one home folder make each other fail, because each start re-extracts
`MesenCore.dll` there. The script therefore runs Mesen one instance at a time. The script writes
the same `CYCFRAME` v2 format as `ducktales.exe`, so `tools/framelog.py` reads either log.

**Where a record is taken.** In Mesen2 2.1.1 (`Core/NES/NesPpu.cpp`) the PPU raises the
`EndFrame` event at scanline 240 dot 0, inside `SendFrame`, before it increments its frame
counter. The Lua script takes record *k* at the *k*-th `EndFrame` (counting from 0).
`ducktales.exe --frame-log-at mesen` takes record *k* at the first instruction boundary after
scanline 240 dot 0 of its frame *k*. Its frame 0 starts at power-on with the PPU at scanline 0
dot 0 (`hw_ppu.c` `ppu_power_on`). Mesen powers on at scanline -1 dot 340
(`NesPpu::Reset`), one dot earlier. Measured: the CPU cycle counters agree to the cycle across
whole frames (for example, both emulators enter the NMI handler on cycle 176307 in
`re_amazon_moves` frame 6). At every record, ours is 1 to 4 cycles later than Mesen's. That
lag is the distance to our next instruction boundary: Mesen raises its event in the middle of
an instruction.

**Input latching.** Mesen2 polls the controllers once a frame at scanline 241 dot 0
(`NesConfig.InputScanline` defaults to 241; `NesPpu.cpp` calls `UpdateInputState`). It then
raises `InputPolled`. The script sets the buttons in that callback (`emu.setInput`), for frame
*k* + 1 after record *k*. `ducktales.exe` sets frame *k* + 1's buttons when its frame *k* + 1
begins at scanline 241 dot 1 (`cyc_host.c` `input_tick`, `hw_ppu.c`). The game strobes the
controller later in VBlank, so both runs present the same buttons to the same game frame. The
one exception is frame 0: Mesen polls for the first time after record 0, so a route that holds
buttons on frame 0 would differ. No route in `tests/routes/` does. The Lua replay accepts
`<frame> <BUTTONS>` and `1:`/`2:` port prefixes. It refuses anything else (logical input,
cheats, disk commands) rather than replaying it differently.

**Power-on state.** These defaults differ:

| | ducktales.exe | Mesen2 2.1.1 (defaults) |
|---|---|---|
| CPU RAM | runs of $F0/$0F (AccuracyCoin console pattern) | all zeros (`RamPowerOnState = AllZeros`) |
| CIRAM | the same $F0/$0F pattern | `RamPowerOnState` fill (zeros) |
| Palette RAM | a fixed table from a reference console | Mesen's own fixed table |
| OAM | zeros | `RamPowerOnState` fill |

`--ram-init zeros` (the default) or `ones` sets both emulators to the same CPU RAM fill;
`native` leaves each at its default. Measured on `amazon_intro` at align 1: with `native`,
CPU RAM differs in 1792 non-stack bytes on frames 0-3 only. From frame 4 the result is identical
to the `zeros` run. So the game clears its RAM within its first four frames, and the power-on
fill has no lasting effect. With `ones`, Mesen also fills OAM with $FF, which shows up as an OAM
difference until the game's first sprite DMA. The palette and CIRAM tables always differ for the
first frames, until the game writes them: at align 1-3, palette for 7 frames (11 in
`himalayas_intro`) and CIRAM for 5-9 frames. In `moon_intro` the counts are higher (228 and
1,218) because they include its later divergence. The comparison reports palette RAM as the PPU reads it, with $3F10/$14/$18/$1C read
through to $3F00/$04/$08/$0C.

## Results

`py -3 -I tests/mesen_compare.py --ignore 0100-01FF --domains ram,oam,palette,ciram`,
`--ram-init zeros`, all routes, all four alignments (output in
`captures/mesen_all_zeros.txt`). "ram*" is CPU RAM without the stack page $0100-$01FF.
Counts are frames with any difference.

| Route | Frames | align 0 | ram* align 1-3 (first frame) | OAM align 1 / 2 / 3 |
|---|---|---|---|---|
| `re_amazon_moves` | 2,128 | shifted by one frame from frame 4 | 4 (220) | 0 / 0 / 0 |
| `transylvania_intro` | 5,112 | shifted | 13 (220) | 0 / 0 / 0 |
| `african_mines_easy` | 7,398 | shifted | 16 (260) | 0 / 21 / 2 |
| `amazon_intro` | 5,092 | shifted | 24 (220) | 23 / 32 / 24 |
| `african_mines_intro` | 5,132 | shifted | 34 (220) | 9 / 9 / 10 |
| `himalayas_intro` | 5,152 | shifted | 749 (220); every frame from 4426 | 708 / 708 / 708 |
| `moon_intro` | 5,172 | shifted | 3,039 (220); nearly every frame from 2148 | 3,021 / 3,021 / 3,023 |

Full CPU RAM, stack page included, differs on most frames at every alignment (for example
2,014 of 2,128 frames in `re_amazon_moves` at align 1). ram* counts are the same at align 1,
2 and 3 in every route.

### What the differences are

1. **Boot one frame late at align 0.** Before it starts, the game waits for VBlank by polling
   `$2002`. At align 0, our poll that falls on the first VBlank (CPU cycle 27393) reads $00,
   and so do the later ones: the read lands on the dot where the flag is set and suppresses it.
   At align 1 the same read returns $80. Mesen's matching read returns $80 at scanline 241 dot
   4. Our boot then takes one more frame, and Mesen's frame *k* equals our frame *k* + 1 from
   frame 4 on (the script prints this hint). The route inputs then hit different game frames,
   and the runs separate. Evidence: `--trace-frame 0/1` of `ducktales.exe` at align 0 and 1
   (no `$2002` read with bit 7 set in frame 0 at align 0), and a Lua read callback on `$2002`
   in Mesen.

2. **Stack bytes (every route, from frame 7).** These are interrupt return frames that the NMI
   pushes ($01F9-$01FE; $01B2-$01BC on a task's stack). Tracing `re_amazon_moves` frames 6 and
   7 with `tools/mesen/trace_diff.py` shows this. Through all of frame 6 both emulators run the
   same instructions on the same cycles, measured from the NMI handler's start ($C000, the NMI
   vector's target). The only gap is where each emulator accounts the 513-514 cycles of the
   sprite DMA started at $C03D. Frame 6 ends in the scheduler's idle loop ($FF35-$FF4B scans
   the task slots $80-$8F). There Mesen takes the frame 7 NMI after a 2-cycle instruction that
   starts at cycle 206076, at scanline 240 dot 340. Ours runs one more 2-cycle instruction
   first. The VBlank edge (scanline 241 dot 1) falls inside cycle 206076, and Mesen counts it
   for that cycle's interrupt poll while we do not. From then on, the idle loop's X/Y registers
   at each NMI differ between the emulators. The NMI handler saves and restores them, and they
   reappear in the stack bytes and in where the scan resumes.

3. **Short, self-healing ram* runs (frames 220-223 in every route, and isolated frames
   later).** Here the record catches work that is still in progress at scanline 240. That
   happens when the main thread is still running at VBlank, so the NMI interrupts a task (stack
   pointer around $B9, not the idle loop), and it interrupts it at a different point in each
   emulator. In frame 220 of `re_amazon_moves` the interrupted A/Y differ ($FF/$13 ours,
   $1E/$15 Mesen). After the RTI the code follows different paths for a while. By frame 224 the
   state is equal again. Where these frames fall depends on the route's load. OAM differences
   at align 1-3 are 8-byte sprite records copied from $0200-$02FF in the same frames.

4. **Persistent one-frame shift in `himalayas_intro` (frame 4426).** In frame 4425 the game
   waits for VBlank with a `$2002` loop at $FEEE-$FEF3 (NMI handling is not used for this
   wait). The read on CPU cycle 131776337, the same cycle number in both emulators, returns $80
   in Mesen (the next instruction starts at scanline 241 dot 4) and $00 in ours. Ours waits one
   more frame. From frame 4426 to the end, Mesen's frame *k* equals our frame *k* + 1 byte for
   byte (outside the stack). This is the boot race of item 1 again, this time at align 1. No
   alignment of ours reproduces Mesen here: ram* counts are the same at align 1, 2 and 3.

5. **Divergence in `moon_intro` (frame 2148).** In frames 2147 and 2148 the non-idle code is
   the same instruction sequence in both emulators. But the different idle-loop scan positions
   of item 2 change how soon a ready task is picked up. Near the end of frame 2148, the task
   that runs at $FF9F-$FFBD starts 42 cycles later in ours than in Mesen (Mesen reaches $FFB0
   at scanline 240 dot 210). Mesen finishes that work before VBlank and ours does not. The runs
   are first one frame apart, then the remaining inputs land on different game frames and the
   gameplay separates. After about frame 3900 the difference shrinks to 27-51 bytes per frame.
   *Hypothesis:* the route has reached a state that inputs no longer change (game over / title),
   but this was not checked.

## Tools

- `tests/mesen_compare.py`: per route and alignment, the first divergent frame and bytes for
  each domain, the number of divergent frames, a one-frame-shift hint, and the cycle-counter
  difference at the first and last record. Options: `--routes`, `--frames`, `--from-frame`,
  `--align`, `--ram-init`, `--domains`, `--ignore LO-HI` (adds the "ram*" line and makes it
  the pass criterion for CPU RAM), `--reuse` (compare existing logs), `--trace-frame N` (both
  instruction traces of frame N, next to the logs). Exit status 0 only if every compared domain
  matched on every frame, so with the current results it always exits 1.
- `tools/mesen/replay_route.lua`: the Mesen side, usable on its own (header comments).
- `tools/mesen/trace_diff.py OURS MESEN [--regs]`: aligns the two traces of one frame on their
  first shared instruction address (the NMI handler). It reports each change in the relative
  cycle offset, then the first instruction whose address differs, or whose registers differ
  with `--regs`.
- `tools/mesen/fetch_mesen2.py`: download and verify Mesen 2.1.1.

## Unknown

- Which emulator matches the hardware on the VBlank-edge cycle (items 1, 2 and 4). On hardware
  the result depends on the CPU/PPU power-on alignment, which varies from boot to boot. Mesen
  uses one fixed alignment (`NesCpu.cpp`: `_ppuOffset = 1` unless randomized). Our four
  `--align` values do not reproduce Mesen's behaviour at all three edges: align 1-3 matches it
  at boot but not in items 2 and 4. Whether both models describe the same race and differ by
  part of a cycle in phase, or model the race window itself differently, has not been
  determined. The TriCNES oracle agrees with `ducktales.exe` on every frame
  (`docs/VALIDATION.md`), so this is a difference between two reference machines, not a
  recompilation error.
- The `moon_intro` divergence after frame 2148, and whether it really ends in a state that
  inputs cannot change (item 5).
- The picture, the APU, CHR RAM and cartridge state are not compared. The Lua script records
  CPU RAM, OAM, palette and (optionally) CIRAM. The picture would need Mesen's ARGB output
  mapped back to palette indices.

## Interpretation (lead's note)

Every divergence above comes from a `$2002` read or an NMI landing within a cycle or two of the
VBlank flag being set. The NESdev wiki's [PPU frame timing](https://www.nesdev.org/wiki/PPU_frame_timing)
page describes this race: a read just before the flag is set returns it clear and suppresses that
frame's flag and NMI, while a read on the setting clock or just after returns it set. Which case a
given read hits depends on the CPU/PPU clock alignment, which a real console chooses at random at
power-on. So the two emulators can disagree here without either being wrong: each models one
alignment's outcome. This explains the class of divergence but has not been confirmed for these
exact reads, and it is not a hardware measurement. Hardware-correct behaviour is left
unconfirmed. Recompiled and interpreted runs still match the TriCNES oracle on every frame, so the
divergences are not recompilation errors.
