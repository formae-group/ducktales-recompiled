/*
 * dt_mods_picture.c - what DuckTales' mods change in the presented picture
 * (app_mods.h), never the machine's.
 *
 *   tiles      the loaded tiles/<key>.png are painted over the presented
 *              picture: it is drawn again with nv_render_source to know each
 *              pixel's tile and palette, and a pixel is changed only where
 *              the presented picture shows the colour that drawing gives it
 *   palettes   a land's palette is used while $F6 is that land and the music
 *              is neither Land Select's nor the title's, else global.pal
 *   sprites    a pixel a sprite replacement drew (keep) is left as it is:
 *              that object's art wins over tile replacements and palettes
 *   dump       --mods-dump: every tile drawn, once (fm_nes.h); the object
 *              frames are dt_mods_sprites.c's
 *   overlay    the scripts' toast and dt.draw, drawn with cyc_overlay
 */
#include "mods/app_mods.h"
#include "mods/ducktales/dt_mods_internal.h"
#include "mods/nes/fm_nes.h"

#include "game/ducktales_symbols.h"

#include "cyc_overlay.h"
#include "cyc_render.h"

#include <stdio.h>
#include <string.h>

static const uint32_t *s_painted_palette;

static bool painting(void)
{
    return fm_nes_tile_count() || dt_mods_palette_count || fm_nes_dumping();
}

bool app_mods_video_active(void)
{
    return painting() || dt_mods_sprite_count() > 0 || fm_script_count() > 0;
}

bool app_mods_dump_dir(const char *dir)
{
    return fm_nes_dump_to(dir) && dt_mods_sprites_dump_to(dir);
}

void app_mods_dump_frame(void)
{
    if (fm_nes_dumping()) {
        int w, h;
        cyc_render_present(&w, &h);
    }
}

uint32_t app_mods_color(uint16_t index, uint32_t argb)
{
    return s_painted_palette ? s_painted_palette[index & 0x1FF] : argb;
}

/* text as the overlay font draws it in width pixels: a character it lacks
 * (any UTF-8 code point past ASCII included) as ?, and a line too long cut
 * with ... at its end. */
static void overlay_text(const char *text, int width, char *out, size_t cap)
{
    size_t fits = (size_t)(width - 12) / 4, n = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c && n + 1 < cap; c++) {
        if ((*c & 0xC0) == 0x80) {
            continue;
        }
        out[n++] = *c == ' ' || (*c < 0x80 && cyc_overlay_glyph((char)*c)) ? (char)*c : '?';
    }
    out[n] = 0;
    if (n > fits && fits >= 3 && fits < cap) {
        snprintf(out + fits - 3, 4, "...");
    }
}

/* A dt.draw rectangle, clipped to the picture. */
static void fill_rect(uint32_t *argb, int width, int height, const FmDraw *d)
{
    int x0 = d->x < 0 ? 0 : d->x, y0 = d->y < 0 ? 0 : d->y;
    int x1 = d->x + d->w > width ? width : d->x + d->w, y1 = d->y + d->h > height ? height : d->y + d->h;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            argb[y * width + x] = 0xFF000000u | d->color;
        }
    }
}

static uint8_t s_overlay_mask[NV_WIDTH * NV_HEIGHT];
static bool s_overlay_drawn;

void app_mods_draw_overlay(uint32_t *argb, int width, int height)
{
    char title[128], body[300], title_shown[128], body_shown[300];
    static uint32_t before[NV_WIDTH * NV_HEIGHT];
    const FmDraw *draws;
    int n = argb ? fm_draws(&draws) : 0;
    bool toast = argb && fm_toast(title, sizeof title, body, sizeof body);
    bool masked = width == NV_WIDTH && height == NV_HEIGHT;
    s_overlay_drawn = masked && (n || toast);
    if (s_overlay_drawn) {
        memcpy(before, argb, sizeof before);
    }
    for (int i = 0; i < n; i++) {
        if (draws[i].text) {
            char text[sizeof draws[i].str];
            overlay_text(draws[i].str, 1 << 20, text, sizeof text);
            cyc_overlay_text(argb, width, height, draws[i].x, draws[i].y, 1, draws[i].color, text);
        } else {
            fill_rect(argb, width, height, &draws[i]);
        }
    }
    if (toast) {
        overlay_text(title, width, title_shown, sizeof title_shown);
        overlay_text(body, width, body_shown, sizeof body_shown);
        cyc_overlay_toast(argb, width, height, title_shown, body_shown);
    }
    for (int i = 0; s_overlay_drawn && i < NV_WIDTH * NV_HEIGHT; i++) {
        s_overlay_mask[i] = argb[i] != before[i];
    }
}

