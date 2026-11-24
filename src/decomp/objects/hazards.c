/*
 * objects/hazards.c - things that lie in wait: the snapper, the dropping block,
 * the block dweller, the boulder, the roller and the treat spout with its
 * treats.
 */
#include "decomp/objects/objects_internal.h"

/* Kind 7 ($8567): a plant that snaps at Scrooge within $0B px. Caught
 * (within 7 px), he is held hurt (mode 5, $7D = $44) at its mouth; when
 * $7D reaches 8 it spits him out, and the plant reopens when $7D is 0.
 * Touched from further away, it knocks him back. Hit: beaten. */
void Obj_Snapper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SNAPPER, 0x22, x, gObjects->kind[x]);
    if (gPlayerMode == PLAYER_MODE_DEATH) {
        return;
    }
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) != 0) {
        if (gPlayerInvulnerable == 0) {
            gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
            gObjects->action[x]--;
            gObjects->state[x]--;
        } else {
            if (gPlayerInvulnerable != 8) {
                return;
            }
            gObjects->flags[x] = (uint8_t)((gObjects->flags[P_] & 0xFC) ^ OBJ_FLAG_FACING_RIGHT);
            Sound_Request(SFX_SPIT, x);
            if (++gObjects->action[x] == 0) {
                gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
                gObjects->action[x]--;
                gObjects->state[x]--;
            }
        }
        ResetAnim(x);
        return;
    }
    uint8_t a = gObjects->animFrame[x];
    if (a == 0) {
        gObjects->animTimer[x] = 0;
        a = Obj_DistanceX(x);
        if (a < 0x0C) {
            if (gObjects->action[x] == 0x12 && gObjects->y[x] < gObjects->y[P_]) {
                ResetAnim(x);
                return;
            }
            gObjects->animFrame[x] = a = 1;
        }
    }
    if (a == 2 && gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_SNAP, x);
    }
    gObjects->hitbox[x] = Rom((uint16_t)(DT_CODE_SNAPPER_HITBOX + gObjects->animFrame[x]));
    if (!Obj_TouchScrooge(x)) {
        return;
    }
    if (Obj_DistanceX(x) >= 8) {
        gPlayerMode = PLAYER_MODE_KNOCKBACK;
        return;
    }
    gObjects->state[x]++;
    gObjects->animTimer[x] = 0x80;
    gObjects->animFrame[x] = 2;
    gPlayerInvulnerable = 0x44;
    gPlayerMode = PLAYER_MODE_HURT;
    uint8_t y = gObjects->action[x] == 0x12 ? 0 : 1;
    gObjects->y[P_] = (uint8_t)(gObjects->y[x] + Rom((uint16_t)(DT_CODE_SNAPPER_HOLD_Y + y)));
    gObjects->worldX[P_] = gObjects->worldX[x];
    gObjects->worldPage[P_] = gObjects->worldPage[x];
    Obj_SetSpeed(P_, 0x0300);
    Obj_SetVelocityY(P_, 0);
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gObjects->action[P_] = Rom((uint16_t)(DT_CODE_SNAPPER_HOLD_POSE + y));
}

/* Kind $0A ($8845): a block that falls when Scrooge comes within 3 px
 * (pose $87: $1F px) and uncovers its tile; it breaks on Scrooge or on the
 * floor. Below Scrooge's Y it is harmful. */
void Obj_Dropper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DROPPER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (Step(x) == 0) {
        if (gBlockOpened) {
            return;
        }
        uint8_t d = Obj_DistanceX(x);
        uint8_t y = gObjects->action[x];
        if (d >= (y == 0x87 ? 0x20 : 0x04)) {
            return;
        }
        Obj_SetVelocityY(x, 0);
        gObjects->state[x]++;
        if (gObjects->action[x] != 0x87) {
            gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
            gObjects->flags[x] = gObjects->state[x] & 0xF0;
            Obj_SetVelocityY(x, 0);
            Tile_EditBySlot(x, y);
            if (gObjects->y[x] < gObjects->y[P_]) {
                gObjects->hitbox[x] = 0x10;
            }
        }
    }
    if (!Obj_TouchScrooge(x)) {
        x = Obj_Fall(x, 0x2D, 0);
        if (!(gProbeHits & 1)) {
            return;
        }
    }
    Obj_Break(x);
}

