/*
 * native_bus.c - the machine the recompiled code runs on in the native
 * runtime: NESRecomp's hw.h bus interface implemented as plain memory and
 * register capture, with no PPU or APU underneath (docs/native-runtime.md).
 *
 *   memory map    $0000-$1FFF CPU RAM (2 KiB mirrored), $2000-$3FFF PPU
 *                 registers (native_video.c), $4014 sprite DMA, $4016/$4017
 *                 controllers, other $4000-$4017 writes captured for a synth,
 *                 $8000-$BFFF the UNROM bank the program selected, $C000-$FFFF
 *                 the last bank; anything else is open bus
 *   time          every bus cycle the generated code performs is counted,
 *                 and the sprite DMA adds the 513 or 514 cycles the console
 *                 stalls for. The count places VBlank, the NMI and the frame
 *                 end; nothing else is simulated
 *   frame loop    native_run_frame(): run compiled code from cpu.pc until the
 *                 frame's VBlank has begun and an instruction has finished
 *   lag           a task runs from the scheduler's dispatch ($FF6A starts
 *                 one, $FF7C resumes one) until it yields and the scheduler
 *                 begins its next pass ($FF38). A VBlank while a task runs
 *                 is a lag frame (NATIVE_LAG_FAITHFUL) or is held back until
 *                 the task yields (NATIVE_LAG_NONE)
 *   hook sites    game.toml [[mod_function_hook]] sites run their enabled
 *                 callbacks (native_hooks.c) at the instruction boundary, as
 *                 the cycle host's scheduler does
 *   save states   native_state_save / native_state_load: CPU, bus and video
 */
#include "native/native_runtime.h"

#include "cpu6502.h"
#include "cyc_hooks.h"
#include "cyc_recomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the generated code and NESRecomp's CPU sequences link against. */
bool hw_frame_done;
bool hw_frame_end_hit;
bool hw_observe_hit;
int hw_observe_line = -1;
int hw_dma_stalls;
bool hw_prg_modified;
bool hw_prg_data_modified;
HwNativeInterval hw_native_interval;
uint8_t hw_code_watch[0x800];
bool cyc_trace_enabled;
uint64_t cyc_trace_hash;
void *cyc_trace_file;
uint32_t cyc_trace_cycle;
uint8_t cyc_hooks_armed;
int32_t cyc_hook_passed = -1;
bool cyc_hook_hit;
uint8_t cyc_ram_code_dirty;

static uint64_t cycles;

/* An isolated call is open (native_isolate_begin): no clock runs and no
 * observer is told, and the machine is put back when it closes. */
static bool scope_open;

/* With cyc_trace_enabled and cyc_trace_file set (ducktales_native
 * --trace-frame), one line per instruction in the cycle host's --trace-out
 * INSN format, so the two traces line up on the CPU cycle number. */
void cyc_trace_instruction(uint16_t pc, uint8_t a, uint8_t x, uint8_t y, uint8_t s, uint8_t p)
{
    if (cyc_trace_file) {
        fprintf((FILE *)cyc_trace_file, "%llu INSN PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X\n",
                (unsigned long long)cycles, pc, a, x, y, s, p);
    }
}

static uint8_t ram[0x800];
static uint8_t *prg;
static size_t prg_size;
static unsigned prg_banks, prg_bank;
static uint8_t data_bus;
static bool dma_pending;
static uint8_t dma_page;
static uint8_t buttons[2], shift[2], strobe;
static uint32_t frame_number;
static uint64_t fallback_instructions, unmodeled_reads;

#define SCHEDULER_PASS   0xFF38u
#define SCHEDULER_START  0xFF6Au
#define SCHEDULER_RESUME 0xFF7Cu
#define NMI_RTI          0xC138u

static NativeLagMode lag_mode;
static NativeLagFrame lag_cur, lag_last;
static bool task_running, deferring, defer_released;
static unsigned defer_polls;
static uint64_t defer_start, overrun_vblank, overrun_nmi, nmi_entry;
static bool overrun_open, in_nmi;
static int32_t overrun_frame;
static uint32_t nmi_writes[0x800], task_reads[0x800];
static bool nmi_seen;
static void (*step_hook)(void);
static void (*latch_hook)(uint8_t buttons[2]);

