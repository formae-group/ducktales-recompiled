/*
 * objects/props.c - objects Scrooge uses or talks to: the lift, the door, the
 * talker, the swinging hold, the key spot, the moving platform, the cart and
 * the visitor with the path it opens.
 */
#include "decomp/objects/objects_internal.h"

/* Kind $0F ($9093): a platform that rises to Y $68 then sinks to $A8 at
 * 1.5 px a frame, carrying Scrooge down 2 px a frame when he stands near
 * it. */
void Obj_Lift(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LIFT, 0x22, x, gObjects->kind[x]);
    Obj_SetVelocityY(x, 0x0180);
    if (Step(x) == 0) {
        Obj_SubVelocityY(x);
        if (gObjects->y[x] >= 0x68) {
            return;
        }
        gObjects->state[x]++;
    } else {
        Obj_AddVelocityY(x);
    }
    if (Obj_DistanceX(x) < 0x1D && Obj_DistanceY(x) < 0x18) {
        gObjects->y[P_] += 2;
    }
    if (gObjects->y[x] >= 0xA8) {
        gObjects->state[x]--;
    }
}

/* Kind $2F ($9065): a door. Touched, it requests warp $06B0 ($7E). Door 0
 * opens only with message bit 3 of $A8, door 4 only with every land's
 * treasure, else it sends Scrooge to Land Select ($A4). */
void Obj_Door(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DOOR, 0x22, x, gObjects->kind[x]);
    if (!Obj_TouchScrooge(x)) {
        return;
    }
    uint8_t warp = gObjects->timer[x];
    if (warp == 0) {
        if (!(gMessagesShown(0) & 8)) {
            return;
        }
    } else if (warp == 4) {
        if ((gTreasures & 0x1F) != 0x1F) {
            gLandRedirect = 0x80;
            return;
        }
    }
    gRespawn = (uint8_t)(warp | 0x80);
}

/* Kind $35 (fixed bank $FDF5): a talking character. Touched (Scrooge not
 * hurt or dying), it shows message $06B0 (mode $0F) and sets its bit in
 * $A8-; message 8 also counts in $A1 and is used up. Launchpad (pose $33)
 * stays quiet in a land where his ride was taken ($A6). */
void Obj_Talker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_TALKER, 0x22, x, gObjects->kind[x]);
    if (gPlayerMode == PLAYER_MODE_DEATH || gPlayerMode == PLAYER_MODE_KNOCKBACK) {
        return;
    }
    if (Step(x) != 0 || !Obj_TouchScrooge(x)) {
        return;
    }
    if (gObjects->action[x] == 0x33 && (gLaunchpadRides & Rom((uint16_t)(DT_CODE_LAND_BIT + gLand)))) {
        return;
    }
    gPlayerMode = PLAYER_MODE_MESSAGE;
    gObjects->state[x]++;
    gSceneStep = gObjects->timer[x];
    gMessageSlot = x;
    if (gSceneStep == 8) {
        gProgressA1++;
        gObjects->state[x] = 0;
        Obj_MarkConsumed(x);
    }
    uint8_t id = gSceneStep;
    gScratch(1) = Rom((uint16_t)(DT_CODE_LAND_BIT + (id & 7)));
    gMessagesShown(id >> 3) |= gScratch(1);
}

/* Kind $0E ($9225): a swinging hold (a vine or rope end) moving through 36
 * motion entries ($92ED, bit 7: 15 frames instead of 5) with signed speed.
 * Scrooge touching it with Up held grabs it (mode 9, climbing pose) and
 * moves with it. */
void Obj_Swing(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SWING, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->yFrac[x] = 0;
        gObjects->xFrac[x] = 0;
        gObjects->state[x]++;
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_SWING, x);
    }
    if (gObjects->timer[x] == 0) {
        uint8_t e = Rom((uint16_t)(DT_CODE_SWING_PATH + gObjects->unk_06C0[x]));
        Obj_SetMotionIndex(x, e & 0x1F);
        gObjects->timer[x] = Rom((uint16_t)(DT_CODE_SWING_FRAMES + (e >> 7)));
        if (++gObjects->unk_06C0[x] == 0x24) {
            gObjects->unk_06C0[x] = 0;
        }
    }
    gObjects->timer[x]--;
    Obj_AddVelocityY(x);
    gScratch(0) = (gObjects->speed[x] & 0x80) ? 0xFF : 0x00;
    uint8_t p = 0;
    gObjects->xFrac[x] = Alu_Adc(gObjects->xFrac[x], gObjects->speedFrac[x], &p);
    gObjects->worldX[x] = Alu_Adc(gObjects->worldX[x], gObjects->speed[x], &p);
    gObjects->worldPage[x] = Alu_Adc(gObjects->worldPage[x], gScratch(0), &p);
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK || gPlayerMode == PLAYER_MODE_DEATH) {
        return;
    }
    if (gPlayerMode != PLAYER_MODE_9) {
        if (!Obj_TouchScrooge(x) || !(gPadHeld & DT_PAD_UP)) {
            return;
        }
        gPlayerMode = PLAYER_MODE_9;
        gObjects->action[P_] = PLAYER_ACTION_CLIMB;
        gObjects->animTimer[P_] = 0;
        gObjects->animFrame[P_] = 0;
    }
    gObjects->y[P_] = gObjects->y[x];
    gObjects->worldX[P_] = gObjects->worldX[x];
    gObjects->worldPage[P_] = gObjects->worldPage[x];
}

