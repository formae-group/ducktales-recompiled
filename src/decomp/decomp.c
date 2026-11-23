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
 *            The isolated call runs with hook sites on, so it takes the same
 *            path as the C: an event site raises nothing there, and at the
 *            damage site ($EB5E) the original returns as its RTS would when
 *            the C's EVENT_PLAYER_DAMAGED was cancelled, in the C's order.
 *
 * A call whose C ends in Decomp_RunOriginal runs the original in both modes
 * and is not compared: the paths that use it lead into code (a row scroll, a
 * message box) that waits for the hardware.
 *
 * Only routines that original code calls have a site (DecompRoutine.site_id):
 * NESRecomp registers at most 128 hook plugins, one address each. A routine
 * that only decompiled code calls is a plain C call. Every routine's C starts
 * with Decomp_Enter, which in check mode, when the C of another routine made
 * the call to a routine without a site, checks that call as a site would: the
 * original in an isolated call and the routine's C on a copy of the image,
 * both from the caller's C state, with the registers the C passes. (A routine
 * with a site is checked there, when the caller's original runs for real.)
 */
#include "decomp/decomp.h"

#include "ducktales/events.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROUTINES     256
#define MAX_BUS_OPS      8192
#define FRAME_END_MARGIN 32

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
static const DecompRoutine *s_top;
static bool s_top_entered, s_inner;
static bool s_damage_cancel[8];
static int s_damage_raised, s_damage_replayed;

typedef struct {
    uint16_t addr;
    uint8_t value;
    bool write;
} BusOp;

static BusOp s_ops[MAX_BUS_OPS];
static int s_nops, s_ops_in_scope;

typedef struct {
    uint64_t ciram, palette, chr;
    unsigned bank;
} HwState;

static HwState s_orig_hw;

static void fail(const char *what, uint16_t routine)
{
    fprintf(stderr, "[decomp] %s ($%04X, frame %llu)\n", what, routine, (unsigned long long)cyc_ring_frame);
    fflush(stderr);
    exit(3);
}

static void record(uint16_t addr, uint8_t value, bool write)
{
    if (s_nops == MAX_BUS_OPS) {
        fail("too many bus operations in one call", addr);
    }
    s_ops[s_nops++] = (BusOp){ addr, value, write };
}

void Decomp_BusWrite(uint16_t addr, uint8_t value)
{
    record(addr, value, true);
    Game_MapperWrite(addr, value);
}

void Decomp_BusRead(uint16_t addr)
{
    record(addr, 0, false);
}

/* The recorded bus operations from index from on, made on the machine (or
 * on the isolated copy inside a scope). */
