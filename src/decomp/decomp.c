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
 * handler's exit (DT_SITE_NMI_EXIT) spends it, so the interrupted code
 * resumes at the console's time.
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

#define MAX_ROUTINES     512
#define MAX_BUS_OPS      16384
#define MAX_STORES       64
#define MAX_NESTED       32
#define FRAME_END_MARGIN 32
#define SCOPE_OP_GAP     8
#define LOAD_GAP         3
#define REGS_TEXT        0x4000

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
    uint64_t ciram, palette, chr, oam, regs;
    unsigned bank;
} HwState;

static HwState s_orig_hw;
static char s_orig_regs[REGS_TEXT], s_c_regs[REGS_TEXT];
static FILE *s_dump;
static int s_played, s_loads;
static uint64_t s_early, s_nmi_debt;
static bool s_regs_text;
static uint8_t s_c_bank, s_handoff_bank;
static uint8_t s_cur_s;
static bool s_handoff_set;
static uint16_t s_handoff;
static uint8_t s_handoff_uses;

static struct {
    uint16_t addr;
    uint8_t value;
} s_store[MAX_STORES];

static int s_stores;
static bool s_task; /* the routine run_c runs is task code */
/* The task routines whose C the task routine run_c runs reached in C: its
 * comparison covers them. */
static int s_nested[MAX_NESTED], s_nnested;

typedef struct {
    uint16_t start, stop_pc;
    uint8_t start_s, call_s, stop_s, stop_bank;
    uint8_t ret[2]; /* the isolated call's return address, where the RTS observer left it */
    bool fixup, stop_armed, stopped, returned;
    CycModRegs regs;
    uint64_t entry_cycles, cycles, io;
} TaskRun;

static TaskRun s_run;
static bool s_run_active;

/* Resume points of task code (fixed bank), by address bit. */
static uint8_t s_resume[0x10000 / 8];

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

static void record_store(uint16_t addr, uint8_t value);

/* Task code makes its device writes when it ends (task_replace), so a write
 * of a routine it calls in C is one of its stores too. */
void Decomp_BusWrite(uint16_t addr, uint8_t value)
{
    record(addr, value, true);
    if (s_task) {
        record_store(addr, value);
    }
    Game_MapperWrite(addr, value);
    if (Game_RomView() >= 0) {
        s_c_bank = (uint8_t)Game_RomView();
    }
}

void Decomp_BusRead(uint16_t addr)
{
    record(addr, 0, false);
}

/* The recorded bus operations from index from on, made on the machine (or
 * on the isolated copy inside a scope), with spare reads of addr spread
 * evenly between them: the PPU acts on a $2006 or $2007 write a few cycles
 * late, so the operations cannot come back to back. Returns the reads made,
 * fewer than spare when spare is not 0. */
static uint64_t play_ops(int from, uint64_t spare, uint16_t addr)
{
    uint64_t slots = (uint64_t)(s_nops - from) + 1, spent = 0;
    for (int i = from; i < s_nops; i++) {
        for (uint64_t due = spare * (uint64_t)(i - from + 1) / slots; spent < due; spent++) {
            (void)cpu_read(addr, 0);
        }
        if (s_ops[i].write) {
            cpu_write(s_ops[i].addr, s_ops[i].value, 0);
        } else {
            (void)cpu_read(s_ops[i].addr, 0);
        }
    }
    return spent;
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

/* The machine's device registers as text, the lines compared. */
static void device_regs(char *regs)
{
    if (!s_dump && !(s_dump = tmpfile())) {
        fail("cannot open a scratch file for the device state", 0);
    }
    rewind(s_dump);
    cyc_hw_state_dump(s_dump);
    fputc(0, s_dump);
    fflush(s_dump);
    rewind(s_dump);
    char line[512];
    size_t used = 0;
    while (fgets(line, sizeof line, s_dump) && line[0]) {
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
        play_ops(s_ops_in_scope, (uint64_t)(s_nops - s_ops_in_scope) * SCOPE_OP_GAP, routine);
    }
    s_ops_in_scope = s_nops;
    return cyc_mod_call(routine, r);
}

Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y)
{
    return Decomp_CallP(routine, a, x, y, s_entry_p);
}

