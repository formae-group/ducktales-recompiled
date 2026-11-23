/*
 * native_runtime.h - the native runtime's machine side (docs/native-runtime.md).
 *
 *   native_bus.c    what the recompiled code calls (NESRecomp's hw.h): CPU
 *                   RAM, the UNROM banks, the controller, APU register
 *                   capture, the sprite DMA, cycle counting and the frame loop
 *   native_video.c  the PPU registers and memories the program writes, and the
 *                   frame timing derived from the cycle count: VBlank, the NMI
 *                   line, the $2002 flags and the sprite-0 hit
 *
 * Time is the CPU cycle count n. Cycle n spans master clocks (12n, 12n+12];
 * the PPU advances one dot every 4 clocks, so 3n dots have passed when cycle
 * n begins. The NMI input is sampled at clock 12n+4 and a register access
 * happens at 12n+12, before that clock's dot - NESRecomp's alignment 0.
 *
 * Lag modes. DuckTales runs its game logic as cooperative tasks; once a task
 * yields for the frame the scheduler (bank 7 $FF35) spins at $FF38-$FF4B
 * until the NMI wakes it. On the console a frame whose task is still running
 * when VBlank begins is a lag frame: the NMI interrupts the task and the
 * picture repeats. NATIVE_LAG_FAITHFUL keeps that, so RAM matches the cycle
 * build on every frame. NATIVE_LAG_NONE holds VBlank back (extra post-render
 * time, as NESRecomp's extra scanlines do) until the task yields, so every
 * frame's logic completes before its NMI. It does so only
 * while the picture is on (NMI enabled, background or sprites shown): the
 * long tasks that run behind a blank screen (land loads, transitions) keep
 * the console's frame count, so their length and the music under them do
 * not change.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- native_bus.c ---- */
typedef enum {
    NATIVE_LAG_NONE,
    NATIVE_LAG_FAITHFUL
} NativeLagMode;

/* How the VBlank that ended a frame was placed. */
typedef enum {
    NATIVE_DEFER_NONE, /* not held back */
    NATIVE_DEFER_IDLE, /* held until the scheduler's idle scan */
    NATIVE_DEFER_POLL, /* released early: the program polled $2002 */
    NATIVE_DEFER_CAP,  /* released at NATIVE_DEFER_CAP_CYCLES */
} NativeDeferEnd;

/* NATIVE_LAG_NONE holds VBlank back at most this long (one frame). */
#define NATIVE_DEFER_CAP_CYCLES 29781u

typedef struct {
    bool nmi;                 /* the NMI was enabled as VBlank began */
    bool picture;             /* ... and background or sprites were shown */
    bool lag;                 /* VBlank found a task still running */
    uint32_t deferred_cycles; /* CPU cycles VBlank was held back */
    NativeDeferEnd defer_end;
    /* A lag that ended during this frame: the frame whose VBlank it missed
     * (-1: none) and the CPU cycles of task work after that VBlank, the
     * NMI handler's cycles excluded. */
    int32_t overrun_frame;
    uint32_t overrun_cycles;
} NativeLagFrame;

void native_set_lag_mode(NativeLagMode mode);
const NativeLagFrame *native_last_lag(void);
/* Per CPU RAM byte: writes made by the NMI handler, and reads made outside
 * it, since power-on (ducktales_native --nmi-audit). */
const uint32_t *native_nmi_writes(void);
const uint32_t *native_task_reads(void);
/* Called when a task yields back to the scheduler, and when the program latches the controllers, with the buttons
 * it is about to latch (the hook may replace them). */
void native_set_step_hook(void (*hook)(void));
void native_set_latch_hook(void (*hook)(uint8_t buttons[2]));

bool native_load_ines(const uint8_t *image, size_t size, char *err, size_t err_size);
void native_power_on(void);
void native_set_controller(int port, uint8_t buttons);
/* Run until the next VBlank start, then to the next instruction boundary. */
void native_run_frame(void);
const uint8_t *native_cpu_ram(void);
uint64_t native_cycle_count(void);
uint32_t native_frame_number(void);
/* Instructions the recompiled code did not cover (run by NESRecomp's
 * instruction templates instead), and bus reads the runtime does not model. */
uint64_t native_fallback_instructions(void);
uint64_t native_unmodeled_reads(void);
/* The PRG image, and what a CPU read of RAM or ROM returns / a store to CPU
 * RAM (any mirror) without bus cycles: for hook callbacks (cyc_mod_peek). */
const uint8_t *native_prg(size_t *size);
bool native_peek(uint16_t addr, uint8_t *value);
bool native_poke(uint16_t addr, uint8_t value);
/* The whole machine between frames: CPU, bus, video. Save returns the size
 * (and writes nothing when out is NULL or cap too small); load refuses a
 * blob of another size or an impossible one. */
size_t native_state_save(uint8_t *out, size_t cap);
bool native_state_load(const uint8_t *in, size_t size);

/* ---- native_hooks.c: game.toml hook sites (cyc_hooks.h) ---- */
/* Bind the plugins registered with mod_function_hooks.h to the program's
 * sites and keep cyc_hooks_armed current; false when a plugin names a site
 * the program does not declare. */
bool native_hooks_init(void);
bool native_hooks_due(uint16_t pc);
void native_hooks_fire(uint16_t pc);

/* ---- native_video.c ---- */
void video_power_on(void);
void video_advance(uint64_t dots);
bool video_nmi_level(void);
uint8_t video_read(uint16_t addr, uint64_t cycle);
void video_write(uint16_t addr, uint8_t value, uint64_t cycle);
void video_oam_dma(uint8_t page, const uint8_t data[256], uint64_t cycle);
void video_frame_begin(uint32_t frame, uint64_t cycle);
void video_frame_end(uint64_t cycle);
void video_log_apu(uint16_t addr, uint8_t value, uint64_t cycle);
size_t video_state_save(uint8_t *out, size_t cap);
bool video_state_load(const uint8_t *in, size_t size);

/* ---- native_bus.c, called by native_video.c ---- */
bool native_vblank_deferred(bool picture_on);
bool native_vblank_poll(void);
void native_vblank_taken(bool nmi_enabled, bool picture_on);

#ifdef __cplusplus
}
#endif
