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
| `$20/$21`, `$2A`, `$2B` | Map grid pointer, row count, row stride | verified | Loaded by `$C7B0` ([levels.md](levels.md)). |
| `$22/$23` | Pointer to the current screen's data (bank 4, `$8000` + id*72) | verified | `$CE2C`, `$CDAF`. |
| `$24` | Map index of the screen being streamed | verified | `$C830`; nametable check. |
| `$25` | Previous frame's `$F2` | verified | `$DBE4`. |
| `$26` | Stream position: bits 0-2 block row, bits 3-5 block column | verified | `$CCBD`, `$C883`. |
| `$27`/`$28` | Attribute / collision byte of the block being built | verified | `$CCFE-$CD22`. |
| `$29`/`$38` | Stream direction last used / requested | hypothesis | `$C7D3`, `$C7FB`. |
| `$2D` | Camera screen index = row x 8 + page | verified | Five intro routes. |
| `$39` | Scrooge's mode: 0 ground, 1 air, 2 pogo, 3 climb, 4 cane, 6 knockback, 7 stuck in snow (Himalayas, freed by a jump, no HP cost), `$0D` death, `$10` holding a land treasure | verified | Jump table at `$E387` ([player.md](player.md)); `full_himalayas` frames 4037-4069 (7) and 5018 (`$10`, with `$A0` bit 3). |
| `$3E`/`$3F` | Push counter (16 frames) / direction held when pushing started | verified | `$E458`, `$E45C`. |
| `$71` | Slippery-floor flag | hypothesis | `$D1BB`, `$E48D`. |
| `$75` | Invincibility timer | hypothesis | `$E360`, `$D282`. |
| `$5D` | Land left: return to Land Select and bank the money | verified | Poke run ([game-state.md](game-state.md)). |
| `$78` | Scene request (`$81` Launchpad ride home) | verified | Poke runs. |
| `$7B` | 5 = final arena | verified | Trigger run with `$A0 = $1F`. |
| `$7C` | 5 in the boss-checkpoint screen | verified | Poke run. |
| `$7E` | Respawn/warp request: bit 7 + index into the 9-byte table at `$DDC1`; `$87` and up cost a life | verified | Poke runs `$80-$87`. |
| `$7D` | Invulnerability timer after a hit (`$46` = 70 frames) | verified | Set at `$EB20`, decremented at `$E35E`; frames 1859-1929 of the exploration route. |
| `$80-$8F` | Four task slots, 4 bytes each: state, wait counter, saved stack pointer or entry low byte, entry high byte | verified | `scheduler`, `task_create`, `task_yield`. |
| `$A0` | Treasures: bits 0-4 one per land (Land Select skips taken lands), bits 5-6 extra | verified (0-4), hypothesis (5-6) | Bank 6 `$8C2E`; Land Select poke runs. |
| `$A3` | Key items (bit 1 = African Mines item that stops the redirect to Transylvania) | verified (bit 1) | Poke runs; bank 6 `$8BF5`. |
| `$A4` | Redirect after a land: positive = Transylvania, negative = Land Select | verified | Poke runs. |
| `$AC`/`$AD` | Ending start / ending variant 0-2 | verified | Poke runs ([game-state.md](game-state.md)). |
| `$B1` | Boss hits left: 5 when the fight starts, minus 1 per hit, 0 when the boss is beaten | verified (Amazon, Himalayas) | `full_himalayas`: 5 at frame 3722 with `$FB = $FF` and music 6, then 4/3/2/1/0 at 3806/3974/4142/4536/4864; the chest appears after 0. |
| `$B0` | Frames per HUD time tick (60) | verified | `$E2AA`. |
| `$E6` | Pause; bit 0 also mutes the music in the sound driver | verified | `$DB46`; driver `$827B`, poke run. |
| `$FB` | Bit 7 = boss fight | verified | Trigger poke runs. |
| `$50` | Row sprite-CHR stream state (negative = done) | verified | `$C42C`. |
| `$95` | Sprite-builder frame counter (parity flips slot order) | verified | Bank 5 `$8069`; OAM check. |
| `$96`/`$97` | Slot being drawn / next OAM offset | verified | Bank 5 builder. |
| `$90` | Scheduler: reset to `$FF` by the NMI, cleared at the start of a pass | verified | NMI `$C0AE`, scheduler `$FF38`. |
| `$91` | Running task slot | verified | Written by the scheduler, read by `task_yield`/`task_exit`. |
| `$92` | Frame counter, incremented by every NMI | verified | NMI `$C0AC`. |
| `$93/$94` | Task entry pointer (and wait count in `$93` for `task_yield`) | verified | `task_create`, `task_yield`. |
| `$98` | When non-zero the NMI skips sprite DMA | verified | NMI `$C034`. |
| `$D0` | Sound priorities: high nibble current effect, low nibble current music | verified | Bank 3 `$8044`, `$80BB`; frame logs ([audio.md](audio.md)). |
| `$D4` | Sound driver busy while `$8003` runs | verified (static) | `$824C`. |
| `$D7` | Sound ticks per frame (0/1 normal; unused speed control) | verified | `$86AC`; poke `$D7` = 2 halves loop periods. |
| `$D8/$D9` | Global music fade rate (bit 7 = out) / step | verified | `$82CC-$833C`; poke runs. |
| `$DA` | Sound frame counter (triplet parity) | verified (static) | `$824A`, `$86C2`. |
| `$DB-$DE` | Driver loop: APU register offset, channel record pointer, channel countdown | verified (static) | `$8251-$82C9`. |
| `$DF` | Channels owned by the current effect (bit 0 pulse 1 ... bit 3 noise) | verified | Frame logs of all 40 effects. |
| `$E0/$E1`, `$E2`, `$E3` | Effect stream pointer, wait frames, loop counter | verified | Effect `$13` frame log; `$860D`. |
| `$C5` | Last music id requested through `$F752` (land music = land number; 5 Land Select, 6 boss, 7 title, 9 game over, `$0A` death) | verified | `$F752`; poke runs. |
| `$C6` | Sound ring write index | verified | Counts requests; not the land (refuted). |
| `$C7`, `$C8-$CF` | Sound request ring: read index and 8 entries, `$88` = empty | verified | NMI `$C110-$C127`. |
| `$E7` | Bank currently selected at `$8000` | verified | `bank_select`; the NMI restores it before `RTI`. |
| `$EF` | Gravity for `$F2B5` (`$33`, reloaded every frame at `$E354`); also used as scratch | verified | Trace. |
| `$F9` | Difficulty: 0 Easy, 1 Normal, 2 Difficult | verified | RAM at frame 1800 after selecting each difficulty differs exactly this way; the damage routine at `$EB5E` applies only every other hit when it is 0. |
| `$E8` | When non-zero the NMI skips the PPU update block | verified | NMI `$C01E`. |
| `$EA` | Sprite-0 HUD split enabled (`$40` in play) | verified | `$D840`, NMI ([rendering.md](rendering.md)). |
| `$E9`/`$ED`/`$EE` | Mid-frame `$2006` address for the vertical row scroll | hypothesis | `$C976`, NMI `$C0DE`. |
| `$F7` | PRG bank holding block and metatile tables (from `$DA10` + `$F6`) | verified | `$D9DA`; four land logs. |
| `$F8` | Turns the background off below the split | verified (code) | NMI `$C0EF`; use unknown. |
| `$FA` | Sprite-0 Y (`$2C`) | verified | `$D83A`, OAM check. |
| `$F0` | Playfield scroll Y written after the split (0 in play) | verified (code) | NMI `$C0FD`. |
| `$F1` | Map row (0 = top) | verified | Land tables, warp poke runs, `$E8B6` ([levels.md](levels.md)). |
| `$F2`/`$F3` | Camera X low byte / page; bit 0 of `$F3` also selects the nametable after the split | verified | `$0650 = ($0730:$0720) - ($F3:$F2)` on 5,201 of 5,250 gameplay frames (exceptions are transitions). |
| `$F6` | Current land 0-4 in Land Select order; 5 while Land Select loads and during the boss scroll; 9 in the ending | verified | Five intro routes; writers `$D746`, `$DB9D`, `$E1C6`. |
| `$FC/$FD` | Top-of-frame scroll Y and X (0 in play) | verified | NMI `$C07E-$C085`; logs. |
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
| `$0720+n`/`$0730+n` | World X low/page | verified | `$F234`/`$F252`; camera identity above. |
| `$0760+n`/`$0770+n` | Horizontal speed fraction/whole (`$0180` for Scrooge) | verified | Set at `$D81D`/`$D822`, reset every frame at `$E495`. |
| `$0780` | Scrooge's vertical direction for background probes (8 rising, 4 falling/standing) | verified | `$E976`/`$E9DF`. |
| `$0790`/`$07A0` | Jump take-off velocity, fraction/whole (`$04BB`) | verified | Written only at `$D815`/`$D81A` on land entry (trace, frame 1638 of `re_amazon_moves`); copied on a jump at `$E3F2-$E3FB`; poking it changes jump height reversibly. |
| `$07B0+n`/`$07C0+n` | Vertical velocity fraction/whole, 8.8 signed, up positive | verified | `$F2A1` applies it to Y, `$F2B5` subtracts gravity. |

