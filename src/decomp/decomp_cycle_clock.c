/*
 * decomp_cycle_clock.c - the original routine on the running machine's clock
 * in the cycle host's backend of the decompilation layer (decomp_cycle.c has
 * the whole account): its bus schedule measured from a snapshot
 * (dc_clock_measure), the C's bus operations matched against it and replayed
 * at its cycles, and check mode's timing check of timed routines against the
 * C's walked path (decomp_timing.c). It owns no setting or option.
 */
#include "decomp/decomp_cycle.h"
#include "decomp/devices.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "cyc_hooks.h"
#include "cyc_ring.h"
#include "cyc_state.h"
#include "cyc_trace.h"
#include "hw_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLOCK_CYCLE_LIMIT 65536

static struct {
    uint64_t start;
    unsigned n;
    bool fetch;
    uint16_t routine;
    bool leave_start; /* a stop at the start counts only after the first instruction */

    struct {
        uint32_t offset;
        uint16_t addr;
        uint8_t value;
        char kind;
        bool fetch, done;
    } cycles[CLOCK_CYCLE_LIMIT];
} s_timing;

static void clock_measure_cycle(const CycTraceCycle *cycle)
{
    if (cycle->dma) {
        return;
    }
    unsigned k = s_timing.n++;
    if (k >= CLOCK_CYCLE_LIMIT || hw.cycles - s_timing.start >= CLOCK_CYCLE_LIMIT || cycle->kind == '-') {
        dc_fail("cannot record a bus cycle", s_timing.routine);
    }
    s_timing.cycles[k].offset = (uint32_t)(hw.cycles - s_timing.start);
    s_timing.cycles[k].addr = cycle->addr;
    s_timing.cycles[k].value = cycle->value;
    s_timing.cycles[k].kind = cycle->kind;
    s_timing.cycles[k].fetch = s_timing.fetch;
    s_timing.cycles[k].done = cycle->done;
    s_timing.fetch = false;
}

/* Timing comes from the same machine and sprite-0 hit, with only the game
 * CPU interpreting, from the routine's start until it reaches stop_pc. Stop
 * before giving up an observer boundary or a nested interrupt. Task code
 * waiting for VBlank sees it in the next frame, so frame_ends may pass; at
 * one more (the read that races the flag's rise at power-on finds it clear,
 * and the wait takes a frame longer) the run is paused where the original
 * was last back at its start with the same S before it, provided it wrote
 * nothing: the next frame calls the routine there again. Only the bus
 * schedule survives, and the original's registers and RAM at the end when
 * asked for. */
ClockEnd dc_clock_measure(uint16_t routine, uint16_t stop_pc, uint8_t stop_s, int frame_ends, uint64_t *duration,
                          CycModRegs *regs, uint8_t *ram)
{
    static CycSnapshot *snap;
    if (!snap && !(snap = cyc_snapshot_new())) {
        dc_fail("out of memory for the timing snapshot", routine);
    }
    cyc_snapshot_take(snap);
    bool frame_done = hw_frame_done, frame_end = hw_frame_end_hit, observe = hw_observe_hit;
    bool traced = cyc_trace_enabled;
    void *file = cyc_trace_file;
    uint64_t hash = cyc_trace_hash;
    uint32_t cycle = cyc_trace_cycle;
    void (*observer)(const CycTraceCycle *) = cyc_trace_cycle_hook;
    void (*return_hook)(void) = cyc_cpu_rts_observer;
    uint8_t muted = cyc_ring_muted;
    cyc_trace_enabled = true;
    cyc_trace_file = NULL;
    cyc_trace_cycle_hook = clock_measure_cycle;
    cyc_cpu_rts_observer = NULL;
    cyc_ring_muted = 1;
    s_timing.routine = routine;
    s_timing.start = cyc_cycle_count();
    s_timing.n = 0;
    uint16_t start_pc = cpu.pc;
    uint8_t start_s = cpu.s;
    bool can_pause = frame_ends > 0;
    unsigned pause_n = 0;
    uint64_t pause_at = 0;
    CycModRegs pause_regs;
    ClockEnd end = CLOCK_MISSED;
    for (;;) {
        if (hw_frame_done) {
            if (hw_observe_hit || !hw_frame_end_hit) {
                end = CLOCK_INTERRUPTED;
                break;
            }
            if (frame_ends-- == 0) {
                end = can_pause && pause_n ? CLOCK_PAUSED : CLOCK_INTERRUPTED;
                for (unsigned k = 0; end == CLOCK_PAUSED && k < pause_n; k++) {
                    if (s_timing.cycles[k].kind == 'W') {
                        end = CLOCK_INTERRUPTED;
                    }
                }
                break;
            }
            hw_frame_done = hw_frame_end_hit = false;
        }
        if (can_pause && s_timing.n && cpu.pc == start_pc && cpu.s == start_s) {
            pause_n = s_timing.n;
            pause_at = cyc_cycle_count() - s_timing.start;
            cyc_mod_regs(&pause_regs);
        }
        if (cpu.jammed || cpu.do_nmi || cpu.do_irq || cpu.do_reset) {
            end = CLOCK_INTERRUPTED;
            break;
        }
        if (cpu.pc == stop_pc && (s_timing.n || !s_timing.leave_start)) {
            end = cpu.s == stop_s ? CLOCK_REACHED : CLOCK_MISSED;
            break;
        }
        if (cyc_cycle_count() - s_timing.start >= CLOCK_CYCLE_LIMIT - 1024) {
            break;
        }
        s_timing.fetch = true;
        cpu_interp_step();
    }
    *duration = cyc_cycle_count() - s_timing.start;
    if (regs) {
        cyc_mod_regs(regs);
    }
    if (ram) {
        memcpy(ram, cyc_cpu_ram(), GAME_RAM_SIZE);
    }
    if (end == CLOCK_PAUSED) {
        s_timing.n = pause_n;
        *duration = pause_at;
        if (regs) {
            *regs = pause_regs;
        }
    }
    cyc_snapshot_restore(snap);
    hw_frame_done = frame_done;
    hw_frame_end_hit = frame_end;
    hw_observe_hit = observe;
    cyc_trace_enabled = traced;
    cyc_trace_file = file;
    cyc_trace_hash = hash;
    cyc_trace_cycle = cycle;
    cyc_trace_cycle_hook = observer;
    cyc_cpu_rts_observer = return_hook;
    cyc_ring_muted = muted;
    return end;
}

