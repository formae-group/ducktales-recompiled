/*
 * decomp_cycle_task.c - task code in the cycle host's backend of the
 * decompilation layer (decomp_cycle.c has the whole account; decomp.h and
 * docs/DECOMPILATION.md "Task code"): the original task routine run
 * isolated at its true stack depth to where the C ends, check and replace of
 * its results, polling task code on the running clock, and the RTS observer
 * that starts the C of a resume point. It owns no setting or option.
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

/* nesrecomp counts the device stores made inside isolated calls
 * (runner/cyc/hw_machine.c); task code compares them. */
extern uint64_t hw_isolated_io_writes;

typedef struct {
    uint16_t start, stop_pc;
    uint8_t start_s, call_s, stop_s, stop_bank;
    uint8_t ret[2]; /* the isolated call's return address, where the RTS observer left it */
    bool fixup, stop_armed, stopped, returned, rti;
    CycModRegs regs;
    uint64_t entry_cycles, cycles, io;
} TaskRun;

static TaskRun s_run;
bool dc_run_active;

/* A site reached inside the isolated run of a task routine's original. */
int dc_task_isolated_site(uint16_t addr)
{
    CycModRegs r;
    cyc_mod_regs(&r);
    if (!s_run.stop_armed || s_run.stopped || addr != s_run.stop_pc || r.s != s_run.stop_s ||
        hw_isolated_cycles == s_run.entry_cycles) {
        return 0;
    }
    if (addr < 0xC000 && hw_prg_bank4(addr) >> 2 != s_run.stop_bank) {
        return 0;
    }
    s_run.stopped = true;
    s_run.regs = r;
    cyc_mod_poke((uint16_t)(0x100 | (uint8_t)(s_run.call_s - 1)), s_run.ret[0]);
    cyc_mod_poke((uint16_t)(0x100 | s_run.call_s), s_run.ret[1]);
    r.s = (uint8_t)(s_run.call_s - 2);
    cyc_mod_set_regs(&r);
    return 1;
}

static bool s_fresh;

/* The last isolated run of a timed routine, reused by the next call with the
 * same registers, bank and RAM (the scheduler's idle passes repeat it). */
static struct {
    const DecompRoutine *rt;
    CycModRegs in;
    unsigned bank;
    uint8_t ram[GAME_RAM_SIZE], orig[GAME_RAM_SIZE];
    TaskRun run;
} s_timed;

/* The original task routine alone, from its start to its RTS or, when the C
 * handed off, to its arrival at the same routine at the same depth; its RAM
 * into dc_orig, the rest into s_run. The call enters at an RTS: the observer
 * moves its return to the start, with S as the routine has it there (the
 * RTS's cycles are not the routine's). A TXS that raises S past the synthetic
 * caller (as the reset vector does) moves the sentinel above the new stack. */
