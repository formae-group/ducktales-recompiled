/*
 * decomp_cycle.c - the cycle host's backend of the decompilation layer
 * (decomp_backend.h, docs/decompilation.md): runs the decompiled routines in
 * place of the recompiled ones, or beside them.
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
 *            set, and the CPU spends the original's cycle count as the C's
 *            bus operations (Decomp_BusWrite) spread evenly among reads of
 *            the routine's first byte, polling interrupts in the last read as
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
 *
 * Task code (decomp.h, docs/decompilation.md "Task code") runs the same way,
 * with four differences. Its C starts at a site or at a resume point: an
 * address the game's RTS returns to (after a JSR to code that is not
 * decompiled, or a task_yield), seen by the RTS observer with no site. Its
 * original runs at the routine's true stack depth: the isolated call's return
 * address takes the place of the routine's own (at S + locals + 1), and the
 * call enters through an RTS whose return the observer moves to the start, at
 * S. When the C hands off, the original is stopped where it reaches the same
 * routine at the same depth (a stop site, with hooks allowed in that isolated
 * call); check compares RAM, the stack down to that depth, the registers the
 * hand-off uses and the number of device writes, and replace stores the C's
 * results, makes its device writes, spends the original's cycles up to the
 * stop and jumps there. Inside an isolated call every other site lets the
 * original run.
 *
 * Device access (Decomp_BusWrite, Decomp_BusRead, Decomp_BusLoad; routines
 * marked OUT_HW). The C records its writes and its side-effect reads; replace
 * mode makes them on the bus after storing the RAM, spread over the
 * original's cycles, and check mode makes them on an isolated copy of the
 * machine and compares CIRAM, palette, OAM, CHR RAM, the PRG bank and every
 * device register the machine lists (cyc_hw_state_dump) but the CPU bus
 * fields and the two PPUMASK bits that show the previous bus value until two
 * dots after a $2001 store (no clock runs in a scope, so they never settle
 * there). A read whose value the C needs (Decomp_BusLoad: the controllers)
 * is made at once with the operations before it: in the C's scope in check
 * mode, for real in replace mode, each spaced as an absolute load is.
 *
 * Routines of the switchable window ($8000-$BFFF) match their site only with
 * their bank (DecompRoutine.bank) mapped. A replaced routine that an NMI
 * interrupts while it spends the original's cycles owes the rest; the NMI
 * handler's RTI spends it, so the interrupted code resumes at the console's
 * time: the RTI of an interrupt routine's C, or the original's
 * (DT_SITE_NMI_EXIT).
 *
 * Interrupt routines (DecompRoutine.interrupt) have C that ends in RTI. The
 * NMI handler's exit (Nmi_Return) is task code: its isolated original enters
 * as task code's does, with the return address one on (an RTI does not add
 * one, as an RTS does), so that its RTI ends the call. The whole handler
 * (Nmi_Handler) cannot run in an isolated call: it waits for the sprite-0 hit,
 * which needs the PPU running. Check mode runs its C at the NMI and makes its
 * bus operations on a copy of the machine with its clock running (an isolated
 * copy has none, so a sprite DMA or a $2005 write there never lands), from a
 * snapshot it then restores, with the C's RAM in the copy (the sprite DMA
 * reads its OAM page there; a page the C changed after its DMA would show as
 * an OAM difference); it lets the original run for real and compares
 * at the original's RTI (DT_SITE_NMI_EXIT): RAM, the registers, the PPU's
 * memories, the PRG bank, the device fields the NMI sets (nmi_field) and the
 * writes to $2006, $2007 and the APU in order (the C's from its bus
 * operations, the original's seen by the trace's cycle observer). Replace
 * mode measures the original on a clocked snapshot, then runs the C's bus
 * operations at those cycles, with the original's intervening bus traffic
 * supplying open-bus values and DMA halt addresses.
 */
#include "decomp/decomp_backend.h"
#include "decomp/devices.h"

#include "ducktales/events.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "cyc_hooks.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "cyc_state.h"
#include "cyc_trace.h"
#include "hw_internal.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* nesrecomp counts the device stores made inside isolated calls
 * (runner/cyc/hw_machine.c); task code compares them. */
extern uint64_t hw_isolated_io_writes;

#define FRAME_END_MARGIN 32
#define SCOPE_OP_GAP     8
#define LOAD_GAP         3
#define REGS_TEXT        0x4000

static uint8_t s_orig[GAME_RAM_SIZE];
static bool s_scope;
static bool s_inner;
static int s_damage_replayed;

static int s_ops_in_scope;

typedef struct {
    uint64_t ciram, palette, chr, oam, regs;
    unsigned bank;
} HwState;

static HwState s_orig_hw;
static char s_orig_regs[REGS_TEXT], s_c_regs[REGS_TEXT];
static FILE *s_dump;
static uint64_t s_early, s_nmi_debt;
static bool s_regs_text;

typedef struct {
    uint16_t start, stop_pc;
    uint8_t start_s, call_s, stop_s, stop_bank;
    uint8_t ret[2]; /* the isolated call's return address, where the RTS observer left it */
    bool fixup, stop_armed, stopped, returned, rti;
    CycModRegs regs;
    uint64_t entry_cycles, cycles, io;
} TaskRun;

static TaskRun s_run;
static bool s_run_active;

uint64_t dc_frame(void)
{
    return cyc_ring_frame;
}

void dc_c_begin(void)
{
    s_scope = false;
    s_ops_in_scope = 0;
    s_early = 0;
}

