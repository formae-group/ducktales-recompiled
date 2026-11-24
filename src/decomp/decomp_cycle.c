/*
 * decomp_cycle.c - the cycle host's backend of the decompilation layer
 * (decomp_backend.h, docs/DECOMPILATION.md): runs the decompiled routines in
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
 * Task code (decomp.h, docs/DECOMPILATION.md "Task code") runs the same way,
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
 *
 * The backend is four files that share decomp_cycle.h: this one (the
 * isolated scope, the plain routines' check and replace, the sites and
 * --decomp), decomp_cycle_task.c (task code), decomp_cycle_nmi.c (the NMI
 * handler) and decomp_cycle_clock.c (the original on the running clock).
 */
#include "decomp/decomp_cycle.h"
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

#define FRAME_END_MARGIN 32
#define LOAD_GAP         3

uint8_t dc_orig[GAME_RAM_SIZE];
static bool s_scope;
static bool s_inner;
int dc_damage_replayed;

static int s_ops_in_scope;

static HwState s_orig_hw;
static char s_orig_regs[REGS_TEXT], s_c_regs[REGS_TEXT];
static FILE *s_dump;
static uint64_t s_early;
uint64_t dc_nmi_debt;
static bool s_regs_text;

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
FILE *dc_device_dump(void)
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
    FILE *f = dc_device_dump();
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

HwState dc_hw_state(void)
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
    dc_timing_enter(routine, "Decomp_Call");
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
        dc_timing_op(dc_nops - 1);
        s_ops_in_scope = dc_nops;
        return value;
    }
    uint64_t gap = (uint64_t)(dc_nops - dc_played + 1) * LOAD_GAP;
    s_early += dc_play_ops(dc_played, gap, dc_top->addr);
    value = cpu_read(addr, 0);
    dc_record(addr, value, false);
    dc_timing_op(dc_nops - 1);
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

/* The original routine alone on the machine's state: its RAM into dc_orig,
 * its registers into *out; returns its cycle count, 0 when it did not
 * return. */
static uint64_t run_original(const DecompRoutine *rt, const CycModRegs *in, CycModRegs *out, bool hooked)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) {
        dc_fail("cannot open an isolated scope", rt->addr);
    }
    dc_damage_replayed = 0;
    cyc_mod_allow_isolated_hooks(hooked);
    cyc_mod_stats(&before);
    *out = *in;
    bool returned = cyc_mod_call(rt->addr, out);
    cyc_mod_stats(&after);
    memcpy(dc_orig, cyc_cpu_ram(), GAME_RAM_SIZE);
    if (rt->out & OUT_HW) {
        s_orig_hw = dc_hw_state();
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
void dc_spend_cycles(uint16_t addr, uint64_t cycles)
{
    while (cycles > 1 && !cpu.nmi_edge) {
        (void)cpu_read(addr, 0);
        cycles--;
    }
    dc_nmi_debt += cycles - 1;
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
    HwState c = dc_hw_state();
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
    if (!agree(rt, in->s, cycles != 0, dc_orig, &orig, dc_image, &c)) {
        report(rt, in, NULL, cycles != 0, dc_orig, &orig, dc_image, &c);
    }
    if (rt->out & OUT_HW) {
        check_hw(rt, in, cycles);
    }
    if (rt->timed) {
        dc_timing_check(rt, in);
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
    dc_timing_pause(true);
    rt->run(&c);
    dc_timing_pause(false);
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
    dc_timing_enter(routine, "Decomp_Enter");
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
bool dc_spans_frame_end(uint64_t cycles)
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
    if (cycles && dc_spans_frame_end(cycles)) {
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
    dc_spend_cycles(rt->addr, spare - dc_play_ops(dc_played, spare, rt->addr));
    return 1;
}

bool dc_isolated(void)
{
    return cyc_mod_isolated();
}

int dc_on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        if (dc_run_active && dc_task_isolated_site(addr)) {
            return 1;
        }
        return addr == DT_SITE_DAMAGE && dc_damage_replayed < dc_damage_raised &&
               dc_damage_cancel[dc_damage_replayed++];
    }
    for (int i = 0; i < dc_count; i++) {
        if (!dc_at_entry(dc_routine[i], addr) || !dc_routine[i]->site_id) {
            continue;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
        if (dc_routine[i]->interrupt && !dc_routine[i]->task) {
            if (dc_mode == DECOMP_CHECK) {
                dc_nmi_check(i, &in);
            } else {
                dc_nmi_replace(i, &in);
            }
            return 0;
        }
        if (dc_routine[i]->task) {
            if (dc_mode == DECOMP_CHECK) {
                dc_task_check(i, &in);
            }
            while (dc_mode == DECOMP_REPLACE && dc_task_replace(i, &in) && dc_handoff_set && dc_handoff == addr) {
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

bool Decomp_SetMode(DecompMode mode)
{
    static bool registered;
    if (!registered && mode != DECOMP_OFF) {
        registered = true;
        atexit(dc_report_stats);
        atexit(dc_timing_summary);
    }
    dc_mode = mode;
    const char *only = getenv("DT_DECOMP_ONLY");
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->site_id) {
            bool on = mode != DECOMP_OFF && (!only || strstr(only, dc_routine[i]->name));
            nes_mod_set_function_hook_enabled(dc_routine[i]->site_id, on);
        }
    }
    nes_mod_set_function_hook_enabled(DT_SITE_NMI_EXIT_ID, mode != DECOMP_OFF);
    dc_nmi_debt = 0;
    dc_nmi_disarm();
    cyc_mod_set_return_hook(mode != DECOMP_OFF ? dc_on_return : NULL);
    return true;
}
