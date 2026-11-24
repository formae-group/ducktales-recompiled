/*
 * native_render.h - the video state one frame of DuckTales is drawn from, and
 * the native renderer that draws it (native/native_render.c). Nothing here
 * depends on a runtime: the cycle host fills NvVideo from its PPU
 * (native/native_check.c), a native runtime fills it from what the game
 * wrote. docs/NATIVE-RENDER.md describes the model and its verification.
 *
 *   memories    CHR RAM (8 KiB), CIRAM (two 1 KiB nametables) with the
 *               cartridge's mirroring, palette RAM (32 bytes, 6 bits) and
 *               OAM (256 bytes), as they stand while the frame is drawn
 *   segments    the frame's scroll and control: from first_line on, until the
 *               next segment, lines are drawn with these $2000/$2001 bits and
 *               this background position, the position moving down one line
 *               per drawn line as the PPU's does. DuckTales uses two: the HUD
 *               at the top, the playfield below the sprite-0 split.
 *   picture     256x240 9-bit color indices (color | emphasis << 6), the
 *               values cyc_frame_index() holds, and their ARGB colors
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV_WIDTH        256
#define NV_HEIGHT       240
#define NV_MAX_SEGMENTS 240
#define NV_TABLE_LINE   0xFFFFu
#define NV_NO_SPLIT     256

typedef enum {
    NV_MIRROR_VERTICAL,   /* $2000/$2800 page 0, $2400/$2C00 page 1 (DuckTales) */
    NV_MIRROR_HORIZONTAL, /* $2000/$2400 page 0, $2800/$2C00 page 1 */
    NV_MIRROR_SINGLE_0,
    NV_MIRROR_SINGLE_1,
} NvMirroring;

/* Control bits of a segment: $2000 bits 3-5 and $2001 as the PPU uses them. */
enum {
    NV_CTRL_SPRITE_1000 = 0x08,
    NV_CTRL_BG_1000 = 0x10,
    NV_CTRL_SPRITE_8X16 = 0x20,
    NV_MASK_GREYSCALE = 0x01,
    NV_MASK_BG_LEFT = 0x02,
    NV_MASK_SPRITE_LEFT = 0x04,
    NV_MASK_BG = 0x08,
    NV_MASK_SPRITES = 0x10,
};

typedef struct {
    uint8_t first_line; /* 0-239, increasing from segment to segment; the first is 0 */
    uint8_t ctrl;       /* NV_CTRL_* */
    uint8_t mask;       /* $2001: NV_MASK_*, emphasis in bits 5-7 */
    uint16_t x;         /* background X at column 0: horizontal nametable bit << 8 | coarse X << 3 | fine X */
    uint16_t y;         /* background Y of first_line: vertical nametable bit << 8 | coarse Y (0-31) << 3 | fine Y */
    /* The segment's last line, from this column on, shows the background at
     * X split_x + column and Y split_y (same encodings as x and y) instead:
     * writes that land during the line (the HUD split: $2005 changes fine X
     * only; the row transition's $2006 moves both). NV_NO_SPLIT: none. */
    uint16_t split_column;
    uint16_t split_x, split_y;
} NvSegment;

typedef struct {
    uint8_t chr[0x2000];
    uint8_t ciram[2][0x400];
    NvMirroring mirroring;
    uint8_t palette[32];
    uint8_t oam[256];
    int segment_count;
    NvSegment segment[NV_MAX_SEGMENTS];
} NvVideo;

/* A sprite of a list that replaces the OAM (NvRenderOptions.sprites). table
 * is NV_TABLE_LINE (the line's $2000 bit 3) or 0x0000/0x1000 for 8x8 tiles. */
typedef struct {
    uint8_t y, tile, attr, x;
    uint16_t table;
} NvSprite;

typedef struct {
    /* Sprites a line shows, the first ones in list order: 8 on the console,
     * 0 for no limit. */
    int sprites_per_line;
    /* NULL: the 64 OAM entries in OAM order. Otherwise this list, front
     * first, any length. */
    const NvSprite *sprites;
    int sprite_count;
} NvRenderOptions;

/* Where a pixel of the picture comes from (nv_render_source): the 8x8 tile
 * (pattern address / 16, 0-511), the pixel's column and row in the tile as
 * the pattern stores it (flips undone) and the palette (0-3 background, 4-7
 * sprites). A background pixel of color 0 still names its tile; a pixel no
 * tile covers (blank line, layer or left columns hidden) is NV_SOURCE_NONE. */
typedef uint32_t NvSource;
#define NV_SOURCE_NONE 0u
#define NV_SOURCE_MAKE(tile, column, row, palette) \
    (0x80000000u | (uint32_t)(tile) | (uint32_t)(column) << 9 | (uint32_t)(row) << 12 | (uint32_t)(palette) << 15)
#define NV_SOURCE_TILE(s)    ((s) & 0x1FFu)
#define NV_SOURCE_COLUMN(s)  (((s) >> 9) & 7u)
#define NV_SOURCE_ROW(s)     (((s) >> 12) & 7u)
#define NV_SOURCE_PALETTE(s) (((s) >> 15) & 7u)

/* The console's rules: OAM, 8 sprites per line. */
void nv_render_options_console(NvRenderOptions *o);

/* Draw the frame (or lines first..last of it). index: 256x240 color indices.
 * bg_opaque (or NULL): 1 where the background's pixel is opaque, as
 * cyc_frame_bg_opaque(). options NULL: the console's rules. */
void nv_render(const NvVideo *video, const NvRenderOptions *options, uint16_t *index, uint8_t *bg_opaque);
void nv_render_lines(const NvVideo *video, const NvRenderOptions *options, int first, int last, uint16_t *index,
                     uint8_t *bg_opaque);
/* nv_render, and source (256x240) for each pixel. */
void nv_render_source(const NvVideo *video, const NvRenderOptions *options, uint16_t *index, NvSource *source);

/* Where the frame sets the sprite-0 hit ($2002 bit 6): the first pixel, in
 * drawing order, where OAM entry 0 and the background are both opaque (the
 * left 8 columns as $2001 clips them, never column 255). False: no hit. A
 * runtime without a PPU answers the game's $2002 polling from this. */
bool nv_sprite0_hit(const NvVideo *video, int *x, int *y);

/* The ARGB color of each 9-bit index: the 2C02's NTSC signal decoded with the
 * nesdev wiki's measured levels (the colors NESRecomp's window shows). */
const uint32_t *nv_palette_argb(void);
void nv_to_argb(const uint16_t *index, uint32_t *argb, int count);

/* The background position a segment reaches after `lines` drawn lines. */
uint16_t nv_advance_y(uint16_t y, int lines);

#ifdef __cplusplus
}
#endif