/* ---- the original's time for timed routines (decomp_timing.c) ---- */

/* Calls of timed routines whose original met an interrupt or the frame end
 * before it got where the C ends, so their path was not compared. */
static uint64_t s_timing_unmeasured;

void dc_timing_summary(void)
{
    if (s_timing_unmeasured) {
        printf("decomp: timing not compared on %llu calls interrupted before they ended\n",
               (unsigned long long)s_timing_unmeasured);
    }
}

static void show_path(const char *who, bool original, int from, int to)
{
    fprintf(stderr, "[decomp]   %s", who);
    for (int k = from; k < to; k++) {
        if (original) {
            fprintf(stderr, " $%04X@%d", s_timing.cycles[k].addr, k);
        } else {
            fprintf(stderr, " $%04X@%u", dc_path[k].pc, dc_path[k].at);
        }
    }
    fprintf(stderr, "\n");
}

/* The first difference between the C's walked path and the original's run:
 * instruction i of the C's path against the fetch at cycle k of the
 * original's (n cycles in all), or what. */
static void timing_report(const DecompRoutine *rt, const CycModRegs *in, int i, int k, int n, const char *what)
{
    char mark[256];
    fprintf(stderr, "[decomp] TIMING %s ($%04X) at frame %llu, entry A=%02X X=%02X Y=%02X S=%02X P=%02X\n", rt->name,
            rt->addr, (unsigned long long)cyc_ring_frame, in->a, in->x, in->y, in->s, in->p);
    if (what) {
        fprintf(stderr, "[decomp]   %s\n", what);
    } else if (k < n && i < dc_npath) {
        fprintf(stderr,
                "[decomp]   instruction %d: the original fetches $%04X at cycle %d, the C's path $%04X at cycle %u\n",
                i, s_timing.cycles[k].addr, k, dc_path[i].pc, dc_path[i].at);
    } else if (k < n) {
        fprintf(stderr,
                "[decomp]   instruction %d: the original goes on to $%04X at cycle %d, where the C's path ends\n", i,
                s_timing.cycles[k].addr, k);
    } else if (i < dc_npath) {
        fprintf(stderr, "[decomp]   instruction %d: the original ends at cycle %d, the C's path goes on to $%04X\n", i,
                n, dc_path[i].pc);
    } else {
        fprintf(stderr, "[decomp]   the original takes %d cycles, the C's path %llu\n", n,
                (unsigned long long)dc_cycles);
    }
    int at = i < dc_npath ? i : dc_npath - 1;
    if (!what && i > 0 && i <= dc_npath && k > 0 && (i == dc_npath || dc_path[i].pc == s_timing.cycles[k].addr)) {
        fprintf(stderr,
                "[decomp]   the instruction at $%04X takes other cycles: a page crossing needs DC_EA, a branch DC_BR\n",
                dc_path[i - 1].pc);
    }
    fprintf(stderr, "[decomp]   the C's path there follows %s\n",
            dc_mark_text(at >= 0 ? (int16_t)dc_path[at].mark : -1, mark, sizeof mark));
    if (!what) {
        int fetches[8], nf = 0;
        for (int c = k; c >= 0 && nf < 5; c--) {
            if (c < n && s_timing.cycles[c].fetch) {
                fetches[nf++] = c;
            }
        }
        fprintf(stderr, "[decomp]   original:");
        for (int f = nf - 1; f >= 0; f--) {
            fprintf(stderr, " $%04X@%d", s_timing.cycles[fetches[f]].addr, fetches[f]);
        }
        fprintf(stderr, "\n");
        show_path("the C's: ", false, i > 4 ? i - 4 : 0, i + 1 < dc_npath ? i + 1 : dc_npath);
    }
    fflush(stderr);
    exit(3);
}

