# Native renderer

DuckTales' picture drawn natively from the game's video state, with no PPU emulation, and the
evidence that it equals the PPU's picture. Status words as in [memory-map.md](memory-map.md).

| File | Contents |
|---|---|
| `src/native/native_render.h` | The renderer's input (`NvVideo`), options and entry points; depends on no runtime |
| `src/native/native_render.c` | The renderer: background, sprites, priority, clipping, greyscale and emphasis, NTSC colors |
| `src/native/native_cycle.[ch]` | `nv_cycle_fill`: `NvVideo` from NESRecomp's cycle host through `cyc_render.h` (both builds) |
| `src/native/native_check.[ch]` | Headless builds only: `--native-render-check FILE` and `--native-render-dump DIR` |
| `tests/native_render_test.py` | Runs the check on the `full_*` routes and prints the differences per route |

## Input

`NvVideo` holds what a frame is drawn from:

- **Memories.** CHR RAM (8 KiB), CIRAM as two 1 KiB pages with the cartridge's mirroring (DuckTales:
  vertical), palette RAM (32 six-bit entries) and OAM (256 bytes), as they stand while the frame is
  drawn. In DuckTales they change only during VBlank or while rendering is off (rendering.md: the
  NMI's update block, the blanked CHR loads), so at the end of the frame they are what it used.
  Verified: no frame of the five `full_*` routes differs outside the split line (below).
- **Segments.** `NvSegment` = first line, `$2000` bits 3-5, `$2001`, and the background position
  of its first line in the PPU's own encoding (`x` = horizontal nametable bit, coarse X, fine X;
  `y` = vertical nametable bit, coarse Y 0-31, fine Y). Within a segment the position moves down one
  line per drawn line, as the PPU's `v` does (`nv_advance_y`: coarse Y 29 wraps to the other
  nametable, 31 wraps in place). DuckTales draws with two in play: the HUD (lines 0 to the split,
  nametable 0 at X 0, `$2000 = $90` or `$98`) and the playfield below it (X = `$F3:$F2`). One-segment
  frames are blanked or have no split (title, Land Select, scenes); three-segment frames are
  transitions where rendering switches mid-frame (19 frames over the five routes).
- **Split column.** A write that lands during a line (the HUD split's `$2005`, the row transition's
  `$2006`) changes that line from some column on: `split_column`, and the position the rest of the
  line shows (`split_x`, `split_y`). A runtime that does not model the write's dot leaves it at
  `NV_NO_SPLIT`, which places the split exactly at a line boundary.

## Rules