void dc_c_end(void)
{
    if (s_scope) {
        cyc_mod_isolate_end();
    }
    s_scope = false;
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; p && i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

static bool device_line_compared(const char *line)
{
    return strncmp(line, "hw.", 3) && strncmp(line, "ppu.greyscale ", 14) && strncmp(line, "ppu.emphasis ", 13);
}

/* The machine's device fields as text, one per line, in the scratch file. */
static FILE *device_dump(void)
{
    if (!s_dump && !(s_dump = tmpfile())) {
        dc_fail("cannot open a scratch file for the device state", 0);
    }
    rewind(s_dump);
    cyc_hw_state_dump(s_dump);
    fputc(0, s_dump);
    fflush(s_dump);
    rewind(s_dump);
    return s_dump;
}

/* The machine's device registers as text, the lines compared. */
static void device_regs(char *regs)
{
    FILE *f = device_dump();
    char line[512];
    size_t used = 0;
    while (fgets(line, sizeof line, f) && line[0]) {
        size_t len = strlen(line);
        if (device_line_compared(line) && used + len + 1 <= REGS_TEXT) {
            memcpy(regs + used, line, len);
            used += len;
        }
    }
    regs[used] = 0;
}

/* The device registers the text of device_regs lists, and the cartridge's
 * bank registers, as one hash: the same field tables nesrecomp dumps. */
static uint64_t device_hash(void)
{
    uint64_t h[4] = { hw_cart_state_hash(0), ppu_state_hash(0), apu_state_hash(0),
                      (uint64_t)hw_extra_timing.extra_scanlines | (uint64_t)hw_extra_timing.line << 16 |
                          (uint64_t)hw_extra_timing.active << 32 };
    return fnv((const uint8_t *)h, sizeof h);
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
    s.oam = fnv(cyc_ppu_oam(), 256);
    s.regs = device_hash();
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
            dc_fail("cannot open an isolated scope", routine);
        }
        s_scope = true;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t i = 0; i < GAME_RAM_SIZE; i++) {
        if (ram[i] != gRam[i]) {
            cyc_mod_poke(i, gRam[i]);
        }
    }
    if (s_ops_in_scope < dc_nops) {
        dc_play_ops(s_ops_in_scope, (uint64_t)(dc_nops - s_ops_in_scope) * SCOPE_OP_GAP, routine);
    }
    s_ops_in_scope = dc_nops;
    return cyc_mod_call(routine, r);
}

Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    return Decomp_CallP(routine, a, x, y, dc_entry_p);
}

Regs Decomp_CallP(uint16_t routine, uint8_t a, uint8_t x, uint8_t y, uint8_t p)
{
    CycModRegs r = { a, x, y, dc_cur_s, p, 0 };
    if (!call_in_scope(routine, &r)) {
        dc_fail("an original routine called from C did not return", routine);
    }
    memcpy(gRam, cyc_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

/* A read the C needs the value of, made with the operations before it: in
 * the C's scope in check mode, on the machine in replace mode. */
uint8_t Decomp_BusLoad(uint16_t addr)
{
    if (dc_top->task) {
        dc_fail("task code reads a device", addr);
    }
    dc_loads++;
    uint8_t value;
    if (dc_mode == DECOMP_CHECK) {
        if (!s_scope) {
            if (!cyc_mod_isolate_begin()) {
                dc_fail("cannot open an isolated scope", addr);
            }
            s_scope = true;
        }
        dc_play_ops(s_ops_in_scope, (uint64_t)(dc_nops - s_ops_in_scope + 1) * SCOPE_OP_GAP, addr);
        value = cpu_read(addr, 0);
        dc_record(addr, value, false);
        s_ops_in_scope = dc_nops;
        return value;
    }
    uint64_t gap = (uint64_t)(dc_nops - dc_played + 1) * LOAD_GAP;
    s_early += dc_play_ops(dc_played, gap, dc_top->addr);
    value = cpu_read(addr, 0);
    dc_record(addr, value, false);
    dc_played = dc_nops;
    return value;
}

/* The measuring call's reads of the values the C gave (Decomp_IsolatedRead). */
static uint8_t isolated_read(uint16_t pc, uint16_t addr, uint8_t value)
{
    for (int k = 0; k < dc_nreads; k++) {
        if (dc_reads[k].pc == pc && dc_reads[k].addr == addr) {
            return dc_reads[k].value;
        }
    }
    return value;
}

/* The original routine alone on the machine's state: its RAM into s_orig,
 * its registers into *out; returns its cycle count, 0 when it did not
 * return. */
static uint64_t run_original(const DecompRoutine *rt, const CycModRegs *in, CycModRegs *out, bool hooked)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) {
        dc_fail("cannot open an isolated scope", rt->addr);
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
        if (s_regs_text) {
            device_regs(s_orig_regs);
        }
    }
    cyc_mod_isolate_end();
    return returned ? after.cycles - before.cycles : 0;
}

/* The original's cycles that the C's bus operations leave to spend, at
 * least the one that polls interrupts. */
static uint64_t spare_cycles(uint64_t cycles)
{
    uint64_t used = (uint64_t)dc_nops + s_early;
    return cycles > used ? cycles - used : 1;
}

/* Stops early when an NMI comes: the rest is spent when the handler returns
 * (on_nmi_exit), so the interrupted code resumes at the console's time. */
