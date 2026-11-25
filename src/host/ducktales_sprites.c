/*
 * ducktales_sprites.c - the presented picture with every sprite the game
 * wants, in a fixed order, and none of the console's per-line or OAM limits
 * (cyc_render.h stock-width compositing; docs/RENDERING.md "Sprites without
 * limits"). The machine still draws its own picture with all of them, so
 * the hashes, sprite-0 hit and overflow stay the console's.
 *
 *   the list    The builder (bank 5 $8000) runs during the frame before the
 *               one that shows its table, so the RAM saved at that frame's
 *               end is the state the shown OAM was built from. The builder's
 *               rules are replayed on it slot by slot (the order of $95), and
 *               each slot's sprites must equal the next OAM entries, or the
 *               slot is left out (it was spawned after the builder ran).
 *               Entries after the last slot that are not parked are kept as
 *               the console shows them, behind everything (the builder does
 *               not always park what a shorter table left behind). While the
 *               HUD is in the OAM its entries are taken from there (a hit can
 *               change the records after the builder ran). An OAM identical
 *               to the last one rebuilt (the builder did not run: deaths,
 *               respawns) keeps that list. Until two frames were saved (power
 *               on, a loaded state) the OAM is drawn as it is (fallback).
 *   the order   OAM entry 0 (the HUD split), Scrooge (slot 0), the HUD
 *               groups, then slots 1 to 15: the builder's even-frame order,
 *               except that Scrooge goes before the HUD. The game draws the
 *               HUD only while he is below the band, so on the console they
 *               never meet; here he covers the lives counter as he covers
 *               the HUD frame, and the HUD stays in front of every enemy.
 *   the HUD     Drawn whenever the builder would draw it apart from Scrooge's
 *               Y: the game leaves it out while he is in the top band.
 *   drawing     Front to back over the exact background (cyc_render
 *               capture): the first opaque sprite pixel decides, and a
 *               behind-background one shows the background where it is
 *               opaque, as on the PPU. Pattern table, 8x16, mask and the
 *               left-column clip are each line's own; the HUD's sprites are
 *               fetched from $1000 as the top of the frame does when the HUD
 *               shows.
 *   mods        the enabled mods' tiles and palettes (mods/app_mods.h) are
 *               painted over the composed picture, drawn again natively from
 *               the same list; with the sprites off, or before a picture can
 *               be composed, over the machine's own picture
 *   replaced    a slot whose kind ($0710) and metasprite id have a sprite
 *               replacement (app_mods_sprite) is drawn as that one picture
 *               in place of its sprites, at its place in the order: anchored
 *               at the slot's X and Y + 1, mirrored about the frame's own
 *               flip axis (piece 0 plain and flipped) when facing the other
 *               way without a _flip file, behind the background with $0600
 *               bit 2, gone while the slot blinks out, cut above the HUD band
 *               as its sprites are and at the picture's edges and left
 *               column, pixel by pixel. It dims with the game's fades: a
 *               quarter for each $10 the fade takes off the sprite colours
 *               (the palette shown against the master palette at $0100,
 *               docs/RENDERING.md "Fades"), black from $40 on. Its pixels
 *               are kept from the tile and palette
 *               painting. With the sprites off the list is the OAM in its
 *               order, 8 sprites a line, the replaced slots' entries swapped
 *               for their pictures; the machine's own picture is shown as
 *               long as no replaced slot is on screen
 *   dump        --mods-dump: every slot frame in the list is drawn unflipped
 *               with the lines' pattern table and palettes, once
 */
#include "host/ducktales_sprites.h"

#include "game/ducktales_symbols.h"
#include "mods/app_mods.h"
#include "native/native_cycle.h"

#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE     0x800
#define SLOT_SPRITES 64
#define HUD_RECORDS  16
#define LIST_MAX     (64 + 3 * HUD_RECORDS + DT_OBJ_SLOTS * SLOT_SPRITES)

