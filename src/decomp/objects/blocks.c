/*
 * objects/blocks.c - blocks and what Scrooge's cane does to them: the cane's reach,
 * a thrown slot hitting others, breaking, opening, launching and kicking a
 * block, and kind 8 (the block) and kind 9 (a launched block).
 */
#include "decomp/objects/objects_internal.h"

/* Scrooge swinging the cane, on the frame it reaches out (frame 3), at the
 * slot: the test the breakable kinds make. The contact test only runs once
 * the swing checks pass. */
static bool Obj_CaneReaches(uint8_t x)
{
    return gObjects->animTimer[P_] == 0 && gObjects->action[P_] == PLAYER_ACTION_CANE && Obj_TouchScrooge(x) &&
           gObjects->animFrame[P_] == 3;
}

/* $80C1: the first slot on screen (15 down to 1) that a thrown slot hits.
 * Kinds other than 8, 9 and $25 count a hit ($0690); the thrower takes the
 * pose $2A, or $2B after a counted hit, unless it is in pose $1B. The slot
 * hit is left in $11. */
bool Obj_HitOthers(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_HIT_OTHERS, 0, x, 0);
    if (!OnScreen(x)) {
        return false;
    }
    gScratch(0) = x;
    gScratch(1) = gObjects->y[x];
    gScratch(2) = gObjects->worldX[x];
    gScratch(3) = gObjects->worldPage[x];
    uint8_t box = gObjects->hitbox[x] & 0x7F;
    uint16_t wx = (uint16_t)(gScratch(3) << 8 | gScratch(2));
    for (uint8_t o = 0x0F; o != 0; o--) {
        if (o == x || !OnScreen(o)) {
            continue;
        }
        if (AbsDiff(gScratch(1), gObjects->y[o]) >= Rom((uint16_t)(DT_CODE_HIT_HEIGHT + box))) {
            continue;
        }
        if (AbsDiff16(wx, Obj_GetWorldX(o)) > Rom((uint16_t)(DT_CODE_HIT_WIDTH + box))) {
            continue;
        }
        uint8_t pose = OBJ_ACTION_BREAK;
        uint8_t kind = gObjects->kind[o];
        if (kind != OBJ_KIND_BLOCK && kind != OBJ_KIND_THROWN && kind != 0x25) {
            gObjects->unk_0690[o]++;
            pose = OBJ_ACTION_BREAK_HIT;
        }
        gContactDepth = o;
        if (gObjects->action[x] != OBJ_ACTION_CHEST) {
            gObjects->action[x] = pose;
            ResetAnim(x);
        }
        return true;
    }
    return false;
}

/* $88DF (and $88E4 without the pose): break into the debris animation; the
 * slot is freed when it ends ($0600 bit 3). */
static void Obj_Shatter(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_SHATTER, 0, x, 0);
    gObjects->kind[x] = OBJ_KIND_NONE;
    gObjects->hitbox[x] = 0;
    gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & 0xF0) | OBJ_FLAG_DELETE_AT_END);
    Sound_Request(SFX_BREAK, x);
}

void Obj_Break(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_BREAK, 0, x, 0);
    gObjects->action[x] = OBJ_ACTION_BREAK;
    Obj_Shatter(x);
}

/* $8A32: become a pickup (kind $13) that jumps out with 4 px/frame, and run
 * the pickup's handler now; the item is in $06B0. */
void Obj_BecomePickupNow(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_BECOME_PICKUP, 0, x, 0);
    gObjects->state[x] &= OBJ_STATUS_LIVE | OBJ_STATUS_WORLD;
    gObjects->hitbox[x] = 0;
    ResetAnim(x);
    Obj_SetVelocityY(x, 0x0400);
    gObjects->kind[x] = OBJ_KIND_PICKUP;
    gObjects->action[x] = OBJ_ACTION_BREAK;
    Obj_Pickup(x);
}

/* $8A11: a block or chest that was opened: mark its record consumed and
 * pick its item, from the land's table for pose $3F (when not 0), else from
 * the item table by $06B0. */
static void Obj_OpenBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_OPEN_BLOCK, 0, x, 0);
    Obj_MarkConsumed(x);
    uint8_t item = 0;
    if (gObjects->action[x] == OBJ_ACTION_LAND_CHEST) {
        item = Rom((uint16_t)(DT_CODE_ITEM_BY_LAND + gLand * 3 + gObjects->timer[x]));
    }
    if (item == 0) {
        item = Rom((uint16_t)(DT_CODE_ITEM_BY_INDEX + gObjects->timer[x]));
    }
    gObjects->timer[x] = item;
    Obj_BecomePickupNow(x);
}

/* $86B5: a block hit by the cane starts flying in the push direction
 * ($3F), with the velocities of its pose ($1A-based) and becomes kind 9. */
