/*
 * decomp_backend.h - what the core of the decompilation layer (decomp.c)
 * shares with the backend linked beside it (docs/decompilation.md):
 *
 *   decomp_cycle.c   the cycle host: check and replace, both against the
 *                    original routine run in an isolated call
 *   decomp_native.c  the native runtime: the C alone, in replace mode only
 *
 * The core keeps the routine tables, the state of the routine whose C runs
 * (dc_run_c) and the calls every backend makes the same way; a backend
 * provides the site callback and what runs around the C.
 */
#ifndef DUCKTALES_DECOMP_BACKEND_H
#define DUCKTALES_DECOMP_BACKEND_H

#include "decomp/decomp.h"

#include "cyc_mod.h"

#include <stdbool.h>
#include <stdint.h>

#define MAX_ROUTINES 256
#define MAX_BUS_OPS  16384
#define MAX_STORES   8

typedef struct {
    uint64_t calls, compared, original;
} RoutineStats;

typedef struct {
    uint16_t addr;
    uint8_t value;
    bool write;
} BusOp;

typedef struct {
    uint16_t addr;
    uint8_t value;
} DeviceStore;

/* The routines of every table, and the mode they run in. */
extern const DecompRoutine *dc_routine[MAX_ROUTINES];
extern RoutineStats dc_stats[MAX_ROUTINES];
extern int dc_count;
extern DecompMode dc_mode;

/* Resume points of task code (fixed bank), by address bit. */
extern uint8_t dc_resume[0x10000 / 8];

/* The call whose C runs: the RAM image it works on, its entry, its bus
 * operations (dc_played of them made so far, dc_loads of them reads whose
 * value it used), its device stores and where it hands off. */
extern uint8_t dc_image[GAME_RAM_SIZE];
extern uint8_t dc_entry_s, dc_entry_p, dc_cur_s, dc_c_bank;
extern bool dc_run_original;
extern const DecompRoutine *dc_top;
extern bool dc_top_entered;
extern bool dc_damage_cancel[8];
extern int dc_damage_raised;
extern BusOp dc_ops[MAX_BUS_OPS];
extern int dc_nops, dc_played, dc_loads;
extern DeviceStore dc_store[MAX_STORES];
extern int dc_stores;
extern bool dc_handoff_set;
extern uint16_t dc_handoff;
extern uint8_t dc_handoff_uses, dc_handoff_bank;

void dc_fail(const char *what, uint16_t routine);
void dc_record(uint16_t addr, uint8_t value, bool write);
uint64_t dc_play_ops(int from, uint64_t spare, uint16_t addr);
void dc_run_c(const DecompRoutine *rt, const CycModRegs *in, Regs *out);
bool dc_live(uint16_t a, uint8_t entry_s);
int dc_find_routine(uint16_t addr, bool resume);
bool dc_at_entry(const DecompRoutine *rt, uint16_t addr);
void dc_report_stats(void);

/* The backend's: the callback of every routine site and flow stop, the
 * frame number its messages give, and what runs before and after a
 * routine's C (dc_run_c). */
int dc_on_entry(uint16_t addr);
uint64_t dc_frame(void);
void dc_c_begin(void);
void dc_c_end(void);

#endif