static void play_ops(int from)
{
    for (int i = from; i < s_nops; i++) {
        if (s_ops[i].write) {
            cpu_write(s_ops[i].addr, s_ops[i].value, 0);
        } else {
            (void)cpu_read(s_ops[i].addr, 0);
        }
    }
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; p && i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

static HwState hw_state(void)
{
    size_t n = 0;
    HwState s;
    const uint8_t *ciram = cyc_ppu_ciram(&n);
    s.ciram = fnv(ciram, n);
    s.palette = fnv(cyc_ppu_palette(), 32);
    const uint8_t *chr = cyc_chr_ram(&n);
    s.chr = fnv(chr, n);
    s.bank = hw_prg_bank4(0x8000);
    return s;
}

/* An original routine in the C's isolated scope, from the image the C works
 * on (gRam) and the bus operations the C made so far; its RAM is left in the
 * scope's machine. */
static bool call_in_scope(uint16_t routine, CycModRegs *r)
{
    if (!s_scope) {
        if (!cyc_mod_isolate_begin()) {
            fail("cannot open an isolated scope", routine);
        }
        s_scope = true;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t i = 0; i < GAME_RAM_SIZE; i++) {
        if (ram[i] != gRam[i]) {
            cyc_mod_poke(i, gRam[i]);
        }
    }
    if (s_ops_in_scope < s_nops) {
        play_ops(s_ops_in_scope);
    }
    s_ops_in_scope = s_nops;
    return cyc_mod_call(routine, r);
}

Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    CycModRegs r = { a, x, y, s_entry_s, s_entry_p, 0 };
    if (!call_in_scope(routine, &r)) {
        fail("an original routine called from C did not return", routine);
    }
    memcpy(gRam, cyc_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

void Decomp_RunOriginal(void)
{
    s_run_original = true;
}

int Decomp_RaiseEvent(int event)
{
    if (s_mode != DECOMP_REPLACE) {
        return EVENT_CONTINUE;
    }
    EventResult result = Events_Raise((EventId)event);
    if (event == EVENT_PLAYER_DAMAGED && s_damage_raised < (int)(sizeof s_damage_cancel / sizeof s_damage_cancel[0])) {
        s_damage_cancel[s_damage_raised++] = result == EVENT_CANCEL;
    }
    return result;
}

/* The C of a routine on an image of RAM at its entry; s_image holds the RAM
 * it leaves, *out its registers. */
static void run_c(const DecompRoutine *rt, const CycModRegs *in, Regs *out)
{
    memcpy(s_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    s_entry_s = in->s;
    s_entry_p = in->p;
    s_run_original = false;
    s_damage_raised = 0;
    s_scope = false;
    s_top = rt;
    s_top_entered = false;
    s_nops = s_ops_in_scope = 0;
    Regs r = { in->a, in->x, in->y, in->p };
    GameRam_Enter(s_image);
    rt->run(&r);
    GameRam_Leave();
    Game_ResetRomView();
    if (s_scope) {
        cyc_mod_isolate_end();
    }
    s_scope = false;
    if (!s_top_entered) {
        fail("a decompiled routine does not start with Decomp_Enter", rt->addr);
    }
    *out = r;
}

/* Offsets in page 1 below entry S + 3 are dead once the routine has returned
 * (its return address included), down to the stack's floor: below it page 1
 * holds data. */
static bool live(uint16_t a, uint8_t entry_s)
{
    return (a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_FLOOR || (a & 0xFF) >= entry_s + 3u;
}

/* The original routine alone on the machine's state: its RAM into s_orig,
 * its registers into *out; returns its cycle count, 0 when it did not
 * return. */
static uint64_t run_original(const DecompRoutine *rt, const CycModRegs *in, CycModRegs *out, bool hooked)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) {
        fail("cannot open an isolated scope", rt->addr);
    }
    s_damage_replayed = 0;
    cyc_mod_allow_isolated_hooks(hooked);
    cyc_mod_stats(&before);
    *out = *in;
    bool returned = cyc_mod_call(rt->addr, out);
    cyc_mod_stats(&after);
    memcpy(s_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    if (rt->out & OUT_HW) {
        s_orig_hw = hw_state();
    }
    cyc_mod_isolate_end();
    return returned ? after.cycles - before.cycles : 0;
}

static void spend_cycles(uint16_t addr, uint64_t cycles)
{
    while (cycles-- > 1) {
        (void)cpu_read(addr, 0);
    }
    (void)cpu_read(addr, CYC_POLL | CYC_DONE);
}

/* Check mode's verdict: the original (returned, its RAM and registers)
 * against the C. */
static bool agree(const DecompRoutine *rt, uint8_t entry_s, bool returned, const uint8_t *orig_ram, const Regs *orig,
                  const uint8_t *c_ram, const Regs *c)
{
    if (!returned) {
        return false;
    }
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (live(a, entry_s) && orig_ram[a] != c_ram[a]) {
            return false;
        }
    }
    if ((rt->out & OUT_A) && orig->a != c->a) {
        return false;
    }
    if ((rt->out & OUT_X) && orig->x != c->x) {
        return false;
    }
    if ((rt->out & OUT_Y) && orig->y != c->y) {
        return false;
    }
    if ((rt->out & OUT_P) && ((orig->p ^ c->p) & (P_N | P_V | P_Z | P_C))) {
        return false;
    }
    return true;
}

/* Prints a difference and stops the program. caller names the routine whose
 * C made the call, NULL for a site. */
static void report(const DecompRoutine *rt, const CycModRegs *in, const char *caller, bool returned,
                   const uint8_t *orig_ram, const Regs *orig, const uint8_t *c_ram, const Regs *c)
{
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu%s%s, entry A=%02X X=%02X Y=%02X S=%02X P=%02X\n",
            rt->name, rt->addr, (unsigned long long)cyc_ring_frame, caller ? ", called from the C of " : "",
            caller ? caller : "", in->a, in->x, in->y, in->s, in->p);
    if (!returned) {
        fprintf(stderr, "[decomp]   the original did not return\n");
    }
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!live(a, in->s) || orig_ram[a] == c_ram[a]) {
            continue;
        }
        if (shown++ < 24) {
            fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, orig_ram[a], c_ram[a]);
        }
    }
    if (shown > 24) {
        fprintf(stderr, "[decomp]   ... %d bytes differ\n", shown);
    }
    fprintf(stderr,
            "[decomp]   registers original A=%02X X=%02X Y=%02X P=%02X decompiled A=%02X X=%02X Y=%02X P=%02X "
            "(contract %02X)\n",
            orig->a, orig->x, orig->y, orig->p, c->a, c->x, c->y, c->p, rt->out);
    fflush(stderr);
    exit(3);
}

