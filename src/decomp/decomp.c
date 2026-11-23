/*
 * decomp.c - runs the decompiled routines in place of the recompiled ones
 * (decomp.h, docs/decompilation.md).
 *
 * Every routine owns one [[mod_function_hook]] site at its entry
 * (config/game.toml). The sites are enabled together, in check or replace
 * mode; with --decomp off (the default) none is, and the generated code runs
 * the original instructions with nothing changed.
 *
 * The C of a routine always runs the same way: on an image of CPU RAM taken
 * at the routine's entry. An original routine it calls (Decomp_Call) runs in
 * an isolated scope opened on the first such call, with the image stored into
 * the scope's scratch machine before and read back after. At the end the
 * scope is closed, which puts the machine back exactly as it was at entry.
 *
 * Either mode also runs the original routine alone (cyc_mod_call in its own
 * scope) and keeps its RAM, registers and cycle count.
 *
 *   check    the C and the original must agree on all of RAM except the
 *            stack below the routine's return (dead after its RTS) and on the
 *            registers of the routine's contract. The original then runs for
 *            real.
 *   replace  the image's changes are stored into the machine with
 *            cyc_mod_poke (the dead stack excepted), the contract's registers
 *            set, and the CPU spends the original's cycle count as reads of
 *            the routine's first byte, polling interrupts in the last one as
 *            the RTS would; then the scheduler returns from the routine as
 *            its RTS would. Interrupts and frames therefore come at the same
 *            points of the game's work as on the console (an NMI that the
 *            original would take inside the routine is taken right after it).
 *
 * A call whose C ends in Decomp_RunOriginal runs the original in both modes
 * and is not compared: the paths that use it lead into code (a row scroll, a
 * message box) that waits for the hardware.
 *
 * Task code (decomp.h) runs the same way, with three differences. The
 * original runs at the routine's true stack depth: the isolated call's return
 * address takes the place of the routine's own (at S + locals + 1), and a
 * site with locals moves S back down at its first instruction, so the values
 * the routine keeps on the stack are where it reads them. When the C hands
 * off, the original is stopped where it reaches the same routine at the same
 * depth (hooks are allowed in that isolated call, and a stop returns it to
 * its caller); check compares RAM, the stack down to that depth, A, X, Y and
 * the number of device writes, and replace stores the C's results, makes its
 * device writes, spends the original's cycles up to the stop and returns into
 * the routine. Inside an isolated call every other site lets the original
 * run.
 */
#include "decomp/decomp.h"

#include "ducktales/events.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "hw_internal.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* nesrecomp counts the device stores made inside isolated calls
 * (runner/cyc/hw_machine.c); task code compares them. */
extern uint64_t hw_isolated_io_writes;

#define MAX_ROUTINES 160
#define MAX_STORES 8

typedef struct {
    uint64_t calls, compared, original;
} RoutineStats;

static const DecompRoutine *s_routine[MAX_ROUTINES];
static RoutineStats s_stats[MAX_ROUTINES];
static int s_count;
static DecompMode s_mode = DECOMP_OFF;

static uint8_t s_image[GAME_RAM_SIZE];
static uint8_t s_orig[GAME_RAM_SIZE];
static bool s_scope;
static uint8_t s_entry_s, s_entry_p;
static bool s_run_original;

static uint8_t s_cur_s;
static bool s_handoff_set;
static uint16_t s_handoff;
static uint8_t s_handoff_uses;
static struct { uint16_t addr; uint8_t value; } s_store[MAX_STORES];
static int s_stores;

typedef struct {
    uint16_t start, stop_pc;
    uint8_t start_s, call_s, stop_s;
    bool fixup, stop_armed, stopped, returned;
    CycModRegs regs;
    uint64_t cycles, io;
} TaskRun;
static TaskRun s_run;
static bool s_run_active;

/* The two stack bytes a handoff uses to reach its routine, put back when the
 * routine's site is reached (the original never wrote them). */
static struct { bool set; uint16_t target, addr; uint8_t old[2], put[2]; } s_restore;