/* An OAM sprite, or with art a sprite replacement: art column c of row r
 * at (left + step * c, top + r), rows above clip cut, attr bit 5 behind;
 * pieces are the sprites it replaces, counted for the line limit. */
typedef struct Sprite {
    uint8_t y, tile, attr, x;
    bool hud;
    const AppModsSprite *art;
    int left, top, clip, step, piece_n;
    const struct Sprite *pieces;
} Sprite;

/* What a slot shows: its kind, metasprite id, position and look. */
typedef struct {
    uint8_t kind, id, x, y;
    bool flip, behind, band;
} Frame;

/* The builder replayed on a RAM image and matched with the OAM. */
typedef struct {
    Sprite slot[DT_OBJ_SLOTS][SLOT_SPRITES];
    Frame frame[DT_OBJ_SLOTS];
    int count[DT_OBJ_SLOTS], start[DT_OBJ_SLOTS], shown[DT_OBJ_SLOTS], end, hud_n;
    Sprite hud[3 * HUD_RECORDS];
} Replay;

static bool s_on = DT_SPRITES_DEFAULT;
static bool s_installed;
static uint8_t s_ram[2][RAM_SIZE]; /* end of the previous frame, end of this one */
static int s_seen;                 /* frames saved since the machine was last replaced */
static uint8_t *s_prg;
static size_t s_prg_size;
static DtSpritesStats s_stats;
static FILE *s_log;
static uint8_t s_last_oam[256];
static bool s_last_valid;
static uint32_t s_background[256 * 240];
static uint8_t s_keep[256 * 240];
static int s_fade; /* $10 steps the shown sprite colours are below the master palette, 0-4 */

/* ---- the builder's rules on a saved RAM image ---- */

static bool load_prg(void)
{
    if (s_prg) {
        return true;
    }
    size_t size = cyc_mod_copy_cart_payload(NULL, 0);
    if (size < (DT_SPR_BANK + 1) * 0x4000u) {
        return false;
    }
    s_prg = malloc(size);
    if (!s_prg) {
        return false;
    }
    if (cyc_mod_copy_cart_payload(s_prg, size) != size) {
        free(s_prg);
        s_prg = NULL;
        return false;
    }
    s_prg_size = size;
    return true;
}

static uint8_t rd(uint16_t addr)
{
    size_t at = (size_t)DT_SPR_BANK * 0x4000u + (size_t)(addr - 0x8000u);
    return addr >= 0x8000 && addr < 0xC000 && at < s_prg_size ? s_prg[at] : 0;
}

static uint16_t rd16(uint16_t lo, uint16_t hi, uint8_t i)
{
    return (uint16_t)(rd((uint16_t)(lo + i)) | rd((uint16_t)(hi + i)) << 8);
}

static bool hud_allowed(const uint8_t *ram)
{
    return (ram[DT_ADDR_HUD_GATE] & 0x80) && !ram[DT_ADDR_MESSAGE] && !ram[DT_ADDR_ENDING] && !ram[DT_ADDR_PAUSE] &&
           ram[DT_ADDR_MODE] != DT_MODE_FROZEN;
}

static bool hud_in_oam(const uint8_t *ram)
{
    return hud_allowed(ram) && ram[DT_ROW_Y + DT_SLOT_SCROOGE] >= DT_HUD_MIN_SCROOGE_Y;
}

static int hud_records(const uint8_t *ram, Sprite *out)
{
    static const uint16_t GROUP[3] = { DT_ADDR_HUD_GROUP_HP, DT_ADDR_HUD_GROUP_LIVES, DT_ADDR_HUD_GROUP_TIME };
    int n = 0;
    for (int g = 0; g < 3; g++) {
        for (int i = 0; i < HUD_RECORDS && ram[GROUP[g] + 4 * i] != DT_HUD_RECORD_END_Y; i++) {
            const uint8_t *r = &ram[GROUP[g] + 4 * i];
            out[n++] = (Sprite){ r[0], r[1], r[2], r[3], true };
        }
    }
    return n;
}

