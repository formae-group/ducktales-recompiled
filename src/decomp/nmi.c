/*
 * nmi.c - the NMI handler of the fixed bank ($C000-$C138), decompiled as
 * interrupt routines (docs/decompilation.md "Interrupt routines"): task code
 * whose C ends in RTI.
 *
 * So far its exit is C: the handler runs as the original up to $C132, where
 * Nmi_Return takes back the registers it saved and returns from the
 * interrupt.
 */
#include "decomp/decomp.h"

#include "ducktales/game.h"

#define NMI_SAVED 5 /* the interrupt's P, then the P, A, X and Y the handler pushed */

/* $C132: Y, X, A and the flags the handler saved come back off the stack;
 * then RTI. */
static void Nmi_Return(Regs *r)
{
    Decomp_Enter(DT_NMI_RETURN, r->a, r->x, r->y);
    r->y = Decomp_Pull();
    r->x = Decomp_Pull();
    r->a = Decomp_Pull();
    r->p = Decomp_Pull();
}

const DecompRoutine gNmiRoutines[] = {
    { DT_NMI_RETURN, "ducktales.decomp.C132", "Nmi_Return", Nmi_Return, OUT_A | OUT_X | OUT_Y | OUT_P, true, NMI_SAVED,
      false, .interrupt = true },
};
const int gNmiRoutineCount = sizeof gNmiRoutines / sizeof gNmiRoutines[0];