static void fail(const char *what, uint16_t routine)
{
    fprintf(stderr, "[decomp] %s ($%04X, frame %llu)\n", what, routine, (unsigned long long)cyc_ring_frame);
    fflush(stderr);
    exit(3);
}

Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    if (!s_scope) {
        if (!cyc_mod_isolate_begin()) fail("cannot open an isolated scope", routine);
        s_scope = true;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t i = 0; i < GAME_RAM_SIZE; i++)
        if (ram[i] != s_image[i]) cyc_mod_poke(i, s_image[i]);
    CycModRegs r = { a, x, y, s_cur_s, s_entry_p, 0 };
    uint64_t io = hw_isolated_io_writes;
    if (!cyc_mod_call(routine, &r)) fail("an original routine called from C did not return", routine);
    if (hw_isolated_io_writes != io && routine != DT_CODE_BANK_SELECT)
        fail("an original routine called from C wrote a device register (hand it off instead)", routine);
    memcpy(s_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

void Decomp_RunOriginal(void) { s_run_original = true; }

void Decomp_Push(uint8_t value) { s_image[0x100 | s_cur_s--] = value; }

uint8_t Decomp_Pull(void) { return s_image[0x100 | ++s_cur_s]; }

void Decomp_SetStack(uint8_t s) { s_cur_s = s; }

void Decomp_PushReturn(uint16_t resume)
{
    uint16_t ret = (uint16_t)(resume - 1);
    Decomp_Push((uint8_t)(ret >> 8));
    Decomp_Push((uint8_t)ret);
}

void Decomp_PullReturn(void) { s_cur_s = (uint8_t)(s_cur_s + 2); }

bool Decomp_HandedOff(void) { return s_handoff_set; }

void Decomp_Handoff(uint16_t routine, uint16_t resume, uint8_t uses)
{
    if (resume) Decomp_PushReturn(resume);
    s_handoff = routine;
    s_handoff_uses = uses;
    s_handoff_set = true;
}

static void record_store(uint16_t addr, uint8_t value)
{
    if (s_stores == MAX_STORES) fail("too many device writes in one routine", addr);
    s_store[s_stores].addr = addr;
    s_store[s_stores].value = value;
    s_stores++;
}

void Decomp_SelectBank(uint8_t bank)
{
    Decomp_Call(DT_CODE_BANK_SELECT, bank, 0, 0);
    record_store((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

void Decomp_Store(uint16_t addr, uint8_t value) { record_store(addr, value); }

int Decomp_RaiseEvent(int event)
{
    if (s_mode != DECOMP_REPLACE) return EVENT_CONTINUE;
    return Events_Raise((EventId)event);
}

/* The C of a routine on an image of RAM at its entry; s_image holds the RAM
 * it leaves, *out its registers. */
static void run_c(const DecompRoutine *rt, const CycModRegs *in, Regs *out)
{
    memcpy(s_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    s_entry_s = in->s;
    s_entry_p = in->p;
    s_run_original = false;
    s_scope = false;
    s_cur_s = in->s;
    s_handoff_set = false;
    s_stores = 0;
    Regs r = { in->a, in->x, in->y, in->p };
    GameRam_Enter(s_image);
    rt->run(&r);
    GameRam_Leave();
    if (s_scope) cyc_mod_isolate_end();
    s_scope = false;
    *out = r;
}

/* Offsets in page 1 below entry S + 3 are dead once the routine has returned
 * (its return address included). The bottom of page 1 is not stack but data
 * (palette, consumed-object bits, the HUD time sprites): always live. */
static bool live(uint16_t a, uint8_t entry_s)
{
    return (a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_LOW || (a & 0xFF) >= entry_s + 3u;
}

/* The original routine alone on the machine's state: its RAM into s_orig,
 * its registers into *out; returns its cycle count, 0 when it did not
 * return. */
static uint64_t run_original(const DecompRoutine *rt, const CycModRegs *in, CycModRegs *out)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) fail("cannot open an isolated scope", rt->addr);
    cyc_mod_stats(&before);
    *out = *in;
    bool returned = cyc_mod_call(rt->addr, out);
    cyc_mod_stats(&after);
    memcpy(s_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    cyc_mod_isolate_end();
    return returned ? after.cycles - before.cycles : 0;
}

static void spend_cycles(uint16_t addr, uint64_t cycles)
{
    while (cycles-- > 1) (void)cpu_read(addr, 0);
    (void)cpu_read(addr, CYC_POLL | CYC_DONE);
}

/* Replace mode gives a call to the original when VBlank would start inside
 * it: the original takes the NMI there, and the NMI's sprite-0 wait would
 * absorb a delay, so taking it after the routine would move the rest of the
 * game earlier (and the frame would end with the routine's results already
 * stored). A margin covers the RTS and the instruction boundary. */
static bool crosses_vblank(uint64_t cycles)
{
    hw_native_flush();
    long now = (long)ppu.scanline * 341 + ppu.dot;
    long dots = 241L * 341 + 1 - now;
    if (dots <= 0) dots += 262L * 341;
    return cycles + 12 >= (uint64_t)(dots / 3);
}

static void report(const DecompRoutine *rt, const CycModRegs *in, const Regs *orig, const Regs *c)
{
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu, entry A=%02X X=%02X Y=%02X S=%02X P=%02X\n", rt->name,
            rt->addr, (unsigned long long)cyc_ring_frame, in->a, in->x, in->y, in->s, in->p);
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!live(a, in->s) || s_orig[a] == s_image[a]) continue;
        if (shown++ < 24) fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, s_orig[a], s_image[a]);
    }
    if (shown > 24) fprintf(stderr, "[decomp]   ... %d bytes differ\n", shown);
    fprintf(stderr, "[decomp]   registers original A=%02X X=%02X Y=%02X P=%02X decompiled A=%02X X=%02X Y=%02X P=%02X (contract %02X)\n",
            orig->a, orig->x, orig->y, orig->p, c->a, c->x, c->y, c->p, rt->out);
    fflush(stderr);
    exit(3);
}

static void check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o);
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return;
    }
    Regs orig = { o.a, o.x, o.y, o.p };
    bool same = cycles != 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE && same; a++)
        if (live(a, in->s) && s_orig[a] != s_image[a]) same = false;
    if ((rt->out & OUT_A) && orig.a != c.a) same = false;
    if ((rt->out & OUT_X) && orig.x != c.x) same = false;
    if ((rt->out & OUT_Y) && orig.y != c.y) same = false;
    if ((rt->out & OUT_P) && ((orig.p ^ c.p) & (P_N | P_V | P_Z | P_C))) same = false;
    if (!same) report(rt, in, &orig, &c);
    s_stats[i].compared++;
}