/* One slot's sprites as bank 5 $8000 writes them, -1 when it draws nothing. */
static int slot_sprites(const uint8_t *ram, int n, Sprite *out, Frame *frame)
{
    uint8_t status = ram[DT_ROW_STATUS + n], flags = ram[DT_ROW_FLAGS + n];
    if (!(status & 0x80) || flags == 0xFF) {
        return -1;
    }
    uint8_t x = ram[DT_ROW_POS_X + n];
    if (status & 0x20) {
        int borrow = ram[DT_ROW_POS_X + n] < ram[DT_ADDR_CAMERA_X];
        x = (uint8_t)(ram[DT_ROW_POS_X + n] - ram[DT_ADDR_CAMERA_X]);
        if ((uint8_t)(ram[DT_ROW_POS_PAGE + n] - ram[DT_ADDR_CAMERA_PAGE] - borrow)) {
            return -1;
        }
        if (status & 0x10) {
            return -1;
        }
    }
    if ((flags & 0x02) && !(ram[DT_ADDR_SPR_PHASE] & 0x02)) {
        return -1;
    }
    bool flip = (flags & 0x40) != 0;
    uint16_t script = rd16(DT_SPR_SCRIPT_LO, DT_SPR_SCRIPT_HI, ram[DT_ROW_ACTION + n]);
    uint8_t id = rd((uint16_t)(script + 2 + (ram[DT_ROW_FRAME + n] & 0x7F)));
    uint16_t meta = rd16(DT_SPR_META_LO, DT_SPR_META_HI, id);
    int count = rd(meta) + 1;
    uint8_t table = (uint8_t)(rd((uint16_t)(meta + 1)) + (flip ? 1 : 0));
    uint16_t offsets = (uint16_t)(rd16(DT_SPR_OFFSET_LO, DT_SPR_OFFSET_HI, table) - 2);
    int y0 = ram[DT_ROW_Y + n], made = 0;
    *frame = (Frame){
        ram[DT_ROW_KIND + n], id, x, (uint8_t)y0, flip, (flags & 0x04) != 0, n != DT_SLOT_SCROOGE && !(flags & 0x10)
    };
    for (int k = 0; k < count && made < SLOT_SPRITES; k++) {
        int at = 2 + 2 * k;
        uint8_t dy = rd((uint16_t)(offsets + at)), dx = rd((uint16_t)(offsets + at + 1));
        int y = y0 + dy, xx = x + dx;
        if ((dy < 0x80 && y > 0xFF) || (dy >= 0x80 && y <= 0xFF)) {
            continue;
        }
        y &= 0xFF;
        if (n != DT_SLOT_SCROOGE && !(flags & 0x10) && y < DT_HUD_BAND_Y) {
            continue;
        }
        if ((dx < 0x80 && xx > 0xFF) || (dx >= 0x80 && xx <= 0xFF)) {
            continue;
        }
        uint8_t attr = (uint8_t)((rd((uint16_t)(meta + at + 1)) ^ (flip ? 0x40 : 0)) | ((flags & 0x04) << 3));
        out[made++] = (Sprite){ (uint8_t)y, rd((uint16_t)(meta + at)), attr, (uint8_t)xx, false };
    }
    return made;
}

static bool same(const Sprite *s, const uint8_t *e)
{
    return s->y == e[0] && s->tile == e[1] && s->attr == e[2] && s->x == e[3];
}

/* The slots replayed on the RAM image the frame's OAM was built from: a slot
 * whose sprites are not the next OAM entries gets count -1. */
