/*
 * native_cycle.c - NvVideo from the cycle host (native/native_cycle.h).
 */
#include "native/native_cycle.h"

#include "cyc_core.h"
#include "cyc_render.h"

#include <string.h>

static void segment_of_line(const CycLine *l, int y, NvSegment *s)
{
    unsigned x6 = ((l->v >> 5) & 0x20u) | (l->v & 0x1Fu);
    s->first_line = (uint8_t)y;
    s->x = (uint16_t)(((x6 - 2u) & 63u) << 3 | l->fine_x);
    s->y = (uint16_t)(((l->v >> 11) & 1u) << 8 | ((l->v >> 5) & 31u) << 3 | ((l->v >> 12) & 7u));
    s->ctrl = (uint8_t)(l->ctrl & (NV_CTRL_SPRITE_1000 | NV_CTRL_BG_1000 | NV_CTRL_SPRITE_8X16));
    s->mask = l->mask;
    s->split_column = NV_NO_SPLIT;
    s->split_x = 0;
    s->split_y = 0;
}

static bool continues(const NvSegment *last, const NvSegment *line)
{
    if (last->ctrl != line->ctrl || last->mask != line->mask) {
        return false;
    }
    if (!(line->mask & (NV_MASK_BG | NV_MASK_SPRITES))) {
        return true;
    }
    return last->x == line->x && nv_advance_y(last->y, line->first_line - last->first_line) == line->y;
}

bool nv_cycle_fill(NvVideo *v)
{
    for (unsigned a = 0; a < 0x2000; a++) {
        v->chr[a] = cyc_render_chr((uint16_t)a);
    }
    bool mirrored = true;
    for (unsigned i = 0; i < 0x400; i++) {
        v->ciram[0][i] = cyc_render_nametable((uint16_t)(0x2000 + i));
        v->ciram[1][i] = cyc_render_nametable((uint16_t)(0x2400 + i));
        mirrored &= cyc_render_nametable((uint16_t)(0x2800 + i)) == v->ciram[0][i] &&
                    cyc_render_nametable((uint16_t)(0x2C00 + i)) == v->ciram[1][i];
    }
    v->mirroring = NV_MIRROR_VERTICAL;
    for (int i = 0; i < 32; i++) {
        v->palette[i] = cyc_render_palette(i);
    }
    memcpy(v->oam, cyc_render_oam(), sizeof v->oam);
    const CycLine *lines = cyc_frame_lines();
    v->segment_count = 0;
    for (int y = 0; y < NV_HEIGHT; y++) {
        NvSegment s;
        segment_of_line(&lines[y], y, &s);
        if (v->segment_count && continues(&v->segment[v->segment_count - 1], &s)) {
            continue;
        }
        v->segment[v->segment_count++] = s;
    }
    return mirrored;
}