static void Obj_LaunchBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_LAUNCH_BLOCK, 0, x, 0);
    gObjects->dir[x] = gPlayerPushDir;
    uint8_t n = (uint8_t)(gObjects->action[x] - 0x1A);
    gScratch(0) = n;
    gObjects->state[x] = (uint8_t)((gObjects->state[x] & 0xF0) | n);
    uint16_t t = (uint16_t)(DT_CODE_BLOCK_LAUNCH + (uint8_t)(n << 2));
    gObjects->vyFrac[x] = Rom(t);
    gObjects->vy[x] = Rom((uint16_t)(t + 1));
    gObjects->speedFrac[x] = Rom((uint16_t)(t + 2));
    gObjects->speed[x] = Rom((uint16_t)(t + 3));
    gObjects->vdir[x] = 0;
    gObjects->kind[x]++;
}

/* $870D: what a hit does to a block: open it (poses $1B and $3F), leave
 * it (pose $22) or shatter it. */
static void Obj_ResolveBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_RESOLVE_BLOCK, 0, x, 0);
    uint8_t a = gObjects->action[x];
    if (a == OBJ_ACTION_CHEST || a == OBJ_ACTION_LAND_CHEST) {
        Obj_OpenBlock(x);
    } else if (a != OBJ_ACTION_STAY) {
        Obj_Break(x);
    }
}

/* $86F7: a block hit by a counted blow ($0690): uncover its tile first
 * (once per frame, $5B), then resolve the hit. y is the caller's Y. */
void Obj_HitBlock(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_OBJ_HIT_BLOCK, 0, x, y);
    if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
        if (gBlockOpened) {
            return;
        }
        Tile_EditBySlot(x, y);
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
    }
    Obj_ResolveBlock(x);
}

/* $869D: launch block x unless a block was already opened or launched this
 * frame ($5B); a hidden block gets its tile uncovered first. y is the
 * caller's Y, which the tile edit keeps. */
void Obj_KickBlock(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_OBJ_KICK_BLOCK, 0, x, y);
    if (gBlockOpened) {
        return;
    }
    gBlockOpened++;
    if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Tile_EditBySlot(x, y);
    }
    Obj_LaunchBlock(x);
}

/* Kind 8 ($8668): a block. Off screen it stays only in poses $1D and $1E.
 * Scrooge's cane launches it (one block a frame, $5B) or, in pose $3F,
 * opens it; a counted blow ($0690) opens or breaks it. y is the kind the
 * dispatch leaves in Y. */
void Obj_Block(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_KIND_BLOCK, 0x22, x, y);
    if (!OnScreen(x) && gObjects->action[x] != 0x1D && gObjects->action[x] != 0x1E) {
        gObjects->state[x] = 0;
        return;
    }
    if (Obj_CaneReaches(x)) {
        if (gObjects->action[x] == OBJ_ACTION_LAND_CHEST) {
            Obj_ResolveBlock(x);
            return;
        }
        Obj_KickBlock(x, gObjects->hitbox[x] & 0x7F);
        return;
    }
    if (gObjects->unk_0690[x] == 0) {
        return;
    }
    Obj_HitBlock(x, y);
}

/* $88BC: fall with probe $2D; the hit is also kept in $11. */
static uint8_t Obj_ThrownFall(uint8_t x)
{
    Decomp_Enter(DT_CODE_THROWN_FALL, 0, x, 0);
    x = Obj_Fall(x, 0x2D, 1);
    gContactDepth = gProbeHits;
    return x;
}

/* $88C8: walk in the slot's direction with probes $29 or $2B. */
static uint8_t Obj_ThrownWalk(uint8_t x)
{
    Decomp_Enter(DT_CODE_THROWN_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        return Obj_WalkRight(x, 0x29, 1);
    }
    return Obj_WalkLeft(x, 0x2B, 1);
}

/* Kind 9 ($8722): a launched block, by step ($0700 bits 0-3) through the
 * table at $88FC/$8905 (steps 0-8; the words past it point into RAM). It rises then falls (vdir), moves, and breaks
 * against a wall or the floor or on the first object it hits; some steps
 * become a pickup or a block again. */
