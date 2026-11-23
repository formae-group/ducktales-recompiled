/*
 * native_check.c - the native renderer against the PPU (native/native_check.h).
 *
 *   input     native/native_cycle.h at the end of each frame (scanline 241,
 *             before the NMI), when the PPU memories are the ones the frame
 *             was drawn with; the mirroring is checked every frame.
 *   split     the line records hold what each line used at its first dot,
 *             so the HUD split's $2005 write, which lands during the line
 *             above the playfield, is not in them. Where that line differs,
 *             the column the write took effect at is searched for
 *             (NvSegment.split_column, fine X of the playfield's segment)
 *             and its distance from the native sprite-0 hit is recorded:
 *             the game polls $2002 in a 7-cycle loop and writes $2005 a
 *             fixed number of cycles after the read that sees the hit, so
 *             every frame must land in one 21-dot window. During a row
 *             transition $2006 moves the whole position mid-line; any one
 *             position from some column is searched for those.
 *   compare   the 9-bit indices with cyc_frame_index(), the ARGB colors with
 *             cyc_frame_argb(), and on drawn lines the background coverage
 *             with cyc_frame_bg_opaque() (the PPU leaves it stale on blank
 *             lines), every pixel: first with line-granular segments, then
 *             with the split column
 */
#include "native/native_check.h"
#include "native/native_cycle.h"
#include "native/native_render.h"

#include "cyc_core.h"
#include "cyc_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMP_FRAMES 8

static FILE *s_report;
static char s_dump_dir[512];
static int s_dumped;
static unsigned long s_frame;
static NvVideo s_video;
static uint16_t s_index[NV_WIDTH * NV_HEIGHT];
static uint32_t s_argb[NV_WIDTH * NV_HEIGHT];
static uint8_t s_opaque[NV_WIDTH * NV_HEIGHT];
static uint16_t s_scratch[NV_WIDTH * NV_HEIGHT];

static struct {
    unsigned long frames, index_frames, argb_frames, opaque_frames, mirror_frames, line_frames;
    unsigned long long index_pixels, argb_pixels, opaque_pixels, line_pixels;
    unsigned long split_fine_x, split_position, split_unexplained, hit_frames;
    int delta_lo_min, delta_lo_max, delta_hi_min, delta_hi_max, hit_to_split_min, hit_to_split_max;
    unsigned long segments[4];
    int max_segments;
} s_sum = { .delta_lo_min = 1 << 30, .delta_hi_min = 1 << 30, .delta_lo_max = -(1 << 30), .delta_hi_max = -(1 << 30),
            .hit_to_split_min = 1 << 30, .hit_to_split_max = -(1 << 30) };