Regs Decomp_CallP(uint16_t routine, uint8_t a, uint8_t x, uint8_t y, uint8_t p)
{
    CycModRegs r = { a, x, y, s_cur_s, p, 0 };
    if (!call_in_scope(routine, &r)) {
        fail("an original routine called from C did not return", routine);
    }
    memcpy(gRam, cyc_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

void Decomp_RunOriginal(void)
{
    if (s_loads) {
        fail("a routine gave its call to the original after a device load", s_top->addr);
    }
    s_run_original = true;
}

/* A read the C needs the value of, made with the operations before it: in
 * the C's scope in check mode, on the machine in replace mode. */
uint8_t Decomp_BusLoad(uint16_t addr)
{
    if (s_top->task) {
        fail("task code reads a device", addr);
    }
    s_loads++;
    uint8_t value;
    if (s_mode == DECOMP_CHECK) {
        if (!s_scope) {
            if (!cyc_mod_isolate_begin()) {
                fail("cannot open an isolated scope", addr);
            }
            s_scope = true;
        }
        play_ops(s_ops_in_scope, (uint64_t)(s_nops - s_ops_in_scope + 1) * SCOPE_OP_GAP, addr);
        value = cpu_read(addr, 0);
        record(addr, value, false);
        s_ops_in_scope = s_nops;
        return value;
    }
    uint64_t gap = (uint64_t)(s_nops - s_played + 1) * LOAD_GAP;
    s_early += play_ops(s_played, gap, s_top->addr);
    value = cpu_read(addr, 0);
    record(addr, value, false);
    s_played = s_nops;
    return value;
}

void Decomp_Push(uint8_t value)
{
    gRam[0x100 | s_cur_s] = value;
    s_cur_s--;
}

uint8_t Decomp_Pull(void)
{
    s_cur_s++;
    return gRam[0x100 | s_cur_s];
}

void Decomp_SetStack(uint8_t s)
{
    s_cur_s = s;
}

void Decomp_PushReturn(uint16_t resume)
{
    uint16_t ret = (uint16_t)(resume - 1);
    Decomp_Push((uint8_t)(ret >> 8));
    Decomp_Push((uint8_t)ret);
}

void Decomp_PullReturn(void)
{
    s_cur_s = (uint8_t)(s_cur_s + 2);
}

bool Decomp_HandedOff(void)
{
    return s_handoff_set;
}

void Decomp_Handoff(uint16_t routine, uint16_t resume, uint8_t uses)
{
    if (resume) {
        Decomp_PushReturn(resume);
    }
    s_handoff = routine;
    s_handoff_bank = s_c_bank;
    s_handoff_uses = uses;
    s_handoff_set = true;
}

static void record_store(uint16_t addr, uint8_t value)
{
    if (s_stores == MAX_STORES) {
        fail("too many device writes in one routine", addr);
    }
    s_store[s_stores].addr = addr;
    s_store[s_stores].value = value;
    s_stores++;
}

void Decomp_SelectBank(uint8_t bank)
{
    Decomp_Call(DT_CODE_BANK_SELECT, bank, 0, 0);
    s_c_bank = bank;
    record_store((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

void Decomp_Store(uint16_t addr, uint8_t value)
{
    record_store(addr, value);
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
    s_nops = s_ops_in_scope = s_played = s_loads = 0;
    s_early = 0;
    s_c_bank = (uint8_t)(hw_prg_bank4(0x8000) >> 2);
    s_cur_s = in->s;
    s_handoff_set = false;
    s_stores = 0;
    s_task = rt->task;
    s_nnested = 0;
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
    uint64_t used = (uint64_t)s_nops + s_early;
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
/* Check mode's hardware verdict. The register hashes differ also when only
 * the fields device_regs leaves out do; then both sides' register texts are
 * compared, the original's from a second run of it. */
static void check_hw(const DecompRoutine *rt, const CycModRegs *in, uint64_t cycles)
{
    if (!cyc_mod_isolate_begin()) {
        fail("cannot open an isolated scope", rt->addr);
    }
    play_ops(0, spare_cycles(cycles), rt->addr);
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
            c.bank != s_orig_hw.bank ? " PRG bank" : "", s_nops);
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
        check_hw(rt, in, cycles);
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
    int nops = s_nops, stores = s_stores, view = Game_RomView();
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
    s_stores = stores;
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
    CycModRegs r = { in.a, in.x, in.y, s_cur_s, in.p, 0 };
    CycModRegs entry = r;
    entry.s = (uint8_t)(s_cur_s - 2);
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
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->addr != routine || (routine < 0xC000 && s_routine[i]->bank != s_c_bank)) {
            continue;
        }
        if (s_inner) {
            return;
        }
        if (s_routine[i]->task) {
            s_stats[i].calls++;
            if (s_nnested == MAX_NESTED) {
                fail("too many task routines reached in C in one call", routine);
            }
            s_nested[s_nnested++] = i;
        } else if (s_mode == DECOMP_CHECK && !s_routine[i]->site_id) {
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
    uint64_t spare = spare_cycles(cycles);
    spend_cycles(rt->addr, spare - play_ops(s_played, spare, rt->addr));
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
    if (!s_run.stop_armed || s_run.stopped || addr != s_run.stop_pc || r.s != s_run.stop_s) {
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
static void run_task_original(const DecompRoutine *rt, const CycModRegs *in)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) {
        fail("cannot open an isolated scope", rt->addr);
    }
    cyc_mod_allow_isolated_hooks(true);
    memset(&s_run, 0, sizeof s_run);
    s_damage_replayed = 0;
    s_run.start = rt->addr;
    s_run.start_s = in->s;
    s_run.call_s = (uint8_t)(in->s + rt->locals + 2);
    s_run.fixup = true;
    s_run.stop_armed = s_handoff_set;
    s_run.stop_pc = s_handoff;
    s_run.stop_s = s_cur_s;
    s_run.stop_bank = s_handoff_bank;
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
    return s_handoff_set ? off > s_cur_s : off >= s_cur_s + 3u;
}

static bool task_same(const DecompRoutine *rt, const Regs *c)
{
    if (!s_run.returned || s_handoff_set != s_run.stopped || s_run.io != (uint64_t)s_stores) {
        return false;
    }
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (task_live(a) && s_orig[a] != s_image[a]) {
            return false;
        }
    }
    const CycModRegs *o = &s_run.regs;
    uint8_t out = s_handoff_set ? s_handoff_uses : rt->out;
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
    if (s_handoff_set) {
        fprintf(stderr, "[decomp]   the C hands off to $%04X at S=%02X; the original %s\n", s_handoff, s_cur_s,
                s_run.stopped    ? "got there"
                : s_run.returned ? "returned instead"
                                 : "did not return or get there");
    } else {
        fprintf(stderr, "[decomp]   the C returns; the original %s\n",
                s_run.stopped    ? "stopped"
                : s_run.returned ? "returned too"
                                 : "did not return");
    }
    fprintf(stderr, "[decomp]   device writes original %llu decompiled %d\n", (unsigned long long)s_run.io, s_stores);
    int shown = 0;
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (!task_live(a) || s_orig[a] == s_image[a]) {
            continue;
        }
        if (shown++ < 24) {
            fprintf(stderr, "[decomp]   $%04X original %02X decompiled %02X\n", a, s_orig[a], s_image[a]);
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

static void nested_compared(void)
{
    for (int k = 0; k < s_nnested; k++) {
        s_stats[s_nested[k]].compared++;
    }
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
    if (!task_same(rt, &c)) {
        task_report(rt, in, &c);
    }
    s_stats[i].compared++;
    nested_compared();
}

/* Replace mode: the C's results stored, its device writes made and the
 * original's cycles spent; then the CPU goes on where the C ends (the routine
 * it hands off to, or its RTS). False when the original runs instead. */
static bool task_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = s_routine[i];
    Regs c;
    run_c(rt, in, &c);
    s_stats[i].calls++;
    if (s_run_original) {
        s_stats[i].original++;
        return false;
    }
    run_task_original(rt, in);
    if (!s_run.returned || s_handoff_set != s_run.stopped) {
        fail("the original task routine did not end where the C does in an isolated call", rt->addr);
    }
    if (spans_frame_end(s_run.cycles) || crosses_vblank(s_run.cycles)) {
        s_stats[i].original++;
        return false;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        /* The routine's own return address is not compared (the isolated run
         * has its own there), but a C that popped it and pushed another (the
         * row change) leaves the bytes the original would. */
        bool own_return = (a & 0xFF00) == 0x0100 &&
                          ((uint8_t)a == s_run.call_s || (uint8_t)a == (uint8_t)(s_run.call_s - 1));
        if ((task_live(a) || own_return) && ram[a] != s_image[a]) {
            cyc_mod_poke(a, s_image[a]);
        }
    }
    uint8_t s = s_cur_s;
    uint16_t pc = s_handoff;
    if (!s_handoff_set) {
        pc = (uint16_t)((ram[0x100 | (uint8_t)(s + 1)] | ram[0x100 | (uint8_t)(s + 2)] << 8) + 1);
        s = (uint8_t)(s + 2);
    }
    uint8_t flags = P_N | P_V | P_Z | P_C;
    uint8_t p = s_handoff_set ? in->p : (uint8_t)((c.p & flags) | (in->p & ~flags));
    CycModRegs set = { c.a, c.x, c.y, s, p, 0 };
    if (s_handoff_set) {
        if (!(s_handoff_uses & OUT_A)) {
            set.a = s_run.regs.a;
        }
        if (!(s_handoff_uses & OUT_X)) {
            set.x = s_run.regs.x;
        }
        if (!(s_handoff_uses & OUT_Y)) {
            set.y = s_run.regs.y;
        }
    }
    cyc_mod_set_regs(&set);
    for (int k = 0; k < s_stores; k++) {
        cpu_write(s_store[k].addr, s_store[k].value, 0);
    }
    spend_cycles(rt->addr, s_run.cycles > (uint64_t)s_stores ? s_run.cycles - (uint64_t)s_stores : 1);
    s_stats[i].compared++;
    nested_compared();
    cpu.pc = pc;
    return true;
}

/* Whether the CPU at addr is at the routine's entry: for a routine of the
 * switchable window, with its bank mapped. */
static bool at_entry(const DecompRoutine *rt, uint16_t addr)
{
    return rt->addr == addr && (addr >= 0xC000 || hw_prg_bank4(addr) >> 2 == rt->bank);
}

static int find_routine(uint16_t addr, bool resume)
{
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->resume == resume && (resume ? at_entry(s_routine[i], addr) : s_routine[i]->addr == addr)) {
            return i;
        }
    }
    return -1;
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
        if (!(s_resume[pc >> 3] & (1u << (pc & 7)))) {
            return;
        }
        int i = find_routine(pc, true);
        if (i < 0) {
            return;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
        if (s_mode == DECOMP_CHECK) {
            task_check(i, &in);
            return;
        }
        if (!task_replace(i, &in) || s_handoff_set) {
            return;
        }
    }
}

static int on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        if (s_run_active && task_isolated_site(addr)) {
            return 1;
        }
        return addr == DT_SITE_DAMAGE && s_damage_replayed < s_damage_raised && s_damage_cancel[s_damage_replayed++];
    }
    for (int i = 0; i < s_count; i++) {
        if (!at_entry(s_routine[i], addr) || !s_routine[i]->site_id) {
            continue;
        }
        CycModRegs in;
        cyc_mod_regs(&in);
        if (s_routine[i]->task) {
            if (s_mode == DECOMP_CHECK) {
                task_check(i, &in);
            } else {
                task_replace(i, &in);
            }
            return 0;
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
    for (int i = 0; i < n; i++) {
        if (s_count == MAX_ROUTINES) {
            fail("more routines than MAX_ROUTINES", table[i].addr);
        }
        s_routine[s_count++] = &table[i];
        if (table[i].site_id) {
            nes_mod_register_function_entry_plugin(table[i].site_id, table[i].addr, on_entry);
        }
        if (table[i].resume) {
            s_resume[table[i].addr >> 3] |= (uint8_t)(1u << (table[i].addr & 7));
        }
    }
}

/* The NMI handler's RTI: the cycles a replaced routine still owed when the
 * NMI came. */
static int on_nmi_exit(uint16_t addr)
{
    uint64_t debt = s_nmi_debt;
    s_nmi_debt = 0;
    if (debt) {
        spend_cycles(addr, debt);
    }
    return 0;
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_sites)
{
    nes_mod_register_function_entry_plugin(DT_SITE_NMI_EXIT_ID, DT_SITE_NMI_EXIT, on_nmi_exit);
    add_table(gMotionRoutines, gMotionRoutineCount);
    add_table(gSoundRoutines, gSoundRoutineCount);
    add_table(gPlayerRoutines, gPlayerRoutineCount);
    add_table(gObjCommonRoutines, gObjCommonRoutineCount);
    add_table(gObjectRoutines, gObjectRoutineCount);
    add_table(gLevelRoutines, gLevelRoutineCount);
    add_table(gCollisionRoutines, gCollisionRoutineCount);
    add_table(gFlowRoutines, gFlowRoutineCount);
    add_table(gSystemRoutines, gSystemRoutineCount);
    add_table(gSpriteRoutines, gSpriteRoutineCount);
    add_table(gAudioRoutines, gAudioRoutineCount);
    for (int i = 0; i < gFlowStopCount; i++) {
        nes_mod_register_function_entry_plugin(gFlowStops[i].site_id, gFlowStops[i].addr, on_entry);
    }
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
    const char *only = getenv("DT_DECOMP_ONLY");
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->site_id) {
            bool on = mode != DECOMP_OFF && (!only || strstr(only, s_routine[i]->name));
            nes_mod_set_function_hook_enabled(s_routine[i]->site_id, on);
        }
    }
    for (int i = 0; i < gFlowStopCount; i++) {
        nes_mod_set_function_hook_enabled(gFlowStops[i].site_id, mode != DECOMP_OFF);
    }
    nes_mod_set_function_hook_enabled(DT_SITE_NMI_EXIT_ID, mode == DECOMP_REPLACE);
    s_nmi_debt = 0;
    cyc_mod_set_return_hook(mode != DECOMP_OFF ? on_return : NULL);
    return true;
}

DecompMode Decomp_Mode(void)
{
    return s_mode;
}
