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

## Native runtime

`tests/native_routes.py` holds the native-runtime prototype (`ducktales_native`, faithful lag mode)
to the recompiled build: CPU RAM, cycles and registers equal on every frame of every route above
(82,621 frames). `tests/lag_modes.py` measures lag on the full_* routes and compares the
no-slowdown mode per logic step. Details: [native-runtime.md](native-runtime.md).

## Native renderer (automated)

`tests/native_render_test.py` draws every frame of the five `full_*` routes with the native renderer
(no PPU) from the frame's video state and compares it with the PPU's picture: 47,440 frames, every
pixel equal except on the HUD split line, where the console's mid-line write lands at a column that
depends on the CPU's polling phase. With that column placed, 115 frames (all vertical row
transitions) still differ on that one line. Details in [native-render.md](native-render.md).

## Native player (automated)

The player build (`ducktales.exe` on the native runtime, docs/native-runtime.md "The player build"),
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
## Native synth

`tests/audio_compare.py` records each route twice from power-on at 48 kHz: the cycle host's APU
(`ducktales.exe --wav-out`) and the native runtime's synth (`ducktales_native --lag faithful
--wav-out`, [audio.md](audio.md#native-synth)). Both runs also write `--ram-hash-out`; RAM and cycle
count were equal on every frame of all ten routes below, so the synth was fed exactly the register
writes the APU received, at the same cycles. The two WAVs had the same number of samples on every
route.

Each frame (800 samples) is compared three ways, none of which depends on waveform phase:

- **level**: |dB difference| of the frame's RMS, on audible frames (APU RMS above 150);
- **spectral**: log-spectral distance over 1/3-octave bands from 50 Hz to 12.5 kHz (2048-sample
  Hann window per frame, bands 60 dB under the loudest one floored);
- **pitch per channel**: for pulse 1, pulse 2 and the triangle, wherever the driver's channel record
  ([audio.md](audio.md#channel-records)) holds one period for five frames, the spectral peak
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

## Not verified

- The release package on a clean Windows installation without the Visual C++ redistributable: the
  test checks the import tables but runs on a developer machine that has the runtime installed. A
  Debug (`/MTd`) window build and a package from a tagged commit have not been built either.
- A complete playthrough of any land on Easy or Difficult, secret treasures, the Transylvania return, the
  ending and the three endings' conditions.
- Difficult mode (the "difficult" route was found to select Easy and was renamed).
- Comparison with Mesen2 (not installed on this machine yet); the TriCNES oracle is the only
  independent reference so far.
- The native player's sound (silent until the native synth), its window by eye, F8/F9 by hand, and
  `HudSplit = original` against the cycle build.
- The native synth by ear, and in a window (the window build still plays the cycle host's APU);
  Launchpad ride music `$0B`; effects alone are covered only where routes trigger them (the
  "no music" row and the effects inside the music rows).
- Window presentation as a person sees it on the 144 Hz monitor, gamepads, audio quality by ear,
  fullscreen, and the sprite presentation by eye in play: needs a person at the machine. The
  application-side pacing, DPI and picture measurements are in [presentation.md](presentation.md).