/* $9649: link the block the dweller sits in ($06D0): a live block in pose
 * $1D or $1E at its world X, else a new block (kind 8, pose $1E) 8 px below
 * it in the highest free slot, else slot 0. */
static void Obj_DwellerLinkBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_DWELLER_LINK_BLOCK, 0, x, 0);
    for (uint8_t y = 0x0F; y != 0; y--) {
        if (!(gObjects->state[y] & OBJ_STATUS_LIVE)) {
            continue;
        }
        if (gObjects->action[y] != 0x1D && gObjects->action[y] != 0x1E) {
            continue;
        }
        if (gObjects->worldPage[y] == gObjects->worldPage[x] && gObjects->worldX[y] == gObjects->worldX[x]) {
            gObjects->unk_06D0[x] = y;
            return;
        }
    }
    uint8_t y = 0x0F;
    while (y != 0 && (gObjects->state[y] & OBJ_STATUS_LIVE)) {
        y--;
    }
    gObjects->unk_06D0[x] = y;
    if (y == 0) {
        return;
    }
    gObjects->worldX[y] = gObjects->worldX[x];
    gObjects->worldPage[y] = gObjects->worldPage[x];
    gObjects->y[y] = (uint8_t)(gObjects->y[x] + 8);
    gObjects->hitbox[y] = 0x80;
    gObjects->kind[y] = OBJ_KIND_BLOCK;
    gObjects->state[y] = 0xA0;
    ResetAnim(y);
    gObjects->unk_0690[y] = 0;
    gObjects->action[y] = 0x1E;
}

/* Kind $12 ($9552): an enemy that lives in a block. It steps out left or
 * right (pose $2F, 32 frames) or waits (pose $30, 60 frames) by a cycle of
 * four moves out of two ($06C0, chosen at random), the block's pose and
 * facing following it. A hit, or its block launched, beats it: sound $2C,
 * pose $31 for 60 frames, then pose $32. */
void Obj_BlockDweller(uint8_t x)
{
    static const uint8_t dirs[4] = { OBJ_DIR_RIGHT, OBJ_DIR_LEFT, OBJ_DIR_LEFT, OBJ_DIR_RIGHT };
    static const uint8_t facing[4] = { OBJ_FLAG_FACING_RIGHT, 0, 0, OBJ_FLAG_FACING_RIGHT };
    static const uint8_t cycles[2] = { 0, 4 };
    static const uint8_t blockPoses[8] = { 0x1E, 0x1D, 0x1E, 0x1E, 0x1E, 0x1D, 0x1E, 0x1E };
    static const uint8_t blockFacing[8] = { 0, OBJ_FLAG_FACING_RIGHT, 0, 0, 0, 0, 0, 0 };

    Decomp_Enter(DT_CODE_KIND_BLOCK_DWELLER, 0x22, x, gObjects->kind[x]);
    if (gObjects->action[x] == 0x31) {
        if (gObjects->timer[x] == 0) {
            gObjects->action[x] = 0x32;
            Obj_Defeat(x);
            return;
        }
        gObjects->timer[x]--;
        return;
    }
    if (Step(x) == 0) {
        Obj_DwellerLinkBlock(x);
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_SetSpeed(x, 0x0100);
        gObjects->state[x]++;
        return;
    }
    uint8_t block = gObjects->unk_06D0[x];
    if (gObjects->unk_0690[x] != 0 || gObjects->kind[block] == OBJ_KIND_THROWN) {
        Sound_Request(SFX_DWELLER_HIT, x);
        gObjects->action[x] = 0x31;
        gObjects->animTimer[x] = 0;
        gObjects->hitbox[x] = 0;
        gObjects->animFrame[x] = 0;
        gObjects->timer[x] = 0x3C;
        return;
    }
    if (gObjects->timer[x] == 0) {
        uint8_t c = gObjects->unk_06C0[x];
        gScratch0 = x;
        if ((gObjects->state[block] & OBJ_STATUS_LIVE) &&
            (gObjects->action[block] == 0x1D || gObjects->action[block] == 0x1E)) {
            gObjects->action[block] = blockPoses[c];
            gObjects->flags[block] = (uint8_t)((gObjects->flags[block] & (uint8_t)~OBJ_FLAG_FACING_RIGHT) |
                                               blockFacing[c]);
        }
        if (c & 1) {
            gObjects->timer[x] = 0x3C;
            gObjects->dir[x] = 0;
            gObjects->action[x] = 0x30;
        } else {
            gObjects->dir[x] = dirs[c >> 1];
            gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & (uint8_t)~OBJ_FLAG_FACING_RIGHT) | facing[c >> 1]);
            gObjects->timer[x] = 0x20;
            gObjects->action[x] = 0x2F;
        }
        ResetAnim(x);
        if ((++gObjects->unk_06C0[x] & 3) == 0) {
            gObjects->unk_06C0[x] = cycles[(gRandom(0) + gRandom(2) + (c & 1)) & 1];
        }
    }
    gObjects->timer[x]--;
    if (gObjects->dir[x] != 0) {
        if (gObjects->dir[x] & OBJ_DIR_LEFT) {
            Obj_MoveLeft(x);
        } else {
            Obj_MoveRight(x);
        }
    }
    (void)Obj_TouchScrooge(x);
}