static void spend_cycles(uint16_t addr, uint64_t cycles)
{
    while (cycles > 1 && !cpu.nmi_edge) {
        (void)cpu_read(addr, 0);
        cycles--;
    }
    s_nmi_debt += cycles - 1;
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
        if (dc_live(a, entry_s) && orig_ram[a] != c_ram[a]) {
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
    if ((rt->out & OUT_C) && ((orig->p ^ c->p) & P_C)) {
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
        if (!dc_live(a, in->s) || orig_ram[a] == c_ram[a]) {
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
/* Check mode's hardware verdict. The register hashes differ also when only
 * the fields device_regs leaves out do; then both sides' register texts are
 * compared, the original's from a second run of it. */
static void check_hw(const DecompRoutine *rt, const CycModRegs *in, uint64_t cycles)
{
    if (!cyc_mod_isolate_begin()) {
        dc_fail("cannot open an isolated scope", rt->addr);
    }
    dc_play_ops(0, spare_cycles(cycles), rt->addr);
    HwState c = hw_state();
    bool regs_same = c.regs == s_orig_hw.regs;
    if (!regs_same) {
        device_regs(s_c_regs);
    }
    cyc_mod_isolate_end();
    if (!regs_same) {
        CycModRegs o;
        s_regs_text = true;
        run_original(rt, in, &o, false);
        s_regs_text = false;
        regs_same = !strcmp(s_orig_regs, s_c_regs);
    }
    if (c.ciram == s_orig_hw.ciram && c.palette == s_orig_hw.palette && c.chr == s_orig_hw.chr &&
        c.oam == s_orig_hw.oam && regs_same && c.bank == s_orig_hw.bank) {
        return;
    }
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu: hardware differs:%s%s%s%s%s%s (%d bus operations)\n",
            rt->name, rt->addr, (unsigned long long)cyc_ring_frame, c.ciram != s_orig_hw.ciram ? " CIRAM" : "",
            c.palette != s_orig_hw.palette ? " palette" : "", c.chr != s_orig_hw.chr ? " CHR-RAM" : "",
            c.oam != s_orig_hw.oam ? " OAM" : "", !regs_same ? " registers" : "",
            c.bank != s_orig_hw.bank ? " PRG bank" : "", dc_nops);
    if (c.bank != s_orig_hw.bank) {
        fprintf(stderr, "[decomp]   PRG bank original %u decompiled %u (4 KiB units)\n", s_orig_hw.bank, c.bank);
    }
    const char *o = regs_same ? "" : s_orig_regs, *d = s_c_regs;
    for (int shown = 0; *o && *d && shown < 24;) {
        size_t lo = strcspn(o, "\n"), ld = strcspn(d, "\n");
        if (lo != ld || memcmp(o, d, lo)) {
            fprintf(stderr, "[decomp]   original %.*s / decompiled %.*s\n", (int)lo, o, (int)ld, d);
            shown++;
        }
        o += lo + (o[lo] != 0);
        d += ld + (d[ld] != 0);
    }
    fflush(stderr);
    exit(3);
}

static void check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o, false);
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return;
    }
    Regs orig = { o.a, o.x, o.y, o.p };
    if (!agree(rt, in->s, cycles != 0, s_orig, &orig, dc_image, &c)) {
        report(rt, in, NULL, cycles != 0, s_orig, &orig, dc_image, &c);
    }
    if (rt->out & OUT_HW) {
        check_hw(rt, in, cycles);
    }
    dc_stats[i].compared++;
}

/* Check mode, a call from the C of another routine: the routine's C on a
 * copy of the image, then its original in the scope from the caller's image,
 * entered as a JSR at the caller's entry S would. The copy makes no checks of
 * its own calls (its real run makes them), except to find the innermost
 * routine of a difference: then it runs again with them, and one that
 * differs reports itself first. */
static void check_inner(int i, Regs in, bool drill)
{
    const DecompRoutine *rt = dc_routine[i];
    const DecompRoutine *top = dc_top;
    bool top_entered = dc_top_entered, run_original = dc_run_original, inner = s_inner;
    int nops = dc_nops, stores = dc_stores, view = Game_RomView();
    uint8_t image[GAME_RAM_SIZE], orig_ram[GAME_RAM_SIZE];
    memcpy(image, gRam, GAME_RAM_SIZE);
    dc_top = rt;
    dc_top_entered = false;
    dc_run_original = false;
    s_inner = !drill;
    Regs c = in;
    GameRam_Enter(image);
    rt->run(&c);
    GameRam_Leave();
    bool entered = dc_top_entered, original = dc_run_original;
    dc_top = top;
    dc_top_entered = top_entered;
    dc_run_original = run_original;
    s_inner = inner;
    dc_nops = nops;
    dc_stores = stores;
    if (s_ops_in_scope > dc_nops) {
        s_ops_in_scope = dc_nops;
    }
    Game_SetRomView(view);
    if (!entered) {
        dc_fail("a decompiled routine does not start with Decomp_Enter", rt->addr);
    }
    dc_stats[i].calls++;
    if (original) {
        dc_stats[i].original++;
        return;
    }
    CycModRegs r = { in.a, in.x, in.y, dc_cur_s, in.p, 0 };
    CycModRegs entry = r;
    entry.s = (uint8_t)(dc_cur_s - 2);
    bool returned = call_in_scope(rt->addr, &r);
    memcpy(orig_ram, cyc_cpu_ram(), GAME_RAM_SIZE);
    Regs orig = { r.a, r.x, r.y, r.p };
    if (!agree(rt, entry.s, returned, orig_ram, &orig, image, &c)) {
        if (!drill) {
            check_inner(i, in, true);
        }
        report(rt, &entry, top->name, returned, orig_ram, &orig, image, &c);
    }
    dc_stats[i].compared++;
}

