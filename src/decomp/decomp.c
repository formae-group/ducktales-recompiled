/*
 * decomp.c - the core of the decompilation layer (decomp.h,
 * docs/DECOMPILATION.md): the routine tables and their sites, the C of one
 * routine on an image of RAM (dc_run_c) and the calls the C makes the same
 * way on every host. What runs at a site is the backend's (decomp_backend.h):
 * decomp_cycle.c in the cycle host, decomp_native.c on the native runtime.
 */
#include "decomp/decomp_backend.h"

#include "ducktales/events.h"

#include "cpu6502.h"
#include "cyc_core.h"
#include "hw.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAIT_READS_MAX 60000 /* two frames of reads */

const DecompRoutine *dc_routine[MAX_ROUTINES];
RoutineStats dc_stats[MAX_ROUTINES];
int dc_count;
DecompMode dc_mode = DECOMP_OFF;
uint8_t dc_resume[0x10000 / 8];

uint8_t dc_image[GAME_RAM_SIZE];
uint8_t dc_entry_s, dc_entry_p, dc_cur_s, dc_rebased_s, dc_c_bank;
bool dc_keep_stack_ram;
bool dc_run_original;
const DecompRoutine *dc_top;
bool dc_top_entered;
bool dc_damage_cancel[8];
int dc_damage_raised;
BusOp dc_ops[MAX_BUS_OPS];
int dc_nops, dc_played, dc_loads;
DeviceStore dc_store[MAX_STORES];
int dc_stores;
bool dc_task;
int dc_nested[MAX_NESTED], dc_nnested;
IsolatedRead dc_reads[MAX_READS];
int dc_nreads;
bool dc_handoff_set;
uint16_t dc_handoff;
uint8_t dc_handoff_uses, dc_handoff_bank;

static char s_error[256];

void dc_fail(const char *what, uint16_t routine)
{
    snprintf(s_error, sizeof s_error, "%s ($%04X, frame %llu)", what, routine, (unsigned long long)dc_frame());
    fprintf(stderr, "[decomp] %s\n", s_error);
    fflush(stderr);
    exit(3);
}

void dc_record(uint16_t addr, uint8_t value, bool write)
{
    if (dc_nops == MAX_BUS_OPS) {
        dc_fail("too many bus operations in one call", addr);
    }
    dc_ops[dc_nops++] = (BusOp){ addr, value, write, 0 };
}

static void record_store(uint16_t addr, uint8_t value);

/* Task code makes its device writes when it ends, so a write of a routine it
 * calls in C is one of its stores too. */
void Decomp_BusWrite(uint16_t addr, uint8_t value)
{
    dc_record(addr, value, true);
    if (dc_task) {
        record_store(addr, value);
    }
    Game_MapperWrite(addr, value);
    if (Game_RomView() >= 0) {
        dc_c_bank = (uint8_t)Game_RomView();
    }
    dc_timing_op(dc_nops - 1);
}

void Decomp_BusRead(uint16_t addr)
{
    dc_record(addr, 0, false);
    dc_timing_op(dc_nops - 1);
}

void Decomp_BusWait(uint16_t addr, uint8_t mask, uint8_t want)
{
    dc_record(addr, want, false);
    dc_ops[dc_nops - 1].wait = mask;
    dc_timing_op(dc_nops - 1);
}

static void bus_wait(const BusOp *op)
{
    for (uint32_t reads = 0; (cpu_read(op->addr, 0) & op->wait) != op->value; reads++) {
        if (reads == WAIT_READS_MAX) {
            dc_fail("a device wait did not end", dc_top->addr);
        }
    }
}

/* The recorded bus operations from index from on, made on the machine (or
 * on the isolated copy inside a scope), with spare reads of addr spread
 * evenly between them: the PPU acts on a $2006 or $2007 write a few cycles
 * late, so the operations cannot come back to back. Returns the reads made,
 * fewer than spare when spare is not 0. */
uint64_t dc_play_ops(int from, uint64_t spare, uint16_t addr)
{
    uint64_t slots = (uint64_t)(dc_nops - from) + 1, spent = 0;
    for (int i = from; i < dc_nops; i++) {
        for (uint64_t due = spare * (uint64_t)(i - from + 1) / slots; spent < due; spent++) {
            (void)cpu_read(addr, 0);
        }
        if (dc_ops[i].write) {
            cpu_write(dc_ops[i].addr, dc_ops[i].value, 0);
        } else if (dc_ops[i].wait && !dc_isolated()) {
            bus_wait(&dc_ops[i]);
        } else {
            (void)cpu_read(dc_ops[i].addr, 0);
        }
    }
    return spent;
}

void Decomp_RunOriginal(void)
{
    if (dc_loads) {
        dc_fail("a routine gave its call to the original after a device load", dc_top->addr);
    }
    dc_run_original = true;
}

