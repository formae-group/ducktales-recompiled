/*
 * motion.c - object motion primitives of the fixed bank, decompiled
 * (docs/decompilation.md). Every object uses them with X = its slot, Scrooge
 * included: velocity and gravity on Y, horizontal speed on world X. Values
 * are 8.8 fixed point split over two object rows.
 *
 * Callers test the flags these leave (for example $F298 keeps the carry of
 * the Y move across the background probe), so each one also gives the flags
 * of its last arithmetic instruction to its site's adapter.
 */
#include "decomp/decomp.h"
#include "decomp/motion.h"

#include "ducktales/game.h"
#include "ducktales/objects.h"

static uint8_t s_a, s_p;

/* $F2A1: Y -= vertical velocity, so a positive velocity moves up. */
void Obj_ApplyVelocityY(int slot)
{
    uint8_t p = P_C;
    gObjects->yFrac[slot] = Alu_Sbc(gObjects->yFrac[slot], gObjects->vyFrac[slot], &p);
    gObjects->y[slot] = s_a = Alu_Sbc(gObjects->y[slot], gObjects->vy[slot], &p);
    s_p = p;
}

/* $F2B5: vertical velocity -= gravity ($EF). */
void Obj_ApplyGravity(int slot)
{
    uint8_t p = P_C;
    gObjects->vyFrac[slot] = Alu_Sbc(gObjects->vyFrac[slot], gGravity, &p);
    gObjects->vy[slot] = s_a = Alu_Sbc(gObjects->vy[slot], 0, &p);
    s_p = p;
}

/* $F234: world X += horizontal speed. */
void Obj_MoveRight(int slot)
{
    uint8_t p = 0;
    gObjects->xFrac[slot] = Alu_Adc(gObjects->xFrac[slot], gObjects->speedFrac[slot], &p);
    gObjects->worldX[slot] = s_a = Alu_Adc(gObjects->worldX[slot], gObjects->speed[slot], &p);
    if (p & P_C) gObjects->worldPage[slot] = s_a = Alu_Adc(gObjects->worldPage[slot], 0, &p);
    s_p = p;
}

/* $F252: world X -= horizontal speed. */
void Obj_MoveLeft(int slot)
{
    uint8_t p = P_C;
    gObjects->xFrac[slot] = Alu_Sbc(gObjects->xFrac[slot], gObjects->speedFrac[slot], &p);
    gObjects->worldX[slot] = s_a = Alu_Sbc(gObjects->worldX[slot], gObjects->speed[slot], &p);
    if (!(p & P_C)) gObjects->worldPage[slot] = s_a = Alu_Sbc(gObjects->worldPage[slot], 0, &p);
    s_p = p;
}

/* ---- site adapters: X = slot; A and N, V, Z, C as the RTS leaves them ---- */

static void finish(Regs *r)
{
    r->a = s_a;
    r->p = (uint8_t)((r->p & (P_I | P_D | 0x30)) | s_p);
}

static void Run_ApplyVelocityY(Regs *r) { Obj_ApplyVelocityY(r->x); finish(r); }
static void Run_ApplyGravity(Regs *r) { Obj_ApplyGravity(r->x); finish(r); }
static void Run_MoveRight(Regs *r) { Obj_MoveRight(r->x); finish(r); }
static void Run_MoveLeft(Regs *r) { Obj_MoveLeft(r->x); finish(r); }

const DecompRoutine gMotionRoutines[] = {
    { DT_CODE_APPLY_VY, "ducktales.decomp.F2A1", "Obj_ApplyVelocityY", Run_ApplyVelocityY, OUT_A | OUT_X | OUT_Y | OUT_P },
    { DT_CODE_APPLY_GRAVITY, "ducktales.decomp.F2B5", "Obj_ApplyGravity", Run_ApplyGravity, OUT_A | OUT_X | OUT_Y | OUT_P },
    { DT_CODE_MOVE_RIGHT_SPEED, "ducktales.decomp.F234", "Obj_MoveRight", Run_MoveRight, OUT_A | OUT_X | OUT_Y | OUT_P },
    { DT_CODE_MOVE_LEFT_SPEED, "ducktales.decomp.F252", "Obj_MoveLeft", Run_MoveLeft, OUT_A | OUT_X | OUT_Y | OUT_P },
};
const int gMotionRoutineCount = sizeof gMotionRoutines / sizeof gMotionRoutines[0];
