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

#define MAX_ROUTINES 64

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
static bool s_damage_cancel[8];
static int s_damage_raised, s_damage_replayed;

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
    CycModRegs r = { a, x, y, s_entry_s, s_entry_p, 0 };
    if (!cyc_mod_call(routine, &r)) fail("an original routine called from C did not return", routine);
    memcpy(s_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    return (Regs){ r.a, r.x, r.y, r.p };
}

void Decomp_RunOriginal(void) { s_run_original = true; }

int Decomp_RaiseEvent(int event)
{
    if (s_mode != DECOMP_REPLACE) return EVENT_CONTINUE;
    EventResult result = Events_Raise((EventId)event);
    if (event == EVENT_PLAYER_DAMAGED && s_damage_raised < (int)(sizeof s_damage_cancel / sizeof s_damage_cancel[0]))
        s_damage_cancel[s_damage_raised++] = result == EVENT_CANCEL;
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
    Regs r = { in->a, in->x, in->y, in->p };
    GameRam_Enter(s_image);
    rt->run(&r);
    GameRam_Leave();
    if (s_scope) cyc_mod_isolate_end();
    s_scope = false;
    *out = r;
}

/* Offsets in page 1 below entry S + 3 are dead once the routine has returned
 * (its return address included). */
static bool live(uint16_t a, uint8_t entry_s)
{
    return (a & 0xFF00) != 0x0100 || (a & 0xFF) >= entry_s + 3u;
}

/* The original routine alone on the machine's state: its RAM into s_orig,
 * its registers into *out; returns its cycle count, 0 when it did not
 * return. */
static uint64_t run_original(const DecompRoutine *rt, const CycModRegs *in, CycModRegs *out, bool hooked)
{
    CycModStats before, after;
    if (!cyc_mod_isolate_begin()) fail("cannot open an isolated scope", rt->addr);
    s_damage_replayed = 0;
    cyc_mod_allow_isolated_hooks(hooked);
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
    uint64_t cycles = run_original(rt, in, &o, false);
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
    uint64_t cycles = run_original(rt, in, &o, true);
    if (!cycles) fail("the original routine did not return in an isolated call", rt->addr);
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++)
        if (live(a, in->s) && ram[a] != s_image[a]) cyc_mod_poke(a, s_image[a]);
    uint8_t flags = P_N | P_V | P_Z | P_C;
    CycModRegs set = { c.a, c.x, c.y, in->s, (uint8_t)((c.p & flags) | (in->p & ~flags)), 0 };
    cyc_mod_set_regs(&set);
    spend_cycles(rt->addr, cycles);
    return 1;
}

static int on_entry(uint16_t addr)
{
    if (cyc_mod_isolated())
        return addr == DT_SITE_DAMAGE && s_damage_replayed < s_damage_raised && s_damage_cancel[s_damage_replayed++];
    for (int i = 0; i < s_count; i++) {
        if (s_routine[i]->addr != addr) continue;
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
        nes_mod_register_function_entry_plugin(table[i].site_id, table[i].addr, on_entry);
    }
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_sites)
{
    add_table(gMotionRoutines, gMotionRoutineCount);
    add_table(gSoundRoutines, gSoundRoutineCount);
    add_table(gPlayerRoutines, gPlayerRoutineCount);
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
    return true;
}

DecompMode Decomp_Mode(void) { return s_mode; }