void Decomp_Enter(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    if (!dc_top_entered && routine == dc_top->addr) {
        dc_top_entered = true;
        return;
    }
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->addr != routine || (routine < 0xC000 && dc_routine[i]->bank != dc_c_bank)) {
            continue;
        }
        if (s_inner) {
            return;
        }
        if (dc_routine[i]->task) {
            dc_enter_task(i, routine);
        } else if (dc_mode == DECOMP_CHECK && !dc_routine[i]->site_id) {
            check_inner(i, (Regs){ a, x, y, dc_entry_p }, false);
        }
        return;
    }
    dc_fail("Decomp_Enter names a routine that is in no table", routine);
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
    const DecompRoutine *rt = dc_routine[i];
    CycModRegs o;
    uint64_t cycles = run_original(rt, in, &o, false);
    dc_stats[i].calls++;
    if (cycles && spans_frame_end(cycles)) {
        dc_stats[i].original++;
        return 0;
    }
    Regs c;
    dc_run_c(rt, in, &c);
    if (dc_run_original) {
        dc_stats[i].original++;
        return 0;
    }
    if (!cycles) {
        dc_fail("the original routine did not return in an isolated call", rt->addr);
    }
    bool cancelled = false;
    for (int k = 0; k < dc_damage_raised; k++) {
        cancelled |= dc_damage_cancel[k];
    }
    if (cancelled) {
        cycles = run_original(rt, in, &o, true);
    }
    if (dc_nreads) {
        cyc_mod_set_ram_read_hook(isolated_read);
        cycles = run_original(rt, in, &o, cancelled);
        cyc_mod_set_ram_read_hook(NULL);
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (dc_live(a, in->s) && ram[a] != dc_image[a]) {
            cyc_mod_poke(a, dc_image[a]);
        }
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    CycModRegs set = { c.a, c.x, c.y, in->s, (uint8_t)((c.p & flags) | (in->p & ~flags)), 0 };
    cyc_mod_set_regs(&set);
    uint64_t spare = spare_cycles(cycles);
    spend_cycles(rt->addr, spare - dc_play_ops(dc_played, spare, rt->addr));
    return 1;
}

/* ---- task code ---- */

/* Whether VBlank starts within this many cycles from now (a margin covers the
 * last instruction). The NMI's handler steps state the task code also uses
 * (the random generator, the scratch bytes): the original takes it inside a
 * long task routine, and taking it after the C's results would change the
 * order, so such a call goes to the original. */
static bool crosses_vblank(uint64_t cycles)
{
    hw_native_flush();
    long now = (long)ppu.scanline * 341 + ppu.dot;
    long dots = 241L * 341 + 1 - now;
    if (dots <= 0) {
        dots += 262L * 341;
    }
    return cycles + 12 >= (uint64_t)(dots / 3);
}

/* A site reached inside the isolated run of a task routine's original. */
static int task_isolated_site(uint16_t addr)
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

/* The original task routine alone, from its start to its RTS or, when the C
 * handed off, to its arrival at the same routine at the same depth; its RAM
 * into s_orig, the rest into s_run. The call enters at an RTS: the observer
 * moves its return to the start, with S as the routine has it there (the
 * RTS's cycles are not the routine's). */
static struct {
    const DecompRoutine *rt;
    CycModRegs in;
    unsigned bank;
    uint8_t ram[GAME_RAM_SIZE], orig[GAME_RAM_SIZE];
    TaskRun run;
} s_timed;