/* Kind $41 (fixed bank $FE50): a spot that, while Scrooge holds the key
 * item the land asks for ($A3 & $F6), sets his special-tile bit ($0600 bit
 * 2) when he touches it. */
void Obj_KeySpot(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_KEY_SPOT, 0x22, x, gObjects->kind[x]);
    if ((gKeyItems & gLand) == 0) {
        return;
    }
    gObjects->hitbox[x] = 0;
    if (Obj_TouchScrooge(x)) {
        gObjects->flags[P_] |= 4;
    }
}

/* Kind $39 (fixed bank $FE69): a platform that moves 48 frames at a time
 * by a cycle of four directions, carrying Scrooge 1 px a frame its way while
 * he touches it (hitbox $8A for the test, $84 after). */
void Obj_MovingPlatform(uint8_t x)
{
    static const uint8_t dirs[4] = { OBJ_DIR_LEFT, OBJ_DIR_RIGHT, OBJ_DIR_RIGHT, OBJ_DIR_LEFT };

    Decomp_Enter(DT_CODE_KIND_MOVING_PLATFORM, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_SetSpeed(x, 0x0100);
        gObjects->state[x]++;
    }
    if (gObjects->unk_06C0[x] == 0) {
        gObjects->dir[x] = dirs[gObjects->timer[x] & 3];
        gObjects->unk_06C0[x] = 0x30;
        gObjects->timer[x]++;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        Obj_MoveRight(x);
    } else {
        Obj_MoveLeft(x);
    }
    gObjects->unk_06C0[x]--;
    gObjects->hitbox[x] = 0x8A;
    if (Obj_TouchScrooge(x)) {
        Obj_SetWorldX(P_, (uint16_t)(Obj_GetWorldX(P_) + ((gObjects->dir[x] & OBJ_DIR_LEFT) ? 0xFFFF : 1)));
    }
    gObjects->hitbox[x] = 0x84;
}

/* Kind $3F (fixed bank $FD68): what kind $3E leaves. It moves right at 3 px
 * a frame; at world X $0770 it is freed, lets Scrooge go (mode 0) and draws
 * block record 6 ($D068), then maps bank 6 back. */
void Obj_PathOpener(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PATH_OPENER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        Obj_SetSpeed(x, 0x0300);
    }
    Obj_MoveRight(x);
    if (gObjects->worldPage[x] != 0x07 || gObjects->worldX[x] < 0x70) {
        return;
    }
    gObjects->state[x] = 0;
    gPlayerMode = PLAYER_MODE_GROUND;
    Block_Draw(6);
    Bank_Select(DT_BANK_OBJECTS);
}

/* Kind $3E (fixed bank $FCA0): a character that comes once ($A1 set; bit 7
 * marks it). With Scrooge within $38 px it takes its record, holds Scrooge
 * (mode 14) and walks in from the camera's left edge at $0180, shown and
 * harmless. Within $38 px of him it leaves a kind $3F (pose $7E) where it
 * stands ($00 keeps its slot) and waits 180 frames; then, on screen, it
 * falls (probe 6) and on the floor walks back left. Off screen it is freed. */
