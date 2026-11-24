/*
 * objects/enemies.c - what every enemy shares: a beaten enemy flying off and
 * its drop, the motion table index, the beaten test, the plain walk and the
 * free-slot search of bank 6.
 */
#include "decomp/objects/objects_internal.h"

/* $89DA: a defeated enemy may drop an item: one time in four by the random
 * bytes ($C1 += $C0 + carry), when its kind has one ($9025) and a slot is
 * free; the item appears where the enemy is. carry is the carry the
 * caller's sound request left. */
static void Obj_Drop(uint8_t x, bool carry)
{
    gScratch(0) = x;
    gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + carry);
    if (gRandom(1) & 3) {
        return;
    }
    uint8_t item = Rom((uint16_t)(DT_CODE_DROP_BY_KIND + gObjects->kind[x]));
    if (item == 0) {
        return;
    }
    gScratch(1) = item;
    uint8_t o;
    if (!Obj_FindFreeSlot(&o)) {
        return;
    }
    Obj_ShowItem(o, gScratch(1));
    uint8_t from = gScratch(0);
    gObjects->y[o] = gObjects->y[from];
    gObjects->worldX[o] = gObjects->worldX[from];
    gObjects->worldPage[o] = gObjects->worldPage[from];
}

/* Kind $0B ($897B): a defeated enemy flies off: up at $02D4, then down
 * (vdir), sideways at $0080 in $07E0's direction, freed when it leaves the
 * screen or the world. */
void Obj_Defeated(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DEFEATED, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->vdir[x] = 0;
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x02D4);
        Obj_SetSpeed(x, 0x0080);
    }
    if (gObjects->vdir[x] == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (gObjects->vy[x] & 0x80) {
            gObjects->vdir[x]++;
            Obj_SetVelocityY(x, 0);
        }
    } else {
        Obj_ApplyGravity(x);
        if (Obj_ApplyVelocityYCarry(x)) {
            gObjects->state[x] = 0;
            return;
        }
    }
    if ((gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) {
        gObjects->state[x] = 0;
    }
}

/* $8932: an enemy is beaten (EVENT_ENEMY_DEFEATED): sound $35, maybe a drop,
 * then it flies off away from Scrooge as kind $0B. Off screen it is just
 * freed. */
void Obj_Defeat(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_DEFEAT, 0, x, 0);
    Decomp_RaiseSlotEvent(EVENT_ENEMY_DEFEATED, x);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
        return;
    }
    Obj_Drop(x, Sound_Request(SFX_DEFEAT, x) & P_C);
    gObjects->animTimer[x] = 0x80;
    gObjects->animFrame[x] = 0;
    gObjects->hitbox[x] = 0;
    gObjects->state[x] &= 0xF0;
    gObjects->kind[x] = OBJ_KIND_DEFEATED;
    gObjects->dir[x] = Obj_GetWorldX(x) >= Obj_GetWorldX(P_) ? OBJ_DIR_RIGHT : OBJ_DIR_LEFT;
    Obj_Defeated(x);
}

/* ---- enemies ---- */

/* $818A: vertical velocity and speed from the table by index (8.8 each). */
void Obj_SetMotionIndex(uint8_t x, uint8_t index)
{
    Decomp_Enter(DT_CODE_OBJ_SET_MOTION, index, x, 0);
    uint8_t i = (uint8_t)(index << 1);
    gObjects->vyFrac[x] = Rom((uint16_t)(DT_CODE_MOTION_VY + i));
    gObjects->vy[x] = Rom((uint16_t)(DT_CODE_MOTION_VY + i + 1));
    gObjects->speedFrac[x] = Rom((uint16_t)(DT_CODE_MOTION_SPEED + i));
    gObjects->speed[x] = Rom((uint16_t)(DT_CODE_MOTION_SPEED + i + 1));
}

/* An enemy that is off screen or was hit by a counted blow is beaten; the
 * marker $4F is reset first. */
bool Enemy_Beaten(uint8_t x, uint8_t hit_pose)
{
    if (OnScreen(x)) {
        if (gObjects->unk_0690[x] == 0) {
            return false;
        }
        if (hit_pose) {
            gObjects->action[x] = hit_pose;
        }
    }
    gHeldSlot = 0x3C;
    Obj_Defeat(x);
    return true;
}

/* $82F9: walk in the slot's direction with probes $18 or $1B. */
uint8_t Enemy_Walk(uint8_t x)
{
    Decomp_Enter(DT_CODE_ENEMY_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        return Obj_WalkRight(x, 0x18, 2);
    }
    return Obj_WalkLeft(x, 0x1B, 2);
}

/* $80B3: the highest free slot 15..1 (the bank 6 copy, which answers in Y). */
bool Obj_FindFreeSlotY(uint8_t *slot)
{
    Decomp_Enter(DT_CODE_OBJ_FIND_FREE_Y, 0, 0, 0);
    for (uint8_t y = 0x0F; y != 0; y--) {
        if (!(gObjects->state[y] & OBJ_STATUS_LIVE)) {
            *slot = y;
            return true;
        }
    }
    *slot = 0;
    return false;
}

/* ---- adapters ---- */

static void Run_Defeated(Regs *r)
{
    Obj_Defeated(r->x);
}

static void Run_FindFreeSlotY(Regs *r)
{
    uint8_t slot;
    SetCarry(r, !Obj_FindFreeSlotY(&slot));
    r->y = slot;
}

static void Run_SetMotionIndex(Regs *r)
{
    Obj_SetMotionIndex(r->x, r->a);
}

static void Run_EnemyWalk(Regs *r)
{
    r->x = Enemy_Walk(r->x);
}

static void Run_Defeat(Regs *r)
{
    Obj_Defeat(r->x);
}

const DecompRoutine gObjEnemyRoutines[] = {
    { DT_CODE_KIND_DEFEATED, NULL, "Obj_Defeated", Run_Defeated, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_FIND_FREE_Y, NULL, "Obj_FindFreeSlotY", Run_FindFreeSlotY, OUT_Y | OUT_C, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SET_MOTION, NULL, "Obj_SetMotionIndex", Run_SetMotionIndex, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_ENEMY_WALK, NULL, "Enemy_Walk", Run_EnemyWalk, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_DEFEAT, NULL, "Obj_Defeat", Run_Defeat, 0, .bank = DT_BANK_OBJECTS },
};
const int gObjEnemyRoutineCount = sizeof gObjEnemyRoutines / sizeof gObjEnemyRoutines[0];
