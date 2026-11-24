/*
 * decomp.h - how a decompiled routine replaces the recompiled one
 * (docs/decompilation.md).
 *
 * A decompiled routine is C over the typed game API (include/ducktales/). It
 * runs in a RAM transaction on a private image of CPU RAM, from a
 * [[mod_function_hook]] site at its entry when original code calls it, or as
 * a plain C call from another decompiled routine. src/decomp/decomp.c runs it
 * in one of three modes (--decomp, or DUCKTALES_DECOMP when the option is not
 * given):
 *
 *   off      no site is enabled: the recompiled code runs (the default)
 *   check    at every call the original routine runs in an isolated call
 *            (cyc_mod.h) and the C runs on the same state; their RAM and
 *            the registers of the routine's contract must be equal, or the
 *            program stops with the difference. Then the original runs, so
 *            the machine is exactly the console's
 *   replace  the C runs instead: its RAM changes are stored, the contract's
 *            registers set, the CPU spends as many cycles as the original
 *            routine takes from the same state, and the routine returns as
 *            its RTS would
 *
 * Code the decompilation has not reached yet stays original in two ways:
 * Decomp_Call runs an original routine from C (in an isolated call whose RAM
 * changes are taken back into the image), and Decomp_RunOriginal hands the
 * whole call to the original routine, for paths into code that waits for the
 * hardware (which an isolated call cannot run).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ducktales/ram.h"

#define P_C 0x01
#define P_Z 0x02
#define P_I 0x04
#define P_D 0x08
#define P_V 0x40
#define P_N 0x80

typedef struct Regs {
    uint8_t a, x, y, p;
} Regs;

/* Which registers the routine's callers use after it returns. */
#define OUT_A 0x01
#define OUT_X 0x02
#define OUT_Y 0x04
#define OUT_P 0x08 /* N, V, Z and C */
#define OUT_C 0x20 /* C only */
#define OUT_HW \
    0x10           /* accesses a device (Decomp_BusWrite, Decomp_BusRead, Decomp_BusLoad): check also compares the
                        PPU's memories, OAM, the device registers and the PRG bank, replace replays the writes */
#define OUT_C 0x20 /* C only */

typedef struct DecompRoutine {
    uint16_t addr;
    const char *site_id; /* the site where original code calls it; NULL when only C does */
    const char *name;
    void (*run)(Regs *r); /* in: the registers at entry; out: at its RTS */
    uint8_t out;
    bool task;      /* task code: may end by handing off to the original (below) */
    uint8_t locals; /* task code: bytes the routine keeps on the stack at its start */
    bool resume;    /* task code: started where an RTS returns to its address, with no site */
    uint8_t bank;   /* entries below $C000: the bank mapped at $8000 */
} DecompRoutine;

typedef enum DecompMode {
    DECOMP_OFF,
    DECOMP_CHECK,
    DECOMP_REPLACE
} DecompMode;

bool Decomp_SetMode(DecompMode mode);
DecompMode Decomp_Mode(void);
/* Each routine's call counts as the "decomp: mode=..." lines the host prints
 * at exit, labelled with mode; and the error that stopped the program ("" when
 * none). The native player writes both to its session summary. */
void Decomp_WriteStats(FILE *f, DecompMode mode);
const char *Decomp_Error(void);
bool Decomp_ParseMode(const char *text, DecompMode *out);

/* The first statement of every routine's C, with the registers the original
 * gets (those the C does not model as 0). In check mode a call from another
 * routine's C is checked here as a site checks it. */
void Decomp_Enter(uint16_t routine, uint8_t a, uint8_t x, uint8_t y);
/* Run an original routine that is not decompiled yet, with these registers;
 * returns the registers it returned with. */
Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y);
/* The same with the status flags the original routine is entered with. */
Regs Decomp_CallP(uint16_t routine, uint8_t a, uint8_t x, uint8_t y, uint8_t p);
/* Give this call to the original routine: the C's changes are dropped and
 * the original runs from its entry. The C must return without doing anything
 * else. */
void Decomp_RunOriginal(void);
/* A write or a side-effect read of a device register (PPU, mapper) by a
 * routine whose contract has OUT_HW. The C runs on RAM only, so it records
 * them: replace mode makes them on the bus after storing the RAM, check mode
 * makes them on an isolated copy of the machine and compares the PPU's
 * memories and the PRG bank with the original's. A mapper write also
 * switches the bank Game_ReadRom sees for the rest of the routine. */
void Decomp_BusWrite(uint16_t addr, uint8_t value);
void Decomp_BusRead(uint16_t addr);
/* A device read whose value the C needs (the controllers): made at once with
 * the recorded operations before it, in the C's isolated copy in check mode
 * and on the bus in replace mode. A routine that gives its call to the
 * original must do so before its first load; task code never loads. */
uint8_t Decomp_BusLoad(uint16_t addr);
/* A RAM value the original reads at pc from addr, for the cycle host's
 * replace mode: devices do not run in its isolated measuring call, so an
 * original whose cycle count depends on what it read from one (the
 * controllers) is measured again after the C, reading these values there.
 * Check mode and the native runtime ignore them. */
void Decomp_IsolatedRead(uint16_t pc, uint16_t addr, uint8_t value);
/* The host's frame end (replace mode gives a call that would still run at
 * the frame end to the original routine, so the RAM at every frame end is
 * the console's). */
void Decomp_FrameEnd(void);
/* Raise an event from decompiled code that replaces the code its site is
 * in (replace mode only: in check mode the original runs without hooks). */