bool native_load_ines(const uint8_t *image, size_t size, char *err, size_t err_size)
{
    if (size < 16 || memcmp(image, "NES\x1A", 4)) {
        snprintf(err, err_size, "not an iNES image");
        return false;
    }
    unsigned mapper = (image[6] >> 4) | (image[7] & 0xF0);
    size_t banks = image[4], offset = 16 + ((image[6] & 4) ? 512 : 0);
    if (mapper != 2 || image[5] != 0 || !banks || (banks & (banks - 1)) || offset + banks * 0x4000 > size) {
        snprintf(err, err_size, "the native runtime runs UNROM (mapper 2) with CHR RAM only");
        return false;
    }
    free(prg);
    prg_size = banks * 0x4000;
    prg = (uint8_t *)malloc(prg_size);
    if (!prg) {
        snprintf(err, err_size, "out of memory");
        return false;
    }
    memcpy(prg, image + offset, prg_size);
    prg_banks = (unsigned)banks;
    return true;
}

void native_power_on(void)
{
    native_hooks_init();
    for (unsigned i = 0; i < sizeof ram; i++) {
        bool bit1_clear = (i & 2) == 0, upper = (i & 0x1F) >= 0x10;
        ram[i] = bit1_clear != upper ? 0xF0 : 0x0F;
    }
    prg_bank = 0;
    cycles = 0;
    data_bus = 0;
    dma_pending = false;
    strobe = shift[0] = shift[1] = 0;
    frame_number = 0;
    fallback_instructions = unmodeled_reads = 0;
    hw_frame_done = false;
    memset(&lag_cur, 0, sizeof lag_cur);
    lag_cur.overrun_frame = -1;
    lag_last = lag_cur;
    task_running = deferring = defer_released = overrun_open = in_nmi = nmi_seen = false;
    defer_polls = 0;
    memset(nmi_writes, 0, sizeof nmi_writes);
    memset(task_reads, 0, sizeof task_reads);
    video_power_on();
    cpu_power_on();
}

void native_set_lag_mode(NativeLagMode mode)
{
    lag_mode = mode;
}

const NativeLagFrame *native_last_lag(void)
{
    return &lag_last;
}

const uint32_t *native_nmi_writes(void)
{
    return nmi_writes;
}

const uint32_t *native_task_reads(void)
{
    return task_reads;
}

void native_set_step_hook(void (*hook)(void))
{
    step_hook = hook;
}

void native_set_latch_hook(void (*hook)(uint8_t buttons[2]))
{
    latch_hook = hook;
}

bool native_vblank_deferred(bool picture_on)
{
    if (lag_mode != NATIVE_LAG_NONE || !task_running || !picture_on || defer_released) {
        return false;
    }
    if (!deferring) {
        deferring = true;
        defer_start = cycles;
    }
    if (cycles - defer_start < NATIVE_DEFER_CAP_CYCLES) {
        return true;
    }
    defer_released = true;
    lag_cur.defer_end = NATIVE_DEFER_CAP;
    return false;
}

/* A $2002 read while VBlank is held back. The first can be the address-latch
 * reset before a VRAM update; a second is a poll that would wait forever on a
 * PPU that is not moving, so VBlank is released. */
bool native_vblank_poll(void)
{
    if (!deferring || defer_released || ++defer_polls < 2) {
        return false;
    }
    defer_released = true;
    lag_cur.defer_end = NATIVE_DEFER_POLL;
    return true;
}

void native_vblank_taken(bool nmi_enabled, bool picture_on)
{
    lag_cur.nmi = nmi_enabled;
    lag_cur.picture = picture_on;
    lag_cur.lag = nmi_enabled && task_running;
    lag_cur.deferred_cycles = deferring ? (uint32_t)(cycles - defer_start) : 0;
    if (deferring && !defer_released) {
        lag_cur.defer_end = NATIVE_DEFER_IDLE;
    }
    if (lag_cur.lag && !overrun_open) {
        overrun_open = true;
        overrun_frame = (int32_t)frame_number;
        overrun_vblank = cycles;
        overrun_nmi = 0;
    }
    deferring = defer_released = false;
    defer_polls = 0;
}

void native_nmi_return(void)
{
    overrun_nmi += cycles - nmi_entry;
    in_nmi = false;
}

static void observe(uint16_t addr, HwCycleKind kind)
{
    unsigned k = (unsigned)kind & 3u;
    if (k == HW_READ && addr == 0xFFFA) {
        nmi_entry = cycles;
        in_nmi = nmi_seen = true;
    }
    if (k != HW_FETCH) {
        return;
    }
    if (addr == NMI_RTI) {
        overrun_nmi += cycles + 6 - nmi_entry;
        in_nmi = false;
    }
    if (addr == SCHEDULER_START || addr == SCHEDULER_RESUME) {
        task_running = true;
    }
    if (addr != SCHEDULER_PASS || !task_running) {
        return;
    }
    task_running = false;
    if (step_hook) {
        step_hook();
    }
    if (!overrun_open) {
        return;
    }
    overrun_open = false;
    lag_cur.overrun_frame = overrun_frame;
    lag_cur.overrun_cycles = (uint32_t)(cycles - overrun_vblank - overrun_nmi);
}