void Decomp_Push(uint8_t value)
{
    gRam[0x100 | dc_cur_s] = value;
    dc_cur_s--;
}

uint8_t Decomp_Pull(void)
{
    dc_cur_s++;
    return gRam[0x100 | dc_cur_s];
}

void Decomp_SetStack(uint8_t s)
{
    dc_rebased_s = dc_cur_s = s;
}

uint8_t Decomp_Stack(void)
{
    return dc_cur_s;
}

void Decomp_KeepStackRam(void)
{
    dc_keep_stack_ram = true;
}

void Decomp_PushReturn(uint16_t resume)
{
    uint16_t ret = (uint16_t)(resume - 1);
    Decomp_Push((uint8_t)(ret >> 8));
    Decomp_Push((uint8_t)ret);
}

void Decomp_PullReturn(void)
{
    dc_cur_s = (uint8_t)(dc_cur_s + 2);
}

bool Decomp_HandedOff(void)
{
    return dc_handoff_set;
}

void Decomp_Handoff(uint16_t routine, uint16_t resume, uint8_t uses)
{
    if (resume) {
        Decomp_PushReturn(resume);
    }
    dc_handoff = routine;
    dc_handoff_bank = dc_c_bank;
    dc_handoff_uses = uses;
    dc_handoff_set = true;
}

static void record_store(uint16_t addr, uint8_t value)
{
    if (dc_stores == MAX_STORES) {
        dc_fail("too many device writes in one routine", addr);
    }
    dc_store[dc_stores].addr = addr;
    dc_store[dc_stores].value = value;
    dc_stores++;
}