/* Kind $24 ($A024): a boulder that, once Scrooge is within 4 px, rolls
 * right at $0180 a frame with probe $35 (the walls move skips the block test
 * for this kind). At a wall it walks once more and stops for good (kind 0).
 * Within $15 px of him both ways it pushes him right ($73). The second walk
 * takes probe 1 and the Y the first one left (gProbeExitY). */
void Obj_Boulder(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BOULDER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 4) {
            return;
        }
        Obj_SetSpeed(x, 0x0180);
        gObjects->state[x]++;
    }
    x = Obj_WalkRight(x, 0x35, 0);
    if (gProbeHits & 1) {
        x = Obj_WalkRight(x, 1, gProbeExitY);
        gObjects->kind[x] = OBJ_KIND_NONE;
    }
    if (Obj_DistanceX(x) < 0x15 && Obj_DistanceY(x) < 0x15) {
        gPlayerPushRight++;
    }
}

/* Kind $23 ($9FEC): something that rolls left at $0180 a frame on the
 * floor (probe $31), harming Scrooge on contact, with sound $27 at the start
 * of each animation cycle; in the air it only falls. */
void Obj_Roller(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_ROLLER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0180);
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, 0x31, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    Obj_MoveLeft(x);
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_ROLL, x);
    }
}

/* Kind $2C ($A1E9): once Scrooge is within 4 px, it throws out four
 * treats (kind $2D, pose $24, 16 px above it, vertical velocity $02D4) one
 * every 60 frames, up, right, up and left (sound $25, pose $49 back to $48
 * on animation frame 7). The thrower's slot is left in $00. */