int Decomp_RaiseEvent(int event);

/* Task code (docs/decompilation.md "Task code"): the game's tasks wait for
 * frames (task_yield), switch banks and drive the PPU, which no isolated call
 * can do. A task routine's C therefore ends in one of two ways: it returns
 * (RTS), or it hands off to the original code, which runs for real. Its C
 * starts at a site (original code calls it), from another routine's C, or at
 * a resume point (DecompRoutine.resume): the address an RTS returns to after
 * the original routine a hand-off reached, which the RTS observer starts
 * with no site.
 *
 *   Decomp_Handoff(routine, resume)  JSR routine with the registers the C
 *       left in its Regs; it returns to resume (the address after the
 *       original's JSR), which is usually the resume point of the next C.
 *       resume 0 is a JMP. The C returns right after it. uses (OUT_A, OUT_X,
 *       OUT_Y) names the registers the routine, or the code after it, reads:
 *       check compares those, and replace takes the others from the
 *       original (they are dead, but task_yield keeps X and Y on the stack).
 *   Decomp_PushReturn / Decomp_PullReturn  a JSR and RTS between two task
 *       routines in C, so that a handoff inside the callee leaves the stack
 *       the original would; Decomp_HandedOff says whether the callee did.
 *   Decomp_Push / Decomp_Pull / Decomp_SetStack / Decomp_Stack  the task's
 *       stack (PHA, PLA, TXS, TSX): values a routine keeps across a yield
 *       live there.
 *   Decomp_SelectBank  bank_select: maps the bank for the Decomp_Calls that
 *       follow, and in replace mode writes the mapper for real.
 *   Decomp_Store  a store to a device register, made for real in replace
 *       mode. The Decomp_BusWrite writes of the routines a task routine
 *       calls in C are its stores too.
 *
 * A task routine whose C another task routine's C reaches is compared as
 * part of that one: it has no check of its own.
 *
 * Check mode runs the original from the same site until it reaches the same
 * routine at the same stack depth (a stop hook in the isolated call), and
 * compares RAM, the stack the C built, A, X, Y and the number of device
 * writes. Replace mode stores the C's results, spends the original's cycles to
 * that point and continues in the routine. */
void Decomp_Handoff(uint16_t routine, uint16_t resume, uint8_t uses);
void Decomp_PushReturn(uint16_t resume);
void Decomp_PullReturn(void);
bool Decomp_HandedOff(void);
void Decomp_Push(uint8_t value);
uint8_t Decomp_Pull(void);
void Decomp_SetStack(uint8_t s);
uint8_t Decomp_Stack(void);
void Decomp_SelectBank(uint8_t bank);
void Decomp_Store(uint16_t addr, uint8_t value);

/* N and Z of a value, the flags of a load. */
static inline uint8_t Flags_NZ(uint8_t p, uint8_t v)
{
    return (uint8_t)((p & ~(P_N | P_Z)) | (v & 0x80) | (v ? 0 : P_Z));
}

/* A + M + C as ADC computes it (binary mode); *p gets N, V, Z, C. */
static inline uint8_t Alu_Adc(uint8_t a, uint8_t m, uint8_t *p)
{
    unsigned sum = (unsigned)a + m + (*p & P_C);
    uint8_t r = (uint8_t)sum;
    uint8_t q = Flags_NZ(*p, r) & (uint8_t)~(P_C | P_V);
    if (sum > 0xFF) {
        q |= P_C;
    }
    if (~(a ^ m) & (a ^ r) & 0x80) {
        q |= P_V;
    }
    *p = q;
    return r;
}

/* A - M - !C as SBC computes it (binary mode). */
static inline uint8_t Alu_Sbc(uint8_t a, uint8_t m, uint8_t *p)
{
    return Alu_Adc(a, (uint8_t)~m, p);
}

/* The flags of CMP: N, Z, C. */
static inline uint8_t Flags_Cmp(uint8_t p, uint8_t a, uint8_t m)
{
    uint8_t r = (uint8_t)(a - m);
    p = Flags_NZ(p, r) & (uint8_t)~P_C;
    return (uint8_t)(p | (a >= m ? P_C : 0));
}

/* The routine tables of each decompiled file. */
extern const DecompRoutine gMotionRoutines[];
extern const int gMotionRoutineCount;
extern const DecompRoutine gSoundRoutines[];
extern const int gSoundRoutineCount;
extern const DecompRoutine gPlayerRoutines[];
extern const int gPlayerRoutineCount;
extern const DecompRoutine gObjCommonRoutines[];
extern const int gObjCommonRoutineCount;
extern const DecompRoutine gObjectRoutines[];
extern const int gObjectRoutineCount;
extern const DecompRoutine gLevelRoutines[];
extern const int gLevelRoutineCount;
extern const DecompRoutine gCollisionRoutines[];
extern const int gCollisionRoutineCount;
extern const DecompRoutine gFlowRoutines[];
extern const int gFlowRoutineCount;
extern const DecompRoutine gSystemRoutines[];
extern const int gSystemRoutineCount;
extern const DecompRoutine gSpriteRoutines[];
extern const int gSpriteRoutineCount;
extern const DecompRoutine gAudioRoutines[];
extern const int gAudioRoutineCount;

/* Addresses a task routine hands off to: sites where an isolated run of the
 * original stops, with no C of their own. */
typedef struct DecompStop {
    uint16_t addr;
    const char *site_id;
} DecompStop;

extern const DecompStop gFlowStops[];
extern const int gFlowStopCount;
