# Rendering

How DuckTales builds its picture: map and screen data, the VRAM update queues, CHR-RAM loads,
palettes, the HUD split and the sprite builder. Status words as in [memory-map.md](memory-map.md);
code addresses are bank 7 (fixed) unless a bank is named. "Check" below means
`tools/render_map.py check` or `tools/render_oam.py check` on a `--frame-log` record: the tools
predict nametables, attributes, CHR RAM, palette, picture and OAM from the ROM tables and compare
them with what the PPU and RAM actually hold (commands under [Reproducing](#reproducing)).

## Summary for a native renderer

- The playfield is two nametables side by side (vertical mirroring, `$2000` and `$2400`); map
  page `p` always lands in nametable `p & 1`. The HUD lives in the top six tile rows of
  nametable 0 and is shown above a sprite-0 split.
- A screen (256x240) is 8x8 blocks of 32x32 px; a block is 2x2 metatiles of 16x16 px; a metatile is
  2x2 tiles plus one byte holding its palette and collision class. Everything can be decoded from the
  ROM without running the game, so a widescreen renderer can draw any number of screens around the
  camera straight from the map (`tools/render_map.py land` does exactly that).
- Background tiles come from pattern table `$1000`, sprites from `$0000`. CHR is RAM: the land's
  background and the common sprites are copied once at land load, and `$0900-$0FFF` (enemy sprites)
  is replaced per map row.
- Sprites are object slots drawn through animation scripts and metasprites; OAM slot order
  alternates every frame (flicker) and there is no other budget than the 63 OAM entries.

## Frame structure (NMI `$C000`)

| Step | Behaviour | Status | Evidence |
|---|---|---|---|
| Sprite 0 | When `$EA` is set the NMI rewrites OAM entry 0 as Y = `$FA`, tile `$21`, attribute `$20` (behind background), X = 8. | verified | `$C006-$C01B`; the `$0200` shadow holds `2C 21 20 08` in every land at frames 1449-1450 (render_oam check). |
| Update block | Skipped while `$E8` is set (`$C197` increments `$E8` and blanks, `$C1A1` decrements it and enables `$2001 = $1E`). Otherwise: `$2000 = $FF & $78` (NMI off, +1 increment), `$2001 = 0`, sprite DMA from `$0200` unless `$98`, queue `$0400` when `$19`, queue `$0440` when `$1A`, palette `$0300-$031F` when `$18`, then scroll `$FD`, `$FC` and `$2001 = $FE`. | verified | `$C01E-$C08A`. |
| HUD pattern tables | With `$EA` set, the top of the frame uses `$2000 = $FF | $18` (background and sprites from `$1000`) when Scrooge's Y (`$0640`) is `$44` or more and `$F6 != 9`; otherwise `$FF` (`$90` in play: sprites from `$0000`). | verified (code, `$FF` value) | `$C08D-$C0A9`; `$FF = $90` in the four lands logged (Amazon, Transylvania, Himalayas, Moon). The sprite builder only adds the HUD sprites under the same `$0640 >= $44` condition (bank 5 `$8030`), so HUD sprites use `$1000` tiles and Scrooge, when he is up in the HUD band, keeps `$0000`. |
| Sprite-0 split | After the task timers, if `$EA` and not `$E8`: waits for sprite-0 hit to clear and set, then optionally writes `$2006 = $EE, $ED` (only while `$E9` is set), `$2001 = $10` when `$F8` is set (background off below the HUD), `$2005 = $F2, $F0` and `$2000 = $90 | ($F3 & 1)`. | verified (code) | `$C0CA-$C108`. In play `$F0 = 0`, `$E9 = 0`, `$ED/$EE = $C0/$24` (Amazon and Moon frame logs, frames 1440-1446). |
| Below the split | The second `$2005` write does not move the picture vertically (only a `$2006` write does), so the playfield shows nametable rows at the same Y as the screen line; X = `$F3:$F2`. | verified | The background pixels of lines 53-239 equal a render of the two nametables at X = `$F2` and Y unchanged: Amazon 47,114 of 47,872 pixels, the rest covered by sprites (render_map check `--picture`). |

Top-of-frame scroll `$FD/$FC` is 0 in play, so the HUD always shows nametable 0 from X = 0.
`$E9` with `$ED/$EE` is the vertical row transition: `$C976` sets `$E9 = $0C` and moves the
`$2006` address written after the split over 60 frames (`$30` counts to `$3C`), which scrolls the
playfield vertically under a fixed HUD (static reading; no route changes rows, see levels.md).

## Map, screen, block and metatile format

| Level | Where | Format | Status | Evidence |
|---|---|---|---|---|
| Map | `$C7B0` with X = `$F6` | rows (`$CE57`), stride 8 (`$CE61`) and a grid pointer (`$CF7F/$CF89`) of screen ids, `$FF` = none | verified | levels.md; `$C71D` and `$C78F` call it with X = `$F6`. |
| Screen id | bank 4, `$8000 + id * 72` | 64 block numbers, row-major (8 rows x 8 columns of 32x32 px), then 8 bytes giving each block a 9th bit (byte = block row, bit 7 = column 0). Blocks 0-511. | verified | `$CE2C` computes `$22/$23 = $8000 + id*72`; `$CDAF` selects bank 4, reads byte `$10` (row*8+column) and bit `$CE20[column]` of byte `$40 + row`. Check: every nametable tile and attribute below the HUD equals the decode in the Amazon (1,728/1,728 tiles, 112/112 attribute bytes). |
| Block | bank `$F7`, `$B300 + block * 4` | metatile ids of the four 16x16 quadrants: top-left, top-right, bottom-left, bottom-right | verified | `$CDAF` builds the pointer (`block*4 + $B300`); `$CCDD-$CD98` reads Y = 3..0 and places quadrant Y at buffer offset `$CE28[Y]` = 0, 2, 8, 10 of the 4x4 tile buffer `$03F0`. |
| Metatile | bank `$F7`, `$BB00/$BC00/$BD00/$BE00 + id` | the four 8x8 tiles (top-left, top-right, bottom-left, bottom-right) | verified | `$CD7E-$CD95`; same check. |
| Metatile attributes | bank `$F7`, `$BF00 + id` | bits 0-1 palette; bits 4-5 collision class (1 solid, 2 climbable, 3 special, see memory-map `$0580`) | verified (palette), verified (collision layout) | `$CCFE-$CD22` shifts both pairs by 2 x quadrant into `$27` (attribute byte) and `$28` (collision byte); attribute check above; `$CDAB` stores `$28` in `$0580`. |
| Tileset bank `$F7` | `$DA10 + $F6` (written by `$D9CA`) | Amazon 1, Transylvania and African Mines 2, Himalayas and Moon 3; Land Select 1 (entry 5); entry 6 is 0 (hypothesis: the title, which loads CHR list 6) | verified | `$F7` in the frame logs equals the table in the four lands logged. |

The bottom block row (row 7) is half a block: only quadrants 0 and 1 are used (rows 28-29), its
attribute byte keeps only its top nibble (`$C903-$C911`), and the queue drops its lower two tile rows.

Two state-dependent substitutions are built into the decoder (`$CD24-$CD7C`, skipped while `$E9`
is set): in the Himalayas with `$A2` non-zero, blocks `$21` and `$29` of screen index `$28` draw
metatile `$88` with palette 0; in the Moon with `$A1` bit 7 set, two block columns of screen index
`$27` (lower half) draw metatiles `$D6/$D7`. Status: static reading (hypothesis: opened passages).

## Column streaming and the `$0400` queue

| Item | Behaviour | Status | Evidence |
|---|---|---|---|
| Camera update | `$EFE2` (frame loop `$DB89`) keeps Scrooge at screen X `$80`, moves `$F3:$F2`, stops at a row end (next cell `$FF` or outside the row; Scrooge's X is then clamped to `$10`/`$F0`), then jumps to `$C7C5`. With `$78` bit 1 a separate path moves the camera by script (hypothesis: the scenes). | verified (code) | `$EFE2-$F0C1`. |
| Steps | `$C7C5`: one step per 4 pixels of camera movement (`$04 = ((prev & 3) + |dX|) >> 2`, rounding toward the scroll direction); on a direction change `$26` first jumps by 2 steps (`$C96A`). | verified (code) | `$C7C5-$C81E`. |
| Step = one 32x32 block | `$26 & 7` = block row, `$26 >> 3` = block column within the screen whose map index is `$24` (page `$2C = $24 & 7`). So 8 steps draw one 32-pixel column top to bottom, and `$26` wraps at `$40` to the next screen. | verified | `$CCBD-$CCCF` builds `$10 = row*8 + column` from `$26`; `$C883-$C89B` builds the PPU address. In the Amazon route `$26` advances four steps (`$3D` to `$01`) while `$F2` moves 14 pixels (frames 1380-1410). |
| Queue entries | Each step writes four entries of 4 tiles (rows `4r..4r+3`, columns `4c..4c+3`, PPU `$2000 + ($2C & 1) * $400 + r*$80 + c*4`) and one attribute entry (`$23C0/$27C0 + $10`). | verified | `$C867-$C93D`. |
| HUD guard | In nametable 0 (even page) during play (`$F6 < 5`) block row 0 is not drawn and block row 1 drops its upper two tile rows; its attribute byte keeps only the lower nibble. | verified | `$C855-$C865`, `$C8D7-$C8FD`; the HUD rows of nametable 0 are excluded from the check and the rest matches. |
| Shadows | Attribute byte also stored at `$0500 + ($2C & 1) * $40 + $10`, collision byte at `$0580 + ($2C & 1) * $40 + $10`. | verified | `$C931-$C93D`, `$CD9F-$CDAB`; `$0540-$057F` equals nametable 1's attribute table 64/64 in the four lands logged, `$0500-$053F` equals nametable 0's except the 4 HUD bytes. |
| Queue format `$0400` | Records of PPU address high, low, byte count - 1, then the bytes; a high byte with bit 7 set ends the list. Drawn by `$C139` when `$19` is non-zero, always with +1 increment (rows, never columns). | verified | `$C139-$C15D`; the NMI writes `$2000 = $FF & $78`. |
| Direct draw | When `$E8` is set (screen blanked) the producers call `$C139` themselves instead of setting `$19`. | verified | `$C847`, `$C952`, `$C4DA`. |
| Initial draw | `$C71D` (land load) draws `$C786[X] + 1` steps (96 or 128, i.e. 12 or 16 block columns) starting at `$26 = $C789[X]` of screen `$2D + $C78C[X]`, X chosen by whether the cells left and right of the start screen exist, one step per call of `$C830`; `$C78F` (Land Select) draws a whole screen (`$26` 0-`$3F`); `$DD4E` (respawn and warps) draws `$11` steps from the respawn entry. | verified (code) | Static reading of the three loops. |

Other users of `$0400`: the HUD money and TOTAL packets (`$0320`, `$0330`, copied at `$D827`), text
and messages (`$EE8B`/`$EEAE`), and one CHR tile (below).

## Tile edits (`$0440` queue)

`$F574` replaces one 16x16 metatile at an object's position: it writes two 2-tile entries and an
attribute entry into `$0440 + $5C`, updates the collision shadow `$0580` and attribute shadow `$0500`
(keeping the HUD half of top-row attribute bytes), and advances `$5C` by 14 (`$F4F5`). The
replacement metatile is `$F653[$F6 * 4 + A]` from bank `$F7` (`$F697`). The frame loop copies `$5C`
to `$1A` and clears it (`$DC19`); the NMI draws the queue with `$C15E`, same format as `$0400`.
Status: verified (code); hypothesis that it is used for breakable or opening blocks (the path through
`$F506-$F555` also spawns an object of type `$F67B[$06B0]` at the same place). The entry point that
sets A and `$08` just before `$F4E8` was not pinned down.

## CHR-RAM uploads

| Routine | When | What | Status | Evidence |
|---|---|---|---|---|
| `$C301` (X = list) | Screen blanked: title (X = 6, `$D531`), Land Select (X = 5, `$D647`), land load (X = `$F6`, `$D7CE`), scenes (`$DF61`, X = map), ending scenes (`$E0F4`, X from `$E121`) | List at `$C358/$C363 + X` of 3-byte records: high nibble = PRG bank, low nibble = pages - 1, source page (`$xx00`), PPU page; a byte `>= $80` ends. Writes `$2007` directly. | verified | `$C301-$C357`. For a land: common sprites to `$0000-$07FF`, one sprite page per land to `$0800`, common background tiles (HUD included) to `$1000-$12FF`, the land's background set to `$1300-$1FFF`. Check: CHR RAM equals the lists byte for byte in the four lands logged, except the message tile below (Transylvania, 14 bytes). |
| `$C42C` (state `$50`) | Land load (`$D7D8`), respawn and warp (`$DCBA`), boss arena and scenes (`$D01B`, `$E00D`, `$E031`): called until `$50` goes negative | Per land row: index `$C508[$F6] + $F1` into `$C511`; a value with bit 7 set picks one of two lists by camera page (`$F3 >= 4`, Moon `>= 3`). The list (`$C563/$C583`, same record format) is queued 64 bytes per call through `$0400`. At the end it loads sprite palette 3 (`$031C-$031F`, copy at `$011C`) from `$C6D9 + 4 * $C53A[...]`. | verified | `$C42C-$C507`. Lists only target `$0900-$0FFF` (enemy sprites of the row). Check: `$0900-$0FFF` and sprite palette 3 match the row's list in the four lands logged. levels.md calls this "streams the first screens": it streams CHR, not nametables. |
| `$ED05` | Message box open/close | Replaces background tile `$12D` (`$12D0`) with 16 bytes from bank 2 `$A8F0` while a message shows and restores it from bank 1 `$82D0` after (`$ED8A`). | verified | `$ED13-$ED2E`; frame log of `transylvania_intro`: `$12D1-$12DF` change at frame 1370 after this entry is queued at frame 1369 (whoaccesses: `$ED15`). |

`$C203` (A = nametable high byte, X = fill tile, Y = attribute) clears a nametable while blanked;
bank 5 `$BAB0` (X = string set) draws text and the HUD frame directly (records: PPU address,
bytes until `$FE`; 0 ends).

## Palettes

| Item | Behaviour | Status | Evidence |
|---|---|---|---|
| Land / screen palette | `$D9CA` copies 16 background bytes from `$DA34 + 16 * $F6` and 16 sprite bytes from `$DAC4` (shared) to `$0300` and the master copy `$0100`. Entries 0-4 lands, 5 Land Select, 6-8 other screens. | verified | Check: palette RAM equals the tables (24/24 non-backdrop entries) in the four lands logged. |
| Sprite palette 3 per row | Set by `$C42C` at the end of each row load (see CHR). | verified | Same check (entries `$1C-$1F`). |
| Scene palettes | `$F76D` (Y = scene) loads both halves from `$E143` through the index tables `$E127/$E12D`; bit 7 of the second index also loads sprite palette 3 from `$E1A3 + 4 * $F9`. Called by the scene loader `$E08F` (which also sets `$F7` from `$E11B`) and bank 5 `$B4F4`. | verified (code) | Static reading. |
| Fades | `$C282` fades out and `$C279` fades in: every 4 frames `$0300` = `$0100` minus `$10` per step, clamped to `$0F`, until the step reaches `$50` or goes negative. | verified (code) | `$C285-$C2C7`. |
| Object effects | Bank 6 writes single sprite-palette entries (`$8D08` `$031D`; `$9A20` `$031E/$031F`). | hypothesis (meaning) | Static xref. |
| Upload | The NMI uploads `$0300-$031F` when `$18` is set, then points `$2006` at `$3F00` and `$0000` to leave the palette. | verified | `$C04E-$C078`. |

Palette byte 0 is the universal backdrop; on the PPU the sprite half's entries 0, 4, 8 and 12 mirror
the background's, so the check ignores them.

## HUD

The HUD background is nametable 0 tile rows 0-5, drawn while blanked by bank 5 `$BAB0` (X = 0 at
land load, `$D7C9`; the money digits through the `$0320/$0330` packets); HP dots, lives and the time digits are
sprites from the `$0340`, `$0358` and `$0140` groups, drawn by the sprite builder from pattern table
`$1000` (see Frame structure). The split sprite sits at Y `$FA` = `$2C` (set at `$D83A` with
`$EA = $40`), so the hit, and the playfield, start a few lines below line `$2C` (hypothesis: the exact line depends on the split tile's pixels). Objects other than slot 0 lose any sprite whose Y
is below `$30` unless their `$0600` bit 4 is set (bank 5 `$81F9-$8208`), which keeps enemies out of
the HUD band. Status: verified (code); the picture check starts at line 53 to stay clear of the split
line.

## Sprites (bank 5 `$8000`)

Called every frame by the frame loop with `$98` set (so the NMI does not DMA a half-built table;
`$DBC5-$DBD2`) after `$C267` parks every entry from 1 (or 0 when `$EA` is clear) at Y `$F8`.

1. `$97` = 4 (entry 0 is the split sprite). If `$7F` bit 7 is set, no message (`$6C`), no ending, no
   pause, `$39 != $0F` and Scrooge's Y `>= $44`, the three HUD groups are copied first (4-byte
   records, `$FF` ends a group).
2. `$95` is incremented; on even values slots are visited 0 to 15, on odd values 15 to 0. This is the
   flicker: when more than 63 sprites are wanted, a different set is cut off on alternate frames.
3. A slot is drawn when `$0700` bit 7 is set and `$0600 != $FF`. With `$0700` bit 5 (world
   coordinates) the screen X `$0650` = world X - camera; an object on another page is not drawn (and is
   deleted when more than `$40` px away, except type `$5A`); bit 4 hides it. `$0600` bit 1 blinks it
   (drawn on frames where `$95` bit 1 is set); Scrooge gets that bit while `$7D` or `$75` runs.
4. Animation: type `$0620` indexes a script (`$B1C7/$B257`): byte 0 = last frame, byte 1 = frame
   length, then one metasprite id per frame. `$0630` counts the frame length (bit 7 freezes it, and
   nothing advances while `$39 = $0F`), `$0610` is the frame. After the last frame the animation
   restarts at 0, or at `$0680` when `$0610` bit 7 is set; with `$0600` bit 3 set the object is deleted instead.
5. Metasprite (`$9681/$9779`): byte 0 = sprites - 1, byte 1 = offset-table id, then tile and
   attribute per sprite. The offset table (`$ADE1/$AEC1`, id + 1 when flipped) holds a signed dY, dX
   pair per sprite, so horizontal flip uses a separate mirrored offset table and only XORs attribute
   bit 6 (`$0600` bit 6). `$0600` bit 2 sets the priority bit (behind background).
6. A sprite whose offset carries it off the 0-255 range is skipped without using an entry; the builder
   stops when `$97` wraps to 0 (64 entries).

Status: verified. `render_oam.py check` replays these rules from the RAM of a frame and predicts the
`$0200` shadow byte for byte: Amazon 116/116, Transylvania 92/92, Himalayas 128/128 and Moon 136/136
bytes at frames 1449 and 1450, with the slot order flipping between the two frames and every unused
entry parked.

## Reproducing

```sh
E=build/headless/Release/ducktales.exe; ROM="roms/DuckTales (USA).nes"
$E "$ROM" --frames 1451 --input tests/routes/amazon_intro.txt --frame-log captures/fl_amazon.bin --frame-log-frames 1449:1450
py -3 -I tools/render_map.py "$ROM" check captures/fl_amazon.bin --frame 1450 --picture
py -3 -I tools/render_oam.py "$ROM" check captures/fl_amazon.bin --frame 1450 --slots
py -3 -I tools/render_map.py "$ROM" tables            # CHR lists, row lists, tileset banks
py -3 -I tools/render_map.py "$ROM" land 0 captures/amazon_map.png --scale 1
py -3 -I tools/whoaccesses.py --input tests/routes/transylvania_intro.txt --frame 1369 0400,0401,0402
```

The same `check` on the other intro routes at frame 1450 gives: Transylvania 1,712/1,728 tiles,
Himalayas 1,713, Moon 1,720; the differences are blocks of the column being streamed (the check picks
one screen per 32-pixel column, while a column in progress mixes two). `african_mines_intro` is in
Transylvania at 1450 after the redirect. PNGs and logs stay under `captures/`.

## Not yet known

- The callers of the tile-edit routine around `$F4E8`/`$F506` (which objects edit tiles) and the exact role of the Himalayas and
  Moon substitutions.
- The full vertical transition (`$C976` with `$E9`, `$ED/$EE`): read statically, never run.
- Scene and ending CHR lists (`$C301` 7-10, `$E121`) and the scene palette tables were not checked
  against a run.
- What `$F8` (background off below the HUD) is used for.