static int replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return 0;
    }
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o);
    if (!cycles) fail("the original routine did not return in an isolated call", rt->addr);
    if (crosses_vblank(cycles)) {
        s_stats[i].original++;
        return 0;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++)
        if (live(a, in->s) && ram[a] != s_image[a]) cyc_mod_poke(a, s_image[a]);
    uint8_t flags = P_N | P_V | P_Z | P_C;
    CycModRegs set = { c.a, c.x, c.y, in->s, (uint8_t)((c.p & flags) | (in->p & ~flags)), 0 };
    cyc_mod_set_regs(&set);
    spend_cycles(rt->addr, cycles);
    return 1;
}

/* ---- task code ---- */

/* A site reached inside the isolated run of a task routine's original. */
static int task_isolated_site(uint16_t addr)
{
    if (!s_run_active) return 0;
    CycModRegs r;
    cyc_mod_regs(&r);
    if (s_run.fixup && addr == s_run.start) {
        s_run.fixup = false;
        r.s = s_run.start_s;
        cyc_mod_set_regs(&r);
        return 0;
    }
    if (!s_run.stop_armed || s_run.stopped || addr != s_run.stop_pc || r.s != s_run.stop_s) return 0;
    s_run.stopped = true;
    s_run.regs = r;
    r.s = (uint8_t)(s_run.call_s - 2);
    cyc_mod_set_regs(&r);
    return 1;
}

/* The original task routine alone, from the site to its RTS or, when the C
 * handed off, to its arrival at the same routine at the same depth; its RAM
 * into s_orig, the rest into s_run. */
static void run_task_original(const DecompRoutine *rt, const CycModRegs *in)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) fail("cannot open an isolated scope", rt->addr);
    cyc_mod_allow_isolated_hooks(true);
    memset(&s_run, 0, sizeof s_run);
    s_run.start = rt->addr;
    s_run.start_s = in->s;
    s_run.call_s = (uint8_t)(in->s + rt->locals + 2);
    s_run.fixup = rt->locals != 0;
    s_run.stop_armed = s_handoff_set;
    s_run.stop_pc = s_handoff;
    s_run.stop_s = s_cur_s;
    s_run_active = true;
    uint64_t io = hw_isolated_io_writes;
    cyc_mod_stats(&before);
    CycModRegs r = *in;
    r.s = s_run.call_s;
    s_run.returned = cyc_mod_call(rt->addr, &r);
    cyc_mod_stats(&after);
    s_run_active = false;
    s_run.io = hw_isolated_io_writes - io;
    s_run.cycles = after.cycles - before.cycles;
    if (!s_run.stopped) s_run.regs = r;
    memcpy(s_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    cyc_mod_isolate_end();
}