static void replay(const uint8_t *ram, const uint8_t *oam, Replay *r)
{
    int pos = 1;
    r->hud_n = hud_allowed(ram) ? hud_records(ram, r->hud) : 0;
    if (hud_in_oam(ram)) {
        for (int i = 0; i < r->hud_n && pos < 64; i++, pos++) {
            r->hud[i] = (Sprite){ oam[pos * 4], oam[pos * 4 + 1], oam[pos * 4 + 2], oam[pos * 4 + 3], true };
        }
    }
    bool odd = ram[DT_ADDR_SPR_PHASE] & 1;
    for (int i = 0; i < DT_OBJ_SLOTS; i++) {
        int n = odd ? DT_OBJ_SLOTS - 1 - i : i;
        r->count[n] = slot_sprites(ram, n, r->slot[n], &r->frame[n]);
        r->start[n] = pos;
        r->shown[n] = 0;
        if (r->count[n] <= 0 || pos >= 64) {
            continue;
        }
        int shown = r->count[n] < 64 - pos ? r->count[n] : 64 - pos, k = 0;
        while (k < shown && same(&r->slot[n][k], &oam[(pos + k) * 4])) {
            k++;
        }
        if (k < shown) {
            r->count[n] = -1;
        } else {
            r->shown[n] = shown;
            pos += shown;
        }
    }
    r->end = pos;
}

/* What the plain and flipped X of a frame's sprites add up to, plus 8: its
 * first sprite's. A plain column u is mirrored to axis - 1 - u. */
static int flip_axis(uint8_t id)
{
    uint16_t meta = rd16(DT_SPR_META_LO, DT_SPR_META_HI, id);
    uint8_t table = rd((uint16_t)(meta + 1));
    uint16_t plain = (uint16_t)(rd16(DT_SPR_OFFSET_LO, DT_SPR_OFFSET_HI, table) - 2);
    uint16_t flipped = (uint16_t)(rd16(DT_SPR_OFFSET_LO, DT_SPR_OFFSET_HI, (uint8_t)(table + 1)) - 2);
    return (int8_t)rd((uint16_t)(plain + 3)) + (int8_t)rd((uint16_t)(flipped + 3)) + 8;
}

/* The two pattern bytes of row r of a sprite h lines tall, flipped
 * vertically with attr bit 7. */
