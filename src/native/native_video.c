/*
 * native_video.c - the PPU side of the native runtime: the registers and
 * memories the program writes ($2000-$2007, the sprite DMA), kept as the
 * console keeps them (native_state.h), and the frame's timing derived from
 * the CPU cycle count. Nothing here draws a pixel.
 *
 *   frame timing      a frame is 262 lines of 341 dots; VBlank starts at line
 *                     241 dot 1, the flags clear at line 261 dot 1, and odd
 *                     frames with rendering on skip line 0 dot 0. Each of
 *                     these is an event at a known dot; video_advance() runs
 *                     the events whose dot has passed
 *   $2002             VBlank as the read starts, sprite-0 hit as it ends;
 *                     reading clears VBlank on the next dot, so a read on the
 *                     dot VBlank is set (or the dot before) loses that NMI,
 *                     as on the console
 *   sprite-0 hit      worked out once per frame at line 0 from OAM entry 0,
 *                     CHR RAM, the nametable and the scroll the program set:
 *                     the first pixel where sprite 0 and the background are
 *                     both opaque, and the dot that pixel is drawn on
 *   frame record      NativeFrame: the memories at line 0, the writes made
 *                     while lines 0-239 were drawn (and the $2002 reads that
 *                     reset the write latch then), the split, the APU writes
 *   held VBlank       with NATIVE_LAG_NONE the bus can hold the VBlank event
 *                     back (native_vblank_deferred): the frame's events move
 *                     later and the PPU stays in post-render line 240
 *   save states       video_state_save / video_state_load (between frames)
 */
#include "native/native_runtime.h"
#include "native/native_state.h"

#include "hw.h"

#include <string.h>

#define LINE_DOTS      341u
#define EVENT_VBLANK   (241u * LINE_DOTS + 1u)
#define EVENT_ODD      (260u * LINE_DOTS + 340u)
#define EVENT_PRERENDER (261u * LINE_DOTS + 1u)
#define EVENT_WRAP     (262u * LINE_DOTS)
#define NEVER          UINT64_MAX

/* Dots from the start of the dot a sprite-0 pixel is drawn on to the first
 * $2002 read (sampled at 3n+4 dots) that sees the flag. */
#ifndef NATIVE_S0_DELAY
#define NATIVE_S0_DELAY 3u
#endif

static const uint32_t EVENT_DOT[4] = { EVENT_VBLANK, EVENT_ODD, EVENT_PRERENDER, EVENT_WRAP };

static NativeVideoMemory mem;
static NativeVideoRegs regs;
static uint8_t io_bus;
static bool vblank, odd_frame;
static uint64_t frame_dot, now_dot, s0_dot;
static unsigned next_event;
static bool frame_ended;
static uint8_t mask_live, mask_pending;
static uint64_t mask_at;
static NativeFrame frames[2];
static unsigned cur;
static uint64_t frame_cycle0, held_dots;

static uint8_t mask_effective(uint64_t dot)
{
    if (mask_at <= dot) {
        mask_live = mask_pending;
        mask_at = NEVER;
    }
    return mask_live;
}

static void position(uint64_t dot, uint16_t *line, uint16_t *x)
{
    uint64_t rel = dot >= frame_dot ? dot - frame_dot : 0;
    *line = (uint16_t)(rel / LINE_DOTS);
    *x = (uint16_t)(rel % LINE_DOTS);
}

static uint8_t bg_pixel(unsigned sx, unsigned line)
{
    unsigned nt = (regs.t >> 10) & 3;
    unsigned x = (regs.t & 31) * 8u + regs.fine_x + sx;
    unsigned y = ((regs.t >> 5) & 31) * 8u + ((regs.t >> 12) & 7) + line;
    unsigned ntx = (nt & 1) ^ ((x >> 8) & 1);
    x &= 255;
    if (y >= 240) y -= 240;
    uint8_t tile = mem.ciram[(ntx << 10) | (y >> 3) * 32u | (x >> 3)];
    unsigned base = (regs.ctrl & 0x10) ? 0x1000u : 0;
    unsigned row = base + tile * 16u + (y & 7);
    unsigned bit = 7 - (x & 7);
    return (uint8_t)(((mem.chr[row] >> bit) & 1) | (((mem.chr[row + 8] >> bit) & 1) << 1));
}

/* The first pixel of this frame where sprite 0 meets an opaque background
 * pixel, with the conditions the PPU applies: both layers on, neither left
 * column clipped there, never at x = 255. */