static void run_task_original(const DecompRoutine *rt, const CycModRegs *in)
{
    const uint8_t *ram = cyc_cpu_ram();
    unsigned bank = hw_prg_bank4(0x8000);
    s_fresh = false;
    if (rt->timed && s_timed.rt == rt && s_timed.in.a == in->a && s_timed.in.x == in->x && s_timed.in.y == in->y &&
        s_timed.in.s == in->s && s_timed.in.p == in->p && s_timed.bank == bank &&
        !memcmp(s_timed.ram, ram, GAME_RAM_SIZE)) {
        s_run = s_timed.run;
        memcpy(dc_orig, s_timed.orig, GAME_RAM_SIZE);
        return;
    }
    if (rt->timed) {
        s_timed.rt = rt;
        s_timed.in = *in;
        s_timed.bank = bank;
        memcpy(s_timed.ram, ram, GAME_RAM_SIZE);
    }
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) {
        dc_fail("cannot open an isolated scope", rt->addr);
    }
    cyc_mod_allow_isolated_hooks(true);
    memset(&s_run, 0, sizeof s_run);
    dc_damage_replayed = 0;
    s_run.start = rt->addr;
    s_run.start_s = in->s;
    s_run.call_s = (uint8_t)(in->s + rt->locals + 2);
    if ((int8_t)(dc_rebased_s - s_run.call_s) > 0) {
        s_run.call_s = dc_rebased_s;
    }
    s_run.fixup = true;
    s_run.rti = rt->interrupt;
    s_run.stop_armed = dc_handoff_set;
    s_run.stop_pc = dc_handoff;
    s_run.stop_s = dc_cur_s;
    s_run.stop_bank = dc_handoff_bank;
    dc_run_active = true;
    uint64_t io = hw_isolated_io_writes;
    cyc_mod_stats(&before);
    CycModRegs r = *in;
    r.s = s_run.call_s;
    s_fresh = true;
    s_run.returned = cyc_mod_call(DT_CODE_RTS, &r) && !s_run.fixup;
    cyc_mod_stats(&after);
    dc_run_active = false;
    s_run.io = hw_isolated_io_writes - io;
    s_run.cycles = after.cycles - before.cycles - s_run.entry_cycles;
    if (!s_run.stopped) {
        s_run.regs = r;
    }
    memcpy(dc_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    cyc_mod_isolate_end();
    if (rt->timed) {
        s_timed.run = s_run;
        memcpy(s_timed.orig, dc_orig, GAME_RAM_SIZE);
    }
}

/* Bytes both runs must agree on: page 1 only above the C's final S, and never
 * the slot of the isolated call's return address (which wraps to the bottom of
 * page 1 for the main task's start, entered with S = $FF). */
static bool task_live(uint16_t a)
{
    if ((a & 0xFF00) != 0x0100) {
        return true;
    }
    uint8_t off = (uint8_t)a;
    if (off == s_run.call_s || off == (uint8_t)(s_run.call_s - 1)) {
        return false;
    }
    if (a < DT_ADDR_STACK_FLOOR || dc_keep_stack_ram) {
        return true;
    }
    return dc_handoff_set ? off > dc_cur_s : off >= dc_cur_s + 3u;
}

static bool task_same(const DecompRoutine *rt, const Regs *c)
{
    if (!s_run.returned || dc_handoff_set != s_run.stopped || s_run.io != (uint64_t)dc_stores) {
        return false;
    }
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (task_live(a) && dc_orig[a] != dc_image[a]) {
            return false;
        }
    }
    const CycModRegs *o = &s_run.regs;
    uint8_t out = dc_handoff_set ? dc_handoff_uses : rt->out;
    if ((out & OUT_A) && o->a != c->a) {
        return false;
    }
    if ((out & OUT_X) && o->x != c->x) {
        return false;
    }
    if ((out & OUT_Y) && o->y != c->y) {
        return false;
    }
    uint8_t p_mask = P_N | P_V | P_Z | P_C;
    if (dc_handoff_set) {
        p_mask |= P_I | P_D;
    }
    if ((out & OUT_P) && ((o->p ^ c->p) & p_mask)) {
        return false;
    }
    return true;
}