void native_set_controller(int port, uint8_t value)
{
    buttons[port & 1] = value;
}

const uint8_t *native_cpu_ram(void)
{
    return ram;
}

uint64_t native_cycle_count(void)
{
    return cycles;
}

uint32_t native_frame_number(void)
{
    return frame_number;
}

uint64_t native_fallback_instructions(void)
{
    return fallback_instructions;
}

uint64_t native_unmodeled_reads(void)
{
    return unmodeled_reads;
}

const uint8_t *native_prg(size_t *size)
{
    *size = prg_size;
    return prg;
}

static inline uint8_t prg_byte(uint16_t addr)
{
    unsigned bank = addr >= 0xC000 ? prg_banks - 1 : prg_bank;
    return prg[(size_t)bank * 0x4000 + (addr & 0x3FFF)];
}

bool native_peek(uint16_t addr, uint8_t *value)
{
    if (addr < 0x2000) {
        *value = ram[addr & 0x7FF];
    } else if (addr >= 0x8000 && prg) {
        *value = prg_byte(addr);
    } else {
        return false;
    }
    return true;
}

bool native_poke(uint16_t addr, uint8_t value)
{
    if (addr >= 0x2000) {
        return false;
    }
    ram[addr & 0x7FF] = value;
    return true;
}

/* ---- hw.h ---- */

unsigned hw_prg_bank4(uint16_t addr)
{
    unsigned slot = (addr >> 12) & 3;
    return (addr >= 0xC000 ? prg_banks - 1 : prg_bank) * 4 + slot;
}

unsigned hw_prg_bank(uint16_t addr)
{
    return hw_prg_bank4(addr) >> 1;
}

bool hw_prg_is_stable(void)
{
    return true;
}

bool hw_prg_is_rom(uint16_t addr)
{
    return addr >= 0x8000;
}

bool hw_irq_line(void)
{
    return false;
}

bool hw_native_prepare(void)
{
    return false;
}

static inline void end_cycle(void)
{
    if (scope_open) {
        cycles++;
        return;
    }
    video_advance(3 * cycles + 3);
    cycles++;
    video_advance(3 * cycles);
    cpu_nmi_input(video_nmi_level());
}

/* The sprite DMA halts the CPU on its next read: one halt cycle, one more to
 * align when it starts on a get cycle (odd cycle numbers here), then 256
 * get/put pairs. The bytes reach OAM at once; the program is halted
 * throughout, so nothing can see them arrive one by one. */
static void sprite_dma(void)
{
    uint8_t data[256];
    for (unsigned i = 0; i < 256; i++) {
        uint16_t addr = (uint16_t)(dma_page << 8 | i);
        data[i] = addr < 0x2000 ? ram[addr & 0x7FF] : addr >= 0x8000 ? prg_byte(addr) : data_bus;
    }
    dma_pending = false;
    video_oam_dma(dma_page, data, cycles);
    unsigned stall = (cycles & 1) ? 513 : 514;
    for (unsigned i = 0; i < stall; i++) {
        end_cycle();
    }
    data_bus = data[255];
    hw_dma_stalls = (int)stall;
}

void hw_cycle_start(uint16_t addr, HwCycleKind kind)
{
    hw_dma_stalls = 0;
    if (!scope_open) {
        observe(addr, kind);
    }
    if (cycles == 0) {
        video_advance(0);
        cpu_nmi_input(video_nmi_level());
    }
    if (dma_pending && (kind == HW_READ || kind == HW_FETCH)) {
        sprite_dma();
    }
}

void hw_cycle_finish(bool instruction_done)
{
    (void)instruction_done;
    end_cycle();
}

static uint8_t controller_read(int port)
{
    uint8_t bit = strobe ? (buttons[port] >> 7) & 1 : (shift[port] >> 7) & 1;
    if (!strobe) {
        shift[port] = (uint8_t)(shift[port] << 1 | 1);
    }
    return bit;
}