static bool sprite0_hit(uint64_t start, int *hit_line, int *hit_x)
{
    uint8_t mask = mask_effective(start);
    if ((mask & 0x18) != 0x18) return false;
    unsigned height = (regs.ctrl & 0x20) ? 16 : 8;
    unsigned y0 = mem.oam[0], tile = mem.oam[1], attr = mem.oam[2], x0 = mem.oam[3];
    for (unsigned r = 0; r < height; r++) {
        unsigned line = y0 + 1 + r;
        if (line >= 240) return false;
        unsigned row = (attr & 0x80) ? height - 1 - r : r;
        unsigned addr;
        if (height == 16) {
            unsigned t = tile & 0xFE;
            if (row >= 8) { t++; row -= 8; }
            addr = (tile & 1) * 0x1000u + t * 16u + row;
        } else {
            addr = ((regs.ctrl & 0x08) ? 0x1000u : 0) + tile * 16u + row;
        }
        uint8_t lo = mem.chr[addr], hi = mem.chr[addr + 8];
        for (unsigned px = 0; px < 8; px++) {
            unsigned sx = x0 + px;
            if (sx >= 255) break;
            unsigned bit = (attr & 0x40) ? px : 7 - px;
            if (!(((lo >> bit) | (hi >> bit)) & 1)) continue;
            if (sx < 8 && (mask & 0x06) != 0x06) continue;
            if (!bg_pixel(sx, line)) continue;
            *hit_line = (int)line;
            *hit_x = (int)sx;
            return true;
        }
    }
    return false;
}

static bool picture_on(uint64_t dot)
{
    return (regs.ctrl & 0x80) && (mask_effective(dot) & 0x18);
}

static void frame_start(void)
{
    NativeFrame *f = &frames[cur];
    int line = -1, x = -1;
    s0_dot = NEVER;
    if (sprite0_hit(frame_dot, &line, &x))
        s0_dot = frame_dot + (uint64_t)line * LINE_DOTS + (uint64_t)x + NATIVE_S0_DELAY;
    f->split_line = (int16_t)line;
    f->split_x = (int16_t)x;
    f->start = mem;
    f->start_regs = regs;
    f->rendered = (mask_effective(frame_dot) & 0x18) != 0;
}

void video_advance(uint64_t dots)
{
    while (frame_dot + EVENT_DOT[next_event] <= dots) {
        uint64_t at = frame_dot + EVENT_DOT[next_event];
        switch (next_event) {
        case 0:
            if (native_vblank_deferred(picture_on(dots))) {
                held_dots += dots + 1 - at;
                frame_dot = dots + 1 - EVENT_VBLANK;
                continue;
            }
            native_vblank_taken((regs.ctrl & 0x80) != 0, picture_on(at));
            vblank = true;
            if (!frame_ended) hw_frame_done = true;
            frame_ended = true;
            break;
        case 1:
            odd_frame = !odd_frame;
            break;
        case 2:
            vblank = false;
            s0_dot = NEVER;
            break;
        default:
            frame_ended = false;
            frame_dot = (odd_frame && (mask_effective(at) & 0x18)) ? at - 1 : at;
            frame_start();
            break;
        }
        next_event = (next_event + 1) & 3;
    }
    now_dot = dots;
}

bool video_nmi_level(void)
{
    return (regs.ctrl & 0x80) && vblank;
}

static void log_ppu(uint16_t addr, uint8_t value, uint64_t cycle)
{
    uint16_t line, x;
    position(now_dot, &line, &x);
    if (line >= 240) return;
    NativeFrame *f = &frames[cur];
    if (f->ppu_write_count >= NATIVE_FRAME_MAX_PPU_WRITES) { f->overflowed = true; return; }
    f->ppu_writes[f->ppu_write_count++] =
        (NativeRegWrite){ (uint32_t)(cycle - frame_cycle0), line, x, addr, value };
}

void video_log_apu(uint16_t addr, uint8_t value, uint64_t cycle)
{
    NativeFrame *f = &frames[cur];
    if (f->apu_write_count >= NATIVE_FRAME_MAX_APU_WRITES) { f->overflowed = true; return; }
    uint16_t line, x;
    position(now_dot, &line, &x);
    f->apu_writes[f->apu_write_count++] =
        (NativeRegWrite){ (uint32_t)(cycle - frame_cycle0), line, x, addr, value };
}

static bool rendering_now(void)
{
    uint16_t line, x;
    position(now_dot, &line, &x);
    return (mask_effective(now_dot) & 0x18) && (line < 240 || line == 261);
}