/* The instruction of the C's path that cycle falls in. */
static int step_at(uint32_t cycle)
{
    int i = 0;
    while (i + 1 < dc_npath && dc_path[i + 1].at <= cycle) {
        i++;
    }
    return i;
}

/* Check mode: the C's walked path against the original run on the machine
 * from the same state (dc_clock_measure, restored afterwards) until it gets
 * where the C ends: each instruction's address and fetch cycle, the total,
 * then each device access (address, direction, value and cycle). Cycles
 * count without DMA, which the bus adds where the original's would come. */
void dc_timing_check(const DecompRoutine *rt, const CycModRegs *in)
{
    uint16_t stop_pc;
    uint8_t stop_s;
    if (dc_handoff_set) {
        stop_pc = dc_handoff;
        stop_s = dc_cur_s;
    } else if (rt->interrupt) {
        s_timing_unmeasured++;
        return;
    } else {
        stop_s = (uint8_t)(in->s + rt->locals + 2);
        const uint8_t *ram = cyc_cpu_ram();
        stop_pc = (uint16_t)((ram[0x100 | (uint8_t)(stop_s - 1)] | ram[0x100 | stop_s] << 8) + 1);
    }
    uint64_t duration;
    s_timing.leave_start = true;
    ClockEnd end = dc_clock_measure(rt->addr, stop_pc, stop_s, 0, &duration, NULL, NULL);
    s_timing.leave_start = false;
    if (end != CLOCK_REACHED) {
        s_timing_unmeasured++;
        return;
    }
    int n = (int)s_timing.n, i = 0, k = 0;
    for (; k < n; k++) {
        if (!s_timing.cycles[k].fetch) {
            continue;
        }
        if (i == dc_npath || dc_path[i].pc != s_timing.cycles[k].addr || dc_path[i].at != (uint32_t)k) {
            timing_report(rt, in, i, k, n, NULL);
        }
        i++;
    }
    if (i != dc_npath || dc_cycles != (uint64_t)n) {
        timing_report(rt, in, i, k, n, NULL);
    }
    int j = 0;
    for (k = 0; k < n; k++) {
        uint16_t addr = s_timing.cycles[k].addr;
        bool write = s_timing.cycles[k].kind == 'W';
        uint8_t value = s_timing.cycles[k].value;
        if (addr < 0x2000 || (addr >= 0x4020 && !(write && addr >= 0x8000))) {
            continue;
        }
        if (j == dc_nops || dc_ops[j].addr != addr || dc_ops[j].write != write || (write && dc_ops[j].value != value) ||
            dc_op_at[j] != (uint32_t)k) {
            char msg[200];
            if (j == dc_nops) {
                snprintf(msg, sizeof msg, "bus operation %d: the original %s $%04X at cycle %d, the C makes no more", j,
                         write ? "writes" : "reads", addr, k);
            } else {
                snprintf(msg, sizeof msg,
                         "bus operation %d: the original %s $%04X=%02X at cycle %d, the C %s $%04X=%02X at %u", j,
                         write ? "writes" : "reads", addr, value, k, dc_ops[j].write ? "writes" : "reads",
                         dc_ops[j].addr, dc_ops[j].value, dc_op_at[j]);
            }
            timing_report(rt, in, step_at(j < dc_nops ? dc_op_at[j] : (uint32_t)k), 0, n, msg);
        }
        j++;
    }
    if (j != dc_nops) {
        char msg[160];
        snprintf(msg, sizeof msg, "bus operation %d: the C %s $%04X at cycle %u, the original makes no more", j,
                 dc_ops[j].write ? "writes" : "reads", dc_ops[j].addr, dc_op_at[j]);
        timing_report(rt, in, step_at(dc_op_at[j]), 0, n, msg);
    }
}

