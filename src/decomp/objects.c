/*
 * objects.c - the object system of bank 6, decompiled (docs/decompilation.md).
 *
 * Obj_UpdateAll runs once per frame from the frame loop ($DB78, bank 6
 * mapped): it loads the objects' gravity and runs the handler of every live
 * slot 1-15 (status bit 7), chosen by its kind through the handler table at
 * $802C/$806F. The slot cursor lives in RAM ($4A) because the original keeps
 * it there across the handler call. Kinds whose handler is decompiled run as
 * C; the others run the original handler through Decomp_Call.
 */
#include "decomp/decomp.h"

#include "ducktales/game.h"
#include "ducktales/objects.h"

#define OBJ_GRAVITY 0x40

#define gObjCursor RAM_U8(DT_ADDR_OBJ_CURSOR)
#define gScratch0  RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch1  RAM_U8(DT_ADDR_SCRATCH1)

/* One live slot: the dispatch leaves the handler's address in $00/$01 (the
 * JMP ($0000) vector), A = $22 (the low byte of the return it pushed),
 * X = slot and Y = kind. The flags are those of the loop: the caller's carry
 * for slot 1, the CPX of the loop for the others, and the V the previous
 * handler left. *r gets the registers the handler returns with. */
static void Obj_RunHandler(uint8_t slot, Regs *r)
{
    uint8_t kind = gObjects->kind[slot];
    gScratch0 = Game_ReadRom((uint16_t)(DT_CODE_OBJ_HANDLER_LO + kind));
    gScratch1 = Game_ReadRom((uint16_t)(DT_CODE_OBJ_HANDLER_HI + kind));
    r->a = 0x22;
    r->y = kind;
    r->p = Flags_NZ(r->p, r->a);
    switch (kind) {
    case OBJ_KIND_NONE: break;
    default: *r = Decomp_CallP((uint16_t)(gScratch1 << 8 | gScratch0), r->a, slot, r->y, r->p); break;
    }
}

/* Bank 6 $8000: once per frame from the frame loop. Returns with X = 16,
 * which the code after it passes on (a sound request keeps it in $00). */
static void Obj_UpdateAll(Regs *r)
{
    gGravity = OBJ_GRAVITY;
    r->a = OBJ_GRAVITY;
    for (gObjCursor = 1; gObjCursor != OBJ_SLOTS; gObjCursor++) {
        r->a = gObjects->state[gObjCursor];
        if (r->a & OBJ_STATUS_LIVE) Obj_RunHandler(gObjCursor, r);
        r->x = (uint8_t)(gObjCursor + 1);
        r->p = Flags_Cmp(r->p, r->x, OBJ_SLOTS);
    }
}

/* ---- site adapters ---- */

static void Run_UpdateAll(Regs *r) { Obj_UpdateAll(r); }

const DecompRoutine gObjectRoutines[] = {
    { DT_CODE_OBJ_UPDATE, "ducktales.decomp.6.8000", "Obj_UpdateAll", Run_UpdateAll, OUT_X, DT_OBJ_BANK },
};
const int gObjectRoutineCount = sizeof gObjectRoutines / sizeof gObjectRoutines[0];