static void oam_write(uint8_t value)
{
    if (rendering_now()) {
        regs.oam_addr = (uint8_t)((regs.oam_addr + 4) & 0xFC);
        return;
    }
    if ((regs.oam_addr & 3) == 2) value &= 0xE3;
    mem.oam[regs.oam_addr++] = value;
}

static void vram_write(uint16_t addr, uint8_t value)
{
    addr &= 0x3FFF;
    if (addr < 0x2000) mem.chr[addr] = value;
    else if (addr < 0x3F00) mem.ciram[addr & 0x7FF] = value;
    else mem.palette[addr & ((addr & 3) ? 0x1F : 0x0F)] = value & 0x3F;
}

static void vram_increment(void)
{
    if (!rendering_now()) {
        regs.v = (uint16_t)((regs.v + ((regs.ctrl & 0x04) ? 32 : 1)) & 0x7FFF);
        return;
    }
    if ((regs.v & 31) == 31) regs.v = (uint16_t)((regs.v & ~31u) ^ 0x0400);
    else regs.v++;
    if ((regs.v & 0x7000) != 0x7000) { regs.v += 0x1000; return; }
    regs.v &= (uint16_t)~0x7000u;
    unsigned y = (regs.v >> 5) & 31;
    if (y == 29) { y = 0; regs.v ^= 0x0800; }
    else if (y == 31) y = 0;
    else y++;
    regs.v = (uint16_t)((regs.v & ~0x03E0u) | (y << 5));
}

/* $2002, $2004 and $2007 accesses finish at the CPU's data sample, seven
 * master clocks into the next cycle, so the console's next dot has already
 * happened when the instruction ends: a frame whose VBlank falls on that dot
 * ends at this instruction boundary rather than the next one. Only the frame
 * boundary moves; the flag itself is set at its own time. */
static void notice_early_vblank(uint64_t cycle)
{
    if (next_event == 0 && !frame_ended && frame_dot + EVENT_VBLANK <= 3 * cycle + 4 &&
        !native_vblank_deferred(picture_on(3 * cycle + 4))) {
        hw_frame_done = true;
        frame_ended = true;
    }
}

uint8_t video_read(uint16_t addr, uint64_t cycle)
{
    video_advance(3 * cycle + 2);
    if ((addr & 7) == 4 || (addr & 7) == 7) notice_early_vblank(cycle);
    if ((addr & 7) != 2) return io_bus;
    if (next_event == 0 && native_vblank_poll()) {
        held_dots += 3 * cycle + 5 - (frame_dot + EVENT_VBLANK);
        frame_dot = 3 * cycle + 5 - EVENT_VBLANK;
    }
    notice_early_vblank(cycle);
    uint64_t sample = 3 * cycle + 4;
    uint64_t prerender = frame_dot + EVENT_PRERENDER;
    bool s0 = s0_dot <= sample && !(s0_dot < prerender && sample >= prerender);
    uint8_t value = (uint8_t)((vblank ? 0x80 : 0) | (s0 ? 0x40 : 0) | (io_bus & 0x1F));
    if (regs.w) log_ppu(0x2002, value, cycle);
    video_advance(3 * cycle + 3);
    vblank = false;
    regs.w = 0;
    io_bus = value;
    return value;
}

void video_write(uint16_t addr, uint8_t value, uint64_t cycle)
{
    video_advance(3 * cycle + 2);
    io_bus = value;
    log_ppu((uint16_t)(0x2000 | (addr & 7)), value, cycle);
    switch (addr & 7) {
    case 0:
        regs.ctrl = value;
        regs.t = (uint16_t)((regs.t & 0x73FF) | ((value & 3) << 10));
        break;
    case 1:
        regs.mask = value;
        mask_effective(now_dot);
        mask_pending = value;
        mask_at = 3 * cycle + 4;
        break;
    case 3:
        regs.oam_addr = value;
        break;
    case 4:
        oam_write(value);
        break;
    case 5:
        if (!regs.w) {
            regs.fine_x = value & 7;
            regs.t = (uint16_t)((regs.t & 0x7FE0) | (value >> 3));
        } else {
            regs.t = (uint16_t)((regs.t & 0x0C1F) | ((value & 0xF8) << 2) | ((value & 7) << 12));
        }
        regs.w ^= 1;
        break;
    case 6:
        if (!regs.w) {
            regs.t = (uint16_t)((regs.t & 0x00FF) | ((value & 0x3F) << 8));
        } else {
            regs.t = (uint16_t)((regs.t & 0x7F00) | value);
            regs.v = regs.t;
        }
        regs.w ^= 1;
        break;
    case 7:
        notice_early_vblank(cycle);
        vram_write(regs.v, value);
        vram_increment();
        break;
    default:
        break;
    }
}