uint8_t hw_read(uint16_t addr)
{
    if (addr < 0x2000) {
        data_bus = ram[addr & 0x7FF];
        if (nmi_seen && !in_nmi && !scope_open) {
            task_reads[addr & 0x7FF]++;
        }
    } else if (addr < 0x4000) {
        if ((addr & 7) == 4 || (addr & 7) == 7) {
            unmodeled_reads++;
        }
        data_bus = video_read(addr, cycles);
    } else if (addr >= 0x8000) {
        data_bus = prg_byte(addr);
    } else if (addr == 0x4016 || addr == 0x4017) {
        data_bus = (uint8_t)((data_bus & 0xE0) | controller_read(addr & 1));
    } else if (addr == 0x4015) {
        unmodeled_reads++;
    }
    return data_bus;
}

uint8_t hw_read_rom(uint16_t addr, uint8_t value)
{
    (void)addr;
    return data_bus = value;
}

uint8_t hw_read_code_ram(uint16_t addr, uint8_t value)
{
    (void)addr;
    return data_bus = value;
}

void hw_write(uint16_t addr, uint8_t value)
{
    if (addr < 0x2000) {
        ram[addr & 0x7FF] = value;
        if (in_nmi && !scope_open) {
            nmi_writes[addr & 0x7FF]++;
        }
    } else if (addr < 0x4000) {
        video_write(addr, value, cycles);
    } else if (addr == 0x4014) {
        dma_pending = true;
        dma_page = value;
    } else if (addr == 0x4016) {
        if ((strobe & 1) && !(value & 1)) {
            uint8_t latched[2] = { buttons[0], buttons[1] };
            if (latch_hook && !scope_open) {
                latch_hook(latched);
            }
            shift[0] = latched[0];
            shift[1] = latched[1];
        }
        strobe = value & 1;
    } else if (addr < 0x4018) {
        video_log_apu(addr, value, cycles);
    } else if (addr >= 0x8000) {
        prg_bank = value & (prg_banks - 1);
    }
    data_bus = value;
}

/* ---- frame loop ---- */

void native_run_frame(void)
{
    video_frame_begin(frame_number, cycles);
    hw_frame_done = false;
    if (cpu.power_on) {
        cpu_power_on_sequence();
    }
    while (!hw_frame_done) {
        if (cyc_hooks_armed) {
            cyc_hook_hit = false;
            if (native_hooks_due(cpu.pc)) {
                uint16_t at = cpu.pc;
                native_hooks_fire(at);
                if (cpu.pc == at) {
                    cyc_hook_passed = at;
                }
                continue;
            }
        }
        if (cpu.jammed) {
            cpu_jam_cycle();
        } else if (cyc_native_has(cpu.pc)) {
            cyc_native_run();
        } else {
            fallback_instructions++;
            cpu_interp_step();
        }
        cyc_hook_passed = -1;
    }
    video_frame_end(cycles);
    lag_last = lag_cur;
    memset(&lag_cur, 0, sizeof lag_cur);
    lag_cur.overrun_frame = -1;
    frame_number++;
}

/* ---- isolated calls (src/decomp/decomp_native.c) ---- */

#define RETURN_PC       0x5FFFu
#define ISOLATED_BUDGET 2000000u

static uint8_t *scope_state;
static size_t scope_size;
static bool scope_frame_done;

bool native_isolated(void)
{
    return scope_open;
}

bool native_isolate_begin(void)
{
    size_t n = native_state_save(NULL, 0);
    if (n > scope_size) {
        uint8_t *p = (uint8_t *)realloc(scope_state, n);
        if (!p) {
            return false;
        }
        scope_state = p;
        scope_size = n;
    }
    native_state_save(scope_state, n);
    scope_frame_done = hw_frame_done;
    hw_frame_done = false;
    scope_open = true;
    return true;
}

void native_isolate_end(void)
{
    scope_open = false;
    native_state_load(scope_state, native_state_save(NULL, 0));
    hw_frame_done = scope_frame_done;
}

/* As the cycle host's isolated call (cyc_run_isolated): the routine from a
 * JSR's return address at RETURN_PC until it returns there with the stack back
 * at r->s; hook sites let the original through. */
