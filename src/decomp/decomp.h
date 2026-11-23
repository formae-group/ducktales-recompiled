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
#define OUT_C 0x10 /* C only */

typedef struct DecompRoutine {
    uint16_t addr;
    const char *site_id; /* the site where original code calls it; NULL when only C does */
    const char *name;
    void (*run)(Regs *r); /* in: the registers at entry; out: at its RTS */
    uint8_t out;
} DecompRoutine;

typedef enum DecompMode {
    DECOMP_OFF,
    DECOMP_CHECK,
    DECOMP_REPLACE
} DecompMode;

bool Decomp_SetMode(DecompMode mode);
DecompMode Decomp_Mode(void);
bool Decomp_ParseMode(const char *text, DecompMode *out);

/* The first statement of every routine's C, with the registers the original
 * gets (those the C does not model as 0). In check mode a call from another
 * routine's C is checked here as a site checks it. */
void Decomp_Enter(uint16_t routine, uint8_t a, uint8_t x, uint8_t y);
/* Run an original routine that is not decompiled yet, with these registers;
 * returns the registers it returned with. */
Regs Decomp_Call(uint16_t routine, uint8_t a, uint8_t x, uint8_t y);
/* Give this call to the original routine: the C's changes are dropped and
 * the original runs from its entry. The C must return without doing anything
 * else. */
void Decomp_RunOriginal(void);
/* The host's frame end (replace mode gives a call that would still run at
 * the frame end to the original routine, so the RAM at every frame end is
 * the console's). */
void Decomp_FrameEnd(void);
/* Raise an event from decompiled code that replaces the code its site is
 * in (replace mode only: in check mode the original runs without hooks). */
int Decomp_RaiseEvent(int event);

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