/* Bytes both runs must agree on: page 1 only above the C's final S, and never
 * the slot of the isolated call's return address. */
static bool task_live(uint16_t a)
{
    if ((a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_LOW) return true;
    uint8_t off = (uint8_t)a;
    if (off == s_run.call_s || off == (uint8_t)(s_run.call_s - 1)) return false;
    return s_handoff_set ? off > s_cur_s : off >= s_cur_s + 3u;
}

static bool task_same(const DecompRoutine *rt, const Regs *c)
{
    if (s_run.returned == false) return false;
    if (s_handoff_set != s_run.stopped) return false;
    if (s_run.io != (uint64_t)s_stores) return false;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++)
        if (task_live(a) && s_orig[a] != s_image[a]) return false;
    const CycModRegs *o = &s_run.regs;
    uint8_t out = s_handoff_set ? s_handoff_uses : rt->out;
    if ((out & OUT_A) && o->a != c->a) return false;
    if ((out & OUT_X) && o->x != c->x) return false;
    if ((out & OUT_Y) && o->y != c->y) return false;
    if ((out & OUT_P) && ((o->p ^ c->p) & (P_N | P_V | P_Z | P_C))) return false;
    return true;
}

static void task_report(const DecompRoutine *rt, const CycModRegs *in, const Regs *c)
{
    const CycModRegs *o = &s_run.regs;
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu, entry A=%02X X=%02X Y=%02X S=%02X P=%02X\n", rt->name,
            rt->addr, (unsigned long long)cyc_ring_frame, in->a, in->x, in->y, in->s, in->p);
    if (s_handoff_set)
        fprintf(stderr, "[decomp]   the C hands off to $%04X at S=%02X; the original %s\n", s_handoff, s_cur_s,
                s_run.stopped ? "got there" : s_run.returned ? "returned instead" : "did not return or get there");
    else
        fprintf(stderr, "[decomp]   the C returns; the original %s\n",
                s_run.stopped ? "stopped" : s_run.returned ? "returned too" : "did not return");
    fprintf(stderr, "[decomp]   device writes original %llu decompiled %d\n", (unsigned long long)s_run.io, s_stores);
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!task_live(a) || s_orig[a] == s_image[a]) continue;
        if (shown++ < 24) fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, s_orig[a], s_image[a]);
    }
    if (shown > 24) fprintf(stderr, "[decomp]   ... %d bytes differ\n", shown);
    fprintf(stderr, "[decomp]   registers original A=%02X X=%02X Y=%02X P=%02X decompiled A=%02X X=%02X Y=%02X P=%02X\n",
            o->a, o->x, o->y, o->p, c->a, c->x, c->y, c->p);
    fflush(stderr);
    exit(3);
}

static void task_check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return;
    }
    run_task_original(rt, in);
    if (!task_same(rt, &c)) task_report(rt, in, &c);
    s_stats[i].compared++;
}