bool native_isolated_call(uint16_t routine, NativeRegs *r)
{
    cpu.a = r->a;
    cpu.x = r->x;
    cpu.y = r->y;
    cpu_set_p(r->p);
    ram[0x100 | r->s] = (uint8_t)((RETURN_PC - 1) >> 8);
    ram[0x100 | (uint8_t)(r->s - 1)] = (uint8_t)(RETURN_PC - 1);
    cpu.s = (uint8_t)(r->s - 2);
    cpu.pc = routine;
    cpu.jammed = 0;
    uint64_t start = cycles;
    while (!(cpu.pc == RETURN_PC && cpu.s == r->s)) {
        if (cpu.jammed || cycles - start > ISOLATED_BUDGET || (int8_t)(cpu.s - r->s) > 0) {
            return false;
        }
        cyc_hook_hit = false;
        if (cyc_hooks_armed && native_hooks_due(cpu.pc)) {
            cyc_hook_passed = cpu.pc;
        }
        if (cyc_native_has(cpu.pc)) {
            cyc_native_run();
        } else {
            cpu_interp_step();
        }
        cyc_hook_passed = -1;
    }
    r->a = cpu.a;
    r->x = cpu.x;
    r->y = cpu.y;
    r->p = cpu_get_p(0);
    return true;
}

/* ---- save states ---- */

typedef struct {
    uint8_t ram[0x800];
    uint64_t cycles, fallback_instructions, unmodeled_reads;
    uint64_t defer_start, overrun_vblank, overrun_nmi, nmi_entry;
    uint32_t prg_bank, frame_number, defer_polls;
    int32_t overrun_frame;
    uint8_t data_bus, dma_pending, dma_page, strobe;
    uint8_t buttons[2], shift[2];
    uint8_t task_running, deferring, defer_released, overrun_open, in_nmi, nmi_seen;
    NativeLagFrame lag_cur, lag_last;
    Cpu6502 cpu;
} BusState;

size_t native_state_save(uint8_t *out, size_t cap)
{
    size_t video = video_state_save(NULL, 0), need = sizeof(BusState) + video;
    if (!out || cap < need) {
        return need;
    }
    BusState s;
    memset(&s, 0, sizeof s);
    memcpy(s.ram, ram, sizeof ram);
    s.cycles = cycles;
    s.fallback_instructions = fallback_instructions;
    s.unmodeled_reads = unmodeled_reads;
    s.defer_start = defer_start;
    s.overrun_vblank = overrun_vblank;
    s.overrun_nmi = overrun_nmi;
    s.nmi_entry = nmi_entry;
    s.prg_bank = prg_bank;
    s.frame_number = frame_number;
    s.defer_polls = defer_polls;
    s.overrun_frame = overrun_frame;
    s.data_bus = data_bus;
    s.dma_pending = dma_pending;
    s.dma_page = dma_page;
    s.strobe = strobe;
    memcpy(s.buttons, buttons, 2);
    memcpy(s.shift, shift, 2);
    s.task_running = task_running;
    s.deferring = deferring;
    s.defer_released = defer_released;
    s.overrun_open = overrun_open;
    s.in_nmi = in_nmi;
    s.nmi_seen = nmi_seen;
    s.lag_cur = lag_cur;
    s.lag_last = lag_last;
    s.cpu = cpu;
    memcpy(out, &s, sizeof s);
    video_state_save(out + sizeof s, video);
    return need;
}

bool native_state_load(const uint8_t *in, size_t size)
{
    if (size != native_state_save(NULL, 0)) {
        return false;
    }
    BusState s;
    memcpy(&s, in, sizeof s);
    if (s.prg_bank >= prg_banks || !video_state_load(in + sizeof s, size - sizeof s)) {
        return false;
    }
    memcpy(ram, s.ram, sizeof ram);
    cycles = s.cycles;
    fallback_instructions = s.fallback_instructions;
    unmodeled_reads = s.unmodeled_reads;
    defer_start = s.defer_start;
    overrun_vblank = s.overrun_vblank;
    overrun_nmi = s.overrun_nmi;
    nmi_entry = s.nmi_entry;
    prg_bank = s.prg_bank;
    frame_number = s.frame_number;
    defer_polls = s.defer_polls;
    overrun_frame = s.overrun_frame;
    data_bus = s.data_bus;
    dma_pending = s.dma_pending != 0;
    dma_page = s.dma_page;
    strobe = s.strobe;
    memcpy(buttons, s.buttons, 2);
    memcpy(shift, s.shift, 2);
    task_running = s.task_running != 0;
    deferring = s.deferring != 0;
    defer_released = s.defer_released != 0;
    overrun_open = s.overrun_open != 0;
    in_nmi = s.in_nmi != 0;
    nmi_seen = s.nmi_seen != 0;
    lag_cur = s.lag_cur;
    lag_last = s.lag_last;
    cpu = s.cpu;
    hw_frame_done = false;
    cyc_hook_passed = -1;
    return true;
}
