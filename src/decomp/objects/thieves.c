/*
 * objects/thieves.c - the enemies that take Scrooge's money: the thief (kinds $30
 * and $31) and the pickpocket ($32) with its walk and theft.
 */
#include "decomp/objects/objects_internal.h"

/* $AD56: the pickpocket's walk, in its direction with probes $18 or $1B. */
static uint8_t Obj_PickpocketWalk(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKPOCKET_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        return Obj_WalkRight(x, 0x18, 2);
    }
    return Obj_WalkLeft(x, 0x1B, 2);
}

/* $AD6B: take 1 from the money's third digit ($06B0 counts the thefts).
 * Blank digits count as 0; a borrow out of the top blanks all six digits,
 * else the leading zeros are blanked again. */
static void Obj_StealMoney(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKPOCKET_STEAL, 0, x, 0);
    gObjects->timer[x]++;
    gScratch0 = 1;
    bool carry = true;
    for (int y = 2; y >= 0; y--) {
        uint8_t d = gMoneyDigit(y) == DT_MONEY_TILE_BLANK ? 0 : gMoneyDigit(y);
        int v = d - gScratch0 - (carry ? 0 : 1);
        carry = v >= 0;
        if (!carry) {
            v += 10;
        }
        gMoneyDigit(y) = (uint8_t)v;
        gScratch0 = 0;
    }
    if (!carry) {
        for (int y = 5; y >= 0; y--) {
            gMoneyDigit(y) = DT_MONEY_TILE_BLANK;
        }
    } else {
        for (int y = 0; y < 6; y++) {
            if (gMoneyDigit(y) == DT_MONEY_TILE_BLANK) {
                continue;
            }
            if (gMoneyDigit(y) != 0) {
                break;
            }
            gMoneyDigit(y) = DT_MONEY_TILE_BLANK;
        }
    }
    gMoneyChanged++;
}

/* Kind $32 ($ACBE): an enemy that walks at Scrooge (speed index 1),
 * jumps (4 px a frame) when it walks into a wall, and the first time it
 * touches him takes 1 from the money's third digit. Off screen or hit, it
 * is beaten. */
void Obj_Pickpocket(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PICKPOCKET, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->speedFrac[x] = 0;
        gObjects->timer[x] = 0;
        gObjects->speed[x] = 1;
        gObjects->state[x]++;
        Obj_SetSpeedIndex(x, 1);
        Obj_FaceScrooge(x);
    }
    bool landed = false;
    if (gObjects->state[x] & 2) {
        x = Obj_Rise(x, 0x21, 2);
        if (gObjects->vy[x] & 0x80) {
            gObjects->state[x]--;
            Obj_SetVelocityY(x, 0);
            x = Obj_Fall(x, 0x1E, 2);
            landed = gProbeHits & 1;
        }
    } else {
        x = Obj_Fall(x, 0x1E, 2);
        landed = gProbeHits & 1;
    }
    if (!landed) {
        gObjects->animFrame[x] = 0;
        gObjects->animTimer[x] = 0;
    }
    x = Obj_PickpocketWalk(x);
    if (gObjects->timer[x] == 0 && Obj_TouchScrooge(x)) {
        Obj_StealMoney(x);
    }
    if ((gObjects->state[x] & 2) || !(gProbeHits & 1)) {
        return;
    }
    if (Obj_GetVelocityY(x) != 0) {
        return;
    }
    gObjects->state[x]++;
    Obj_SetVelocityY(x, 0x0400);
}

/* Kind $30 ($AC3D, also kind $31's handler): an enemy that paces 1 px a
 * frame, right first, for its kind's span ($16 or $0B frames, doubled after
 * the first turn), resting 120 frames at each turn and facing its way; each
 * frame it touches Scrooge it takes 1 from the money's third digit (which
 * also adds to its span left). A hit beats it. */
void Obj_Thief(uint8_t x)
{
    static const uint8_t spans[2] = { 0x16, 0x0B };

    Decomp_Enter(DT_CODE_KIND_THIEF, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        gObjects->unk_06C0[x] = 0;
        uint8_t span = spans[(uint8_t)(gObjects->kind[x] - OBJ_KIND_THIEF)];
        gObjects->timer[x] = span;
        gObjects->unk_06D0[x] = span;
        Obj_SetSpeed(x, 0x0100);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    if (gObjects->unk_06C0[x] != 0) {
        gObjects->unk_06C0[x]--;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        Obj_MoveRight(x);
    } else {
        gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
        Obj_MoveLeft(x);
    }
    if (Obj_TouchScrooge(x)) {
        Obj_StealMoney(x);
    }
    if (--gObjects->timer[x] != 0) {
        return;
    }
    gObjects->dir[x] ^= 3;
    gObjects->unk_06C0[x] = 0x78;
    gObjects->timer[x] = (uint8_t)(gObjects->unk_06D0[x] << 1);
}

/* ---- adapters ---- */

static void Run_Pickpocket(Regs *r)
{
    Obj_Pickpocket(r->x);
}

static void Run_PickpocketWalk(Regs *r)
{
    Obj_PickpocketWalk(r->x);
}

static void Run_StealMoney(Regs *r)
{
    Obj_StealMoney(r->x);
}

static void Run_Thief(Regs *r)
{
    Obj_Thief(r->x);
}

const DecompRoutine gObjThiefRoutines[] = {
    { DT_CODE_KIND_THIEF, NULL, "Obj_Thief", Run_Thief, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PICKPOCKET, NULL, "Obj_Pickpocket", Run_Pickpocket, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKPOCKET_WALK, NULL, "Obj_PickpocketWalk", Run_PickpocketWalk, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKPOCKET_STEAL, NULL, "Obj_StealMoney", Run_StealMoney, 0, .bank = DT_BANK_OBJECTS },
};
const int gObjThiefRoutineCount = sizeof gObjThiefRoutines / sizeof gObjThiefRoutines[0];