/* The C's bus operations made on a copy of the machine at entry must leave
 * the PPU's memories and the PRG bank as the original left them. */
static void check_hw(const DecompRoutine *rt)
{
    HwState c;
    if (s_nops) {
        if (!cyc_mod_isolate_begin()) {
            fail("cannot open an isolated scope", rt->addr);
        }
        play_ops(0);
        c = hw_state();
        cyc_mod_isolate_end();
    } else {
        c = hw_state();
    }
    if (c.ciram == s_orig_hw.ciram && c.palette == s_orig_hw.palette && c.chr == s_orig_hw.chr &&
        c.bank == s_orig_hw.bank) {
        return;
    }
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu: hardware differs:%s%s%s%s (%d bus operations)\n",
            rt->name, rt->addr, (unsigned long long)cyc_ring_frame, c.ciram != s_orig_hw.ciram ? " CIRAM" : "",
            c.palette != s_orig_hw.palette ? " palette" : "", c.chr != s_orig_hw.chr ? " CHR-RAM" : "",
            c.bank != s_orig_hw.bank ? " PRG bank" : "", s_nops);
    if (c.bank != s_orig_hw.bank) {
        fprintf(stderr, "[decomp]   PRG bank original %u decompiled %u (4 KiB units)\n", s_orig_hw.bank, c.bank);
    }
    fflush(stderr);
    exit(3);
}

static void check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o, false);
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return;
    }
    Regs orig = { o.a, o.x, o.y, o.p };
    if (!agree(rt, in->s, cycles != 0, s_orig, &orig, s_image, &c)) {
        report(rt, in, NULL, cycles != 0, s_orig, &orig, s_image, &c);
    }
    if (rt->out & OUT_HW) {
        check_hw(rt);
    }
    s_stats[i].compared++;
}

/* Check mode, a call from the C of another routine: the routine's C on a
 * copy of the image, then its original in the scope from the caller's image,
 * entered as a JSR at the caller's entry S would. The copy makes no checks of
 * its own calls (its real run makes them), except to find the innermost
 * routine of a difference: then it runs again with them, and one that
 * differs reports itself first. */
static void check_inner(int i, Regs in, bool drill)
{
    const DecompRoutine *rt = s_routine[i];
    const DecompRoutine *top = s_top;
    bool top_entered = s_top_entered, run_original = s_run_original, inner = s_inner;
    int nops = s_nops, view = Game_RomView();
    uint8_t image[GAME_RAM_SIZE], orig_ram[GAME_RAM_SIZE];
    memcpy(image, gRam, GAME_RAM_SIZE);
    s_top = rt;
    s_top_entered = false;
    s_run_original = false;
    s_inner = !drill;
    Regs c = in;
    GameRam_Enter(image);
    rt->run(&c);
    GameRam_Leave();
    bool entered = s_top_entered, original = s_run_original;
    s_top = top;
    s_top_entered = top_entered;
    s_run_original = run_original;
    s_inner = inner;
    s_nops = nops;
    if (s_ops_in_scope > s_nops) {
        s_ops_in_scope = s_nops;
    }
    Game_SetRomView(view);
    if (!entered) {
        fail("a decompiled routine does not start with Decomp_Enter", rt->addr);
    }
    s_stats[i].calls++;
    if (original) {
        s_stats[i].original++;
        return;
    }
    CycModRegs r = { in.a, in.x, in.y, s_entry_s, in.p, 0 };
    CycModRegs entry = r;
    entry.s = (uint8_t)(s_entry_s - 2);
    bool returned = call_in_scope(rt->addr, &r);
    memcpy(orig_ram, cyc_cpu_ram(), GAME_RAM_SIZE);
    Regs orig = { r.a, r.x, r.y, r.p };
    if (!agree(rt, entry.s, returned, orig_ram, &orig, image, &c)) {
        if (!drill) {
            check_inner(i, in, true);
        }
        report(rt, &entry, top->name, returned, orig_ram, &orig, image, &c);
    }
    s_stats[i].compared++;
}

