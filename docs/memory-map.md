# Memory map (CPU RAM)

Status per entry: **verified** means observed in a run with controlled input or read directly from
code whose behaviour is unambiguous; **hypothesis** means a pattern that needs more experiments.
Source of each runtime observation: `--frame-log` captures of the routes in `tests/routes/`,
decoded with `tools/framelog.py`.

## Zero page

| Address | Meaning | Status | Evidence |
|---|---|---|---|
| `$14` | Controller 1, buttons newly pressed this frame | verified | Becomes `$80` on the frame A is pressed and `$00` while it stays held (`re_amazon_moves`, frames 1600, 1780). |
| `$16` | Controller 1, buttons held | verified | `$01` while Right is held, `$02` Left, `$80` A, `$40` B, `$C4` A+B+Down (`re_amazon_moves`). |
| `$18` | Palette upload request; the NMI copies `$0300-$031F` to the PPU when it is set | verified | NMI code at `$C04E-$C078`. |
| `$39` | Scrooge's mode: 0 ground, 1 air, 2 pogo, 3 climb, 4 cane, 6 knockback, `$0D` death | verified | Jump table at `$E387` ([player.md](player.md)). |
| `$3E`/`$3F` | Push counter (16 frames) / direction held when pushing started | verified | `$E458`, `$E45C`. |
| `$71` | Slippery-floor flag | hypothesis | `$D1BB`, `$E48D`. |
| `$75` | Invincibility timer | hypothesis | `$E360`, `$D282`. |
| `$7D` | Invulnerability timer after a hit (`$46` = 70 frames) | verified | Set at `$EB20`, decremented at `$E35E`; frames 1859-1929 of the exploration route. |
| `$80-$8F` | Four task slots, 4 bytes each: state, wait counter, saved stack pointer or entry low byte, entry high byte | verified | `scheduler`, `task_create`, `task_yield`. |
| `$90` | Scheduler: reset to `$FF` by the NMI, cleared at the start of a pass | verified | NMI `$C0AE`, scheduler `$FF38`. |
| `$91` | Running task slot | verified | Written by the scheduler, read by `task_yield`/`task_exit`. |
| `$92` | Frame counter, incremented by every NMI | verified | NMI `$C0AC`. |
| `$93/$94` | Task entry pointer (and wait count in `$93` for `task_yield`) | verified | `task_create`, `task_yield`. |
| `$98` | When non-zero the NMI skips sprite DMA | verified | NMI `$C034`. |
| `$C7`, `$C8-$CF` | Sound request ring: read index and 8 entries, `$88` = empty | verified | NMI `$C110-$C127`. |
| `$E7` | Bank currently selected at `$8000` | verified | `bank_select`; the NMI restores it before `RTI`. |
| `$EF` | Gravity for `$F2B5` (`$33`, reloaded every frame at `$E354`); also used as scratch | verified | Trace. |
| `$F9` | Difficulty: 0 Easy, 1 Normal, 2 Difficult | verified | RAM at frame 1800 after selecting each difficulty differs exactly this way; the damage routine at `$EB5E` applies only every other hit when it is 0. |
| `$E8` | When non-zero the NMI skips the PPU update block | verified | NMI `$C01E`. |
| `$EA` | Sprite-0 split enabled (HUD) | hypothesis | NMI writes sprite 0 and waits for the sprite-0 hit when set. |
| `$F0/$F2/$F3` | Scroll Y, scroll X and nametable bit used after the split | hypothesis | Written to `$2005`/`$2000` after the sprite-0 wait. |
| `$FC/$FD` | Scroll Y and X written at the top of the frame | hypothesis | NMI `$C07E-$C085`. |
| `$FF` | PPUCTRL shadow | verified | Every `$2000` write in the NMI derives from it. |

## Object tables (`$0600-$06FF`)

The game keeps objects as a structure of arrays: each 16-byte row is one attribute and the
column is the object slot (0-15). Slot 0 is Scrooge.

| Row | Meaning (slot 0) | Status | Evidence |
|---|---|---|---|
| `$0600+n` | Flags | hypothesis | Non-zero for live slots, `$00` for empty ones. |
| `$0620+n` | Action/animation state | verified for slot 0 | Scrooge: 0 stand, 1 walk, 2 jump, 3 crouch, 5 hurt, 6 pogo, 7 push, 8 landed (`$E6CC`), 9 cane swing ([player.md](player.md)). |
| `$0640+n` | Y position on screen | verified | Scrooge falls in from `$40` to `$B4` (ground), rises on a jump and oscillates during pogo. |
| `$0650+n` | X position on screen | verified | Scrooge goes from `$80` to `$44` while Left is held at the left edge of The Amazon; equals world X minus `$F2` (bank 5 `$80CB`). |
| `$0660+n` | Y fraction | verified | `$F2A1`. |
| `$0670+n` | X fraction | verified | Alternates `$00`/`$80` while walking at 1.5 px/frame. |
| `$0720+n`/`$0730+n` | World X low/high | verified | `$F234`/`$F252`. |
| `$0760+n`/`$0770+n` | Horizontal speed fraction/whole (`$0180` for Scrooge) | verified | Set at `$D81D`/`$D822`, reset every frame at `$E495`. |
| `$0780` | Scrooge's vertical direction for background probes (8 rising, 4 falling/standing) | verified | `$E976`/`$E9DF`. |
| `$0790`/`$07A0` | Jump take-off velocity, fraction/whole (`$04BB`) | verified | Written only at `$D815`/`$D81A` on land entry (trace, frame 1638 of `re_amazon_moves`); copied on a jump at `$E3F2-$E3FB`; poking it changes jump height reversibly. |
| `$07B0+n`/`$07C0+n` | Vertical velocity fraction/whole, 8.8 signed, up positive | verified | `$F2A1` applies it to Y, `$F2B5` subtracts gravity. |

## Other RAM

| Range | Meaning | Status | Evidence |
|---|---|---|---|
| `$0200-$02FF` | OAM shadow, sent by sprite DMA every NMI | verified | NMI writes `$02` to `$4014`. |
| `$0300-$031F` | Palette buffer | verified | NMI palette upload. |
| `$0580-` | Background collision class map, 2 bits per 16x16 cell (1 solid, 2 climbable, 3 special) | verified | `$D3B7`; probe tests at `$D120`. |
| `$0320-$032A` | HUD money update packet: PPU address `$2043`, length 7, then the tiles | verified | Dumps of the Amazon route. |
| `$0324-$0329` | Money, six decimal digits stored as tile ids (0-9, `$24` = suppressed leading zero), tens digit last; the HUD shows a fixed trailing 0 | verified | Collecting gems turns `$0327` 2, 4, 6 while the HUD shows 2000, 4000, 6000; the writer is the digit adder in bank 6 at `$8BBB`. |
| `$0340-$0367` | HUD sprite records, 4 bytes each (Y, tile, attribute, X); Y = `$FF` ends a group | verified | Dumps of the Moon route. |
| `$0340+4i` | HP dots: attribute (`+2`) 0 = full, 1 = lost; dots end at the first record with Y = `$FF` | verified | The damage routine at `$EB6A` marks the last full dot; all reset to 0 on respawn; three marked dots precede each death in the Moon route. |
| `$0361` | Extra lives, also the tile of the HUD "P.n" digit (0-9) | verified | 2, 1, 0 across the Moon route deaths; 2 on a new game; bank 6 `$8BB0` increments it up to 9. |