static void sprite_row(uint8_t tile, uint8_t attr, int r, int h, uint16_t table, uint8_t *lo, uint8_t *hi)
{
    int row = (attr & 0x80) ? h - 1 - r : r;
    uint16_t pattern = h == 16 ? (uint16_t)(((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row >= 8 ? 16 : 0))
                               : (uint16_t)(table + tile * 16);
    *lo = cyc_render_chr((uint16_t)(pattern + (row & 7)));
    *hi = cyc_render_chr((uint16_t)(pattern + (row & 7) + 8));
}

/* --mods-dump: the frame unflipped, its sprites front first, with the
 * pattern table, size and palettes of the line below the slot's Y. */
static void dump_frame(const Frame *f)
{
    static uint32_t px[272 * 272];
    uint16_t meta = rd16(DT_SPR_META_LO, DT_SPR_META_HI, f->id);
    int count = rd(meta) + 1, line = f->y + 1 > 239 ? 239 : f->y + 1;
    uint16_t offsets = (uint16_t)(rd16(DT_SPR_OFFSET_LO, DT_SPR_OFFSET_HI, rd((uint16_t)(meta + 1))) - 2);
    int h = cyc_render_line_sprite16(line) ? 16 : 8, x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    uint16_t table = cyc_render_line_sprite_table(line);
    count = count < SLOT_SPRITES ? count : SLOT_SPRITES;
    for (int k = 0; k < count; k++) {
        int dy = (int8_t)rd((uint16_t)(offsets + 2 + 2 * k)), dx = (int8_t)rd((uint16_t)(offsets + 3 + 2 * k));
        x0 = dx < x0 ? dx : x0;
        y0 = dy < y0 ? dy : y0;
        x1 = dx + 8 > x1 ? dx + 8 : x1;
        y1 = dy + h > y1 ? dy + h : y1;
    }
    int w = x1 - x0, height = y1 - y0;
    memset(px, 0, (size_t)w * (size_t)height * sizeof *px);
    for (int k = 0; k < count; k++) {
        int dy = (int8_t)rd((uint16_t)(offsets + 2 + 2 * k)) - y0,
            dx = (int8_t)rd((uint16_t)(offsets + 3 + 2 * k)) - x0;
        uint8_t tile = rd((uint16_t)(meta + 2 + 2 * k)), attr = rd((uint16_t)(meta + 3 + 2 * k)), lo, hi;
        for (int r = 0; r < h; r++) {
            sprite_row(tile, attr, r, h, table, &lo, &hi);
            for (int c = 0; c < 8; c++) {
                int bit = (attr & 0x40) ? c : 7 - c, v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                uint32_t *p = &px[(dy + r) * w + dx + c];
                if (v && !*p) {
                    *p = cyc_render_color(16 + (attr & 3) * 4 + v) | 0xFF000000u;
                }
            }
        }
    }
    app_mods_sprite_dump(f->kind, f->id, px, w, height, -x0, -y0);
}

/* Slot s of r into the list: its sprite replacement, or its first count
 * sprites. */
static int add_slot(const Replay *r, int s, int count, Sprite *list, int n, int *replaced)
{
    const Frame *f = &r->frame[s];
    if (r->count[s] <= 0) {
        return n;
    }
    if (app_mods_sprite_wanted(f->kind, f->id)) {
        dump_frame(f);
    }
    bool mirror;
    const AppModsSprite *art = app_mods_sprite(f->kind, f->id, f->flip, &mirror);
    if (art) {
        list[n] = (Sprite){ 0xFF, 0, f->behind ? 0x20 : 0, 0, false, art };
        list[n].left = mirror ? f->x + flip_axis(f->id) - 1 + art->ax : f->x - art->ax;
        list[n].step = mirror ? -1 : 1;
        list[n].top = f->y + 1 - art->ay;
        list[n].clip = f->band ? DT_HUD_BAND_Y + 1 : 0;
        list[n].pieces = r->slot[s];
        list[n].piece_n = count;
        ++*replaced;
        return n + 1;
    }
    for (int k = 0; k < count; k++) {
        list[n++] = r->slot[s][k];
    }
    return n;
}

/* The list in drawing order (front first) from the RAM image the frame's OAM
 * was built from. */
static int rebuild(const uint8_t *ram, const uint8_t *oam, Sprite *list, bool *hud_restored, int *leftover)
{
    static Replay r;
    int n = 0, replaced = 0;
    replay(ram, oam, &r);
    list[n++] = (Sprite){ oam[0], oam[1], oam[2], oam[3], false };
    n = add_slot(&r, DT_SLOT_SCROOGE, r.count[DT_SLOT_SCROOGE], list, n, &replaced);
    for (int i = 0; i < r.hud_n; i++) {
        list[n++] = r.hud[i];
    }
    *hud_restored = r.hud_n > 0 && !hud_in_oam(ram);
    for (int s = 0; s < DT_OBJ_SLOTS; s++) {
        if (s != DT_SLOT_SCROOGE) {
            n = add_slot(&r, s, r.count[s], list, n, &replaced);
        }
    }
    *leftover = 0;
    for (int e = r.end; e < 64; e++) {
        if (oam[e * 4] >= 0xF0) {
            continue;
        }
        list[n++] = (Sprite){ oam[e * 4], oam[e * 4 + 1], oam[e * 4 + 2], oam[e * 4 + 3], false };
        ++*leftover;
    }
    return n;
}

/* The OAM in its order with each replaced slot's entries as its picture,
 * where its first entry was; *replaced counts the pictures. */
static int console_list(const uint8_t *ram, const uint8_t *oam, Sprite *list, int *replaced)
{
    static Replay r;
    int n = 0;
    replay(ram, oam, &r);
    *replaced = 0;
    for (int e = 0; e < 64; e++) {
        int s = 0;
        while (s < DT_OBJ_SLOTS && !(r.shown[s] && e == r.start[s])) {
            s++;
        }
        if (s == DT_OBJ_SLOTS) {
            list[n++] = (Sprite){ oam[e * 4], oam[e * 4 + 1], oam[e * 4 + 2], oam[e * 4 + 3], false };
            continue;
        }
        n = add_slot(&r, s, r.shown[s], list, n, replaced);
        e += r.shown[s] - 1;
    }
    return n;
}

/* ---- drawing ---- */

/* The fade of the sprite colours shown: the most $10 steps an entry is below
 * its master colour of the same hue, one more than the master's when it is
 * clamped to $0F. */
static int sprite_fade(const uint8_t *ram)
{
    int fade = 0;
    for (int e = 17; e < 32; e++) {
        uint8_t master = ram[DT_ADDR_PALETTE + e] & 0x3F, shown = cyc_render_palette(e);
        int steps = shown == 0x0F && master != 0x0F                       ? (master >> 4) + 1
                    : (shown & 0x0F) == (master & 0x0F) && shown < master ? (master >> 4) - (shown >> 4)
                                                                          : 0;
        fade = steps > fade ? steps : fade;
    }
    return fade > 4 ? 4 : fade;
}

/* limit: sprites a line shows (8 on the console), 0 for no limit; a sprite
 * replacement counts as its sprites on the line, and the columns only its
 * sprites past the limit cover are left out of it there. s_keep marks the
 * pixels the replacements drew. */
static int draw(uint32_t *out, const Sprite *list, int n, uint16_t hud_table, int limit)
{
    int crowded = 0;
    const uint8_t *opaque = cyc_frame_bg_opaque();
    memset(s_keep, 0, sizeof s_keep);
    s_fade = sprite_fade(s_ram[1]);
    for (int y = 0; y < 240; y++) {
        uint32_t *line = &out[y * 256];
        const uint32_t *bg = &s_background[y * 256];
        memcpy(line, bg, 256 * sizeof *line);
        uint8_t mask = cyc_render_line_mask(y);
        if (!(mask & 0x10)) {
            continue;
        }
        bool tall = cyc_render_line_sprite16(y);
        int h = tall ? 16 : 8, on_line = 0;
        uint16_t table = cyc_render_line_sprite_table(y);
        uint32_t color[256];
        uint8_t taken[256] = { 0 }, behind[256], art[256] = { 0 };
        for (int i = 0; i < n; i++) {
            const Sprite *s = &list[i];
            if (s->art) {
                int r = y - s->top;
                uint8_t cut[256] = { 0 };
                for (int k = 0; k < s->piece_n; k++) {
                    const Sprite *p = &s->pieces[k];
                    int pr = y - (p->y + 1);
                    if (p->y >= 0xEF || pr < 0 || pr >= h) {
                        continue;
                    }
                    uint8_t mark = ++on_line > limit && limit ? 1 : 2;
                    for (int c = 0; c < 8 && p->x + c < 256; c++) {
                        cut[p->x + c] = cut[p->x + c] > mark ? cut[p->x + c] : mark;
                    }
                }
                if (r < 0 || r >= s->art->h || y < s->clip) {
                    continue;
                }
                const uint32_t *row = &s->art->argb[r * s->art->w];
                for (int c = 0; c < s->art->w; c++) {
                    int px = s->left + s->step * c;
                    if (px < 0 || px > 255 || taken[px] || cut[px] == 1 || (px < 8 && !(mask & 0x04)) ||
                        (row[c] >> 24) < 0x80) {
                        continue;
                    }
                    uint32_t rgb = row[c], keep = (uint32_t)(4 - s_fade);
                    taken[px] = art[px] = 1;
                    behind[px] = (s->attr & 0x20) != 0;
                    color[px] = 0xFF000000u | ((rgb >> 16 & 0xFF) * keep / 4) << 16 |
                                ((rgb >> 8 & 0xFF) * keep / 4) << 8 | (rgb & 0xFF) * keep / 4;
                }
                continue;
            }
            int r = y - (s->y + 1);
            if (s->y >= 0xEF || r < 0 || r >= h) {
                continue;
            }
            if (++on_line > limit && limit) {
                continue;
            }
            uint8_t lo, hi;
            sprite_row(s->tile, s->attr, r, h, s->hud ? hud_table : table, &lo, &hi);
            for (int c = 0; c < 8; c++) {
                int px = s->x + c;
                if (px > 255) {
                    break;
                }
                if (taken[px] || (px < 8 && !(mask & 0x04))) {
                    continue;
                }
                int bit = (s->attr & 0x40) ? c : 7 - c;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!v) {
                    continue;
                }
                taken[px] = 1;
                behind[px] = (s->attr & 0x20) != 0;
                color[px] = cyc_render_color(16 + (s->attr & 3) * 4 + v);
            }
        }
        crowded += on_line > 8;
        for (int x = 0; x < 256; x++) {
            if (taken[x] && !(behind[x] && opaque[y * 256 + x])) {
                line[x] = color[x];
                s_keep[y * 256 + x] = art[x];
            }
        }
    }
    return crowded;
}