/* Report the first bus operation that could not be placed at its original
 * cycle. A mismatch must not silently turn into a replace fallback. */
static void timing_fail(unsigned k, int op, uint16_t routine)
{
    fprintf(stderr, "[decomp] $%04X timing: original cycle %u %c $%04X=%02X, C bus operation %d of %d\n", routine,
            s_timing.cycles[k].offset, s_timing.cycles[k].kind, s_timing.cycles[k].addr, s_timing.cycles[k].value, op,
            dc_nops);
    if (op < dc_nops) {
        fprintf(stderr, "[decomp]   C %c $%04X=%02X mask=%02X\n", dc_ops[op].write ? 'W' : 'R', dc_ops[op].addr,
                dc_ops[op].value, dc_ops[op].wait);
    }
    dc_fail("bus operation does not match the original", routine);
}

/* A device access of the measured original: writes, and the reads of an
 * instruction's last cycle below $6000. */
static bool clock_device(unsigned k)
{
    uint16_t addr = s_timing.cycles[k].addr;
    return addr >= 0x2000 && (s_timing.cycles[k].kind == 'W' || (addr < 0x6000 && s_timing.cycles[k].done));
}

/* Drive the original's non-device bus traffic without storing any of its
 * RAM results. DMA sees the C's RAM, and RDY generates the same stalls as
 * before; each CPU access checks its offset after those stalls. A C wait
 * consumes as many original reads as it takes to satisfy its mask. */
void dc_clock_replay(uint64_t duration, uint16_t routine, int ops)
{
    uint64_t start = cyc_cycle_count();
    int op = 0;
    for (unsigned k = 0; k < s_timing.n; k++) {
        uint16_t addr = s_timing.cycles[k].addr;
        uint8_t value = s_timing.cycles[k].value;
        bool write = s_timing.cycles[k].kind == 'W';
        bool device = clock_device(k);
        if (device) {
            if (op == dc_nops || dc_ops[op].addr != addr || dc_ops[op].write != write ||
                (write && dc_ops[op].value != value)) {
                timing_fail(k, op, routine);
            }
            if (write) {
                value = dc_ops[op].value;
            }
        }
        hw_cycle_start(addr, write ? HW_WRITE : s_timing.cycles[k].fetch ? HW_FETCH : HW_READ);
        if (cyc_cycle_count() - start != s_timing.cycles[k].offset) {
            timing_fail(k, op, routine);
        }
        if (write && device) {
            hw_write(addr, value);
        } else if (write) {
            hw.data_bus = hw.internal_bus = value;
            if (cyc_trace_enabled) {
                cyc_trace_access(addr, value, true);
            }
        } else if (addr < 0x2000) {
            (void)hw_read_code_ram(addr, value);
        } else {
            uint8_t read = hw_read(addr);
            if (device && read != value) {
                timing_fail(k, op, routine);
            }
            value = read;
        }
        if (device && (!dc_ops[op].wait || (value & dc_ops[op].wait) == dc_ops[op].value)) {
            op++;
        }
        hw_cycle_finish(s_timing.cycles[k].done);
    }
    if (op != ops || cyc_cycle_count() - start != duration) {
        dc_fail("the routine did not use its full bus schedule", routine);
    }
}

/* Check mode: the C's bus operations against the measured original's, placed
 * as dc_clock_replay places them, ops of them made. Returns the original's
 * device writes. */
int dc_clock_match(uint16_t routine, int ops)
{
    int op = 0, writes = 0;
    for (unsigned k = 0; k < s_timing.n; k++) {
        if (!clock_device(k)) {
            continue;
        }
        uint16_t addr = s_timing.cycles[k].addr;
        uint8_t value = s_timing.cycles[k].value;
        bool write = s_timing.cycles[k].kind == 'W';
        if (op == dc_nops || dc_ops[op].addr != addr || dc_ops[op].write != write ||
            (write && dc_ops[op].value != value)) {
            timing_fail(k, op, routine);
        }
        writes += write;
        if (!dc_ops[op].wait || (value & dc_ops[op].wait) == dc_ops[op].value) {
            op++;
        }
    }
    if (op != ops) {
        fprintf(stderr, "[decomp] $%04X timing: the original ends at C bus operation %d of %d, not %d\n", routine, op,
                dc_nops, ops);
        dc_fail("bus operation does not match the original", routine);
    }
    return writes;
}
