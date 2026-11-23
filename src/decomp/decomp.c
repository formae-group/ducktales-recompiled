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
 *            points of the game's work as on the console. An NMI that the
 *            original would take inside the routine is taken right after it,
 *            and the cycles still to spend when it came are spent when the
 *            NMI handler returns (a site at its RTI), so the interrupted code
 *            resumes at the console's time although the handler waits for
 *            the sprite-0 hit.
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
 * Device access (Decomp_Read/Decomp_Write, routines marked OUT_DEVICE). In
 * check mode the C's accesses go to its isolated scope, so a store changes
 * the scope's PPU, APU or mapper as the original's stores changed the
 * original's scope; at the end both scopes' devices are compared: CIRAM,
 * palette, OAM, CHR RAM and every device register the machine lists
 * (cyc_hw_state_dump) but the CPU bus fields and the two PPUMASK bits that
 * show the previous bus value until two dots after a $2001 store (no clock
 * runs in a scope, so they never settle there; the stored value itself is
 * compared). In replace mode the C's accesses are real CPU accesses on the
 * machine, made after storing the RAM the C has changed so far. Each takes
 * the four cycles of an absolute load or store (three reads of the
 * routine's first byte, then the access), so accesses are spaced as the
 * original's are; the cycles spent afterwards are the original's minus
 * those. A routine with device access must have a site: an inner check
 * would run the C and the original on the same scope's devices.
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

#define MAX_ROUTINES         64
#define FRAME_END_MARGIN     32
#define DEVICE_ACCESS_CYCLES 4

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
static uint64_t s_io_cycles, s_nmi_debt;

typedef struct {
    uint8_t ciram[0x1000], palette[32], oam[256], chr[0x2000];
    char regs[0x4000];
} DeviceState;

static DeviceState s_dev_orig, s_dev_c;
static FILE *s_dump;

static void device_state(DeviceState *d);

static void fail(const char *what, uint16_t routine)
{
    fprintf(stderr, "[decomp] %s ($%04X, frame %llu)\n", what, routine, (unsigned long long)cyc_ring_frame);
    fflush(stderr);
    exit(3);
}

static void open_scope(uint16_t routine)
{
    if (s_scope) {
        return;
    }
    if (!cyc_mod_isolate_begin()) {
        fail("cannot open an isolated scope", routine);
    }
    s_scope = true;
}

static void close_scope(void)
{
    if (s_scope) {
        cyc_mod_isolate_end();
    }
    s_scope = false;
}

/* An original routine in the C's isolated scope, from the image the C works
 * on (gRam); its RAM is left in the scope's machine. */
static bool call_in_scope(uint16_t routine, CycModRegs *r)
{
    open_scope(routine);
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t i = 0; i < GAME_RAM_SIZE; i++) {
        if (ram[i] != gRam[i]) {
            cyc_mod_poke(i, gRam[i]);
        }
    }
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
    if (s_io_cycles) {
        fail("a routine gave its call to the original after accessing a device", s_top->addr);
    }
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
    s_io_cycles = 0;
    s_top = rt;
    s_top_entered = false;
    Regs r = { in->a, in->x, in->y, in->p };
    GameRam_Enter(s_image);
    rt->run(&r);
    GameRam_Leave();
    if (s_mode == DECOMP_CHECK && (rt->out & OUT_DEVICE)) {
        open_scope(rt->addr);
        device_state(&s_dev_c);
    }
    close_scope();
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

static void device_access(uint16_t addr)
{
    if (s_mode == DECOMP_CHECK) {
        open_scope(addr);
        return;
    }
    close_scope();
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (live(a, s_entry_s) && ram[a] != gRam[a]) {
            cyc_mod_poke(a, gRam[a]);
        }
    }
    for (int i = 1; i < DEVICE_ACCESS_CYCLES; i++) {
        (void)cpu_read(s_top->addr, 0);
    }
    s_io_cycles += DEVICE_ACCESS_CYCLES;
}

uint8_t Decomp_Read(uint16_t addr)
{
    device_access(addr);
    return cpu_read(addr, 0);
}

void Decomp_Write(uint16_t addr, uint8_t value)
{
    device_access(addr);
    cpu_write(addr, value, 0);
}

static bool device_line_compared(const char *line)
{
    return strncmp(line, "hw.", 3) && strncmp(line, "ppu.greyscale ", 14) && strncmp(line, "ppu.emphasis ", 13);
}