/* The mods' tiles and palettes over the picture, drawn from list (NULL: the
 * OAM) with limit sprites a line; keep: the pixels sprite replacements drew. */
static void paint_mods(uint32_t *out, const Sprite *list, int n, uint16_t hud_table, int limit, const uint8_t *keep)
{
    static NvVideo video;
    static NvSprite sprites[LIST_MAX];
    if (!nv_cycle_fill(&video)) {
        return;
    }
    NvRenderOptions options = { 8, NULL, 0 };
    if (list) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            if (!list[i].art) {
                sprites[m++] = (NvSprite){ list[i].y, list[i].tile, list[i].attr, list[i].x,
                                           list[i].hud ? hud_table : NV_TABLE_LINE };
            }
        }
        options = (NvRenderOptions){ limit, sprites, m };
    }
    app_mods_video_apply(&video, &options, s_ram[1], keep, out);
    app_mods_draw_overlay(out, 256, 240);
}

/* With the sprites off: the OAM's own order and limit, drawn by draw only
 * while a replaced slot is in it (else the machine's picture is exact). */
static bool compose_console(uint32_t *out, int width, int height)
{
    static Sprite list[LIST_MAX];
    int replaced = 0;
    if (!app_mods_sprites_active() || width != 256 || height != 240 || s_seen < 2 || !load_prg() ||
        !cyc_render_background(s_background)) {
        return false;
    }
    int n = console_list(s_ram[0], cyc_render_oam(), list, &replaced);
    if (!replaced) {
        return false;
    }
    draw(out, list, n, 0, 8);
    paint_mods(out, list, n, 0, 8, s_keep);
    return true;
}

