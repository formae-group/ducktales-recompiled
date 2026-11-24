/*
 * native_state.h - what the native runtime hands to a renderer and a synth.
 *
 * The native runtime (docs/NATIVE-RUNTIME.md) runs the recompiled program
 * against a plain bus: no PPU dots, no APU ticks. The game's register writes
 * still land somewhere, and this header is that place:
 *
 *   NativeVideoMemory  CHR RAM, the two nametables (CIRAM, vertical
 *                      mirroring), palette RAM and OAM, kept exactly as the
 *                      console's memories would hold them
 *   NativeVideoRegs    the PPU registers as the program set them: $2000,
 *                      $2001, the scroll/address latch (t, v, fine X, w)
 *   NativeFrame        one machine frame (VBlank to VBlank): the memories and
 *                      registers at the start of the picture (scanline 0), the
 *                      PPU writes made while the picture was drawn with the
 *                      scanline and dot they landed on, the sprite-0 split,
 *                      and every APU register write with its CPU cycle
 *
 * A renderer draws frame N from start, applying ppu_writes[] in raster order
 * (they are already sorted); a renderer that only wants the HUD split uses
 * split_line. A synth plays apu_writes[] at their cycle offsets (1789773 Hz
 * NTSC CPU clock; cycle 0 is the frame's first cycle). Nothing here is read
 * back by the game: changing it cannot change the machine.
 *
 * Positions are the console's: scanline 0-239 visible, 241 VBlank start,
 * 261 pre-render; dot 0-340.
 *
 * In the no-slowdown lag mode (native_runtime.h) a frame can be longer than
 * 29781 cycles: held_cycles of it is post-render time added before VBlank so
 * the game's logic could finish, and writes made then report scanline 241
 * dot 0. A frame is still presented for one 60 Hz period, so a synth plays it
 * in cycles - held_cycles, scaling apu_writes[] offsets to match, rather than
 * playing the extra time (native_audio.h does).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_FRAME_MAX_PPU_WRITES 4096
#define NATIVE_FRAME_MAX_APU_WRITES 1024

typedef struct {
    uint8_t chr[0x2000];  /* pattern tables, CHR RAM ($0000-$1FFF) */
    uint8_t ciram[0x800]; /* nametables $2000 (0-$3FF) and $2400 ($400-$7FF) */
    uint8_t palette[32];  /* 6-bit entries; $3F10/14/18/1C alias 0/4/8/C */
    uint8_t oam[256];     /* attribute bytes keep only bits 7-5 and 1-0 */
} NativeVideoMemory;

typedef struct {
    uint8_t ctrl;     /* $2000 */
    uint8_t mask;     /* $2001 */
    uint8_t oam_addr; /* $2003 */
    uint8_t fine_x;   /* x */
    uint16_t t;       /* temporary VRAM address (scroll latch) */
    uint16_t v;       /* VRAM address */
    uint8_t w;        /* first/second write toggle */
} NativeVideoRegs;

typedef struct {
    uint32_t cycle;    /* CPU cycle within the frame */
    uint16_t scanline; /* where the PPU was when the write landed */
    uint16_t dot;
    uint16_t addr; /* $2000-$2007, or $4014 for a sprite DMA; $2002 is a read
                                 that reset the $2005/$2006 write latch (value: what it read) */
    uint8_t value;
} NativeRegWrite;

typedef struct {
    uint32_t frame;          /* machine frame number, 0 = power-on */
    uint32_t cycles;         /* CPU cycles this frame took, DMA included */
    uint32_t held_cycles;    /* of those, cycles VBlank was held back */
    NativeVideoMemory start; /* memories as scanline 0 began */
    NativeVideoRegs start_regs;
    bool rendered;      /* background or sprites were on at scanline 0 */
    int16_t split_line; /* scanline of the sprite-0 hit, -1 = none */
    int16_t split_x;
    uint32_t ppu_write_count; /* writes while scanlines 0-239 were drawn */
    NativeRegWrite ppu_writes[NATIVE_FRAME_MAX_PPU_WRITES];
    uint32_t apu_write_count; /* $4000-$4013, $4015, $4017 */
    NativeRegWrite apu_writes[NATIVE_FRAME_MAX_APU_WRITES];
    bool overflowed; /* a log was full; the rest was dropped */
} NativeFrame;

/* The memories and registers right now (between frames: as VBlank began). */
const NativeVideoMemory *native_video_memory(void);
const NativeVideoRegs *native_video_regs(void);
/* The frame that just ended. Valid until the next frame ends. */
const NativeFrame *native_last_frame(void);

#ifdef __cplusplus
}
#endif