void Obj_Thrown(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_THROWN, 0x22, x, gObjects->kind[x]);
    uint8_t step = Step(x);
    gScratch(0) = Rom((uint16_t)(DT_CODE_THROWN_STEP_LO + step));
    gScratch(1) = Rom((uint16_t)(DT_CODE_THROWN_STEP_HI + step));
    uint16_t at = (uint16_t)(gScratch(1) << 8 | gScratch(0));
    switch (at) {
        case 0x8735:
            if (gObjects->vdir[x] == 0) {
                x = Obj_Rise(x, 0x2F, 1);
                if (gObjects->vy[x] & 0x80) {
                    Obj_SetVelocityY(x, 0);
                    gObjects->vdir[x]++;
                }
            } else {
                x = Obj_ThrownFall(x);
            }
            x = Obj_ThrownWalk(x);
            if ((gProbeHits | gContactDepth) & 1) {
                Obj_Break(x);
            } else if (Obj_HitOthers(x)) {
                Obj_Shatter(x);
            }
            return;
        case 0x876D:
            x = Obj_ThrownFall(x);
            if (!(gProbeHits & 1)) {
                gObjects->vdir[x] = 1;
            } else {
                if (gObjects->vdir[x] != 0) {
                    Obj_OpenBlock(x);
                    return;
                }
                x = Obj_ThrownWalk(x);
                if (gProbeHits & 1) {
                    Obj_OpenBlock(x);
                    return;
                }
            }
            (void)Obj_HitOthers(x);
            return;
        case 0x8792:
            x = Obj_ClimbUp(x, 0x2F, 1);
            if (gProbeHits & 1) {
                Obj_Break(x);
                return;
            }
            x = Obj_ThrownWalk(x);
            if (gProbeHits & 1) {
                Obj_Break(x);
                return;
            }
            if (!Obj_HitOthers(x)) {
                return;
            }
            {
                uint8_t o = gContactDepth;
                if (gObjects->kind[o] == OBJ_KIND_BLOCK && gObjects->action[o] == OBJ_ACTION_CHEST) {
                    Obj_LaunchBlock(o);
                    Obj_Break(x);
                } else {
                    Obj_Shatter(x);
                }
            }
            return;
        case 0x87D0:
            gObjects->action[x] = 0x20;
            x = Obj_ThrownFall(x);
            if (!(gProbeHits & 1)) {
                gObjects->vdir[x] = 1;
            } else {
                if (gObjects->vdir[x] != 0) {
                    Obj_Break(x);
                    return;
                }
                x = Obj_ThrownWalk(x);
                if (gProbeHits & 1) {
                    Obj_Break(x);
                    return;
                }
            }
            if (Obj_HitOthers(x)) {
                Obj_Shatter(x);
            }
            return;
        case 0x87FF:
            x = Obj_ThrownFall(x);
            if (!(gProbeHits & 1)) {
                gObjects->vdir[x] = 1;
            } else if (gObjects->vdir[x] == 0) {
                x = Obj_ThrownWalk(x);
            }
            if (gObjects->vdir[x] == 0 && Obj_HitOthers(x)) {
                gObjects->action[x] = OBJ_ACTION_STAY;
                uint8_t o = gContactDepth;
                if (gObjects->kind[o] == OBJ_KIND_BLOCK) {
                    Obj_HitBlock(o, o);
                    return;
                }
            }
            if (gProbeHits & 1) {
                gObjects->kind[x] = OBJ_KIND_BLOCK;
            }
            return;
    }
}

/* ---- adapters ---- */

static void Run_Block(Regs *r)
{
    Obj_Block(r->x, r->y);
}

static void Run_Thrown(Regs *r)
{
    Obj_Thrown(r->x);
}

static void Run_KickBlock(Regs *r)
{
    Obj_KickBlock(r->x, r->y);
}

static void Run_HitOthers(Regs *r)
{
    SetCarry(r, !Obj_HitOthers(r->x));
}

static void Run_LaunchBlock(Regs *r)
{
    Obj_LaunchBlock(r->x);
}

static void Run_ResolveBlock(Regs *r)
{
    Obj_ResolveBlock(r->x);
}

static void Run_Break(Regs *r)
{
    Obj_Break(r->x);
}

static void Run_Shatter(Regs *r)
{
    Obj_Shatter(r->x);
}

static void Run_OpenBlock(Regs *r)
{
    Obj_OpenBlock(r->x);
}

static void Run_BecomePickupNow(Regs *r)
{
    Obj_BecomePickupNow(r->x);
}

static void Run_HitBlock(Regs *r)
{
    Obj_HitBlock(r->x, r->y);
}

static void Run_ThrownFall(Regs *r)
{
    r->x = Obj_ThrownFall(r->x);
}

static void Run_ThrownWalk(Regs *r)
{
    r->x = Obj_ThrownWalk(r->x);
}

const DecompRoutine gObjBlockRoutines[] = {
    { DT_CODE_KIND_BLOCK, NULL, "Obj_Block", Run_Block, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_THROWN, NULL, "Obj_Thrown", Run_Thrown, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_HIT_OTHERS, NULL, "Obj_HitOthers", Run_HitOthers, OUT_C, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_LAUNCH_BLOCK, NULL, "Obj_LaunchBlock", Run_LaunchBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_KICK_BLOCK, NULL, "Obj_KickBlock", Run_KickBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_RESOLVE_BLOCK, NULL, "Obj_ResolveBlock", Run_ResolveBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BREAK, NULL, "Obj_Break", Run_Break, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SHATTER, NULL, "Obj_Shatter", Run_Shatter, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_OPEN_BLOCK, NULL, "Obj_OpenBlock", Run_OpenBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BECOME_PICKUP, NULL, "Obj_BecomePickupNow", Run_BecomePickupNow, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_HIT_BLOCK, NULL, "Obj_HitBlock", Run_HitBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_THROWN_FALL, NULL, "Obj_ThrownFall", Run_ThrownFall, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_THROWN_WALK, NULL, "Obj_ThrownWalk", Run_ThrownWalk, OUT_X, .bank = DT_BANK_OBJECTS },
};
const int gObjBlockRoutineCount = sizeof gObjBlockRoutines / sizeof gObjBlockRoutines[0];