void Decomp_Enter(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    if (!s_top_entered && routine == s_top->addr) {
        s_top_entered = true;
        return;
    }
    if (s_mode != DECOMP_CHECK || s_inner) {
        return;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->addr != routine) {
            continue;
        }
        if (!s_routine[i]->site_id) {
            check_inner(i, (Regs){ a, x, y, s_entry_p }, false);
        }
        return;
    }
    fail("Decomp_Enter names a routine that is in no table", routine);
}

/* The host's frame end, where the RAM log is taken: one frame after the last. */
static uint64_t s_last_frame_end, s_frame_cycles;

void Decomp_FrameEnd(void)
{
    uint64_t now = cyc_cycle_count();
    if (s_last_frame_end && now > s_last_frame_end) {
        s_frame_cycles = now - s_last_frame_end;
    }
    s_last_frame_end = now;
}

/* Whether a routine taking this many cycles from now would still run when
 * the frame ends (with a margin of a few instructions). */
static bool spans_frame_end(uint64_t cycles)
{
    if (cpu.do_nmi || cpu.nmi_edge) {
        return true;
    }
    if (!s_frame_cycles) {
        return false;
    }
    return cyc_cycle_count() + cycles + FRAME_END_MARGIN >= s_last_frame_end + s_frame_cycles;
}

static int replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o, false);
    s_stats[i].calls++;
    if (cycles && spans_frame_end(cycles)) {
        s_stats[i].original++;
        return 0;
    }
    Regs c;
    run_c(rt, in, &c);
    if (s_run_original) {
        s_stats[i].original++;
        return 0;
    }
    if (!cycles) {
        fail("the original routine did not return in an isolated call", rt->addr);
    }
    bool cancelled = false;
    for (int k = 0; k < s_damage_raised; k++) {
        cancelled |= s_damage_cancel[k];
    }
    if (cancelled) {
        cycles = run_original(rt, in, &o, true);
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (live(a, in->s) && ram[a] != s_image[a]) {
            cyc_mod_poke(a, s_image[a]);
        }
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    CycModRegs set = { c.a, c.x, c.y, in->s, (uint8_t)((c.p & flags) | (in->p & ~flags)), 0 };
    cyc_mod_set_regs(&set);
    play_ops(0);
    spend_cycles(rt->addr, cycles > (uint64_t)s_nops ? cycles - (uint64_t)s_nops : 1);
    return 1;
}

static int on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        return addr == DT_SITE_DAMAGE && s_damage_replayed < s_damage_raised && s_damage_cancel[s_damage_replayed++];
    }
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->addr != addr) {
            continue;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
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
        if (table[i].site_id) {
            nes_mod_register_function_entry_plugin(table[i].site_id, table[i].addr, on_entry);
        }
    }
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_sites)
{
    add_table(gMotionRoutines, gMotionRoutineCount);
    add_table(gSoundRoutines, gSoundRoutineCount);
    add_table(gPlayerRoutines, gPlayerRoutineCount);
    add_table(gLevelRoutines, gLevelRoutineCount);
    add_table(gCollisionRoutines, gCollisionRoutineCount);
}

static void report_stats(void)
{
    if (s_mode == DECOMP_OFF) {
        return;
    }
    for (int i = 0; i < s_count; i++) {
        printf("decomp: mode=%s routine=%s addr=$%04X calls=%llu compared=%llu original=%llu\n",
               s_mode == DECOMP_CHECK ? "check" : "replace", s_routine[i]->name, s_routine[i]->addr,
               (unsigned long long)s_stats[i].calls, (unsigned long long)s_stats[i].compared,
               (unsigned long long)s_stats[i].original);
    }
    fflush(stdout);
}

bool Decomp_ParseMode(const char *text, DecompMode *out)
{
    if (!text) {
        return false;
    }
    if (!strcmp(text, "off")) {
        *out = DECOMP_OFF;
    } else if (!strcmp(text, "check")) {
        *out = DECOMP_CHECK;
    } else if (!strcmp(text, "replace")) {
        *out = DECOMP_REPLACE;
    } else {
        return false;
    }
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
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->site_id) {
            nes_mod_set_function_hook_enabled(s_routine[i]->site_id, mode != DECOMP_OFF);
        }
    }
    return true;
}

DecompMode Decomp_Mode(void)
{
    return s_mode;
}