static void task_report(const DecompRoutine *rt, const CycModRegs *in, const Regs *c)
{
    const CycModRegs *o = &s_run.regs;
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu, entry A=%02X X=%02X Y=%02X S=%02X P=%02X\n", rt->name,
            rt->addr, (unsigned long long)cyc_ring_frame, in->a, in->x, in->y, in->s, in->p);
    if (dc_handoff_set) {
        fprintf(stderr, "[decomp]   the C hands off to $%04X at S=%02X; the original %s\n", dc_handoff, dc_cur_s,
                s_run.stopped    ? "got there"
                : s_run.returned ? "returned instead"
                                 : "did not return or get there");
    } else {
        fprintf(stderr, "[decomp]   the C returns; the original %s\n",
                s_run.stopped    ? "stopped"
                : s_run.returned ? "returned too"
                                 : "did not return");
    }
    fprintf(stderr, "[decomp]   device writes original %llu decompiled %d\n", (unsigned long long)s_run.io, dc_stores);
    if (rt->timed) {
        fprintf(stderr, "[decomp]   cycles original %llu decompiled %llu\n", (unsigned long long)s_run.cycles,
                (unsigned long long)dc_cycles);
    }
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!task_live(a) || dc_orig[a] == dc_image[a]) {
            continue;
        }
        if (shown++ < 24) {
            fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, dc_orig[a], dc_image[a]);
        }
    }
    if (shown > 24) {
        fprintf(stderr, "[decomp]   ... %d bytes differ\n", shown);
    }
    fprintf(stderr,
            "[decomp]   registers original A=%02X X=%02X Y=%02X P=%02X decompiled A=%02X X=%02X Y=%02X P=%02X\n", o->a,
            o->x, o->y, o->p, c->a, c->x, c->y, c->p);
    fflush(stderr);
    exit(3);
}

static void poll_check(int i, const CycModRegs *in, const Regs *c);
static bool poll_replace(int i, const CycModRegs *in, const Regs *c);

/* Check mode: the polling routine last compared, whose original runs for real
 * until this cycle; its poll looping back to the site is not a new call. */
static struct {
    int routine;
    uint64_t until;
} s_poll_run = { -1, 0 };

void dc_task_check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    if (rt->polls && i == s_poll_run.routine && cyc_cycle_count() < s_poll_run.until) {
        return;
    }
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return;
    }
    if (rt->polls) {
        poll_check(i, in, &c);
        return;
    }
    run_task_original(rt, in);
    if (rt->interrupt && !dc_handoff_set) {
        c.p = dc_image[0x100 | (uint8_t)(dc_cur_s + 1)];
    }
    if (!task_same(rt, &c)) {
        task_report(rt, in, &c);
    }
    if (rt->timed && s_fresh) {
        dc_timing_check(rt, in);
    } else if (rt->timed && dc_cycles != s_run.cycles) {
        dc_fail("TIMING: the C's path takes other cycles than the original's last run with this entry", rt->addr);
    }
    dc_stats[i].compared++;
    dc_nested_compared();
}

/* Replace mode: the C's results stored, its device writes made and the
 * original's cycles spent; then the CPU goes on where the C ends (the routine
 * it hands off to, or its RTS). False when the original runs instead. */