Each line is drawn from its segment, as the 2C02 composes it (nesdev wiki "PPU rendering", "PPU
sprite evaluation", "PPU sprite priority"):

1. Background: the nametable byte at the line's position plus the column (fine X included), the
   attribute quadrant, the pattern row from `$2000` bit 4. `$2001` bit 3 shows the layer and bit 1
   the left 8 columns; a hidden or transparent pixel is color 0.
2. Sprites: a line `y` (1-239) shows the sprites whose rows cover it (OAM Y + 1 is the first line),
   the first ones in list order up to `sprites_per_line` (8 on the console, 0 for no limit), 8x8 or
   8x16 and the 8x8 pattern table from the line's `$2000`, flips from the attribute. Line 0 shows
   none. `$2001` bit 4 shows them, bit 2 the left 8 columns.
3. Priority: at each column the first listed sprite with an opaque pixel decides. If it is marked
   behind the background and the background is opaque there, the background shows, and that sprite
   still hides the sprites after it, as on the PPU.
4. Output: palette RAM's entry (`$3F00` for color 0), greyscale (`$2001` bit 0) and emphasis
   (bits 5-7) as the same 9-bit index `cyc_frame_index()` holds; a line with background and sprites
   off is the backdrop. `nv_palette_argb` decodes the indices to the colors NESRecomp's window shows
   (the 2C02's NTSC signal with the nesdev wiki's measured levels).

`nv_sprite0_hit` gives the first pixel where OAM entry 0 and the background are both opaque (left
columns as `$2001` clips them, never column 255): what a runtime without a PPU answers the game's
`$2002` polling with.

`NvRenderOptions.sprites` replaces the OAM with any list (front first, any length, each sprite with
its own 8x8 pattern table or the line's): with `sprites_per_line = 0` this is the rule of the
flicker-free compositor (rendering.md "Sprites without limits"), drawn natively.

## Verification

`--native-render-check FILE` (headless builds) fills `NvVideo` with `nv_cycle_fill` at the end of
every frame (scanline 241 dot 1, before the NMI), draws it with the console's rules and compares
every pixel with the machine: the 9-bit index with `cyc_frame_index()`, the ARGB color with
`cyc_frame_argb()`, the background coverage with `cyc_frame_bg_opaque()` on drawn lines.

`py -3 -I tests/native_render_test.py --build build/headless` (2026-11-22, five `full_*` routes;
the same numbers again after merging main at dabe175):

| Route | Frames | Line-granular segments: frames / pixels differing | Split column placed: frames / pixels | `$2005` splits / `$2006` splits / not placed | Differences off the split line |
|---|---|---|---|---|---|
| `full_amazon` | 9,600 | 5,104 / 122,468 | 53 / 5,091 | 4,584 / 467 / 53 | 0 |
| `full_himalayas` | 6,200 | 2,708 / 84,680 | 17 / 2,136 | 2,342 / 349 / 17 | 0 |
| `full_transylvania` | 7,120 | 2,066 / 42,836 | 0 / 0 | 1,944 / 122 / 0 | 0 |
| `full_african_mines` | 13,520 | 4,696 / 97,288 | 15 / 1,885 | 4,452 / 229 / 15 | 0 |
| `full_moon` | 11,000 | 6,451 / 146,124 | 30 / 3,771 | 5,963 / 458 / 30 | 0 |

Status: verified. Every pixel of every line except the split line equals the PPU's on all 47,440
frames, the ARGB colors differ exactly where the indices do (so the 512 colors are identical), and
`$2800/$2C00` mirror `$2000/$2400` on every frame.

### The split line

The line records the cycle host keeps (`cyc_frame_lines`) are taken at each line's first dot, so the
split's writes, which land during the line above the playfield, are not in them. With line-granular
segments that line is the only one that differs (up to 144 pixels of it in `full_amazon`). The check then searches, on that line only, for the column the write took effect
at:

- **`$2005` (fine X).** The rest of the line keeps the HUD's tiles with the playfield's fine X. This
  places the split exactly in 19,285 frames. The column always lies 64 to 84 pixels after the native
  sprite-0 hit (the hit's column; one line = 341): every route's frames fit that one 21-dot window,
  which is the game's 7-cycle polling loop (`$C0D9 BIT $2002 / BVC`) followed by the fixed 21 cycles
  to `STA $2005` at `$C0FA`. So the split column is predicted natively up to the loop's phase.
- **`$2006` (row transition, `$E9` set).** The write moves the whole position mid-line; any one
  position from some column (Y one line above the next line's) places it in 1,625 more frames.
- **Not placed: 115 frames, all row transitions.** One position from one column does not reproduce
  them: the write lands inside a tile fetch, so one tile mixes the old and the new address, and the
  later `$2005` write changes fine X again on the same line. Reproducing them needs the PPU's fetch
  timing to the dot; on the console they are 100-150 pixels of one line of a 60-frame transition.

A runtime that supplies no split column draws the split at a line boundary: the one line the
console redraws differently every frame (the jitter of the polling loop) then stays still.

## Replacing the background capture in the compositor

`src/host/ducktales_sprites.c` (the flicker-free compositor) draws its sprites over the PPU's
captured background (`cyc_render_capture_background` / `cyc_render_background`) with
`cyc_frame_bg_opaque`. The native renderer replaces both:

1. In `compose`, call `nv_cycle_fill(&video)` instead of `cyc_render_background(s_background)`, and
   stop enabling the capture in `apply` (the PPU then does no extra work for presentation).
2. Turn the rebuilt list into `NvSprite`s: `table = 0x1000` for HUD records (`hud_table`), else
   `NV_TABLE_LINE`; OAM entry 0 (the split sprite) first as now.
3. `nv_render(&video, &(NvRenderOptions){ 0, list, n }, index, NULL)` then `nv_to_argb` into the
   canvas. Priority, behind-background, the left clip, 8x16 and the per-line pattern tables are the
   renderer's; `draw()` goes away.
4. The fallback (fewer than two saved frames) is the same call with `options = NULL`, which is the
   console's picture exactly as verified above, except the split line.

The only visible difference from today's composed picture is the split line: drawn at the line
boundary instead of the console's per-frame column. Later, with a native runtime (no cycle host),
`nv_cycle_fill` is replaced by an adapter from the runtime's frame record (its memories at line 0,
and its register writes with their line and dot become segments and split columns).

## Not verified

- `sprites_per_line = 0` and replacement lists have no reference to compare with; on lines with 8
  or fewer sprites they take the same path as the verified console rule.
- Horizontal or single-screen mirroring, 8x16 sprites, greyscale and emphasis: implemented, never
  used by DuckTales on these routes, so not exercised.
- Mid-line `$2001` changes (`$F8`, background off below the HUD) never occurred on the routes.
- The window build compiles and links the renderer but does not use it yet.