void app_mods_keep_overlay(uint32_t *pic, const uint32_t *src, int width, int height)
{
    if (!s_overlay_drawn || width != NV_WIDTH || height != NV_HEIGHT) {
        return;
    }
    for (int i = 0; i < NV_WIDTH * NV_HEIGHT; i++) {
        if (s_overlay_mask[i]) {
            pic[i] = src[i];
        }
    }
}

void app_mods_video_apply(const NvVideo *video, const NvRenderOptions *options, const uint8_t *ram, const uint8_t *keep,
                          uint32_t *argb)
{
    if (!painting() || !video || !argb) {
        return;
    }
    static uint16_t index[NV_WIDTH * NV_HEIGHT];
    static NvSource source[NV_WIDTH * NV_HEIGHT];
    static const FmNesTile *cache[512 * 8];
    static uint8_t cached[512 * 8];
    uint64_t hashes[512];
    nv_render_source(video, options, index, source);
    for (int t = 0; t < 512; t++) {
        hashes[t] = fm_nes_tile_hash(&video->chr[t * 16]);
    }
    memset(cached, 0, sizeof cached);
    int land = LANDS;
    if (ram && ram[DT_ADDR_LAND] < LANDS && ram[DT_ADDR_LAST_MUSIC] != DT_MUSIC_LAND_SELECT &&
        ram[DT_ADDR_LAST_MUSIC] != DT_MUSIC_TITLE) {
        land = ram[DT_ADDR_LAND];
    }
    const uint32_t *pal = dt_mods_palette_for[land] ? dt_mods_palette_for[land] : dt_mods_palette_for[LANDS];
    s_painted_palette = pal;
    const uint32_t *base = nv_palette_argb();
    const bool tiles = fm_nes_tile_count() || fm_nes_dumping();
    for (int p = 0; p < NV_WIDTH * NV_HEIGHT; p++) {
        uint16_t i = index[p] & 0x1FF;
        if (argb[p] != base[i] || (keep && keep[p])) {
            continue;
        }
        uint32_t out = pal ? pal[i] : base[i];
        NvSource s = source[p];
        if (s != NV_SOURCE_NONE && tiles) {
            unsigned tile = NV_SOURCE_TILE(s), group = NV_SOURCE_PALETTE(s), slot = tile * 8 + group;
            if (!cached[slot]) {
                const uint8_t *e = &video->palette[group * 4];
                uint32_t palette = (uint32_t)(video->palette[0] & 0x3F) << 24 | (uint32_t)(e[1] & 0x3F) << 16 |
                                   (uint32_t)(e[2] & 0x3F) << 8 | (e[3] & 0x3F);
                const FmNesTile *art = fm_nes_tile_find(hashes[tile], palette);
                cache[slot] = art ? art : fm_nes_tile_find(hashes[tile], FM_NES_ANY_PALETTE);
                cached[slot] = 1;
                fm_nes_dump_tile(&video->chr[tile * 16], group >= 4, hashes[tile], palette, base);
            }
            const FmNesTile *art = cache[slot];
            if (art) {
                uint32_t c =
                    art->argb[(int)NV_SOURCE_ROW(s) * art->h / 8 * art->w + (int)NV_SOURCE_COLUMN(s) * art->w / 8];
                if ((c >> 24) >= 0x80) {
                    out = c | 0xFF000000u;
                }
            }
        }
        argb[p] = out;
    }
}