static void run_task_original(const DecompRoutine *rt, const CycModRegs *in)
{
    const uint8_t *ram = cyc_cpu_ram();
    unsigned bank = hw_prg_bank4(0x8000);
    if (dc_ntime && s_timed.rt == rt && s_timed.in.a == in->a && s_timed.in.x == in->x && s_timed.in.y == in->y &&
        s_timed.in.s == in->s && s_timed.in.p == in->p && s_timed.bank == bank &&
        !memcmp(s_timed.ram, ram, GAME_RAM_SIZE)) {
        s_run = s_timed.run;
        memcpy(s_orig, s_timed.orig, GAME_RAM_SIZE);
        return;
    }
    if (dc_ntime) {
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
    s_damage_replayed = 0;
    s_run.start = rt->addr;
    s_run.start_s = in->s;
    s_run.call_s = rt->own_stack ? 0xFF : (uint8_t)(in->s + rt->locals + 2);
    s_run.fixup = true;
    s_run.rti = rt->interrupt;
    s_run.stop_armed = dc_handoff_set;
    s_run.stop_pc = dc_handoff;
    s_run.stop_s = dc_cur_s;
    s_run.stop_bank = dc_handoff_bank;
    s_run_active = true;
    uint64_t io = hw_isolated_io_writes;
    cyc_mod_stats(&before);
    CycModRegs r = *in;
    r.s = s_run.call_s;
    s_run.returned = cyc_mod_call(DT_CODE_RTS, &r) && !s_run.fixup;
    cyc_mod_stats(&after);
    s_run_active = false;
    s_run.io = hw_isolated_io_writes - io;
    s_run.cycles = after.cycles - before.cycles - s_run.entry_cycles;
    if (!s_run.stopped) {
        s_run.regs = r;
    }
    memcpy(s_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    cyc_mod_isolate_end();
    if (dc_ntime) {
        s_timed.run = s_run;
        memcpy(s_timed.orig, s_orig, GAME_RAM_SIZE);
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
    if (a < DT_ADDR_STACK_FLOOR) {
        return true;
    }
    return dc_handoff_set ? off > dc_cur_s : off >= dc_cur_s + 3u;
}

static bool task_same(const DecompRoutine *rt, const Regs *c)
{
    if (!s_run.returned || dc_handoff_set != s_run.stopped || s_run.io != (uint64_t)dc_stores ||
        (dc_ntime && dc_cycles != s_run.cycles)) {
        return false;
    }
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (task_live(a) && s_orig[a] != dc_image[a]) {
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
    if ((out & OUT_P) && ((o->p ^ c->p) & (P_N | P_V | P_Z | P_C))) {
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
    if (dc_ntime) {
        fprintf(stderr, "[decomp]   cycles original %llu decompiled %llu\n", (unsigned long long)s_run.cycles,
                (unsigned long long)dc_cycles);
    }
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!task_live(a) || s_orig[a] == dc_image[a]) {
            continue;
        }
        if (shown++ < 24) {
            fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, s_orig[a], dc_image[a]);
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

static void task_check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return;
    }
    run_task_original(rt, in);
    if (rt->interrupt && !dc_handoff_set) {
        c.p = dc_image[0x100 | (uint8_t)(dc_cur_s + 1)];
    }
    if (!task_same(rt, &c)) {
        task_report(rt, in, &c);
    }
    dc_stats[i].compared++;
    dc_nested_compared();
}

/* Replace mode: the C's results stored, its device writes made and the
 * original's cycles spent; then the CPU goes on where the C ends (the routine
 * it hands off to, or its RTS). False when the original runs instead. */
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
    run_task_original(rt, in);
    if (!s_run.returned || dc_handoff_set != s_run.stopped) {
        dc_fail("the original task routine did not end where the C does in an isolated call", rt->addr);
    }
    if (dc_ntime && dc_cycles != s_run.cycles) {
        dc_fail("the cycles the C states are not the original's", rt->addr);
    }
    bool rti = rt->interrupt && !dc_handoff_set;
    uint64_t debt = rti ? s_nmi_debt : 0;
    if (spans_frame_end(s_run.cycles + debt) || crosses_vblank(s_run.cycles + debt)) {
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
    uint8_t p = rti                                            ? c.p
                : dc_handoff_set && !(dc_handoff_uses & OUT_P) ? in->p
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
    s_nmi_debt -= debt;
    spend_cycles(rt->addr, (s_run.cycles > (uint64_t)dc_stores ? s_run.cycles - (uint64_t)dc_stores : 1) + debt);
    dc_stats[i].compared++;
    dc_nested_compared();
    cpu.pc = pc;
    return true;
}

/* The RTS observer: inside a task routine's isolated run it moves the entry
 * RTS to the routine's start; on the machine it starts the C of a resume
 * point the RTS returned to (and of the next one, when that C returns). */
static void on_return(void)
{
    if (cyc_mod_isolated()) {
        if (s_run_active && s_run.fixup) {
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
            task_check(i, &in);
            return;
        }
        if (!task_replace(i, &in) || dc_handoff_set) {
            return;
        }
    }
}

/* ---- interrupt routines: the NMI handler ---- */

#define NMI_WRITES_MAX 1024

/* The writes to $2006, $2007 and the APU's registers (not the sprite DMA or
 * the joypad strobe), in order. */
typedef struct {
    int n;

    struct {
        uint16_t addr;
        uint8_t value;
    } w[NMI_WRITES_MAX];
} NmiWrites;

static bool sequenced(uint16_t addr)
{
    return addr == PPU_ADDR || addr == PPU_DATA ||
           (addr >= APU_BASE && addr <= APU_FRAME && addr != OAM_DMA && addr != JOYPAD1);
}

static void nmi_write(NmiWrites *s, uint16_t addr, uint8_t value)
{
    if (s->n == NMI_WRITES_MAX) {
        dc_fail("too many device writes in one NMI", DT_CODE_NMI);
    }
    s->w[s->n].addr = addr;
    s->w[s->n].value = value;
    s->n++;
}

/* Check mode's live check of the NMI handler: the C's results, kept from the
 * NMI until the original's RTI. */
static struct {
    bool armed;
    int routine;
    uint8_t s;
    Regs regs;
    uint8_t ram[GAME_RAM_SIZE];
    HwState hw;
    uint8_t oam[256];
    char fields[REGS_TEXT];
    NmiWrites c, live;
} s_nmi;

/* The device fields the NMI sets that stay as set until its RTI: the PPU's
 * control and mask bits, t, fine X and the write latch (v, OAMADDR and the
 * status flags move with rendering), and the APU's written registers, frame
 * counter mode and DMC settings, with the $4015 enables (the length counters
 * and timers run). */
static const char *const NMI_FIELDS[] = {
    "ppu.nmi_enable",     "ppu.inc32",      "ppu.sprite16",  "ppu.sprite_table",    "ppu.bg_table",
    "ppu.show_bg8",       "ppu.show_spr8",  "ppu.show_bg",   "ppu.show_spr",        "ppu.t",
    "ppu.fine_x",         "ppu.addr_latch", "apu.five_step", "apu.irq_inhibit",     "apu.reg",
    "apu.dmc_irq_enable", "apu.dmc_loop",   "apu.dmc_rate",  "apu.dmc_sample_addr", "apu.dmc_sample_len",
    "apu.dmc_enable",
};
#define LENGTH_COUNTER_BYTES 5 /* count, halt, reload, reload value, enabled ($4015) */

static bool nmi_field(const char *line)
{
    for (size_t k = 0; k < sizeof NMI_FIELDS / sizeof NMI_FIELDS[0]; k++) {
        size_t n = strlen(NMI_FIELDS[k]);
        if (!strncmp(line, NMI_FIELDS[k], n) && line[n] == ' ') {
            return true;
        }
    }
    return false;
}

/* The fields of NMI_FIELDS as text, and the enabled byte of each length
 * counter. */
static void nmi_fields(char *out)
{
    FILE *f = device_dump();
    char line[512];
    size_t used = 0;
    while (fgets(line, sizeof line, f) && line[0]) {
        if (!strncmp(line, "apu.len ", 8)) {
            const char *hex = line + 8;
            char enabled[64];
            int n = snprintf(enabled, sizeof enabled, "apu.len.enabled");
            for (int ch = 0; ch < 4; ch++) {
                const char *b = hex + 2 * (ch * LENGTH_COUNTER_BYTES + LENGTH_COUNTER_BYTES - 1);
                n += snprintf(enabled + n, sizeof enabled - (size_t)n, " %c%c", b[0], b[1]);
            }
            snprintf(line, sizeof line, "%s\n", enabled);
        } else if (!nmi_field(line)) {
            continue;
        }
        size_t len = strlen(line);
        if (used + len + 1 > REGS_TEXT) {
            dc_fail("the NMI's device fields do not fit", DT_CODE_NMI);
        }
        memcpy(out + used, line, len);
        used += len;
    }
    out[used] = 0;
}

/* The original's writes, from the trace's cycle observer between the NMI and
 * its RTI; the trace's own state is put back after. */
static struct {
    bool on, was;
    void *file;
    uint64_t hash;
    uint32_t cycle;
    void (*prev)(const CycTraceCycle *cycle);
} s_watch;

static void nmi_observe(const CycTraceCycle *cycle)
{
    if (s_watch.prev) {
        s_watch.prev(cycle);
    }
    if (cycle->kind == 'W' && !cycle->dma && sequenced(cycle->addr)) {
        nmi_write(&s_nmi.live, cycle->addr, cycle->value);
    }
}

static void nmi_watch(bool on)
{
    if (on == s_watch.on) {
        return;
    }
    s_watch.on = on;
    if (on) {
        s_watch.was = cyc_trace_enabled;
        s_watch.file = cyc_trace_file;
        s_watch.hash = cyc_trace_hash;
        s_watch.cycle = cyc_trace_cycle;
        s_watch.prev = cyc_trace_cycle_hook;
        if (!s_watch.was) {
            cyc_trace_file = NULL;
        }
        cyc_trace_cycle_hook = nmi_observe;
        cyc_trace_enabled = true;
        return;
    }
    cyc_trace_cycle_hook = s_watch.prev;
    cyc_trace_enabled = s_watch.was;
    if (!s_watch.was) {
        cyc_trace_file = s_watch.file;
        cyc_trace_hash = s_watch.hash;
        cyc_trace_cycle = s_watch.cycle;
    }
}

/* The C at the NMI, its bus operations made on a copy of the machine; the
 * original then runs for real. */
static void nmi_check(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    if (s_nmi.armed) {
        dc_fail("the NMI handler did not reach its RTI", rt->addr);
    }
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    if (dc_run_original) {
        dc_stats[i].original++;
        return;
    }
    static CycSnapshot *snap;
    if (!snap && !(snap = cyc_snapshot_new())) {
        dc_fail("out of memory for the machine snapshot", rt->addr);
    }
    cyc_snapshot_take(snap);
    memcpy(hw.ram, dc_image, sizeof hw.ram);
    cyc_ring_muted = 1;
    cyc_hooks_suspend(true);
    dc_play_ops(0, (uint64_t)dc_nops * SCOPE_OP_GAP, rt->addr);
    s_nmi.hw = hw_state();
    memcpy(s_nmi.oam, cyc_ppu_oam(), sizeof s_nmi.oam);
    nmi_fields(s_nmi.fields);
    cyc_snapshot_restore(snap);
    cyc_hooks_suspend(false);
    cyc_ring_muted = 0;
    s_nmi.c.n = s_nmi.live.n = 0;
    for (int k = 0; k < dc_nops; k++) {
        if (dc_ops[k].write && sequenced(dc_ops[k].addr)) {
            nmi_write(&s_nmi.c, dc_ops[k].addr, dc_ops[k].value);
        }
    }
    nmi_watch(true);
    memcpy(s_nmi.ram, dc_image, GAME_RAM_SIZE);
    s_nmi.regs = c;
    s_nmi.s = in->s;
    s_nmi.routine = i;
    s_nmi.armed = true;
}

/* Page 1 above the handler's entry S (the interrupt's P and return address)
 * and all of RAM outside it; what the handler pushed below is dead after its
 * RTI. */
static bool nmi_live(uint16_t a)
{
    return (a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_FLOOR || (uint8_t)a > s_nmi.s;
}

static void nmi_report(const DecompRoutine *rt, const CycModRegs *o, const uint8_t *ram, const HwState *hw,
                       const char *fields)
{
    const Regs *c = &s_nmi.regs;
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at its RTI, frame %llu\n", rt->name, rt->addr,
            (unsigned long long)cyc_ring_frame);
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (nmi_live(a) && ram[a] != s_nmi.ram[a] && shown++ < 24) {
            fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, ram[a], s_nmi.ram[a]);
        }
    }
    if (shown > 24) {
        fprintf(stderr, "[decomp]   ... %d bytes differ\n", shown);
    }
    fprintf(stderr,
            "[decomp]   registers original A=%02X X=%02X Y=%02X S=%02X P=%02X decompiled A=%02X X=%02X Y=%02X S=%02X "
            "P=%02X\n",
            o->a, o->x, o->y, o->s, o->p, c->a, c->x, c->y, s_nmi.s, c->p);
    fprintf(stderr, "[decomp]   hardware:%s%s%s%s%s\n", hw->ciram != s_nmi.hw.ciram ? " CIRAM" : "",
            hw->palette != s_nmi.hw.palette ? " palette" : "", hw->chr != s_nmi.hw.chr ? " CHR-RAM" : "",
            hw->oam != s_nmi.hw.oam ? " OAM" : "", hw->bank != s_nmi.hw.bank ? " PRG bank" : "");
    const uint8_t *oam = cyc_ppu_oam();
    for (int k = 0, n = 0; k < 256 && n < 8; k++) {
        if (oam[k] != s_nmi.oam[k]) {
            fprintf(stderr, "[decomp]   OAM $%02X original %02X decompiled %02X\n", k, oam[k], s_nmi.oam[k]);
            n++;
        }
    }
    const NmiWrites *lw = &s_nmi.live, *cw = &s_nmi.c;
    if (lw->n != cw->n || memcmp(lw->w, cw->w, sizeof lw->w[0] * (size_t)lw->n)) {
        int k = 0;
        while (k < lw->n && k < cw->n && lw->w[k].addr == cw->w[k].addr && lw->w[k].value == cw->w[k].value) {
            k++;
        }
        fprintf(stderr, "[decomp]   device writes: original %d decompiled %d, first difference at %d:", lw->n, cw->n,
                k);
        if (k < lw->n) {
            fprintf(stderr, " original $%04X=%02X", lw->w[k].addr, lw->w[k].value);
        }
        if (k < cw->n) {
            fprintf(stderr, " decompiled $%04X=%02X", cw->w[k].addr, cw->w[k].value);
        }
        fputc('\n', stderr);
    }
    const char *x = fields, *d = s_nmi.fields;
    while (*x && *d) {
        size_t lx = strcspn(x, "\n"), ld = strcspn(d, "\n");
        if (lx != ld || memcmp(x, d, lx)) {
            fprintf(stderr, "[decomp]   original %.*s / decompiled %.*s\n", (int)lx, x, (int)ld, d);
        }
        x += lx + (x[lx] != 0);
        d += ld + (d[ld] != 0);
    }
    fflush(stderr);
    exit(3);
}

/* At the original's RTI: the machine as the original left it against the
 * C's. */
static void nmi_compare(void)
{
    if (!s_nmi.armed) {
        return;
    }
    s_nmi.armed = false;
    nmi_watch(false);
    const DecompRoutine *rt = dc_routine[s_nmi.routine];
    CycModRegs o;
    cyc_mod_regs(&o);
    const uint8_t *ram = cyc_cpu_ram();
    HwState hw = hw_state();
    static char fields[REGS_TEXT];
    nmi_fields(fields);
    const Regs *c = &s_nmi.regs;
    bool same = o.s == s_nmi.s && o.a == c->a && o.x == c->x && o.y == c->y &&
                !((o.p ^ c->p) & (P_N | P_V | P_Z | P_C)) && hw.ciram == s_nmi.hw.ciram &&
                hw.palette == s_nmi.hw.palette && hw.chr == s_nmi.hw.chr && hw.oam == s_nmi.hw.oam &&
                hw.bank == s_nmi.hw.bank && !strcmp(fields, s_nmi.fields) && s_nmi.live.n == s_nmi.c.n &&
                !memcmp(s_nmi.live.w, s_nmi.c.w, sizeof s_nmi.c.w[0] * (size_t)s_nmi.c.n);
    for (uint16_t a = 0; same && a < GAME_RAM_SIZE; a++) {
        same = !nmi_live(a) || ram[a] == s_nmi.ram[a];
    }
    if (!same) {
        nmi_report(rt, &o, ram, &hw, fields);
    }
    dc_stats[s_nmi.routine].compared++;
}

#define NMI_CYCLE_LIMIT 32768

static struct {
    uint64_t start;
    unsigned n;
    bool fetch;

    struct {
        uint16_t offset, addr;
        uint8_t value;
        char kind;
        bool fetch, done;
    } cycles[NMI_CYCLE_LIMIT];
} s_nmi_timing;

static void nmi_measure_cycle(const CycTraceCycle *cycle)
{
    if (cycle->dma) {
        return;
    }
    unsigned k = s_nmi_timing.n++;
    if (k >= NMI_CYCLE_LIMIT || hw.cycles - s_nmi_timing.start >= NMI_CYCLE_LIMIT || cycle->kind == '-') {
        dc_fail("cannot record an NMI bus cycle", DT_CODE_NMI);
    }
    s_nmi_timing.cycles[k].offset = (uint16_t)(hw.cycles - s_nmi_timing.start);
    s_nmi_timing.cycles[k].addr = cycle->addr;
    s_nmi_timing.cycles[k].value = cycle->value;
    s_nmi_timing.cycles[k].kind = cycle->kind;
    s_nmi_timing.cycles[k].fetch = s_nmi_timing.fetch;
    s_nmi_timing.cycles[k].done = cycle->done;
    s_nmi_timing.fetch = false;
}

/* Timing comes from the same machine and sprite-0 hit, with only the game
 * CPU interpreting. Stop before giving up a frame/observer boundary, a
 * nested interrupt or the RTI itself. Only the bus schedule survives. */
static bool nmi_measure(uint8_t entry_s, uint64_t *duration)
{
    static CycSnapshot *snap;
    if (!snap && !(snap = cyc_snapshot_new())) {
        dc_fail("out of memory for the NMI timing snapshot", DT_CODE_NMI);
    }
    cyc_snapshot_take(snap);
    bool traced = cyc_trace_enabled;
    void *file = cyc_trace_file;
    uint64_t hash = cyc_trace_hash;
    uint32_t cycle = cyc_trace_cycle;
    void (*observer)(const CycTraceCycle *) = cyc_trace_cycle_hook;
    void (*return_hook)(void) = cyc_cpu_rts_observer;
    uint8_t muted = cyc_ring_muted;
    cyc_trace_enabled = true;
    cyc_trace_file = NULL;
    cyc_trace_cycle_hook = nmi_measure_cycle;
    cyc_cpu_rts_observer = NULL;
    cyc_ring_muted = 1;
    s_nmi_timing.start = cyc_cycle_count();
    s_nmi_timing.n = 0;
    while (cpu.pc != DT_SITE_NMI_EXIT && !hw_frame_done && !cpu.jammed && !cpu.do_nmi && !cpu.do_irq && !cpu.do_reset &&
           cyc_cycle_count() - s_nmi_timing.start < NMI_CYCLE_LIMIT - 1024) {
        s_nmi_timing.fetch = true;
        cpu_interp_step();
    }
    bool reached = cpu.pc == DT_SITE_NMI_EXIT && cpu.s == entry_s && !hw_frame_done && !cpu.do_nmi && !cpu.do_irq &&
                   !cpu.do_reset;
    *duration = cyc_cycle_count() - s_nmi_timing.start;
    cyc_snapshot_restore(snap);
    cyc_trace_enabled = traced;
    cyc_trace_file = file;
    cyc_trace_hash = hash;
    cyc_trace_cycle = cycle;
    cyc_trace_cycle_hook = observer;
    cyc_cpu_rts_observer = return_hook;
    cyc_ring_muted = muted;
    return reached;
}

/* Report the first bus operation that could not be placed at its original
 * cycle. A mismatch must not silently turn into a replace fallback. */
static void nmi_timing_fail(unsigned k, int op)
{
    fprintf(stderr, "[decomp] NMI timing: original cycle %u %c $%04X=%02X, C bus operation %d of %d\n",
            s_nmi_timing.cycles[k].offset, s_nmi_timing.cycles[k].kind, s_nmi_timing.cycles[k].addr,
            s_nmi_timing.cycles[k].value, op, dc_nops);
    if (op < dc_nops) {
        fprintf(stderr, "[decomp]   C %c $%04X=%02X mask=%02X\n", dc_ops[op].write ? 'W' : 'R', dc_ops[op].addr,
                dc_ops[op].value, dc_ops[op].wait);
    }
    dc_fail("NMI bus operation does not match the original", DT_CODE_NMI);
}

/* Drive the original's non-device bus traffic without storing any of its
 * RAM results. DMA sees the C's RAM, and RDY generates the same stalls as
 * before; each CPU access checks its offset after those stalls. A C wait
 * consumes as many original reads as it takes to satisfy its mask. */
static void nmi_replay(uint64_t duration)
{
    uint64_t start = cyc_cycle_count();
    int op = 0;
    for (unsigned k = 0; k < s_nmi_timing.n; k++) {
        uint16_t addr = s_nmi_timing.cycles[k].addr;
        uint8_t value = s_nmi_timing.cycles[k].value;
        bool write = s_nmi_timing.cycles[k].kind == 'W';
        bool device = addr >= 0x2000 && (write || (addr < 0x6000 && s_nmi_timing.cycles[k].done));
        if (device) {
            if (op == dc_nops || dc_ops[op].addr != addr || dc_ops[op].write != write ||
                (write && dc_ops[op].value != value)) {
                nmi_timing_fail(k, op);
            }
            if (write) {
                value = dc_ops[op].value;
            }
        }
        hw_cycle_start(addr, write ? HW_WRITE : s_nmi_timing.cycles[k].fetch ? HW_FETCH : HW_READ);
        if (cyc_cycle_count() - start != s_nmi_timing.cycles[k].offset) {
            nmi_timing_fail(k, op);
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
                nmi_timing_fail(k, op);
            }
            value = read;
        }
        if (device && (!dc_ops[op].wait || (value & dc_ops[op].wait) == dc_ops[op].value)) {
            op++;
        }
        hw_cycle_finish(s_nmi_timing.cycles[k].done);
    }
    if (op != dc_nops || cyc_cycle_count() - start != duration) {
        dc_fail("the NMI did not use its full bus schedule", DT_CODE_NMI);
    }
}

