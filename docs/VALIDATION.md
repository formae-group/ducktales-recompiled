# Validation

Status as of 2026-11-21. "Automated" means re-runnable with `tests/verify_routes.py`;
"manual" means a person looked at it; "not verified" means nobody has checked it yet.

## Method

`tests/verify_routes.py` runs each input schedule in `tests/routes/` three ways at CPU/PPU
alignments 0-3:

1. the recompiled build (`ducktales.exe`, headless),
2. the same build with `--interp-only`,
3. `ducktales_oracle.exe`, NESRecomp's TriCNES-derived reference machine.

Per frame it compares a hash of every CPU bus access, CPU RAM, CIRAM, OAM, palette, CHR-RAM, the
picture's color indices, the registers and the cycle count; native and interpreter runs also
compare hardware internals. A route passes only if all three agree on every frame and at least
99.9% of CPU cycles ran recompiled code.

Coverage metric: `native_cycles / cycles` as reported by the host, i.e. CPU cycles spent in
recompiled code versus cycles of instructions the interpreter started. PPU/APU simulation is not
counted as interpretation.

## Results (automated)

| Route | Frames | Content | Native cycles | Match |
|---|---|---|---|---|
| `amazon_intro` | 5,092 | Title, Normal, The Amazon, walking/jumping/pogo/cane | 100.0% | all frames, 4 alignments |
| `transylvania_intro` | 5,112 | Same pattern in Transylvania | 100.0% | all frames, 4 alignments |
| `african_mines_intro` | 5,132 | Same pattern in African Mines | 100.0% | all frames, 4 alignments |
| `himalayas_intro` | 5,152 | Same pattern in The Himalayas | 100.0% | all frames, 4 alignments |
| `moon_intro` | 5,172 | The Moon, death, game over, back to title | 100.0% | all frames, 4 alignments |
| `african_mines_easy` | 7,350 | Easy, African Mines, different input pattern, game over; did not contribute seeds | 100.0% | all frames, 4 alignments |
| `re_amazon_moves` | 2,128 | Controlled moves used for RAM mapping | 100.0% | all frames, 4 alignments |
| `full_amazon` | 9,600 | Normal, The Amazon from power-on to the LAND CLEAR tally: vine shaft, gorilla pogo, boulder corridor, hidden rope, golfed rock pile, boss (5 hits), treasure chest, $1,049,000; loses one life near frame 4840 and continues from the checkpoint | 100.0% | all frames, 4 alignments |
| `full_himalayas` | 6,200 | Normal, The Himalayas from power-on to the LAND CLEAR tally: shaft drop to row 4, pogo over the block stack, rope climb dodging the row-3 snowball, sky platforms, boss (5 hits), crown, time bonus, TOTAL $1,043,000; no lives lost | 100.0% | all frames, 4 alignments |
| `full_transylvania` | 7,120 | Normal, Transylvania from power-on to the LAND CLEAR tally: rope to row 5, rope to row 4, fake wall under the mirror, right mirror to row 3 page 7, Magica DeSpell (5 hits), treasure chest, $1,041,000; no lives lost | 100.0% | all frames, 4 alignments |
| `full_african_mines` | 13,520 | `full_transylvania`, then African Mines: Louie sends Scrooge to Transylvania, the start mirror leads to the Skeleton Key chest (`$A3` bit 1), the left mirror returns to Land Select; African Mines with the key, chain to row 1, pogo over the abyss, chain to row 2, King Terra-Firmie (5 hits), treasure chest, $2,095,000 total; no lives lost | 100.0% | all frames, 4 alignments |
| `difficult_himalayas` | 6,200 | `full_himalayas` on Difficult (one Right on the title, `$F9` = 2): the same inputs and path, no damage taken, LAND CLEAR, TOTAL $1,043,000. Difficult adds only `$F828-$F831` (the respawned object's Difficult branch of `$F812`) to what the other routes execute | 100.0% | all frames, 4 alignments |

| `full_moon` | 11,000 | Normal, The Moon from power-on to the LAND CLEAR tally: ship climb to the key item chest (`$A3` bit 2), the door and the NPC (`$A1`), one deliberate death near frame 5820 with respawn at the land start, Gizmoduck's pillar, the shaft to row 5, boss (5 hits in 4 pogos), treasure chest, $1,064,000 | 100.0% | all frames, 4 alignments |

Before seeding, the first title/Land Select/Amazon route ran 65.5% native (378 compiled
instructions); after seeding 9,053 instructions are compiled. Before `full_amazon` was seeded it started 731
ROM instructions on the interpreter that no earlier route reached (the later Amazon screens, the boss
and the clear tally); its misses were added to `config/ducktales.seeds` and the build regenerated.
`full_transylvania` then added 404 new addresses, `full_african_mines` 526 and `full_moon` 480 (merged as a plain union of
lines).

The windowed SDL2 build also ran 600 frames hidden (`--hidden --exit-after 600`) with audio
enabled and exited cleanly.

## Sprite presentation (automated)

`py -3 -I tests/sprites_test.py --build build/headless --dev-build build/headless-dev` (2026-11-22, all
checks pass): per-frame hashes are identical with `--no-sprite-flicker` on and off (full_amazon frames
0-3299); with it off the presented picture equals the machine's picture on every frame of four windows
(Amazon 1215-1304 vine shaft, Amazon 3200-3229 Gyro's helicopter, Himalayas 4371-4400 boss, Transylvania
1581-1610); with it on the two differ only inside sprites, are identical on every frame where the console
loses nothing, every frame with a line over 8 sprites or more than 63 wanted is drawn differently, and
on the 36 vine-shaft frames where the builder leaves the HUD out every HUD pixel Scrooge does not cover
is drawn. Outside the test, hashes with the feature on and off were also compared on every frame of all
five `full_*` routes (identical), and the window build was run hidden on full_amazon: its presented
frame 1230 equals the headless presented picture with the feature on and the machine's picture with it
off. With the feature linked and on by default, `tests/verify_routes.py --build build/headless` passes all
twelve routes at all four alignments with 100.0% native cycles (2026-11-22).

## Release package (automated)

`py -3 -I tests/package_test.py --build build/windows` (2026-11-22, all checks pass, on a
clean tree at this change, so the archive is `DuckTalesRecompiled-<commit>-win64.zip`): the archive
holds exactly the release files in one top-level folder and no ROM, source, symbols or test tool;
its name carries the version the executable reports; neither `ducktales.exe` (static `/MT` runtime;
imports COMDLG32, IMM32, KERNEL32, SDL2, SHELL32, USER32, WS2_32) nor `SDL2.dll` (system DLLs and
msvcrt) imports a Visual C++ runtime DLL; unpacked into a temporary folder and run there with PATH
reduced to the Windows folders, the executable imports a copy of the ROM with `--import-rom`, starts
from the imported copy hidden for 300 frames, writes `config.ini` next to itself, and the menu
self-test draws all 14 pages. `tests/window_smoke.py --build build/windows` also passes all 16 checks
on the `/MT` build.

## Menu on formae-ui (automated)

The menu moved to formae-ui 0.1.0 (`external/formae-ui`) with no visible change (2026-11-22). The
window build of main (`64ef62e`'s tree) and of this change were each configured with `git describe`
unavailable, so the About page shows the same version (`unknown`) and date in both, and run with
`DUCKTALES_UI_SELFTEST` at fixed paths (the menu run and the first-run run with a modified ROM).
Decoded RGB of the 16 PNGs, compared pixel by pixel over 5 runs before and 6 runs after:

- 15 PNGs are identical in every run of both builds: the 14 menu pages other than the Reset
  confirmation (the 768x720 window, 1280x720 and both 3840x2160 off-screen pages included) and both
  first-run screens.
- `menu_13_reset` varies from run to run in the unmodified build as well (up to 6 levels per
  channel, uniformly over the picture): the shot is taken during ImGui's modal dim fade-in, whose
  progress depends on frame timing. One run before and two runs after are identical to each other on
  all 16 PNGs.

With the change, `tests/window_smoke.py --build build/windows` passes all 16 checks and
`tests/package_test.py --build build/windows` all 12; the package now also carries
`licenses/formae-ui-LICENSE.txt`.

## Fast verification

`tests/quick_verify.py` runs a build at alignment 0 against golden per-frame hashes (CONTRIBUTING.md,
"Verification"); `tests/verify_routes.py` stays the nightly and release gate. Measured 2026-11-23 on
the shared 16-thread PC:

- **Gate behind the golden files.** `verify_routes.py --build build/headless --jobs 3` on the
  9126ee3 merge: all 12 routes pass at the four alignments, 330,504 frames per mode (82,626 per
  alignment), 100.0% native cycles, oracle = recompiled = interpreter on every frame and hardware
  internals equal between recompiled and interpreter. 11,855 s (3 h 18 min) wall, with every oracle
  run uncached (a rebuilt oracle has a new SHA-256, which keys the cache).
- **Golden recording** (`--record`) from that build: the 82,626 frames with 16 fields each
  (CPU RAM, nametables, palette, OAM, CHR RAM, cycles, the six registers, bus trace, memories and
  picture, interrupt lines, hardware internals), a cycle-host save state and a native save state
  every 500 frames, and coverage. Each of the 173 segments replayed from its cycle state with
  `--interp-only` and each of the 161 native states replayed to the next one equal the golden
  frames. 787 s with `--jobs 3`; 380 s with `--jobs 4` on a less loaded PC.
- **Golden on later main.** Recorded again on the merges of main 8c88b14 and 3f86ea7 (formatting,
  decompiled player and flow, native audio, mods): identical to the 9126ee3 recording on every
  field of all 82,626 frames, coverage identical, so the gate result carries over.
- **Failure detection.** A golden file altered at `full_amazon` frame 3000 (`ram` and `PC`): the
  native run stopped there after 1.9 s and printed both fields; one altered at frame 3100 (`hw`)
  with `--from-state 3000` on the cycle build stopped at frame 3100.

| Run (all 12 routes, 82,626 frames, alignment 0) | Wall clock |
|---|---|
| `quick_verify.py` native, `--jobs 4` | 10.4 s |
| `quick_verify.py` native, `--jobs 2` | 17.8 s |
| `quick_verify.py` native, `--jobs 1` | 40.4 s |
| `quick_verify.py --runtime cycle`, `--jobs 4` | 192.7 s |
| `verify_routes.py`, 4 alignments x 3 runs, `--jobs 3`, oracle uncached | 11,855 s |

`tests/decomp_test.py --changed DF1E --from-state --span 600 --modes check replace` (`LandExit_Next`,
`--jobs 2`, main 9c2f14e) picks the 5 full routes, starts them at frames 5000-10000 and passes in 32 s;
the full `--modes check replace` on all 12 routes from power-on passes in 689 s (check 12/12,
895,620 calls; replace 12/12, 82,626 frames). `--changed D79B-D8A5` (the land load, run by every
route) with `--span 600` passed check and replace on all 12 routes in 420 s, so the land load in
replace mode is RAM-identical on every route.

Selection: `--routes-for 6:8B21 --from-state auto --span 600` (the pickup dispatcher) picks 10 routes
and checks 6,000 frames in 1.1 s; `--changed 4:8000-BFFF --from-state auto --span 600` picks only
`full_moon`, the one route that runs bank 4, from frame 7500 in 0.3 s. Coverage comes from
`--interp-only --miss-log`, so it lists instruction starts: an address inside an instruction, or code
no route runs (bank 6 `$BABC`, for one), selects nothing and says so.

## Native runtime

`tests/native_routes.py` holds the native-runtime prototype (`ducktales_native`, faithful lag mode)
to the recompiled build: CPU RAM, cycles and registers equal on every frame of every route above
(82,621 frames). `tests/lag_modes.py` measures lag on the full_* routes and compares the
no-slowdown mode per logic step. Details: [NATIVE-RUNTIME.md](NATIVE-RUNTIME.md).

## Native renderer (automated)

`tests/native_render_test.py` draws every frame of the five `full_*` routes with the native renderer
(no PPU) from the frame's video state and compares it with the PPU's picture: 47,440 frames, every
pixel equal except on the HUD split line, where the console's mid-line write lands at a column that
depends on the CPU's polling phase. With that column placed, 115 frames (all vertical row
transitions) still differ on that one line. Details in [NATIVE-RENDER.md](NATIVE-RENDER.md).

## Native player (automated)

The player build (`ducktales.exe` on the native runtime, docs/NATIVE-RUNTIME.md "The player build"),
2026-11-22:

- `py -3 -I tests/native_player_test.py --build build/windows --routes full_amazon full_moon
  full_himalayas --frames 6000 --every 20`: the presented picture (1x, default presentation: clean
  HUD split, No sprite flicker on) of `ducktales.exe --lag faithful` and of
  `ducktales_cycle.exe --extra-scanlines 0` is identical pixel for pixel in all 900 pictures (300 per
  route, every 20th frame of frames 0-5999), and in 40 of 40 on `amazon_intro` (frames 0-1999,
  every 50).
- `py -3 -I tests/window_smoke.py --build build/windows`: all 16 checks pass on the native
  `ducktales.exe` (start, ROM import and refusals, config.ini round trips, presentation sizes and
  VSync, the menu self-test's 14 pages, the first-run screen), and with `--exe ducktales_cycle.exe`.
- Save state round trip: `ducktales.exe --frames 3000 --input tests/routes/full_amazon.txt --lag
  faithful --state-check 2000`: saved after frame 2000, CPU RAM, the cycle count and the video
  memories identical on all 300 frames run again after loading it.
- Enhancement hooks on the native runtime, `full_amazon` frames 0-2999 with every enhancement on
  (`--jump-scale 150 --no-damage on --infinite-time on --infinite-lives on --start-lives 5`): the
  jump, damage and new-game-lives sites fired (14, 12 and 1 times). The higher jump reaches
  `$EAB9-$EAC1` in bank 6, which no route seeds, so 694 instructions ran on the interpreter there
  (347 with `--jump-scale 150` alone); none without enhancements (`fallback_instructions=0`).
- With the player's bus changes (hook dispatch, the `$2002` latch-reset log, `native_route.c`):
  `tests/native_routes.py --build build/headless` passes all 12 routes (RAM, cycles and registers
  identical on every frame; PPU memory hashes differ on the same frame counts as before, 2-42 per
  route), and `tests/verify_routes.py` on `amazon_intro` and `full_moon` passes at all four
  alignments (100.0% native, oracle and interpreter match).
- `tests/package_test.py --build build/windows`: all checks pass with the native `ducktales.exe`
  in the ZIP (imports AVRT, COMDLG32, IMM32, KERNEL32, SDL2, SHELL32, USER32 and WINMM only).
- Hidden window pacing: 600 frames with `--hidden --exit-after 600` took 12.0 s including start-up
  (600 frames are 9.98 s at 60.0988 Hz); 3,000 frames `--uncapped` took 7.8 s.
- Menu over a running game, 2026-11-22: `window_smoke.py` runs the menu self-test twice. With
  `MenuPausesGame = on` it reports 0 game frames; with the default it reports 74 game frames
  (73 in a second run) over its 14 steps on the native `ducktales.exe`, and 0 on
  `ducktales_cycle.exe`, whose host sets no menu tick. The menu settings
  round-trip and default to `MenuPausesGame = off`.
- No console window, 2026-11-22: `ducktales.exe` and `ducktales_cycle.exe` are Windows programs
  (PE subsystem 2, was 3). `--import-rom` through a pipe printed its two lines and exited 0;
  window_smoke, package_test and native_player_test (`full_amazon`, 30 of 30 pictures identical)
  pass on that build.

## Native synth

By ear (2026-11-22): the owner played the native player in its window and reports that the sound
plays cleanly, with no stutter or glitch, while the window is not focused.

`tests/audio_compare.py` records each route twice from power-on at 48 kHz: the cycle host's APU
(`ducktales.exe --wav-out`) and the native runtime's synth (`ducktales_native --lag faithful
--wav-out`, [AUDIO.md](AUDIO.md#native-synth)). Both runs also write `--ram-hash-out`; RAM and cycle
count were equal on every frame of all ten routes below, so the synth was fed exactly the register
writes the APU received, at the same cycles. The two WAVs had the same number of samples on every
route.

Each frame (800 samples) is compared three ways, none of which depends on waveform phase:

- **level**: |dB difference| of the frame's RMS, on audible frames (APU RMS above 150);
- **spectral**: log-spectral distance over 1/3-octave bands from 50 Hz to 12.5 kHz (2048-sample
  Hann window per frame, bands 60 dB under the loudest one floored);
- **pitch per channel**: for pulse 1, pulse 2 and the triangle, wherever the driver's channel record
  ([AUDIO.md](AUDIO.md#channel-records)) holds one period for five frames, the spectral peak
  within half a semitone of that note in each WAV (4096-sample window, 8x zero padding, parabolic
  interpolation), kept when the APU's peak stands 10 dB over the frame's median and no harmonic
  1-8 of another tone channel lies within a semitone; the difference in cents.

**Why these tolerances.** Phase cannot be compared: during a rest the driver writes period 0, the
triangle then steps once per CPU cycle until the next quarter-frame clock stops it, so the step it
resumes from depends on single cycles of write timing, and so does the noise LFSR's phase. The
pulses do stay sample-locked (least-squares fit of the APU mix to the synth's solo channels: pulse
coefficients mostly 0.9-1.0 over 20-frame windows of `amazon_intro`, against -1 to +1 for the
triangle), but the sum's short-term band energies move by a
few dB with the triangle's phase. The yardstick is therefore the console against itself: the APU
recorded at CPU/PPU alignment 1 against alignment 0, the same route and input (power-on alignment
is random on a console, so a player hears either). A group passes if level median <= 0.5 dB and p95
<= 2 dB (1 dB is about the smallest loudness step a listener notices), spectral median <= 3 dB and
p95 <= 7 dB, every channel's pitch median <= 3 cents (pitch steps of 5-10 cents are the smallest
heard), or if a value over its limit is within 1.1 x that noise floor. The pitch p95 is reported,
not judged: it is 20-100 cents in the noise floor too, because a peak in a four-channel mix is
sometimes another channel's partial. On `full_amazon`, `full_transylvania`, `full_african_mines`
and `full_himalayas` the alignment change sends the game down another path, so their floor is not
used (`audio_compare.py` uses a route's floor only when its own medians are within the limits).

Results (2026-11-22, `py -3 -I tests/audio_compare.py --jobs 2 --floor`): **pass**, 72,974 frames
(67,122 audible) over ten routes. Median / p95 of |difference|:

| Route | Frames | Level dB | Spectral dB | Pulse 1 cents (n) | Pulse 2 cents (n) | Triangle cents (n) | Floor: level / spectral |
|---|---|---|---|---|---|---|---|
| `amazon_intro` | 5,083 | 0.18 / 1.07 | 1.79 / 4.85 | 0.13 / 33 (1,054) | 0.57 / 60 (1,070) | 0.71 / 55 (3,109) | 0.22 / 1.53, 2.20 / 5.72 |
| `transylvania_intro` | 5,103 | 0.20 / 1.07 | 1.76 / 4.94 | 0.31 / 31 (1,731) | 0.25 / 58 (1,178) | 0.56 / 29 (3,135) | 0.22 / 1.20, 2.38 / 5.32 |
| `african_mines_intro` | 5,123 | 0.18 / 1.13 | 1.53 / 4.93 | 0.04 / 22 (1,346) | 0.07 / 57 (1,151) | 0.58 / 46 (2,749) | 0.24 / 1.47, 2.39 / 5.65 |
| `himalayas_intro` | 5,143 | 0.24 / 1.30 | 2.29 / 5.17 | 0.18 / 55 (1,004) | 0.24 / 72 (669) | 1.22 / 90 (3,168) | 0.34 / 2.46, 2.92 / 7.20 |
| `moon_intro` | 5,163 | 0.15 / 0.99 | 1.30 / 4.91 | 0.01 / 25 (995) | 0.02 / 49 (1,034) | 0.37 / 84 (2,968) | 0.17 / 1.08, 1.65 / 5.13 |
| `full_amazon` | 9,584 | 0.22 / 1.25 | 2.09 / 5.03 | 0.35 / 78 (2,696) | 0.71 / 69 (1,185) | 1.17 / 58 (4,750) | not in step |
| `full_transylvania` | 7,108 | 0.22 / 1.25 | 1.93 / 4.96 | 0.37 / 87 (2,501) | 0.36 / 42 (1,117) | 0.69 / 33 (3,274) | not in step |
| `full_african_mines` | 13,497 | 0.20 / 1.21 | 1.94 / 5.04 | 0.15 / 87 (3,820) | 0.16 / 52 (1,843) | 0.54 / 57 (5,893) | not in step |
| `full_himalayas` | 6,189 | 0.19 / 1.08 | 1.21 / 3.73 | 0.18 / 34 (1,474) | 0.24 / 23 (767) | 0.89 / 51 (3,727) | not in step |
| `full_moon` | 10,981 | 0.18 / 1.07 | 2.08 / 5.10 | 0.55 / 77 (2,877) | 0.56 / 69 (1,659) | 0.73 / 18 (7,682) | 0.23 / 1.35, 2.40 / 5.57 |

Per track, all ten routes together (the track is the music id in `$C5` while `$D0` says music is
playing):

| Track | Frames | Level dB | Spectral dB | Pulse 1 cents | Pulse 2 cents | Triangle cents |
|---|---|---|---|---|---|---|
| no music (effects only) | 3,705 | 0.00 / 0.35 | 0.07 / 3.21 | - | 0.00 / 0.03 | - |
| `$00` The Amazon | 8,993 | 0.20 / 1.16 | 2.22 / 5.05 | 0.56 / 29 | 1.32 / 67 | 1.38 / 50 |
| `$01` Transylvania | 13,799 | 0.19 / 1.12 | 2.04 / 4.99 | 0.38 / 30 | 0.44 / 62 | 0.86 / 32 |
| `$02` African Mines | 3,091 | 0.17 / 1.28 | 2.30 / 5.23 | - | - | 0.60 / 92 |
| `$03` The Himalayas | 6,166 | 0.22 / 1.22 | 1.87 / 4.83 | 0.77 / 45 | 0.84 / 41 | 2.10 / 88 |
| `$04` The Moon | 9,974 | 0.16 / 1.01 | 2.15 / 5.10 | 0.66 / 53 | 0.33 / 68 | 0.73 / 14 |
| `$05` Land Select | 7,550 | 0.19 / 0.95 | 0.83 / 2.44 | 0.01 / 1.5 | 0.01 / 1.2 | 0.16 / 2.2 |
| `$06` Boss fight | 12,347 | 0.26 / 1.38 | 2.23 / 5.15 | 0.16 / 97 | 1.08 / 56 | 0.82 / 58 |
| `$07` Title | 5,213 | 0.19 / 1.37 | 2.16 / 5.18 | 1.29 / 79 | 1.77 / 92 | 1.80 / 92 |
| `$08` Invincibility | 687 | 0.21 / 1.15 | 1.68 / 5.63 | 0.27 / 1.8 | 0.31 / 62 | 16.31 / 88 * |
| `$09` Game over | 483 | 0.14 / 1.05 | 0.59 / 3.56 | 0.00 / 2.3 | 0.01 / 13 | 0.07 / 84 |
| `$0A` Death | 910 | 0.24 / 0.81 | 0.44 / 3.34 | 0.01 / 2.6 | 0.01 / 12 | 0.07 / 77 |

\* Over the 3-cent limit and within the noise floor: the APU against itself gives 17.76 cents on the
same 229 frames, so this is the measurement, not the synth.
Every other value is within its fixed limit. `$0B` (Launchpad ride) is on no route. The six
in-step routes were run again on their own with the corrected floor selection: same synth numbers,
pass.

The synth adds no machine state and the cycle build is unchanged. With it, `tests/verify_routes.py`
passes `re_amazon_moves`, `amazon_intro` and `full_himalayas` at all four alignments with 100.0%
native cycles, and `tests/native_routes.py` finds RAM, cycles and registers identical on every frame
of the same three routes (2,128, 5,092 and 6,200 frames; PPU memories differ on 3, 14 and 24 frames,
the known one-frame store lag of `$2007` writes). With the synth running (`--wav-out`), the native
runtime's RAM and cycles equalled the cycle host's on every frame of the ten routes above.

In the player build (`ducktales.exe` on the native runtime, `build/windows`), off screen with
`--lag faithful --wav-out`, the WAV is byte-identical to `ducktales_native --wav-out` on
`amazon_intro` (5,092 frames), `full_moon` (11,000) and `full_himalayas` (6,200), so the numbers
above are the player's. With `--lag none` (No slowdown) `amazon_intro` gives 4,066,839 samples for
5,092 frames, 61 fewer than 5,092 x 48,000 / 60.0988, the stretched frames being played in their
normal length. After plugging the synth in: `tests/window_smoke.py --build build/windows` and
`tests/package_test.py --build build/windows` pass (all checks); `tests/native_routes.py` passes
`amazon_intro` and `full_moon` (RAM, cycles and registers identical on every frame), and
`tests/verify_routes.py` passes both at all four alignments with 100.0% native cycles. The synth
costs about 0.2 ms a frame: `ducktales_native` ran `full_moon` at 1,336 frames a second with
`--wav-out` and 1,885 without, on the same idle machine.

## Extra voices

The two native-player sound options ([AUDIO.md](AUDIO.md#extra-voices)), on branch `audio/native`.

**Off is unchanged.** WAVs recorded before any code change were compared with the same runs on
the finished code, with both options off. They were byte-identical: `ducktales.exe --lag faithful
--wav-out` and `ducktales_native --lag faithful --wav-out` on `amazon_intro` (5,092 frames) and
`full_moon` (11,000), and `ducktales_native` on the Land Select scenarios below (`--sound-request
350:13+15`, `350:05+13`, `350:13`, 471 frames). With both on, the player's WAV equals
`ducktales_native`'s on `amazon_intro`.

**They only read the machine.** With both options on and the synth running, `tests/native_routes.py
--voices on` finds RAM, cycles and registers identical to the cycle host on every frame of
`amazon_intro`, `full_moon` and `full_amazon`, as `--voices off` does (PPU memories differ on
the same 14, 13 and 24 frames either way, the known `$2007` store lag). The player's
`--state-check 3000` with both on: identical over 300 frames. `tests/verify_routes.py` passes
`amazon_intro` and `full_moon` (100.0% native cycles). `tests/enhancements_test.py`,
`tests/window_smoke.py` (the Audio page with the two toggles), `tests/package_test.py` and
`tests/native_player_test.py` (60 of 60 pictures identical on `full_amazon` and `full_moon`) pass.

**The ports follow the driver.** A temporary check, which was not committed, replayed each step of
the port against the real driver on eight routes (`amazon_intro`, `full_amazon`, `full_moon`,
`full_himalayas`, `full_transylvania`, `full_african_mines`, `african_mines_easy`,
`transylvania_intro`). It started each frame from the RAM of the frame before, ran the port's
update, and compared the result with the driver's RAM and APU writes:
- **Effects.** `$E0-$E3`, the mask, the priority, `+$10-$1E` of each owned channel, and the final
  register values and high-byte writes matched on 23,233 of 23,234 effect frames, 2,826 of them
  effect starts. The one miss is a start frame in `transylvania_intro`.
- **Music.** The same voice was run on 224,452 channel updates of channels no effect held. It
  differed on 93 of them. The ones inspected (`amazon_intro`, `full_moon`, `full_amazon`) were
  of two kinds:
  - the first update after power-on or after the game's reset clears RAM;
  - a channel an effect took and gave back within one frame, so it was held by no effect at either
    end of the frame.

  Three driver details were found and ported during the check: the instrument command found by the
  stream walk, the triplet frame that ticks twice and reads its note on the second tick, and the
  tremolo level read through `$E4`.

**What they add** (pulse 2 alone, `--audio-channels 2`). This is the RMS of each 800-sample block
from the first frame after the request (`--sound-request 350:IDS` on `amazon_intro`, music 5
playing). "Added" is the RMS of the option-on minus option-off signal.

| Frame | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `$13+$15`, off | 1025 | 1210 | 854 | 1454 | 1875 | 1218 | 1125 | 1434 | 933 | 747 | 954 | 616 | 335 | 430 | 701 | 833 | 832 |
| `$13+$15`, OverlapEffects | 1586 | 2035 | 1977 | 1871 | 2133 | 1890 | 1355 | 1298 | 1824 | 1966 | 2003 | 1224 | 744 | 932 | 1045 | 942 | 832 |
| added (`$15`) | 1365 | 1721 | 1830 | 1187 | 1328 | 1802 | 1836 | 1081 | 1558 | 1872 | 1817 | 1094 | 680 | 830 | 816 | 479 | 0 |
| `$05+$13`, off | 1025 | 1210 | 854 | 1454 | 1875 | 1218 | 1125 | 1434 | 933 | 747 | 954 | 624 | 335 | 430 | 701 | 832 | 832 |
| `$05+$13`, MusicUnderEffects | 1025 | 1210 | 854 | 1454 | 1875 | 1218 | 1125 | 1434 | 933 | 919 | 1208 | 883 | 884 | 937 | 920 | 832 | 832 |
| added (pulse 2's music) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 636 | 771 | 818 | 818 | 804 | 470 | 0 | 0 |
| `$13`, MusicUnderEffects added | 743 | 743 | 775 | 776 | 677 | 781 | 761 | 769 | 766 | 793 | 473 | 670 | 813 | 810 | 499 | 0 | 0 |

Off, `$15` is silent: the driver drops it. With OverlapEffects it sounds for the 16 frames `$13`
lasts and stops with it. In `$05+$13` the restarted song's pulse 2 rests for its first 9 frames,
then the music under `$13` is heard until `$13` gives the channel back. With `$13` alone over the
running song, the music is heard from the first frame. From frame 15 or 16 on, the rows are equal:
the channel is the music's again.

## Presented sound

What the native player's audio device plays, after the output stage
([AUDIO.md](AUDIO.md#output-stage)), on branch `audio/robustness` (2026-11-24).
`tests/audio_output_test.py` runs `ducktales.exe --audio-out` (`build/windows`, off screen, no window)
with the frame loop and the device on simulated clocks: the 12 routes at the defaults (No slowdown
off, VSync 144 Hz, volume 100) and in the worst case (No slowdown on, both extra voices on, a mod
replacing every music and effect with full-scale sines, volume 100, VSync 60 Hz, device clock
+300 ppm), `amazon_intro` at VSync 60 and 120 Hz and with none (a minimised window), the window
events at the defaults and in the worst case (menu paused 180 frames, fast-forward 300 frames, a
1.5 s window drag, a loaded state, sound off 120 frames, a volume sweep 100-0-100, a 480-sample device
buffer), a 120 ms hitch, and `full_african_mines` with the device clock 500 ppm fast and slow: 32 runs.
A click is a step between output samples larger than the game's own largest step around it (the
synth's samples before the mods' files) plus 1% of full scale, including the steps from silence at
the start and to silence at the close.

**The synth's level.** `--wav-out` peaks over the 12 routes (all 48 runs: extra voices off/on, No
slowdown off/on): -11.96 to -9.95 dBFS with the voices off, -11.96 to -7.88 dBFS with them on
(`full_himalayas`), no sample at full scale. The console's channels cannot clip the output; with the
voices' two extra sets of channels a full-scale sum is possible in theory (all twelve channels at
volume 15 together), never on a route. The default output is unchanged: `ducktales.exe --wav-out` is
byte-identical to fc462ad on `amazon_intro` and `full_moon`, voices off and on, No slowdown off and on.

**Before** (the old queue in `native_output.c` at 2c483c6, the logic `queue_audio` had): 32 of 32 runs
fail. Default runs: no sample at full scale, 1-2 clicks each (the power-on start, a step of up to
4,633, and the device closing mid-waveform), 5 of the 12 with an underrun. Worst case: the mix peaks at
+6.02 dBFS and the 16-bit clamp hard-clips 46,619 (`re_amazon_moves`) to 1,342,521
(`full_african_mines`) samples a route; 2-46 clicks. Window events: 6 underruns (every hold ran the
device dry and stepped to silence), 9 clicks, 163 in the worst case. Device clock 500 ppm slow: 5 whole
frames dropped at 100 ms queued. Steps of up to 27,910 came from mod files starting and stopping
(measured with the output stage in place and the mods' fades out), and on `full_moon` with every
music and effect replaced, steps of up to 4,634 where the synth's channels were cut for the mod (1,866
with the 5 ms ramp).

**After** (619ac4e): **32 of 32 pass**, 0 samples at full scale, 0 clicks, 0 underruns except the one
in the hitch run, declicked.

| Runs | Mix peak dBFS | Output peak dBFS | Largest step | Underruns | Frames dropped | Frames resampled | Queue peak ms |
|---|---|---|---|---|---|---|---|
| 12 routes, defaults | -11.96 to -9.95 | -11.98 to -9.95 | 7 | 0 | 0 | 194 each | 66.8 |
| 12 routes, worst case | +5.96 to +6.02 | -1.00 | 296 | 0 | 0 | 766-2,436 | 77.5 |
| VSync 60 / 120 / none | -10.13 | -10.13 | 0 | 0 | 0 | 594 / 285 / 147 | 77.5 / 69.5 / 63.2 |
| Window events, defaults / worst | -10.13 / +6.02 | -10.21 / -1.00 | 9 / 266 | 0 | 0 | 887 / 2,470 | 38.5 / 48.2 |
| Hitch 120 ms | -10.13 | -10.13 | 26 | 1 | 0 | 426 | 66.8 |
| Device clock +500 / -500 ppm | -9.95 | -9.95 | 0 / 5 | 0 | 0 | 5,087 / 4,266 | 65.8 / 81.6 |

At the defaults the limiter never acts (0 samples turned down) and the output equals the synth's
samples except on resampled frames, which change length by at most 0.5% (the frame clock and VSync or
the device clock disagreeing; 194 of a route's frames at the start, while the queue fills). In the
worst case the limiter holds the output at -1.00 dBFS. `quick_verify.py` native: 12/12 routes, 82,626
frames. The window's own audio path (the SDL callback, the holds from the menu, the window's frame,
F9, F11 and closing) is the same `native_output.c` and was run by `tests/window_smoke.py`, not heard.

## Decompiled flow and HUD

On the integrated tree, all three Release builds and the hidden window smoke test passed.
`decomp_test.py` passed check and replace on `amazon_intro` and `full_moon` (16,092 frames
in replace), plus 11 poke cases and both event cases. At default decompilation settings,
`verify_routes.py` passed `amazon_intro` at all four alignments with 100% native CPU cycles
and matching oracle and interpreter output. The full 12-route decompilation suite passed on
the flow branch before integration; it has not been rerun in full on this merge.

The long ending pokes compare all three variants (`$AD = 0, 1, 2`) from a saved Amazon state
through the credits and the final wait. All three passed check and replace (4,150 frames each).
The objects at `5:$B380` and `5:$B400` were compared on all their calls (9 and 2,622), the
variant-1 walk at `5:$B46A` on all 353, and the code after the raster split at `7:$E23C` on
all 2,718. The pokes also compared all 12 calls to the CHR loader, whose return registers are
kept across the yield after unblanking. Cycle quick verification passed 13/13 affected routes
(7,800 frames from golden states, alignment 0).

The reset `$FEE0-$FF34` is compared from power-on on all 13 routes: `decomp_test.py --changed
FEE0-FF34 --from-state --span 600` passed check and replace (13/13 each, every reset routine 13/13
compared). A warm reset (`--dt-reset-at 1200` on `amazon_intro` from its frame-1000 state) passed
check with all five reset routines compared, and its replace run equals the check run on all 301
frames (296 NMIs in both); there `Reset` is given to the original because the NMI edge is about
to latch.

The polling task code (the reset's wait `$FEEE-$FEFA`, the credits' split `$E229-$E23B`) and the
resume point `$DC29`: `decomp_test.py --changed 7:E229-E23B FEEE-FEFA 7:DC29-DC2B --from-state
--span 600` passed check and replace on 13/13 routes; `Reset_WaitVblank` was compared on all 65
calls (5 a route: the first wait at power-on takes two frames and pauses once) and
`Reset_WaitVblankEnd` on all 52, none given to the original. `--changed 7:DC29-DC2B` from its own
segment passed 13/13 (`FrameLoop_Next` 3,000 calls compared). The `warm-reset` events case passed
check (`Reset_WaitVblank` 19 calls, `Reset_WaitVblankEnd` 16, all compared) and replace (1,301
frames equal). The ending, ending-0 and ending-2 pokes passed check and replace with `Ending_Split`
(`$E229`) compared on all 2,718 calls. Cycle `quick_verify.py` passed 13/13 (with the NMI `$C000-$C138`
in the ranges, as the NMI's measure and replay are now shared), and `native_decomp_test.py` passed
13/13 with logic steps equal. After merging main 0d8b498 (the scheduler in C) the same runs passed
again, with the scheduler's range `FF35-FF7C` added to the changed ranges: check and replace 13/13,
the events, the three ending pokes, cycle `quick_verify.py` 13/13 and `native_decomp_test.py` 13/13,
nothing given to the original.

## Decompiled level, player, flow and objects (2026-11-23)

Each branch passed its own gate before the merge: format, `quick_verify.py` native and cycle
(12/12 routes), and the full `decomp_test.py`. The tree that joins all four (main 0c9ef61 with
`decomp/flow` and `decomp/objects`, tree a02e215) passed one combined gate, with `--jobs 2`:

- `quick_verify.py` native: 12/12 routes, 82,626 frames, 24.2 s. With `--runtime cycle`: 12/12,
  82,626 frames, 304.7 s.
- `decomp_test.py`, all modes, 1,191 s:
  - check: 12/12 routes, 1,983,338 calls
  - replace: 12/12 routes, 82,626 frames RAM-identical
  - pokes: 18/18 in check (194,198 calls) and 18/18 in replace (1,920 frames each)
  - events: 2/2 in check and 2/2 in replace

Not run on that tree: the full `verify_routes.py`, `enhancements_test.py` and the window build tests.

**Full gate on main 21a7ffb** (those four plus `decomp/system`): `verify_routes.py --jobs 3` in a
clean worktree, every oracle run uncached. All 12 routes pass at the four alignments, with 100.0%
native cycles and oracle = recompiled = interpreter. 3,409 s wall, against 11,855 s on 9126ee3; that
earlier run had other agents' builds and tests on the PC, so the two times say nothing about the code.
`tests/window_smoke.py --build build/windows` passed on the same tree.

**Owner playtest with the decompiled C on** (2026-11-23, main 21a7ffb, `build/windows`
`ducktales_cycle.exe --decomp replace`): about 8,400 game frames in one land, from the title screen
through the land load, one death and respawn, climbing and row changes. 92 of the 198 routines ran
as C, and the log has no decomp error. The owner's verdict: "tudo perfeito". The native player
(`ducktales.exe`) cannot run the decompiled C yet.

**Objects batch on main 7bf545d** (main a856a27 with `decomp/objects` 01ed6f8 and the routine-cap
fix, tree cd3306f). Before the fix, `add_table` stopped registering at 256 routines without an
error, and this tree has 271, so the last tables would have gone unchecked; the cap is now 512 and
a full table stops the run. main a856a27 registered 243, under the old cap. Gate with `--jobs 2`:

- `quick_verify.py` native: 12/12 routes, 82,626 frames, 50.1 s. With `--runtime cycle`: 12/12,
  82,626 frames, 489.9 s.
- `decomp_test.py`, all modes, 5,145 s (three other agents' tests ran on the PC at the same time):
  - check: 12/12 routes, 3,187,426 calls
  - replace: 12/12 routes, 82,626 frames RAM-identical
  - pokes: 45/45 in check and 45/45 in replace
  - events: 2/2 in check and 2/2 in replace

The two-run `decomp_test.py` (check as the baseline of replace, pokes from a save state) on the same
builds and tree: all passed in 1,904 s, against 5,145 s above (both with other agents' tests on the
PC). Its check, replace and events lines and the routine table of the 12 routes are identical line
for line to the run above; the 45 poke cases pass in check and in replace from frame 1,500.
Beforehand, `re_amazon_moves` in check mode and stock gave byte-identical RAM logs (2,128 frames).

**Per-routine process on main (2026-11-23).** From main 1ab2759 on, each decompiled routine is merged
after its own `--changed` checks, build and `quick_verify.py` native; the full `decomp_test.py`
runs on main. On main fc462ad (objects, flow, level, player and the native runtime merged, 325
routines, 87.3%): all passed in 1,312 s, with `--jobs 2`:

- check: 12/12 routes, 3,556,280 calls
- replace: 12/12 routes, 82,626 frames RAM-identical
- pokes: 48/48 in check and 48/48 in replace
- events: 2/2 in check and 2/2 in replace

Owner playtests on the same day: `ducktales_cycle.exe --decomp check` on main 5f34aa9 (15,410
frames, 772,193 calls compared, no mismatch) and a59a409 (13,167 frames, 667,296 calls, all
compared, none given to the original, no mismatch); and `ducktales.exe` with Decompiled code and
No slowdown on, at fc462ad, where the owner saw no difference ("pra mim tava perfeito"; that build
wrote no session summary yet).

**NMI cycle-host replacement (2026-11-24).** `decomp_test.py --changed C000-C182 --from-state
--span 600 --modes check replace` passed all 13 routes in both modes (7,800 replace frames).
Check compared `Nmi_Handler` and `Nmi_Return` 7,709 times each, with no calls given to the original.
Independent `--trace-out` captures matched all device and DMA bus records from NMI entry
through RTI at title frame 8 and in the sprite-0 split on `full_amazon` frame 343. The split
trace matched 961 reads of `$2002`; RTI began 9,796 cycles after entry in both runs.
At decompilation off, `quick_verify.py --runtime cycle --changed C000-C182` matched the golden
hashes on 13/13 routes (88,826 frames, alignment 0). `native_decomp_test.py` passed on 13/13
routes; NMI-counted yields were unchanged (one on five routes, two on eight).

## Release 1.0.0

On main `8569c86` (the release tree before its changelog and documentation commit), 2026-11-23:

- `tests/quick_verify.py --build build/headless --jobs 4`: 12/12 routes pass on the native
  runtime, 82,626 frames compared, 10.6 s; with `--runtime cycle` 12/12 routes, 82,626 frames,
  every golden field, 206.5 s. The golden files are the 9126ee3 recording, which 3f86ea7 reproduced
  exactly ("Fast verification"), so the full gate of 9126ee3 carries over through them; the full
  `verify_routes.py` was not run again on this tree.
- `tests/window_smoke.py --build build/windows`: 18/18 PASS with the help markers (formae-ui
  0.3.0), including the game running behind the menu on the native player (76 game frames) and no
  game frame with `MenuPausesGame = on`.
- By eye (the owner, 2026-11-23, before publishing): the 1.0.0 window build plays normally, the game
  keeps running behind the menu during a level, the "(?)" tooltips show with the mouse and with the
  keyboard and controller, and the sound is right.
- The mods branch before its merge: `tests/package_test.py` 12/12 (13 files), `tests/mods_test.py`
  47/47, `tests/native_player_test.py` `full_amazon` 60 of 60 pictures identical.

## Mods polish (2026-11-24)

On `mods/polish` (based on 1b91636, tree of 81e5db3), with `--jobs 2`:

- `tests/mods_test.py`: 63/63 PASS on the cycle/headless host (167 s) and 42/42 on the native
  player. Each run generates throwaway fixtures in the test itself: two good mod.toml files (byte
  order mark, mixed case) load, and ten bad packages are refused with their reason (missing key,
  unquoted value, key set twice, malformed `prg_sha256`, an id over 63 bytes, a NUL byte,
  duplicate tile, palette and sound, and a refused copy with the id of a valid mod). It also checks
  `mods.log`, script errors at start, ignored cancels, the UTF-8 toast cut, the atomic store and
  a hot reload saved twice within one second. `--mods-dump` with mods off writes 127 templates to
  a nested UTF-8 path, and all 30 machine hash records are identical with and without the dump.
- `mods_catalogue_test`: 8/8.
- `tests/window_smoke.py --build build/windows`: all passed, 15 pictures, including
  `menu_15_settings_mods`; `--mods off` leaves `Enabled = on` in the config.
- `tests/quick_verify.py --runtime cycle`: 13/13 routes pass, 88,826 frames, 274.3 s.

Not run on this tree: the full `verify_routes.py`, `decomp_test.py` and the window by eye.

## Mods phase 2: events, state and drawing (2026-11-24)

On `mods/phase2` (from main 074541c), with `--jobs 2`:

- `tests/decomp_test.py --modes events` (`build/dev`, developer diagnostics), 322 s: NoDamage,
  JumpScale and StartLives pass in check and replace (StartLives failed before `Title_Lit` raised
  the new-game event). `full_moon` (8,046 events, 12 kinds) and `full_amazon` (7,181 events, 13
  kinds) log the same events, frame for frame and with the same values, with `--decomp off`,
  `check` and `replace`. In check mode the enemy-defeated and item-collected counts equal the
  calls of `Obj_Defeat` and `Pickup_Take`.
- `tests/mods_test.py`: all 82 checks pass (headless and native player). The new step 10 runs
  `full_amazon` to frame 9,600 on both hosts: land-start, checkpoint and land-clear once each,
  boss-defeated once with the last of 5 boss hits, 16 enemy-defeated and 3 item-collected events
  with their values, 48 money-changed deltas that add up to the money shown last (1,049,000),
  `dt.objects()` and `dt.camera()` agreeing with `dt.state`, `dt.draw` capped at 64 a frame and
  drawn over the presented picture. With all of it running, the per-frame machine hashes of the
  9,600 frames equal a run without mods, and the native player logs the same events in the same
  frames as the cycle host.
- `tests/quick_verify.py`: native 13/13 routes, 88,826 frames, 19.5 s; `--runtime cycle` 13/13,
  88,826 frames, 302.0 s (both with mods off).
- `mods_catalogue_test` 8/8; `tests/window_smoke.py --build build/windows`: all passed.

Found on the way: the native player's HUD split repair (`ui/app_present.c`) redrew two lines over
the scripts' overlay (a `dt.draw` rectangle lost 66 of its 384 pixels); the overlay's pixels now
stay on top.

Not run on this tree: the full `verify_routes.py`, the full `decomp_test.py` (check, replace and
pokes), and the window by eye.

## Mods files split (2026-11-24)

On `mods/split` (from 17142f1, merged with main 0939ea9), code moves only: `fm_script.c` into
`fm_script_state.c`, `fm_script_show.c` and `fm_script_store.c`, `dt_mods.c` into `dt_mods_script.c`
and `dt_mods_picture.c`, and `tests/mods_test.py` into `tests/mods/`. On the merged tree, with
`--jobs 2`:

- Headless, headless with developer diagnostics (`build/dev`) and windows builds without warnings;
  `tools/format.py --check` clean.
- `--decomp check`: 391 `decomp:` lines in 30 frames, no MISMATCH or TIMING in 300 frames.
- `tests/quick_verify.py`: native 13/13 routes, 88,826 frames, 35.2 s; `--runtime cycle` 13/13,
  88,826 frames, 341.0 s.
- `tests/native_decomp_test.py`: all 13 routes, logic steps equal.
- `tools/decomp_progress.py`: 100.0%, 391 routines, 28,371 of 28,371 bytes (player 3,626, objects
  13,465, flow 3,901, level 3,643, system 3,463), as before.
- `tests/mods_test.py`: all 82 checks pass (headless and native player); `mods_catalogue_test` 8/8;
  `tests/window_smoke.py --build build/windows`: all passed.

Not run on this tree: the full `verify_routes.py`, the full `decomp_test.py`, and the window by eye.

## Full decompilation (2026-11-24)

Main d54b09b, with every executed PRG byte in a routine with C: `tools/decomp_progress.py` reports
100%, 28,371 of 28,371 bytes executed by the 13 routes and 92 poke cases, in 391 routines, with no
original routine reached.

| Check | Result |
|---|---|
| `tests/decomp_test.py` (every route, all modes, 1,454 s with 2 jobs) | check 13/13 (16,263,223 calls compared), replace 13/13 (88,826 frames), pokes 92/92 in check and replace, events 12/12 (warm reset, start lives, and the full_moon and full_amazon event logs equal with --decomp off, check and replace) |
| `tests/quick_verify.py` (native) | 13/13, 88,826 frames |
| `tests/native_decomp_test.py` | 13/13, logic steps equal |
| `--decomp check --frames 300` | no mismatch; 391 routines report at 30 frames |

Not run on this tree: the full `tests/verify_routes.py` (last run on 21a7ffb with 12 routes).

## Not verified

- The full `tests/verify_routes.py`, `tests/enhancements_test.py` and `tests/decomp_test.py` on
  the 1.0.0 tree itself (see "Release 1.0.0").

- `tests/quick_verify.py --runtime cycle` with `--from-state` on routes other than the ones above, and
  any timing on an idle PC: every number in "Fast verification" was measured with other agents'
  builds and tests running.
- The release package on a clean Windows installation without the Visual C++ redistributable: the
  test checks the import tables but runs on a developer machine that has the runtime installed. A
  Debug (`/MTd`) window build and a package from a tagged commit have not been built either.
- A complete playthrough of any land on Easy, secret treasures, the Transylvania return, the
  ending and the three endings' conditions.
- Difficult mode beyond `difficult_himalayas` (one land, no damage taken; the earlier "difficult" route was
  found to select Easy and was renamed).
- Comparison with Mesen2 (not installed on this machine yet); the TriCNES oracle is the only
  independent reference so far.
- The native player's window by eye, F8/F9 by hand, and
  `HudSplit = original` against the cycle build.
- The native player's sound on a real audio device: `tests/audio_output_test.py` simulates the
  frame loop and the device's clock; a real device's buffer sizes, its clock and Windows' mixer,
  a real window drag, F9 and closing were not listened to, and the loudness and pitch of the
  limiter and the 0.5% resampling not judged by ear.
- The extra voices by ear in the player's window, and `$37` (all four channels) under music; only
  off-screen runs were made.
- The native synth by ear beyond the owner's check of the unfocused window ("Native synth");
  Launchpad ride music `$0B`; effects alone are covered only where routes trigger them (the
  "no music" row and the effects inside the music rows).
- Window presentation as a person sees it on the 144 Hz monitor, gamepads, audio quality by ear,
  fullscreen, and the sprite presentation by eye in play: needs a person at the machine. The
  application-side pacing, DPI and picture measurements are in [PRESENTATION.md](PRESENTATION.md).
