/*
 * decomp_native.c - the native runtime's backend of the decompilation layer
 * (decomp_backend.h, docs/decompilation.md): the decompiled routines alone,
 * in place of the recompiled ones. There is one mode, replace; nothing is
 * compared here (the cycle host's check and replace, decomp_cycle.c, are the
 * proof that the C does what the original does).
 *
 * At a routine's site its C runs on an image of RAM (dc_run_c). An original
 * routine the C calls (Decomp_Call) runs in an isolated call on the machine
 * as the image has it (native_isolate_begin), as in the cycle host; no other
 * original code of the routine runs. Then the image's changes are stored (the
 * stack below the routine's return excepted), the C's device operations made
 * on the bus and its contract's registers set, and the routine returns as its
 * RTS would. Task code goes on where its C ends: the routine it hands off to,
 * or its RTS, after its device stores. A call whose C gives it to the
 * original (Decomp_RunOriginal) runs the original.
 *
 * The C's cycles are not the original's, so where VBlank falls in a logic step
 * changes: the player allows the mode only with No slowdown, where a step's
 * result does not depend on its cycle count (docs/native-runtime.md).
 */
#include "decomp/decomp_backend.h"

#include "native/native_runtime.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "mod_function_hooks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_GAP 3

static bool s_scope;
static int s_ops_in_scope;

uint64_t dc_frame(void)
{
    return native_frame_number();
}

void dc_c_begin(void)
{
    s_scope = false;
    s_ops_in_scope = 0;
}

void dc_c_end(void)
{
    if (s_scope) {
        native_isolate_end();
    }
    s_scope = false;
}

Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    return Decomp_CallP(routine, a, x, y, dc_entry_p);
}

/* The original routine in the C's isolated call, from the image the C works on
 * and the bus operations it made so far. */
Regs Decomp_CallP(uint16_t routine, uint8_t a, uint8_t x, uint8_t y, uint8_t p)
{
    if (!s_scope) {
        if (!native_isolate_begin()) {
            dc_fail("cannot open an isolated call", routine);
        }
        s_scope = true;
    }
    const uint8_t *ram = native_cpu_ram();
    for (uint16_t i = 0; i < GAME_RAM_SIZE; i++) {
        if (ram[i] != dc_image[i]) {
            native_poke(i, dc_image[i]);
        }
    }
    dc_play_ops(s_ops_in_scope, (uint64_t)(dc_nops - s_ops_in_scope) * OP_GAP, routine);
    s_ops_in_scope = dc_nops;
    NativeRegs r = { a, x, y, dc_cur_s, p };
    if (!native_isolated_call(routine, &r)) {
        dc_fail("an original routine called from C did not return", routine);
    }
    memcpy(dc_image, native_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

/* A read the C needs the value of, made on the machine after the C's
 * operations before it. */
uint8_t Decomp_BusLoad(uint16_t addr)
{
    if (dc_top->task) {
        dc_fail("task code reads a device", addr);
    }
    dc_c_end();
    dc_loads++;
    dc_play_ops(dc_played, (uint64_t)(dc_nops - dc_played + 1) * OP_GAP, dc_top->addr);
    uint8_t value = cpu_read(addr, 0);
    dc_record(addr, value, false);
    dc_played = dc_nops;
    return value;
}

void Decomp_Enter(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    (void)a;
    (void)x;
    (void)y;
    if (!dc_top_entered && routine == dc_top->addr) {
        dc_top_entered = true;
        return;
    }
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->addr == routine && (routine >= 0xC000 || dc_routine[i]->bank == dc_c_bank)) {
            if (dc_routine[i]->task) {
                dc_stats[i].calls++;
            }
            return;
        }
    }
    dc_fail("Decomp_Enter names a routine that is in no table", routine);
}

static CycModRegs regs(void)
{
    return (CycModRegs){ cpu.a, cpu.x, cpu.y, cpu.s, cpu_get_p(0), cpu.pc };
}

static void store_image(bool (*keep)(uint16_t a, uint8_t s), uint8_t s)
{
    const uint8_t *ram = native_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (keep(a, s) && ram[a] != dc_image[a]) {
            native_poke(a, dc_image[a]);
        }
    }
}

static int replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return 0;
    }
    store_image(dc_live, in->s);
    uint8_t flags = P_N | P_V | P_Z | P_C;
    cpu.a = c.a;
    cpu.x = c.x;
    cpu.y = c.y;
    cpu_set_p((uint8_t)((c.p & flags) | (in->p & ~flags)));
    dc_play_ops(dc_played, (uint64_t)(dc_nops - dc_played) * OP_GAP, rt->addr);
    return 1;
}

/* Page 1 above the C's final S (its return address included), and all of RAM
 * outside the stack. */
static bool task_kept(uint16_t a, uint8_t s)
{
    return (a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_FLOOR || (uint8_t)a > s;
}

/* Task code: the C's results stored and its device operations and stores
 * made; then the CPU goes on where the C ends. False when the original runs
 * instead. */
static bool task_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return false;
    }
    store_image(task_kept, dc_cur_s);
    const uint8_t *ram = native_cpu_ram();
    uint8_t s = dc_cur_s;
    uint16_t pc = dc_handoff;
    if (!dc_handoff_set) {
        pc = (uint16_t)((ram[0x100 | (uint8_t)(s + 1)] | ram[0x100 | (uint8_t)(s + 2)] << 8) + 1);
        s = (uint8_t)(s + 2);
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    cpu.a = c.a;
    cpu.x = c.x;
    cpu.y = c.y;
    cpu.s = s;
    cpu_set_p(dc_handoff_set ? in->p : (uint8_t)((c.p & flags) | (in->p & ~flags)));
    dc_play_ops(dc_played, (uint64_t)(dc_nops - dc_played) * OP_GAP, rt->addr);
    for (int k = 0; k < dc_stores; k++) {
        cpu_write(dc_store[k].addr, dc_store[k].value, 0);
    }
    dc_stats[i].compared++;
    cpu.pc = pc;
    return true;
}

/* The RTS observer: starts the C of a resume point the RTS returned to (and of
 * the next one, when that C returns). */
static void on_return(void)
{
    if (native_isolated()) {
        return;
    }
    for (;;) {
        uint16_t pc = cpu.pc;
        if (!(dc_resume[pc >> 3] & (1u << (pc & 7)))) {
            return;
        }
        CycModRegs in = regs();
        if (!task_replace(dc_find_routine(pc, true), &in) || dc_handoff_set) {
            return;
        }
    }
}

int dc_on_entry(uint16_t addr)
{
    if (native_isolated()) {
        return 0;
    }
    for (int i = 0; i < dc_count; i++) {
        if (!dc_at_entry(dc_routine[i], addr) || !dc_routine[i]->site_id) {
            continue;
        }
        CycModRegs in = regs();
        if (dc_routine[i]->task) {
            task_replace(i, &in);
            return 0;
        }
        return replace(i, &in);
    }
    return 0;
}

bool Decomp_SetMode(DecompMode mode)
{
    static bool registered;
    if (mode == DECOMP_CHECK) {
        return false;
    }
    if (!registered && mode != DECOMP_OFF) {
        registered = true;
        atexit(dc_report_stats);
    }
    dc_mode = mode;
    const char *only = getenv("DT_DECOMP_ONLY");
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->site_id) {
            bool on = mode == DECOMP_REPLACE && (!only || strstr(only, dc_routine[i]->name));
            nes_mod_set_function_hook_enabled(dc_routine[i]->site_id, on);
        }
    }
    cyc_cpu_rts_observer = mode == DECOMP_REPLACE ? on_return : NULL;
    return true;
}
