/*
 * decomp_cycle_nmi.c - the NMI handler in the cycle host's backend of the
 * decompilation layer (decomp_cycle.c has the whole account): check mode runs
 * its C at the NMI, makes its bus operations on a snapshot of the running
 * machine and compares at the original's RTI (DT_SITE_NMI_EXIT); replace mode
 * measures the original on a clocked snapshot and replays the C's bus
 * operations at its cycles; at the RTI a replaced routine's owed cycles are
 * spent. It owns no setting or option.
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
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    FILE *f = dc_device_dump();
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
void dc_nmi_check(int i, const CycModRegs *in)
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
    s_nmi.hw = dc_hw_state();
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
    HwState hw = dc_hw_state();
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

void dc_nmi_replace(int i, const CycModRegs *in)
{
    const DecompRoutine *rt = dc_routine[i];
    Regs c;
    dc_run_c(rt, in, &c);
    dc_stats[i].calls++;
    uint64_t duration;
    if (dc_run_original ||
        dc_clock_measure(rt->addr, DT_SITE_NMI_EXIT, in->s, 0, &duration, NULL, NULL) != CLOCK_REACHED) {
        dc_stats[i].original++;
        return;
    }
    const uint8_t *ram = cyc_cpu_ram();
    for (uint16_t a = 0; a < GAME_RAM_SIZE; a++) {
        if (ram[a] != dc_image[a]) {
            cyc_mod_poke(a, dc_image[a]);
        }
    }
    dc_clock_replay(duration, rt->addr, dc_nops);
    CycModRegs set = { c.a, c.x, c.y, dc_cur_s, c.p, 0 };
    cyc_mod_set_regs(&set);
    uint64_t debt = dc_nmi_debt;
    dc_nmi_debt = 0;
    if (debt) {
        dc_spend_cycles(DT_SITE_NMI_EXIT, debt);
    }
    cpu.pc = DT_SITE_NMI_EXIT;
    cpu_interp_step();
    dc_stats[i].compared++;
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
    uint64_t debt = dc_nmi_debt;
    dc_nmi_debt = 0;
    if (debt) {
        dc_spend_cycles(addr, debt);
    }
    return 0;
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_nmi_exit)
{
    nes_mod_register_function_entry_plugin(DT_SITE_NMI_EXIT_ID, DT_SITE_NMI_EXIT, on_nmi_exit);
}


void dc_nmi_disarm(void)
{
    s_nmi.armed = false;
    nmi_watch(false);
}