static int compose(uint32_t *out, int width, int height, int native_x0, const uint32_t *native, void *user)
{
    (void)native_x0;
    (void)user;
    if (!s_on || width != 256 || height != 240 || !load_prg() || !cyc_render_background(s_background)) {
        s_stats.declined++;
        if (s_log) {
            fprintf(s_log, "frame=%lu declined\n", (unsigned long)(cyc_ring_frame - 1));
        }
        if (!app_mods_video_active() || width != 256 || height != 240 || !native) {
            return 0;
        }
        if (!compose_console(out, width, height)) {
            memcpy(out, native, 256 * 240 * sizeof *out);
            paint_mods(out, NULL, 0, 0, 8, NULL);
        }
        return 1;
    }
    static Sprite list[LIST_MAX];
    static int n, leftover;
    static bool hud_restored;
    static uint16_t hud_table;
    const uint8_t *oam = cyc_render_oam(), *ram = s_ram[0];
    const char *how = "reused";
    if (s_last_valid && !memcmp(oam, s_last_oam, sizeof s_last_oam)) {
        s_stats.reused++;
    } else if (s_seen >= 2) {
        n = rebuild(ram, oam, list, &hud_restored, &leftover);
        hud_table = ram[DT_ADDR_LAND] == DT_LAND_HUD_TABLE_OFF ? 0 : 0x1000;
        memcpy(s_last_oam, oam, sizeof s_last_oam);
        s_last_valid = true;
        s_stats.composed++;
        how = "rebuilt";
    } else {
        n = leftover = 0;
        hud_restored = false;
        hud_table = 0x1000;
        for (int e = 0; e < 64; e++) {
            list[n++] = (Sprite){ oam[e * 4], oam[e * 4 + 1], oam[e * 4 + 2], oam[e * 4 + 3], false };
        }
        s_stats.fallback++;
        how = "fallback";
    }
    if (hud_restored) {
        s_stats.hud_restored++;
    }
    if (leftover) {
        s_stats.leftover++;
    }
    int visible = 0;
    for (int i = 1; i < n; i++) {
        visible += list[i].y < 0xEF;
    }
    if (visible > 63) {
        s_stats.beyond_63 += (uint64_t)(visible - 63);
    }
    int crowded = draw(out, list, n, hud_table, 0);
    if (app_mods_video_active()) {
        paint_mods(out, list, n, hud_table, 0, s_keep);
    }
    s_stats.lines_over_8 += (uint64_t)crowded;
    if (s_log) {
        fprintf(s_log, "frame=%lu %s sprites=%d lines_over_8=%d hud_restored=%d leftover=%d\n",
                (unsigned long)(cyc_ring_frame - 1), how, visible, crowded, hud_restored ? 1 : 0, leftover);
    }
    return 1;
}