static int task_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return 0;
    }
    run_task_original(rt, in);
    if (!s_run.returned || s_handoff_set != s_run.stopped)
        fail("the original task routine did not end where the C does in an isolated call", rt->addr);
    if (crosses_vblank(s_run.cycles)) {
        s_stats[i].original++;
        return 0;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++)
        if (task_live(a) && ram[a] != s_image[a]) cyc_mod_poke(a, s_image[a]);
    uint8_t s = s_cur_s;
    if (s_handoff_set) {
        uint16_t ret = (uint16_t)(s_handoff - 1);
        s_restore.set = true;
        s_restore.target = s_handoff;
        s_restore.addr = (uint16_t)(0x100 | (uint8_t)(s - 1));
        s_restore.old[0] = cyc_cpu_ram()[s_restore.addr];
        s_restore.old[1] = cyc_cpu_ram()[0x100 | s];
        s_restore.put[0] = (uint8_t)ret;
        s_restore.put[1] = (uint8_t)(ret >> 8);
        cyc_mod_poke((uint16_t)(0x100 | s), (uint8_t)(ret >> 8));
        cyc_mod_poke(s_restore.addr, (uint8_t)ret);
        s = (uint8_t)(s - 2);
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    uint8_t p = s_handoff_set ? in->p : (uint8_t)((c.p & flags) | (in->p & ~flags));
    CycModRegs set = { c.a, c.x, c.y, s, p, 0 };
    if (s_handoff_set) {
        if (!(s_handoff_uses & OUT_A)) set.a = s_run.regs.a;
        if (!(s_handoff_uses & OUT_X)) set.x = s_run.regs.x;
        if (!(s_handoff_uses & OUT_Y)) set.y = s_run.regs.y;
    }
    cyc_mod_set_regs(&set);
    for (int k = 0; k < s_stores; k++) cpu_write(s_store[k].addr, s_store[k].value, 0);
    spend_cycles(rt->addr, s_run.cycles > (uint64_t)s_stores ? s_run.cycles - (uint64_t)s_stores : 1);
    return 1;
}

static int on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) return task_isolated_site(addr);
    if (s_restore.set && addr == s_restore.target) {
        s_restore.set = false;
        const uint8_t *ram = cyc_cpu_ram();
        uint16_t lo = s_restore.addr, hi = (uint16_t)(0x100 | (uint8_t)(lo + 1));
        if (ram[lo] == s_restore.put[0] && ram[hi] == s_restore.put[1]) {
            cyc_mod_poke(lo, s_restore.old[0]);
            cyc_mod_poke(hi, s_restore.old[1]);
        }
    }
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->addr != addr) continue;
        CycModRegs in;
        cyc_mod_regs(&in);
        if (s_routine[i]->task) {
            if (s_mode == DECOMP_CHECK) {
                task_check(i, &in);
                return 0;
            }
            return task_replace(i, &in);
        }
        if (s_mode == DECOMP_CHECK) {
            check(i, &in);
            return 0;
        }
        return replace(i, &in);
    }
    return 0;
}

static void add_table(const DecompRoutine *table, int n)
{
    for (int i = 0; i < n && s_count < MAX_ROUTINES; i++) {
        s_routine[s_count++] = &table[i];
        nes_mod_register_function_entry_plugin(table[i].site_id, table[i].addr, on_entry);
    }
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_sites)
{
    add_table(gMotionRoutines, gMotionRoutineCount);
    add_table(gSoundRoutines, gSoundRoutineCount);
    add_table(gPlayerRoutines, gPlayerRoutineCount);
    add_table(gFlowRoutines, gFlowRoutineCount);
    for (int i = 0; i < gFlowStopCount; i++)
        nes_mod_register_function_entry_plugin(gFlowStops[i].site_id, gFlowStops[i].addr, on_entry);
}

static void report_stats(void)
{
    if (s_mode == DECOMP_OFF) return;
    for (int i = 0; i < s_count; i++)
        printf("decomp: mode=%s routine=%s addr=$%04X calls=%llu compared=%llu original=%llu\n",
               s_mode == DECOMP_CHECK ? "check" : "replace", s_routine[i]->name, s_routine[i]->addr,
               (unsigned long long)s_stats[i].calls, (unsigned long long)s_stats[i].compared,
               (unsigned long long)s_stats[i].original);
    fflush(stdout);
}

bool Decomp_ParseMode(const char *text, DecompMode *out)
{
    if (!text) return false;
    if (!strcmp(text, "off")) *out = DECOMP_OFF;
    else if (!strcmp(text, "check")) *out = DECOMP_CHECK;
    else if (!strcmp(text, "replace")) *out = DECOMP_REPLACE;
    else return false;
    return true;
}

bool Decomp_SetMode(DecompMode mode)
{
    static bool registered;
    if (!registered && mode != DECOMP_OFF) {
        registered = true;
        atexit(report_stats);
    }
    s_mode = mode;
    for (int i = 0; i < s_count; i++) nes_mod_set_function_hook_enabled(s_routine[i]->site_id, mode != DECOMP_OFF);
    for (int i = 0; i < gFlowStopCount; i++) nes_mod_set_function_hook_enabled(gFlowStops[i].site_id, mode != DECOMP_OFF);
    return true;
}

DecompMode Decomp_Mode(void) { return s_mode; }