static void nmi_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    uint64_t duration;
    if (dc_run_original || !nmi_measure(in->s, &duration)) {
        dc_stats[i].original++;
        return;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (ram[a] != dc_image[a]) {
            cyc_mod_poke(a, dc_image[a]);
        }
    }
    nmi_replay(duration);
    CycModRegs set = { c.a, c.x, c.y, dc_cur_s, c.p, 0 };
    cyc_mod_set_regs(&set);
    uint64_t debt = s_nmi_debt;
    s_nmi_debt = 0;
    if (debt) {
        spend_cycles(DT_SITE_NMI_EXIT, debt);
    }
    cpu.pc = DT_SITE_NMI_EXIT;
    cpu_interp_step();
    dc_stats[i].compared++;
}

bool dc_isolated(void)
{
    return cyc_mod_isolated();
}

int dc_on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        if (s_run_active && task_isolated_site(addr)) {
            return 1;
        }
        return addr == DT_SITE_DAMAGE && s_damage_replayed < dc_damage_raised && dc_damage_cancel[s_damage_replayed++];
    }
    for (int i = 0; i < dc_count; i++) {
        if (!dc_at_entry(dc_routine[i], addr) || !dc_routine[i]->site_id) {
            continue;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
        if (dc_routine[i]->interrupt && !dc_routine[i]->task) {
            if (dc_mode == DECOMP_CHECK) {
                nmi_check(i, &in);
            } else {
                nmi_replace(i, &in);
            }
            return 0;
        }
        if (dc_routine[i]->task) {
            if (dc_mode == DECOMP_CHECK) {
                task_check(i, &in);
            }
            while (dc_mode == DECOMP_REPLACE && task_replace(i, &in) && dc_handoff_set && dc_handoff == addr) {
                cyc_mod_regs(&in);
            }
            return 0;
        }
        if (dc_mode == DECOMP_CHECK) {
            check(i, &in);
            return 0;
        }
        return replace(i, &in);
    }
    return 0;
}