/* ---- the setting and the host ---- */

static void apply(void)
{
    if (!s_installed) {
        cyc_render_set_compositor(compose, NULL);
        s_installed = true;
    }
    cyc_render_capture_background(s_on || app_mods_sprites_active());
    cyc_render_set_stock_compositing(s_on || app_mods_video_active());
}

void dt_sprites_mods_changed(void)
{
    if (!s_installed) {
        apply();
    }
    cyc_render_capture_background(s_on || app_mods_sprites_active());
    cyc_render_set_stock_compositing(s_on || app_mods_video_active());
}

void dt_sprites_set(bool on)
{
    if (s_installed && on == s_on) {
        return;
    }
    s_on = on;
    apply();
}

bool dt_sprites_get(void)
{
    return s_on;
}

bool dt_sprites_parse(const char *text, bool *out)
{
    static const char *ON[] = { "1", "on", "true", "yes" }, *OFF[] = { "0", "off", "false", "no" };
    if (!text) {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    char word[8];
    size_t n = 0;
    while (text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '\r' && text[n] != '\n') {
        if (n + 1 >= sizeof word) {
            return false;
        }
        word[n] = (char)(text[n] >= 'A' && text[n] <= 'Z' ? text[n] + 32 : text[n]);
        n++;
    }
    word[n] = 0;
    for (const char *rest = text + n; *rest; rest++) {
        if (*rest != ' ' && *rest != '\t' && *rest != '\r' && *rest != '\n') {
            return false;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!strcmp(word, ON[i])) {
            *out = true;
            return true;
        }
        if (!strcmp(word, OFF[i])) {
            *out = false;
            return true;
        }
    }
    return false;
}

void dt_sprites_frame_end(void)
{
    if (!s_installed) {
        apply();
    }
    memcpy(s_ram[0], s_ram[1], RAM_SIZE);
    memcpy(s_ram[1], cyc_cpu_ram(), RAM_SIZE);
    if (s_seen < 2) {
        s_seen++;
    }
}

void dt_sprites_forget(void)
{
    if (!s_installed) {
        apply();
    }
    s_seen = 0;
    s_last_valid = false;
}

void dt_sprites_stats(DtSpritesStats *out)
{
    *out = s_stats;
}

void dt_sprites_set_log(FILE *f)
{
    s_log = f;
}