## Other RAM

| Range | Meaning | Status | Evidence |
|---|---|---|---|
| `$0100-$011F` | Master palette (fade source) | verified | `$C28F`, `$D9FD`. |
| `$0120-$013F` | Consumed-object bits of the current land, one per object record | verified | Bank 6 `$AF2B`, `$F7F0`, bank 5 `$B760`. |
| `$0151/$0155/$0159` | HUD time digits | verified | `$E2AE`; change every 60 frames. |
| `$0200-$02FF` | OAM shadow, sent by sprite DMA every NMI | verified | NMI writes `$02` to `$4014`. |
| `$0300-$031F` | Palette buffer | verified | NMI palette upload. |
| `$0370/$038F/$03AE/$03CD` | Sound channel records, 31 bytes each (pulse 1, pulse 2, triangle, noise) | verified | `$8251` loop; frame logs. |
| `$03EC/$03ED` | Current song's instrument table pointer | verified (static) | `$80A6`, `$88D9`. |
| `$03F0-$03FF` | 4x4 tile buffer of the block being built | verified | `$CD7E`. |
| `$0400-$043F` | VRAM update queue (address high, low, count-1, data; bit 7 of the high byte ends it); `$19` = pending | verified | `$C139`. |
| `$0440-$047F` | Tile-edit queue; `$5C` = write offset, `$1A` = pending | verified | `$C15E`, `$F574`. |
| `$0500-$057F` | Attribute-table copies of nametables 0 and 1 | verified | `$C93D`. |
| `$0580-$05FF` | Collision class copy, 2 bits per 16x16 cell (1 solid, 2 climbable, 3 special), same layout | verified | `$CDAB`, `$D3B7`; probes at `$D120`. |
| `$0320-$032A` | HUD money update packet: PPU address `$2043`, length 7, then the tiles | verified | Dumps of the Amazon route. |
| `$0324-$0329` | Money, six decimal digits stored as tile ids (0-9, `$24` = suppressed leading zero), tens digit last; the HUD shows a fixed trailing 0 | verified | Collecting gems turns `$0327` 2, 4, 6 while the HUD shows 2000, 4000, 6000; the writer is the digit adder in bank 6 at `$8BBB`. |
| `$0334-$033A` | TOTAL money digits | verified | `land_done` poke run. |
| `$0340-$0367` | HUD sprite records, 4 bytes each (Y, tile, attribute, X); Y = `$FF` ends a group | verified | Dumps of the Moon route. |
| `$0340+4i` | HP dots: attribute (`+2`) 0 = full, 1 = lost; dots end at the first record with Y = `$FF` | verified | The damage routine at `$EB6A` marks the last full dot; all reset to 0 on respawn; three marked dots precede each death in the Moon route. |
| `$0361` | Extra lives, also the tile of the HUD "P.n" digit (0-9) | verified | 2, 1, 0 across the Moon route deaths; 2 on a new game; bank 6 `$8BB0` increments it up to 9. |