void Obj_TreatSpout(uint8_t x)
{
    static const uint8_t dirs[4] = { 0, OBJ_DIR_RIGHT, 0, OBJ_DIR_LEFT };
    static const uint16_t speeds[4] = { 0x0000, 0x0100, 0x0000, 0x0100 };

    Decomp_Enter(DT_CODE_KIND_TREAT_SPOUT, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 4) {
            return;
        }
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
    }
    if (gObjects->unk_06C0[x] == 0) {
        uint8_t y = 0x0F;
        while (gObjects->state[y] & OBJ_STATUS_LIVE) {
            if (--y == 0) {
                return;
            }
        }
        Sound_Request(SFX_SPOUT, x);
        gObjects->action[x]++;
        gObjects->state[y] = 0xA0;
        gObjects->flags[y] = 0xA0;
        gObjects->action[y] = 0x24;
        gObjects->worldPage[y] = gObjects->worldPage[x];
        gObjects->worldX[y] = gObjects->worldX[x];
        gObjects->y[y] = (uint8_t)(gObjects->y[x] - 0x10);
        ResetAnim(x);
        ResetAnim(y);
        gObjects->hitbox[y] = 0;
        gObjects->kind[y] = OBJ_KIND_TREAT;
        gScratch0 = x;
        Obj_SetVelocityY(y, 0x02D4);
        uint8_t i = gObjects->timer[x];
        gObjects->dir[y] = dirs[i];
        Obj_SetSpeed(y, speeds[i]);
        gObjects->unk_06C0[x] = ++gObjects->timer[x] == 4 ? 0xFF : 0x3C;
        return;
    }
    if (!(gObjects->unk_06C0[x] & 0x80)) {
        gObjects->unk_06C0[x]--;
    }
    if (gObjects->action[x] == 0x48 || gObjects->animTimer[x] != 7) {
        return;
    }
    gObjects->action[x]--;
    ResetAnim(x);
}

/* Kind $2D ($A2BA): a treat that rises, then falls at up to 3 px a frame
 * while its sideways speed runs down by $40 a frame; touched, it is freed
 * and Scrooge gets an HP dot back ($8B82). It is freed past Y $F0. */
void Obj_Treat(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_TREAT, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (gObjects->vy[x] & 0x80) {
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
        }
    } else {
        if (gObjects->vy[x] != 0xFD) {
            Obj_ApplyGravity(x);
        }
        Obj_ApplyVelocityY(x);
    }
    if (gObjects->dir[x] != 0) {
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            Obj_MoveRight(x);
        } else {
            Obj_MoveLeft(x);
        }
        if (Step(x) != 0) {
            Obj_SetSpeed(x, (uint16_t)(Obj_GetSpeed(x) - 0x40));
            if (Obj_GetSpeed(x) == 0) {
                gObjects->dir[x] = 0;
            }
        }
    }
    if (Obj_TouchScrooge(x)) {
        gObjects->state[x] = 0;
        Hud_RestoreDot();
        return;
    }
    if (gObjects->y[x] >= 0xF0) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
    }
}

/* ---- adapters ---- */

static void Run_Snapper(Regs *r)
{
    Obj_Snapper(r->x);
}

static void Run_Dropper(Regs *r)
{
    Obj_Dropper(r->x);
}

static void Run_BlockDweller(Regs *r)
{
    Obj_BlockDweller(r->x);
}

static void Run_DwellerLinkBlock(Regs *r)
{
    Obj_DwellerLinkBlock(r->x);
}

static void Run_Boulder(Regs *r)
{
    Obj_Boulder(r->x);
}

static void Run_Roller(Regs *r)
{
    Obj_Roller(r->x);
}

static void Run_TreatSpout(Regs *r)
{
    Obj_TreatSpout(r->x);
}

static void Run_Treat(Regs *r)
{
    Obj_Treat(r->x);
}

const DecompRoutine gObjHazardRoutines[] = {
    { DT_CODE_KIND_SNAPPER, NULL, "Obj_Snapper", Run_Snapper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DROPPER, NULL, "Obj_Dropper", Run_Dropper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TREAT, NULL, "Obj_Treat", Run_Treat, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TREAT_SPOUT, NULL, "Obj_TreatSpout", Run_TreatSpout, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_ROLLER, NULL, "Obj_Roller", Run_Roller, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOULDER, NULL, "Obj_Boulder", Run_Boulder, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BLOCK_DWELLER, NULL, "Obj_BlockDweller", Run_BlockDweller, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_DWELLER_LINK_BLOCK, NULL, "Obj_DwellerLinkBlock", Run_DwellerLinkBlock, 0, .bank = DT_BANK_OBJECTS },
};
const int gObjHazardRoutineCount = sizeof gObjHazardRoutines / sizeof gObjHazardRoutines[0];