bool dc_task_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return false;
    }
    if (rt->polls) {
        return poll_replace(i, in, &c);
    }
    run_task_original(rt, in);
    if (!s_run.returned || dc_handoff_set != s_run.stopped) {
        dc_fail("the original task routine did not end where the C does in an isolated call", rt->addr);
    }
    if (rt->timed && !dc_nops && dc_cycles != s_run.cycles) {
        dc_fail("the cycles the C states are not the original's", rt->addr);
    }
    bool rti = rt->interrupt && !dc_handoff_set;
    uint64_t debt = rti ? dc_nmi_debt : 0;
    if (dc_spans_frame_end(s_run.cycles + debt) || dc_crosses_vblank(s_run.cycles + debt)) {
        dc_stats[i].original++;
        return false;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        /* The routine's own return address is not compared (the isolated run
         * has its own there), but a C that popped it and pushed another (the
         * row change) leaves the bytes the original would. */
        bool own_return = (a & 0xFF00) == 0x0100 &&
                          ((uint8_t)a == s_run.call_s || (uint8_t)a == (uint8_t)(s_run.call_s - 1));
        if ((task_live(a) || own_return) && ram[a] != dc_image[a]) {
            cyc_mod_poke(a, dc_image[a]);
        }
    }
    uint8_t s = dc_cur_s;
    uint16_t pc = dc_handoff;
    if (rti) {
        c.p = ram[0x100 | (uint8_t)(s + 1)];
        pc = (uint16_t)(ram[0x100 | (uint8_t)(s + 2)] | ram[0x100 | (uint8_t)(s + 3)] << 8);
        s = (uint8_t)(s + 3);
    } else if (!dc_handoff_set) {
        pc = (uint16_t)((ram[0x100 | (uint8_t)(s + 1)] | ram[0x100 | (uint8_t)(s + 2)] << 8) + 1);
        s = (uint8_t)(s + 2);
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    uint8_t p = rti              ? c.p
                : dc_handoff_set ? (dc_handoff_uses & OUT_P ? c.p : in->p)
                                 : (uint8_t)((c.p & flags) | (in->p & ~flags));
    CycModRegs set = { c.a, c.x, c.y, s, p, 0 };
    if (dc_handoff_set) {
        if (!(dc_handoff_uses & OUT_A)) {
            set.a = s_run.regs.a;
        }
        if (!(dc_handoff_uses & OUT_X)) {
            set.x = s_run.regs.x;
        }
        if (!(dc_handoff_uses & OUT_Y)) {
            set.y = s_run.regs.y;
        }
    }
    cyc_mod_set_regs(&set);
    for (int k = 0; k < dc_stores; k++) {
        cpu_write(dc_store[k].addr, dc_store[k].value, 0);
    }
    dc_nmi_debt -= debt;
    dc_spend_cycles(rt->addr, (s_run.cycles > (uint64_t)dc_stores ? s_run.cycles - (uint64_t)dc_stores : 1) + debt);
    dc_stats[i].compared++;
    dc_nested_compared();
    cpu.pc = pc;
    return true;
}

/* The RTS observer: inside a task routine's isolated run it moves the entry
 * RTS to the routine's start; on the machine it starts the C of a resume
 * point the RTS returned to (and of the next one, when that C returns). */