static void write_ppm(const char *name, const uint32_t *argb)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s", s_dump_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", NV_WIDTH, NV_HEIGHT);
    for (int i = 0; i < NV_WIDTH * NV_HEIGHT; i++) {
        uint8_t rgb[3] = { (uint8_t)(argb[i] >> 16), (uint8_t)(argb[i] >> 8), (uint8_t)argb[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void dump(const uint32_t *machine, const uint16_t *ref)
{
    static uint32_t diff[NV_WIDTH * NV_HEIGHT];
    char name[64];
    for (int i = 0; i < NV_WIDTH * NV_HEIGHT; i++) diff[i] = s_index[i] != ref[i] ? 0xFFFF00FFu : (machine[i] >> 2) & 0x3F3F3Fu;
    snprintf(name, sizeof name, "frame%06lu_native.ppm", s_frame);
    write_ppm(name, s_argb);
    snprintf(name, sizeof name, "frame%06lu_machine.ppm", s_frame);
    write_ppm(name, machine);
    snprintf(name, sizeof name, "frame%06lu_diff.ppm", s_frame);
    write_ppm(name, diff);
}

static void summary(void)
{
    if (!s_report) return;
    fprintf(s_report,
            "summary frames=%lu index_mismatch_frames=%lu index_mismatch_pixels=%llu argb_mismatch_frames=%lu "
            "argb_mismatch_pixels=%llu bg_opaque_mismatch_frames=%lu bg_opaque_mismatch_pixels=%llu "
            "mirroring_mismatch_frames=%lu line_model_mismatch_frames=%lu line_model_mismatch_pixels=%llu "
            "split_fine_x=%lu split_position=%lu split_unexplained=%lu hit_frames=%lu hit_to_split_lines=%d..%d "
            "split_fine_x_column_minus_hit=lo %d..%d hi %d..%d "
            "segments_1=%lu segments_2=%lu segments_3=%lu segments_4plus=%lu max_segments=%d\n",
            s_sum.frames, s_sum.index_frames, s_sum.index_pixels, s_sum.argb_frames, s_sum.argb_pixels, s_sum.opaque_frames,
            s_sum.opaque_pixels, s_sum.mirror_frames, s_sum.line_frames, s_sum.line_pixels, s_sum.split_fine_x, s_sum.split_position,
            s_sum.split_unexplained, s_sum.hit_frames, s_sum.hit_to_split_min, s_sum.hit_to_split_max, s_sum.delta_lo_min,
            s_sum.delta_lo_max, s_sum.delta_hi_min, s_sum.delta_hi_max, s_sum.segments[0], s_sum.segments[1],
            s_sum.segments[2], s_sum.segments[3], s_sum.max_segments);
    fflush(s_report);
    if (s_report != stdout) fclose(s_report);
    s_report = NULL;
}

bool nv_check_option(const char *name, const char *value)
{
    if (!strcmp(name, "--native-render-check")) {
        s_report = !strcmp(value, "-") ? stdout : fopen(value, "w");
        if (!s_report) return false;
        atexit(summary);
        return true;
    }
    if (!strcmp(name, "--native-render-dump")) {
        snprintf(s_dump_dir, sizeof s_dump_dir, "%s", value);
        return true;
    }
    return false;
}

static unsigned long line_mismatches(const uint16_t *ours, const uint16_t *ref, int y)
{
    unsigned long n = 0;
    for (int x = 0; x < NV_WIDTH; x++) n += ours[y * NV_WIDTH + x] != ref[y * NV_WIDTH + x];
    return n;
}

static void min_max(int value, int *lo, int *hi)
{
    if (value < *lo) *lo = value;
    if (value > *hi) *hi = value;
}

/* The columns c at which line y of ours equals the machine's when columns
 * before c come from old and the rest from tail: *lo..*hi, false when none. */
static bool split_columns(const uint16_t *old, const uint16_t *tail, const uint16_t *ref, int y, int *lo, int *hi)
{
    const uint16_t *o = &old[y * NV_WIDTH], *t = &tail[y * NV_WIDTH], *r = &ref[y * NV_WIDTH];
    int d = NV_WIDTH, e = 0;
    while (d > 0 && t[d - 1] == r[d - 1]) d--;
    while (e < NV_WIDTH && o[e] == r[e]) e++;
    if (e < d) return false;
    *lo = d;
    *hi = e;
    return true;
}

/* The split line of segment k (the line above it): where its end shows the
 * playfield. First its fine X alone from some column (the $2005 write; this
 * column is the evidence of the write's timing), then any one background
 * position from some column (the row transition's $2006 write, Y the one the
 * next line steps down from). */
static void find_split(int k, const uint16_t *ref, bool hit, int hit_x, int hit_y)
{
    NvSegment *above = &s_video.segment[k - 1];
    const NvSegment *below = &s_video.segment[k];
    int line = below->first_line - 1, lo = 0, hi = 0, pass;
    bool found = false;
    uint16_t y_here = nv_advance_y(above->y, line - above->first_line), y_before = y_here;
    for (unsigned y = 0; y < 512; y++)
        if (nv_advance_y((uint16_t)y, 1) == below->y) {
            y_before = (uint16_t)y;
            break;
        }
    for (pass = 0; pass < 2 && !found; pass++) {
        for (unsigned p = 0; p < 512 && !found; p++) {
            if (pass == 0 && p != ((above->x & ~7u) + (below->x & 7u))) continue;
            above->split_x = (uint16_t)p;
            above->split_y = pass == 0 ? y_here : y_before;
            above->split_column = 0;
            nv_render_lines(&s_video, NULL, line, line, s_scratch, NULL);
            found = split_columns(s_index, s_scratch, ref, line, &lo, &hi);
        }
    }
    if (!found) {
        above->split_column = NV_NO_SPLIT;
        s_sum.split_unexplained++;
        return;
    }
    above->split_column = (uint16_t)lo;
    nv_render_lines(&s_video, NULL, line, line, s_index, s_opaque);
    if (pass == 2) {
        s_sum.split_position++;
        return;
    }
    s_sum.split_fine_x++;
    if (!hit) return;
    min_max((line - hit_y) * 341 + lo - hit_x, &s_sum.delta_lo_min, &s_sum.delta_lo_max);
    min_max((line - hit_y) * 341 + hi - hit_x, &s_sum.delta_hi_min, &s_sum.delta_hi_max);
}

void nv_check_frame_end(void)
{
    if (!s_report) return;
    unsigned long frame = s_frame;
    bool mirrored = nv_cycle_fill(&s_video);
    nv_render(&s_video, NULL, s_index, s_opaque);
    const uint16_t *ref = cyc_frame_index();
    const uint32_t *machine = cyc_frame_argb();
    const uint8_t *opaque = cyc_frame_bg_opaque();
    int hit_x = 0, hit_y = 0;
    bool hit = nv_sprite0_hit(&s_video, &hit_x, &hit_y);
    s_sum.hit_frames += hit;
    if (hit && s_video.segment_count > 1) min_max(s_video.segment[1].first_line - hit_y, &s_sum.hit_to_split_min, &s_sum.hit_to_split_max);
    unsigned long line_px = 0;
    for (int y = 0; y < NV_HEIGHT; y++) line_px += line_mismatches(s_index, ref, y);
    s_sum.line_frames += line_px != 0;
    s_sum.line_pixels += line_px;
    for (int k = 1; line_px && k < s_video.segment_count; k++) {
        int line = s_video.segment[k].first_line - 1;
        if (line >= 0 && line_mismatches(s_index, ref, line)) find_split(k, ref, hit, hit_x, hit_y);
    }
    nv_to_argb(s_index, s_argb, NV_WIDTH * NV_HEIGHT);
    unsigned long index_px = 0, argb_px = 0, opaque_px = 0;
    int first = -1, top = NV_HEIGHT, bottom = -1, opaque_top = NV_HEIGHT, opaque_bottom = -1;
    for (int i = 0; i < NV_WIDTH * NV_HEIGHT; i++) {
        if (s_index[i] != ref[i]) {
            index_px++;
            if (first < 0) first = i;
            if (i / NV_WIDTH < top) top = i / NV_WIDTH;
            bottom = i / NV_WIDTH;
        }
        argb_px += s_argb[i] != machine[i];
    }
    for (int y = 0; y < NV_HEIGHT; y++) {
        const NvSegment *s = &s_video.segment[0];
        for (int k = 1; k < s_video.segment_count && s_video.segment[k].first_line <= y; k++) s = &s_video.segment[k];
        if (!(s->mask & (NV_MASK_BG | NV_MASK_SPRITES))) continue;
        unsigned long before = opaque_px;
        for (int x = 0; x < NV_WIDTH; x++) opaque_px += s_opaque[y * NV_WIDTH + x] != (opaque[y * NV_WIDTH + x] != 0);
        if (opaque_px == before) continue;
        if (y < opaque_top) opaque_top = y;
        opaque_bottom = y;
    }
    s_sum.frames++;
    s_sum.index_frames += index_px != 0;
    s_sum.index_pixels += index_px;
    s_sum.argb_frames += argb_px != 0;
    s_sum.argb_pixels += argb_px;
    s_sum.opaque_frames += opaque_px != 0;
    s_sum.opaque_pixels += opaque_px;
    s_sum.mirror_frames += !mirrored;
    s_sum.segments[s_video.segment_count < 4 ? s_video.segment_count - 1 : 3]++;
    if (s_video.segment_count > s_sum.max_segments) s_sum.max_segments = s_video.segment_count;
    if (index_px || argb_px || opaque_px || !mirrored) {
        fprintf(s_report, "frame=%lu index_pixels=%lu argb_pixels=%lu bg_opaque_pixels=%lu mirrored=%d lines=%d-%d opaque_lines=%d-%d",
                frame, index_px, argb_px, opaque_px, mirrored ? 1 : 0, top, bottom, opaque_top, opaque_bottom);
        if (first >= 0)
            fprintf(s_report, " first=%d,%d native=%03X machine=%03X", first % NV_WIDTH, first / NV_WIDTH, s_index[first],
                    ref[first]);
        if (hit) fprintf(s_report, " hit=%d,%d", hit_x, hit_y);
        fprintf(s_report, " segments=%d", s_video.segment_count);
        for (int i = 0; i < s_video.segment_count; i++) {
            const NvSegment *s = &s_video.segment[i];
            fprintf(s_report, " [%d x=%d y=%03X c=%02X m=%02X split=%d]", s->first_line, s->x, s->y, s->ctrl, s->mask,
                    s->split_column);
        }
        fputc('\n', s_report);
        if (s_dump_dir[0] && index_px && s_dumped < DUMP_FRAMES) {
            dump(machine, ref);
            s_dumped++;
        }
    }
    s_frame++;
}
