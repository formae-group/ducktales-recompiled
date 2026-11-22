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

## Not verified

- A complete playthrough of any land on Easy or Difficult, secret treasures, the Transylvania return, the
  ending and the three endings' conditions.
- Difficult mode (the "difficult" route was found to select Easy and was renamed).
- Comparison with Mesen2 (not installed on this machine yet); the TriCNES oracle is the only
  independent reference so far.
- Window presentation as a person sees it on the 144 Hz monitor, gamepads, audio quality by ear,
  fullscreen: needs a person at the machine. The application-side pacing, DPI and picture
  measurements are in [presentation.md](presentation.md).