void Decomp_SelectBank(uint8_t bank)
{
    Decomp_Call(DT_CODE_BANK_SELECT, bank, 0, 0);
    dc_timing_bank(bank);
    dc_c_bank = bank;
    record_store((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

void Decomp_IsolatedRead(uint16_t pc, uint16_t addr, uint8_t value)
{
    if (dc_nreads == MAX_READS) {
        dc_fail("too many isolated reads in one routine", pc);
    }
    dc_reads[dc_nreads++] = (IsolatedRead){ pc, (uint16_t)(addr & 0x7FF), value };
}

void Decomp_Store(uint16_t addr, uint8_t value)
{
    record_store(addr, value);
}

int Decomp_RaiseEvent(int event)
{
    if (dc_mode != DECOMP_REPLACE) {
        return EVENT_CONTINUE;
    }
    EventResult result = Events_Raise((EventId)event);
    if (event == EVENT_PLAYER_DAMAGED &&
        dc_damage_raised < (int)(sizeof dc_damage_cancel / sizeof dc_damage_cancel[0])) {
        dc_damage_cancel[dc_damage_raised++] = result == EVENT_CANCEL;
    }
    return result;
}

void Decomp_RaiseSlotEvent(int event, uint8_t slot)
{
    if (dc_mode == DECOMP_REPLACE) {
        Events_RaiseSlot((EventId)event, slot);
    }
}

bool dc_outside;

void Decomp_Outside(void (*fn)(void *ctx), void *ctx)
{
    bool was = dc_outside;
    dc_outside = true;
    fn(ctx);
    dc_outside = was;
}

/* The C of a routine on an image of RAM at its entry; dc_image holds the RAM
 * it leaves, *out its registers. */
void dc_run_c(const DecompRoutine *rt, const CycModRegs *in, Regs *out)
{
    memcpy(dc_image, cyc_cpu_ram(), GAME_RAM_SIZE);
    dc_entry_s = in->s;
    dc_entry_p = in->p;
    dc_run_original = false;
    dc_damage_raised = 0;
    dc_top = rt;
    dc_top_entered = false;
    dc_nops = dc_played = dc_loads = 0;
    dc_c_bank = (uint8_t)(hw_prg_bank4(0x8000) >> 2);
    dc_rebased_s = dc_cur_s = in->s;
    dc_keep_stack_ram = false;
    dc_handoff_set = false;
    dc_stores = 0;
    dc_task = rt->task;
    dc_nnested = 0;
    dc_nreads = 0;
    dc_c_begin();
    dc_timing_begin(rt);
    Regs r = { in->a, in->x, in->y, in->p };
    GameRam_Enter(dc_image);
    rt->run(&r);
    dc_timing_end(rt);
    GameRam_Leave();
    Game_ResetRomView();
    dc_c_end();
    if (!dc_top_entered) {
        dc_fail("a decompiled routine does not start with Decomp_Enter", rt->addr);
    }
    *out = r;
}

/* Offsets in page 1 below entry S + 3 are dead once the routine has returned
 * (its return address included), down to the stack's floor: below it page 1
 * holds data. */
bool dc_live(uint16_t a, uint8_t entry_s)
{
    return (a & 0xFF00) != 0x0100 || a < DT_ADDR_STACK_FLOOR || (a & 0xFF) >= entry_s + 3u;
}

int dc_find_routine(uint16_t addr, bool resume)
{
    for (int i = 0; i < dc_count; i++) {
        if (dc_routine[i]->resume == resume &&
            (resume ? dc_at_entry(dc_routine[i], addr) : dc_routine[i]->addr == addr)) {
            return i;
        }
    }
    return -1;
}

/* Whether the CPU at addr is at the routine's entry: for a routine of the
 * switchable window, with its bank mapped. */
bool dc_at_entry(const DecompRoutine *rt, uint16_t addr)
{
    return rt->addr == addr && (addr >= 0xC000 || hw_prg_bank4(addr) >> 2 == rt->bank);
}

/* A task routine reached in C from another one: counted as called, and as
 * compared when the one that reached it is (dc_nested_compared). */
void dc_enter_task(int i, uint16_t routine)
{
    dc_stats[i].calls++;
    if (dc_nnested == MAX_NESTED) {
        dc_fail("too many task routines reached in C in one call", routine);
    }
    dc_nested[dc_nnested++] = i;
}

void dc_nested_compared(void)
{
    for (int k = 0; k < dc_nnested; k++) {
        dc_stats[dc_nested[k]].compared++;
    }
}

void dc_add_table(const DecompRoutine *table, int n)
{
    for (int i = 0; i < n; i++) {
        if (dc_count == MAX_ROUTINES) {
            dc_fail("more routines than MAX_ROUTINES", table[i].addr);
        }
        dc_routine[dc_count++] = &table[i];
        if (table[i].site_id && !nes_mod_register_function_entry_plugin(table[i].site_id, table[i].addr, dc_on_entry)) {
            dc_fail("a routine's site could not be registered", table[i].addr);
        }
        if (table[i].resume) {
            dc_resume[table[i].addr >> 3] |= (uint8_t)(1u << (table[i].addr & 7));
        }
    }
}

const char *Decomp_Error(void)
{
    return s_error;
}

void Decomp_WriteStats(FILE *f, DecompMode mode)
{
    for (int i = 0; i < dc_count; i++) {
        fprintf(f, "decomp: mode=%s routine=%s addr=$%04X calls=%llu compared=%llu original=%llu\n",
                mode == DECOMP_CHECK ? "check" : "replace", dc_routine[i]->name, dc_routine[i]->addr,
                (unsigned long long)dc_stats[i].calls, (unsigned long long)dc_stats[i].compared,
                (unsigned long long)dc_stats[i].original);
    }
}

void dc_report_stats(void)
{
    if (dc_mode == DECOMP_OFF) {
        return;
    }
    Decomp_WriteStats(stdout, dc_mode);
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

DecompMode Decomp_Mode(void)
{
    return dc_mode;
}

NES_MOD_CONSTRUCTOR(dt_register_decomp_sites)
{
    dc_add_table(gMotionRoutines, gMotionRoutineCount);
    dc_add_table(gSoundRoutines, gSoundRoutineCount);
    dc_add_table(gPlayerRoutines, gPlayerRoutineCount);
    dc_add_table(gObjCommonRoutines, gObjCommonRoutineCount);
    dc_add_table(gObjDispatchRoutines, gObjDispatchRoutineCount);
    dc_add_table(gObjWalkerRoutines, gObjWalkerRoutineCount);
    dc_add_table(gObjFlyerRoutines, gObjFlyerRoutineCount);
    dc_add_table(gObjHazardRoutines, gObjHazardRoutineCount);
    dc_add_table(gObjBlockRoutines, gObjBlockRoutineCount);
    dc_add_table(gObjEnemyRoutines, gObjEnemyRoutineCount);
    dc_add_table(gObjPropRoutines, gObjPropRoutineCount);
    dc_add_table(gObjBossRoutines, gObjBossRoutineCount);
    dc_add_table(gObjThiefRoutines, gObjThiefRoutineCount);
    dc_add_table(gObjSceneRoutines, gObjSceneRoutineCount);
    dc_add_table(gObjItemRoutines, gObjItemRoutineCount);
    dc_add_table(gLevelRoutines, gLevelRoutineCount);
    dc_add_table(gCollisionRoutines, gCollisionRoutineCount);
    dc_add_table(gFlowRoutines, gFlowRoutineCount);
    dc_add_table(gSystemRoutines, gSystemRoutineCount);
    dc_add_table(gSpriteRoutines, gSpriteRoutineCount);
    dc_add_table(gAudioRoutines, gAudioRoutineCount);
    dc_add_table(gNmiRoutines, gNmiRoutineCount);
}