void Obj_Visitor(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_VISITOR, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x38 || gProgressA1 == 0) {
            return;
        }
        gProgressA1 |= 0x80;
        Obj_MarkConsumed(x);
        gPlayerMode = PLAYER_MODE_14;
        gObjects->worldX[x] = gCameraX;
        gObjects->worldPage[x] = gCameraPage;
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        gObjects->state[x]++;
        Obj_SetSpeed(x, 0x0180);
        gObjects->hitbox[x] = 0;
        return;
    }
    if (!(gObjects->state[x] & 2)) {
        Obj_MoveRight(x);
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        if (Obj_DistanceX(x) >= 0x38) {
            return;
        }
        gObjects->state[x]++;
        gObjects->timer[x] = 0xB4;
        gScratch0 = x;
        uint8_t o;
        if (!Obj_FindFreeSlot(&o)) {
            return;
        }
        gObjects->action[o] = 0x7E;
        gObjects->y[o] = gObjects->y[x];
        gObjects->worldX[o] = gObjects->worldX[x];
        gObjects->worldPage[o] = gObjects->worldPage[x];
        gObjects->animFrame[o] = 0;
        gObjects->animTimer[o] = 0;
        gObjects->state[o] = 0xA0;
        gObjects->flags[o] = 0xA0;
        gObjects->kind[o] = OBJ_KIND_PATH_OPENER;
        return;
    }
    if (gObjects->timer[x] != 0) {
        gObjects->timer[x]--;
        return;
    }
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    x = Obj_Fall(x, 0x06, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    Obj_MoveLeft(x);
    gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
}

/* Kind $21 ($9EB3): a cart Scrooge rides. Touched, it takes him (mode 10,
 * his pose $8B, its pose $3E) and rolls left at $0180 (probes $38) once on
 * the rails (probe $31 below). While he rides, his mode is 10 on the rails
 * and 11 in the air, he sits $0B px above it, and each new animation frame
 * of the cart plays sound $33. */
void Obj_Cart(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CART, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        if (!Obj_TouchScrooge(x)) {
            return;
        }
        gPlayerMode = PLAYER_MODE_CARRIED_A;
        gObjects->action[x] = 0x3E;
        gObjects->action[P_] = 0x8B;
        gObjects->animTimer[x] = 0;
        gObjects->animTimer[P_] = 0;
        gObjects->animFrame[x] = 0;
        gObjects->animFrame[P_] = 0;
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0180);
    }
    gObjects->timer[x] = PLAYER_MODE_CARRIED_B;
    x = Obj_Fall(x, 0x31, 2);
    if (gProbeHits & 1) {
        x = Obj_WalkLeft(x, 0x38, 0);
        gObjects->timer[x] = PLAYER_MODE_CARRIED_A;
    }
    if (gPlayerMode != PLAYER_MODE_CARRIED_A && gPlayerMode != PLAYER_MODE_CARRIED_B) {
        return;
    }
    gPlayerMode = gObjects->timer[x];
    gObjects->y[P_] = (uint8_t)(gObjects->y[x] - 0x0B);
    gObjects->worldX[P_] = gObjects->worldX[x];
    gObjects->worldPage[P_] = gObjects->worldPage[x];
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_CART, x);
    }
}

/* ---- adapters ---- */

static void Run_Swing(Regs *r)
{
    Obj_Swing(r->x);
}

static void Run_Lift(Regs *r)
{
    Obj_Lift(r->x);
}

static void Run_KeySpot(Regs *r)
{
    Obj_KeySpot(r->x);
}

static void Run_MovingPlatform(Regs *r)
{
    Obj_MovingPlatform(r->x);
}

static void Run_PathOpener(Regs *r)
{
    Obj_PathOpener(r->x);
}

static void Run_Visitor(Regs *r)
{
    Obj_Visitor(r->x);
}

static void Run_Cart(Regs *r)
{
    Obj_Cart(r->x);
}

static void Run_Door(Regs *r)
{
    Obj_Door(r->x);
}

static void Run_Talker(Regs *r)
{
    Obj_Talker(r->x);
}

const DecompRoutine gObjPropRoutines[] = {
    { DT_CODE_KIND_SWING, NULL, "Obj_Swing", Run_Swing, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LIFT, NULL, "Obj_Lift", Run_Lift, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CART, NULL, "Obj_Cart", Run_Cart, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_VISITOR, NULL, "Obj_Visitor", Run_Visitor, 0 },
    { DT_CODE_KIND_PATH_OPENER, NULL, "Obj_PathOpener", Run_PathOpener, 0 },
    { DT_CODE_KIND_MOVING_PLATFORM, NULL, "Obj_MovingPlatform", Run_MovingPlatform, 0 },
    { DT_CODE_KIND_KEY_SPOT, NULL, "Obj_KeySpot", Run_KeySpot, 0 },
    { DT_CODE_KIND_DOOR, NULL, "Obj_Door", Run_Door, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TALKER, NULL, "Obj_Talker", Run_Talker, 0 },
};
const int gObjPropRoutineCount = sizeof gObjPropRoutines / sizeof gObjPropRoutines[0];
