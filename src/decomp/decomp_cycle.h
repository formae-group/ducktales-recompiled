/*
 * decomp_cycle.h - what the files of the cycle host's backend of the
 * decompilation layer (decomp_cycle.c, docs/DECOMPILATION.md) share; no other
 * file includes it. It owns no setting or option: --decomp and
 * DT_DECOMP_ONLY are decomp_cycle.c's (Decomp_SetMode).
 *
 *   decomp_cycle.c        the isolated scope, the plain routines' check and
 *                         replace, the device state, the sites
 *   decomp_cycle_task.c   task code, polling task code and the RTS observer
 *   decomp_cycle_nmi.c    the NMI handler: check mode's comparison at its RTI,
 *                         replace mode, and the cycles owed at the RTI
 *   decomp_cycle_clock.c  the original on the running machine's clock: its bus
 *                         schedule measured and replayed, and the timing check
 */
#pragma once
#include "decomp/decomp_backend.h"

#include "cyc_mod.h"

#include <stdint.h>
#include <stdio.h>

#define SCOPE_OP_GAP 8
#define REGS_TEXT    0x4000

typedef struct {
    uint64_t ciram, palette, chr, oam, regs;
    unsigned bank;
} HwState;

/* decomp_cycle.c: the original's RAM from its isolated run, the damage
 * events an isolated call has replayed, and the cycles a replaced routine
 * owes after an NMI cut its spending short. */
extern uint8_t dc_orig[GAME_RAM_SIZE];
extern int dc_damage_replayed;
extern uint64_t dc_nmi_debt;
FILE *dc_device_dump(void);
HwState dc_hw_state(void);
bool dc_spans_frame_end(uint64_t cycles);
bool dc_crosses_vblank(uint64_t cycles);
void dc_spend_cycles(uint16_t addr, uint64_t cycles);

/* decomp_cycle_task.c: dc_run_active while a task routine's original runs
 * isolated. */
extern bool dc_run_active;
int dc_task_isolated_site(uint16_t addr);
void dc_task_check(int i, const CycModRegs *in);
bool dc_task_replace(int i, const CycModRegs *in);
void dc_on_return(void);

/* decomp_cycle_nmi.c */
void dc_nmi_check(int i, const CycModRegs *in);
void dc_nmi_replace(int i, const CycModRegs *in);
void dc_nmi_disarm(void);

/* decomp_cycle_clock.c */
typedef enum {
    CLOCK_REACHED,
    CLOCK_PAUSED,
    CLOCK_INTERRUPTED,
    CLOCK_MISSED
} ClockEnd;

ClockEnd dc_clock_measure(uint16_t routine, uint16_t stop_pc, uint8_t stop_s, int frame_ends, uint64_t *duration,
                          CycModRegs *regs, uint8_t *ram);
void dc_clock_replay(uint64_t duration, uint16_t routine, int ops);
int dc_clock_match(uint16_t routine, int ops);
void dc_timing_check(const DecompRoutine *rt, const CycModRegs *in);
void dc_timing_summary(void);