/* The original NMI handler's RTI: check mode's comparison with the C, or the
 * cycles a replaced routine still owed when the NMI came. Not in an isolated
 * run (Nmi_Return's original reaches it). */
static int on_nmi_exit(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        return 0;
    }
    if (dc_mode == DECOMP_CHECK) {
        nmi_compare();
        return 0;
    }
    uint64_t debt = s_nmi_debt;
    s_nmi_debt = 0;
    if (debt) {
        spend_cycles(addr, debt);
    }
    return 0;
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_nmi_exit)
{
    nes_mod_register_function_entry_plugin(DT_SITE_NMI_EXIT_ID, DT_SITE_NMI_EXIT, on_nmi_exit);
}

bool Decomp_SetMode(DecompMode mode)
{
    static bool registered;
    if (!registered && mode != DECOMP_OFF) {
        registered = true;
        atexit(dc_report_stats);
    }
    dc_mode = mode;
    const char *only = getenv("DT_DECOMP_ONLY");
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->site_id) {
            bool on = mode != DECOMP_OFF && (!only || strstr(only, dc_routine[i]->name));
            nes_mod_set_function_hook_enabled(dc_routine[i]->site_id, on);
        }
    }
    for (int i = 0; i < gFlowStopCount; i++) {
        nes_mod_set_function_hook_enabled(gFlowStops[i].site_id, mode != DECOMP_OFF);
    }
    nes_mod_set_function_hook_enabled(DT_SITE_NMI_EXIT_ID, mode != DECOMP_OFF);
    s_nmi_debt = 0;
    s_nmi.armed = false;
    nmi_watch(false);
    cyc_mod_set_return_hook(mode != DECOMP_OFF ? on_return : NULL);
    return true;
}
