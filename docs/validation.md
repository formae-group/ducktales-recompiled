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

## Not verified

- The release package on a clean Windows installation without the Visual C++ redistributable: the
  test checks the import tables but runs on a developer machine that has the runtime installed. A
  Debug (`/MTd`) window build and a package from a tagged commit have not been built either.
- A complete playthrough of any land on Easy or Difficult, secret treasures, the Transylvania return, the
  ending and the three endings' conditions.
- Difficult mode (the "difficult" route was found to select Easy and was renamed).
- Comparison with Mesen2 (not installed on this machine yet); the TriCNES oracle is the only
  independent reference so far.
- Window presentation as a person sees it on the 144 Hz monitor, gamepads, audio quality by ear,
  fullscreen, and the sprite presentation by eye in play: needs a person at the machine. The
  application-side pacing, DPI and picture measurements are in [presentation.md](presentation.md).