void dc_on_return(void)
{
    if (cyc_mod_isolated()) {
        if (dc_run_active && s_run.fixup) {
            s_run.fixup = false;
            s_run.entry_cycles = hw_isolated_cycles;
            s_run.ret[0] = cyc_cpu_ram()[0x100 | (uint8_t)(s_run.call_s - 1)];
            s_run.ret[1] = cyc_cpu_ram()[0x100 | s_run.call_s];
            if (s_run.rti) {
                uint16_t ret = (uint16_t)((s_run.ret[0] | s_run.ret[1] << 8) + 1);
                cyc_mod_poke((uint16_t)(0x100 | (uint8_t)(s_run.call_s - 1)), (uint8_t)ret);
                cyc_mod_poke((uint16_t)(0x100 | s_run.call_s), (uint8_t)(ret >> 8));
            }
            CycModRegs r;
            cyc_mod_regs(&r);
            r.s = s_run.start_s;
            cyc_mod_set_regs(&r);
            cpu.pc = s_run.start;
        }
        return;
    }
    for (;;) {
        uint16_t pc = cpu.pc;
        if (!(dc_resume[pc >> 3] & (1u << (pc & 7)))) {
            return;
        }
        int i = dc_find_routine(pc, true);
        if (i < 0) {
            return;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
        if (dc_mode == DECOMP_CHECK) {
            dc_task_check(i, &in);
            return;
        }
        if (!dc_task_replace(i, &in) || dc_handoff_set) {
            return;
        }
    }
}

/* Where task code ends: the routine it hands off to, or the return of its
 * RTS (S after it). */
static uint16_t task_end(uint8_t *s)
{
    *s = dc_cur_s;
    if (dc_handoff_set) {
        return dc_handoff;
    }
    *s = (uint8_t)(dc_cur_s + 2);
    return (uint16_t)((dc_image[0x100 | (uint8_t)(dc_cur_s + 1)] | dc_image[0x100 | (uint8_t)(dc_cur_s + 2)] << 8) + 1);
}

/* Polling task code (DecompRoutine.polls): its original runs on a snapshot of
 * the running machine, the clock with it, to where the C ends; RAM, the
 * registers and the device operations are compared there. The original then
 * runs for real. */
static void poll_check(int i, const CycModRegs *in, const Regs *c)
{
    const DecompRoutine *rt = dc_routine[i];
    uint8_t s;
    uint16_t pc = task_end(&s);
    if (pc < 0xC000) {
        dc_fail("polling task code ends below $C000", rt->addr);
    }
    memset(&s_run, 0, sizeof s_run);
    s_run.call_s = (uint8_t)(in->s + rt->locals + 2);
    uint64_t duration;
    ClockEnd end = dc_clock_measure(rt->addr, pc, s, 1, &duration, &s_run.regs, dc_orig);
    s_poll_run.routine = i;
    s_poll_run.until = cyc_cycle_count() + duration;
    if (end == CLOCK_INTERRUPTED) {
        dc_stats[i].original++;
        return;
    }
    if (end == CLOCK_PAUSED) {
        dc_clock_match(rt->addr, 0);
        dc_stats[i].compared++;
        return;
    }
    s_run.returned = end == CLOCK_REACHED;
    s_run.stopped = s_run.returned && dc_handoff_set;
    if (s_run.returned) {
        s_run.io = (uint64_t)dc_clock_match(rt->addr, dc_nops);
    }
    if (!task_same(rt, c)) {
        task_report(rt, in, c);
    }
    dc_stats[i].compared++;
    dc_nested_compared();
}

/* Replace mode: the C's RAM, then the original's bus schedule with the C's
 * device operations at the original's cycles (a wait as many reads as the
 * original made); the CPU goes on where the C ends. False when the original
 * runs instead (an interrupt or an observation point inside). */
static bool poll_replace(int i, const CycModRegs *in, const Regs *c)
{
    const DecompRoutine *rt = dc_routine[i];
    uint8_t s;
    uint16_t pc = task_end(&s);
    if (pc < 0xC000) {
        dc_fail("polling task code ends below $C000", rt->addr);
    }
    memset(&s_run, 0, sizeof s_run);
    s_run.call_s = (uint8_t)(in->s + rt->locals + 2);
    uint64_t duration;
    ClockEnd end = dc_clock_measure(rt->addr, pc, s, 1, &duration, &s_run.regs, NULL);
    if (end == CLOCK_INTERRUPTED) {
        dc_stats[i].original++;
        return false;
    }
    if (end == CLOCK_MISSED) {
        dc_fail("the original task routine did not end where the C does", rt->addr);
    }
    if (end == CLOCK_PAUSED) {
        dc_clock_replay(duration, rt->addr, 0);
        CycModRegs set = s_run.regs;
        cyc_mod_set_regs(&set);
        dc_stats[i].compared++;
        cpu.pc = rt->addr;
        return true;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        bool own_return = (a & 0xFF00) == 0x0100 &&
                          ((uint8_t)a == s_run.call_s || (uint8_t)a == (uint8_t)(s_run.call_s - 1));
        if ((task_live(a) || own_return) && ram[a] != dc_image[a]) {
            cyc_mod_poke(a, dc_image[a]);
        }
    }
    dc_clock_replay(duration, rt->addr, dc_nops);
    const CycModRegs *o = &s_run.regs;
    uint8_t uses = dc_handoff_set ? dc_handoff_uses : OUT_A | OUT_X | OUT_Y;
    uint8_t flags = P_N | P_V | P_Z | P_C;
    uint8_t p = dc_handoff_set ? (uses & OUT_P ? c->p : o->p) : (uint8_t)((c->p & flags) | (o->p & ~flags));
    CycModRegs set = { uses & OUT_A ? c->a : o->a, uses & OUT_X ? c->x : o->x, uses & OUT_Y ? c->y : o->y, s, p, 0 };
    cyc_mod_set_regs(&set);
    dc_stats[i].compared++;
    dc_nested_compared();
    cpu.pc = pc;
    return true;
}