/* The devices of the machine (an isolated scope's). */
static void device_state(DeviceState *d)
{
    size_t n;
    const uint8_t *m = cyc_ppu_ciram(&n);
    memset(d, 0, sizeof *d);
    memcpy(d->ciram, m, n < sizeof d->ciram ? n : sizeof d->ciram);
    memcpy(d->palette, cyc_ppu_palette(), sizeof d->palette);
    memcpy(d->oam, cyc_ppu_oam(), sizeof d->oam);
    m = cyc_chr_ram(&n);
    if (m) {
        memcpy(d->chr, m, n < sizeof d->chr ? n : sizeof d->chr);
    }
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
        if (!device_line_compared(line) || used + len + 1 > sizeof d->regs) {
            continue;
        }
        memcpy(d->regs + used, line, len);
        used += len;
    }
    d->regs[used] = 0;
}

/* Check mode: the devices after the C against those after the original. */
static void compare_devices(const DecompRoutine *rt)
{
    const char *what = NULL;
    if (memcmp(s_dev_orig.ciram, s_dev_c.ciram, sizeof s_dev_c.ciram)) {
        what = "nametables (CIRAM)";
    } else if (memcmp(s_dev_orig.palette, s_dev_c.palette, sizeof s_dev_c.palette)) {
        what = "palette RAM";
    } else if (memcmp(s_dev_orig.oam, s_dev_c.oam, sizeof s_dev_c.oam)) {
        what = "OAM";
    } else if (memcmp(s_dev_orig.chr, s_dev_c.chr, sizeof s_dev_c.chr)) {
        what = "CHR RAM";
    } else if (strcmp(s_dev_orig.regs, s_dev_c.regs)) {
        what = "device registers";
    }
    if (!what) {
        return;
    }
    fprintf(stderr, "[decomp] MISMATCH %s ($%04X) at frame %llu: %s differ after the routine\n", rt->name, rt->addr,
            (unsigned long long)cyc_ring_frame, what);
    const char *o = s_dev_orig.regs, *c = s_dev_c.regs;
    int shown = 0;
    while (*o && *c && shown < 24) {
        size_t lo = strcspn(o, "\n"), lc = strcspn(c, "\n");
        if (lo != lc || memcmp(o, c, lo)) {
            fprintf(stderr, "[decomp]   original %.*s / decompiled %.*s\n", (int)lo, o, (int)lc, c);
            shown++;
        }
        o += lo + (o[lo] != 0);
        c += lc + (c[lc] != 0);
    }
    fflush(stderr);
    exit(3);
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
    if (s_mode == DECOMP_CHECK && (rt->out & OUT_DEVICE)) {
        device_state(&s_dev_orig);
    }
    cyc_mod_isolate_end();
    return returned ? after.cycles - before.cycles : 0;
}

/* Stops early when an NMI comes: the rest is spent when the handler returns
 * (on_nmi_exit). */
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
    if (rt->out & OUT_DEVICE) {
        compare_devices(rt);
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
    if (rt->out & OUT_DEVICE) {
        fail("a routine with device access has no site", rt->addr);
    }
    const DecompRoutine *top = s_top;
    bool top_entered = s_top_entered, run_original = s_run_original, inner = s_inner;
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

/* Whether the CPU at addr is at the routine's entry: for a routine of the
 * switchable window, with its bank mapped. */
static bool at_entry(const DecompRoutine *rt, uint16_t addr)
{
    return rt->addr == addr && (addr >= 0xC000 || hw_prg_bank(addr) >> 1 == rt->bank);
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
        if (!at_entry(s_routine[i], routine)) {
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
    spend_cycles(rt->addr, cycles > s_io_cycles ? cycles - s_io_cycles : 1);
    return 1;
}

static int on_entry(uint16_t addr)
{
    if (cyc_mod_isolated()) {
        return addr == DT_SITE_DAMAGE && s_damage_replayed < s_damage_raised && s_damage_cancel[s_damage_replayed++];
    }
    for (int i = 0; i < s_count; i++) {
        if (!at_entry(s_routine[i], addr)) {
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
    add_table(gSystemRoutines, gSystemRoutineCount);
    add_table(gSpriteRoutines, gSpriteRoutineCount);
    add_table(gAudioRoutines, gAudioRoutineCount);
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
    nes_mod_set_function_hook_enabled(DT_SITE_NMI_EXIT_ID, mode == DECOMP_REPLACE);
    s_nmi_debt = 0;
    return true;
}

DecompMode Decomp_Mode(void)
{
    return s_mode;
}