void video_oam_dma(uint8_t page, const uint8_t data[256], uint64_t cycle)
{
    video_advance(3 * cycle + 2);
    log_ppu(0x4014, page, cycle);
    for (unsigned i = 0; i < 256; i++) oam_write(data[i]);
    io_bus = data[255];
}

void video_power_on(void)
{
    static const uint8_t POWER_ON_PALETTE[32] = {
        0x00, 0x00, 0x28, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x01, 0x20, 0x00, 0x08, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    };
    uint8_t chr[sizeof mem.chr];
    memcpy(chr, mem.chr, sizeof chr);
    memset(&mem, 0, sizeof mem);
    memcpy(mem.chr, chr, sizeof chr);
    for (unsigned i = 0; i < sizeof mem.ciram; i++) {
        bool bit1_clear = (i & 2) == 0, upper = (i & 0x1F) >= 0x10;
        mem.ciram[i] = bit1_clear != upper ? 0xF0 : 0x0F;
    }
    memcpy(mem.palette, POWER_ON_PALETTE, sizeof mem.palette);
    memset(&regs, 0, sizeof regs);
    io_bus = 0;
    vblank = false;
    odd_frame = true;
    frame_dot = now_dot = 0;
    s0_dot = NEVER;
    next_event = 0;
    frame_ended = false;
    held_dots = 0;
    mask_live = mask_pending = 0;
    mask_at = NEVER;
    cur = 0;
}

void video_frame_begin(uint32_t frame, uint64_t cycle)
{
    NativeFrame *f = &frames[cur];
    f->frame = frame;
    f->ppu_write_count = f->apu_write_count = 0;
    f->overflowed = false;
    frame_cycle0 = cycle;
    if (frame == 0) frame_start();
}

void video_frame_end(uint64_t cycle)
{
    frames[cur].cycles = (uint32_t)(cycle - frame_cycle0);
    frames[cur].held_cycles = (uint32_t)(held_dots / 3);
    held_dots = 0;
    cur ^= 1;
}

const NativeVideoMemory *native_video_memory(void) { return &mem; }
const NativeVideoRegs *native_video_regs(void) { return &regs; }
const NativeFrame *native_last_frame(void) { return &frames[cur ^ 1]; }

typedef struct {
    NativeVideoMemory mem;
    NativeVideoRegs regs;
    uint64_t frame_dot, now_dot, s0_dot, mask_at, frame_cycle0, held_dots;
    uint32_t next_event;
    uint8_t io_bus, vblank, odd_frame, frame_ended, mask_live, mask_pending;
} VideoState;

size_t video_state_save(uint8_t *out, size_t cap)
{
    if (!out || cap < sizeof(VideoState)) return sizeof(VideoState);
    VideoState s;
    memset(&s, 0, sizeof s);
    s.mem = mem;
    s.regs = regs;
    s.frame_dot = frame_dot;
    s.now_dot = now_dot;
    s.s0_dot = s0_dot;
    s.mask_at = mask_at;
    s.frame_cycle0 = frame_cycle0;
    s.held_dots = held_dots;
    s.next_event = next_event;
    s.io_bus = io_bus;
    s.vblank = vblank;
    s.odd_frame = odd_frame;
    s.frame_ended = frame_ended;
    s.mask_live = mask_live;
    s.mask_pending = mask_pending;
    memcpy(out, &s, sizeof s);
    return sizeof s;
}

bool video_state_load(const uint8_t *in, size_t size)
{
    if (size != sizeof(VideoState)) return false;
    VideoState s;
    memcpy(&s, in, sizeof s);
    if (s.next_event > 3) return false;
    mem = s.mem;
    regs = s.regs;
    frame_dot = s.frame_dot;
    now_dot = s.now_dot;
    s0_dot = s.s0_dot;
    mask_at = s.mask_at;
    frame_cycle0 = s.frame_cycle0;
    held_dots = s.held_dots;
    next_event = s.next_event;
    io_bus = s.io_bus;
    vblank = s.vblank != 0;
    odd_frame = s.odd_frame != 0;
    frame_ended = s.frame_ended != 0;
    mask_live = s.mask_live;
    mask_pending = s.mask_pending;
    return true;
}
