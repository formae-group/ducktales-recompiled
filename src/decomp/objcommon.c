/*
 * objcommon.c - object routines of the fixed bank shared by every object
 * kind, decompiled (docs/decompilation.md "Objects").
 *
 * The movers (Obj_WalkRight/Left, Obj_Fall, Obj_Rise) move a slot with the
 * motion primitives, test the background probes $40.. against the collision
 * map through the probe $D120 and its snaps (collision.c), and push the slot out
 * of the block objects (kind 8) it overlaps. A slot moved off the world
 * (past page $FF or below page 0, or past the bottom of the screen while
 * falling) is despawned. Obj_TouchScrooge is the contact test between a slot
 * and Scrooge that starts his knock-back.
 *
 * Hitbox entries ($07D0 bits 0-6) index two ROM tables of half-heights and
 * half-widths, read at run time. The scratch bytes $00-$04 are left as the
 * original leaves them.
 */
#include "decomp/collision.h"
#include "decomp/decomp.h"
#include "decomp/motion.h"
#include "decomp/objcommon.h"

#include "ducktales/game.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define P_ PLAYER_SLOT

#define OBJ_KIND_BLOCK     0x08
#define OBJ_KIND_09        0x09
#define OBJ_KIND_24        0x24
#define HITBOX_INDEX       0x7F
#define HITBOX_HARMFUL     0x70
#define DESPAWN_HELD_RESET 0x3C

#define gProbeHits    RAM_U8(DT_ADDR_PROBE_HITS)
#define gContactDepth RAM_U8(DT_ADDR_CONTACT_DEPTH)
#define gContactSlot  RAM_U8(DT_ADDR_CONTACT_SLOT)
#define gProbeFirst   RAM_U8(DT_ADDR_PROBE_FIRST)
#define gProbeCount   RAM_U8(DT_ADDR_PROBE_COUNT)
#define gHeldSlot     RAM_U8(DT_ADDR_HELD_SLOT)
#define gScratch(i)   RAM_U8(DT_ADDR_SCRATCH0 + (i))

static uint8_t HitboxHeight(uint8_t box)
{
    return Game_ReadRom((uint16_t)(DT_CODE_HITBOX_HEIGHT + box));
}

static uint8_t HitboxWidth(uint8_t box)
{
    return Game_ReadRom((uint16_t)(DT_CODE_HITBOX_WIDTH + box));
}

/* $F179: free the slot; a slot that held the marker in $4F (slot | $80)
 * resets it to $3C. */
void Obj_Despawn(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_DESPAWN, 0, slot, 0);
    gObjects->flags[slot] = 0;
    gObjects->state[slot] = 0;
    gObjects->record[slot] = 0xFF;
    if ((slot | 0x80) == gHeldSlot) {
        gHeldSlot = DESPAWN_HELD_RESET;
    }
}

/* $F436: does the slot touch Scrooge? Only slots on screen ($0600 bit 7)
 * can. A crouching Scrooge counts 8 px more apart vertically. A harmful
 * hitbox (bits 4-6) knocks him back (mode 6) unless he is invulnerable, and
 * while the power-up runs it hurts the object instead ($0690) and counts as
 * no contact. Returns true for a contact (the original's carry clear). */
bool Obj_TouchScrooge(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_TOUCH_SCROOGE, 0, slot, 0);
    if (!(gObjects->flags[slot] & 0x80)) {
        return false;
    }
    uint8_t box = gObjects->hitbox[slot] & HITBOX_INDEX;
    uint8_t dy = AbsDiff(gObjects->y[P_], gObjects->y[slot]);
    if (gObjects->action[P_] == PLAYER_ACTION_CROUCH) {
        dy = (uint8_t)(dy + 8);
    }
    if (dy >= HitboxHeight(box)) {
        return false;
    }
    if (AbsDiff16(Obj_GetWorldX(P_), Obj_GetWorldX(slot)) > HitboxWidth(box)) {
        return false;
    }
    if (!(gObjects->hitbox[slot] & HITBOX_HARMFUL)) {
        return true;
    }
    if (gPlayerPower) {
        gObjects->unk_0690[slot]++;
        return false;
    }
    if (gPlayerInvulnerable) {
        return false;
    }
    gPlayerMode = PLAYER_MODE_KNOCKBACK;
    return true;
}

/* The block object (kind 8, live and on screen) that a slot on screen
 * overlaps, searched from slot 15 down; $00-$03 keep the slot, its Y and
 * world X. */
static bool Obj_FindBlock(uint8_t slot, bool horizontal)
{
    if (!(gObjects->flags[slot] & 0x80)) {
        return false;
    }
    gScratch(0) = slot;
    gScratch(1) = gObjects->y[slot];
    gScratch(2) = gObjects->worldX[slot];
    gScratch(3) = gObjects->worldPage[slot];
    uint16_t wx = (uint16_t)(gScratch(3) << 8 | gScratch(2));
    for (uint8_t x = 0x0F; x != 0; x--) {
        if (x == slot || !(gObjects->state[x] & OBJ_STATUS_LIVE) || !(gObjects->flags[x] & 0x80)) {
            continue;
        }
        if (gObjects->kind[x] != OBJ_KIND_BLOCK) {
            continue;
        }
        uint8_t box = gObjects->hitbox[x] & HITBOX_INDEX;
        uint8_t dy = AbsDiff(gScratch(1), gObjects->y[x]);
        uint8_t dx = AbsDiff16(wx, Obj_GetWorldX(x));
        if (horizontal) {
            if (dy >= HitboxHeight(box)) {
                continue;
            }
            gScratch(4) = (uint8_t)(HitboxWidth(box) + 1);
            if (dx >= gScratch(4)) {
                continue;
            }
            gScratch(4) = dx;
            gContactDepth = (uint8_t)(HitboxWidth(box) - dx);
            gContactSlot = x;
        } else {
            if (dx >= HitboxWidth(box)) {
                continue;
            }
            gScratch(4) = (uint8_t)(HitboxHeight(box) + 1);
            if (dy >= gScratch(4)) {
                continue;
            }
            gScratch(4) = dy;
            gContactDepth = (uint8_t)(HitboxHeight(box) - dy);
        }
        gProbeHits |= 1;
        return true;
    }
    return false;
}

/* $F332: the block a slot overlaps after a horizontal move: depth in $11,
 * the block's slot in $12 (0 when none). */
bool Obj_TouchBlockH(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_TOUCH_BLOCK_H, 0, slot, 0);
    gContactSlot = 0;
    return Obj_FindBlock(slot, true);
}

/* $F3B7: the same after a vertical move (depth in $11; $12 is left). */
bool Obj_TouchBlockV(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_TOUCH_BLOCK_V, 0, slot, 0);
    return Obj_FindBlock(slot, false);
}

static bool PushedByBlocks(uint8_t x)
{
    return x != 0 && gObjects->kind[x] != OBJ_KIND_09 && Obj_TouchBlockH(x);
}

/* $F0F8: move right by the slot's speed, test probes first..first+count
 * against the background (snapping back on a hit) and against blocks; face
 * right. Returns the X the probes left. */
uint8_t Obj_WalkRight(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_MOVE_WITH_WALLS, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    uint8_t page = gObjects->worldPage[slot];
    Obj_MoveRight(slot);
    if (page == 0xFF && gObjects->worldPage[slot] == 0) {
        Obj_Despawn(slot);
        return slot;
    }
    Bg_Probe(slot);
    if (gProbeHits & 1) {
        Bg_SnapRight(slot);
    }
    uint8_t x = slot;
    gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    if (gObjects->kind[x] == OBJ_KIND_24 && x != 0) {
        return x;
    }
    if (PushedByBlocks(x)) {
        Obj_SetWorldX(x, (uint16_t)(Obj_GetWorldX(x) - gContactDepth));
    }
    return x;
}

/* $F13B: the same moving left, facing left. */
uint8_t Obj_WalkLeft(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_OBJ_WALK_LEFT, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    uint8_t page = gObjects->worldPage[slot];
    Obj_MoveLeft(slot);
    if (page == 0 && gObjects->worldPage[slot] == 0xFF) {
        Obj_Despawn(slot);
        return slot;
    }
    Bg_Probe(slot);
    if (gProbeHits & 1) {
        Bg_SnapLeft(slot);
    }
    uint8_t x = slot;
    gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
    if (PushedByBlocks(x)) {
        Obj_SetWorldX(x, (uint16_t)(Obj_GetWorldX(x) + gContactDepth));
    }
    return x;
}

/* $F298: Y -= velocity, then the background probes. True when the move
 * did not borrow (the carry the original keeps across the probe). */
static bool Obj_MoveYProbe(uint8_t slot)
{
    uint16_t y = (uint16_t)(gObjects->y[slot] << 8 | gObjects->yFrac[slot]);
    bool carry = y >= Obj_GetVelocityY(slot);
    Obj_ApplyVelocityY(slot);
    Bg_Probe(slot);
    return carry;
}

static bool LandsOnBlocks(uint8_t x)
{
    return gObjects->hitbox[x] != 0 && gObjects->kind[x] != OBJ_KIND_09 && Obj_TouchBlockV(x);
}

/* $F1BE: fall: gravity, Y move and probes; a slot whose move did not
 * borrow (it left the screen at the bottom) is despawned. Landing on the
 * background or on a block stops the vertical velocity. */
uint8_t Obj_Fall(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_OBJ_FALL, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    Obj_ApplyGravity(slot);
    bool off = Obj_MoveYProbe(slot);
    uint8_t x = slot;
    if (off) {
        Obj_Despawn(x);
        return x;
    }
    if (gProbeHits & 1) {
        Bg_SnapFeet(x);
    } else {
        if (!LandsOnBlocks(x)) {
            return x;
        }
        gObjects->y[x] -= gContactDepth;
    }
    Obj_SetVelocityY(x, 0);
    return x;
}

/* $F1F9: rise: Y move and probes, then gravity. A head bump on the
 * background or on a block sets the velocity to -1 px. */
uint8_t Obj_Rise(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_OBJ_RISE, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    (void)Obj_MoveYProbe(slot);
    uint8_t x = slot;
    Obj_ApplyGravity(x);
    if (gProbeHits & 1) {
        Bg_SnapHead(x);
    } else {
        if (!LandsOnBlocks(x)) {
            return x;
        }
        gObjects->y[x] = (uint8_t)(gObjects->y[x] + gContactDepth + 1);
    }
    Obj_SetVelocityY(x, 0xFF00);
    return x;
}

/* $F192: a climbing step down: Y += the vertical velocity, then the probes
 * first..first+count. A slot whose Y carries out (past the bottom of the
 * screen) is despawned; a hit snaps its feet onto the tile. */
uint8_t Obj_ClimbDown(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_CLIMB_DOWN, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    bool off = (uint32_t)(gObjects->y[slot] << 8 | gObjects->yFrac[slot]) + Obj_GetVelocityY(slot) > 0xFFFF;
    Obj_AddVelocityY(slot);
    if (off) {
        Obj_Despawn(slot);
        return slot;
    }
    Bg_Probe(slot);
    if (gProbeHits & 1) {
        Bg_SnapFeet(slot);
    }
    return slot;
}

/* $F1A8: a climbing step up: Y -= the vertical velocity, then the probes.
 * A slot whose Y borrows (past the top) is despawned; a hit pushes it below
 * the tile its head hit. */
uint8_t Obj_ClimbUp(uint8_t slot, uint8_t first, uint8_t count)
{
    Decomp_Enter(DT_CODE_CLIMB_UP, first, slot, count);
    gProbeFirst = first;
    gProbeCount = count;
    bool off = (uint16_t)(gObjects->y[slot] << 8 | gObjects->yFrac[slot]) < Obj_GetVelocityY(slot);
    Obj_SubVelocityY(slot);
    if (off) {
        Obj_Despawn(slot);
        return slot;
    }
    Bg_Probe(slot);
    if (gProbeHits & 1) {
        Bg_SnapHead(slot);
    }
    return slot;
}

/* $F6BD: direction towards Scrooge: 1 when he is right of the slot, 2 when
 * he is at it or left of it. */
void Obj_FaceScrooge(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_FACE_SCROOGE, 0, slot, 0);
    gObjects->dir[slot] = Obj_GetWorldX(slot) >= Obj_GetWorldX(P_) ? OBJ_DIR_LEFT : OBJ_DIR_RIGHT;
}

/* $F7DA / $F7CC: horizontal (world X, low byte) and vertical distance
 * between Scrooge and the slot. */
uint8_t Obj_DistanceX(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_DISTANCE_X, 0, slot, 0);
    return AbsDiff16(Obj_GetWorldX(P_), Obj_GetWorldX(slot));
}

uint8_t Obj_DistanceY(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_DISTANCE_Y, 0, slot, 0);
    return AbsDiff(gObjects->y[P_], gObjects->y[slot]);
}

/* $F7F0: set the slot's land record as consumed in $0120-$013F, so the
 * spawner does not bring it back; the bit is left in $10. */
void Obj_MarkConsumed(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_MARK_CONSUMED, 0, slot, 0);
    uint8_t record = gObjects->record[slot];
    gProbeHits = (uint8_t)(1u << (record & 7));
    RAM_U8(DT_ADDR_CONSUMED + (record >> 3)) |= gProbeHits;
}

/* $F822: on Difficult, actions $25 and $26 become $28 and $24. */
void Obj_DifficultAction(uint8_t slot)
{
    Decomp_Enter(DT_CODE_OBJ_DIFFICULT_ACTION, 0, slot, 0);
    if (gDifficulty != DIFFICULTY_DIFFICULT) {
        return;
    }
    if (gObjects->action[slot] == 0x25) {
        gObjects->action[slot] = 0x28;
    } else if (gObjects->action[slot] == 0x26) {
        gObjects->action[slot] = 0x24;
    }
}

/* $F312: horizontal speed from the speed table by index. */
void Obj_SetSpeedIndex(uint8_t slot, uint8_t index)
{
    Decomp_Enter(DT_CODE_OBJ_SET_SPEED, index, slot, 0);
    uint8_t i = (uint8_t)(index << 1);
    gObjects->speedFrac[slot] = Game_ReadRom((uint16_t)(DT_CODE_SPEED_TABLE + i));
    gObjects->speed[slot] = Game_ReadRom((uint16_t)(DT_CODE_SPEED_TABLE + i + 1));
}

/* $F7BE: the highest free slot (status bit 7 clear) of 15..1; false when
 * every one is taken (*slot is then 0). */
bool Obj_FindFreeSlot(uint8_t *slot)
{
    Decomp_Enter(DT_CODE_OBJ_FIND_FREE, 0, 0, 0);
    for (uint8_t x = 0x0F; x != 0; x--) {
        if (!(gObjects->state[x] & OBJ_STATUS_LIVE)) {
            *slot = x;
            return true;
        }
    }
    *slot = 0;
    return false;
}

/* ---- site adapters ---- */

static void SetCarry(Regs *r, bool carry)
{
    r->p = (uint8_t)(carry ? r->p | P_C : r->p & ~P_C);
}

static void Run_TouchScrooge(Regs *r)
{
    uint8_t slot = r->x;
    bool on = gObjects->flags[slot] & 0x80;
    SetCarry(r, !Obj_TouchScrooge(slot));
    if (on) {
        r->y = gObjects->hitbox[slot] & HITBOX_INDEX;
    }
}

static void Run_WalkRight(Regs *r)
{
    r->x = Obj_WalkRight(r->x, r->a, r->y);
}

static void Run_WalkLeft(Regs *r)
{
    r->x = Obj_WalkLeft(r->x, r->a, r->y);
}

static void Run_Fall(Regs *r)
{
    r->x = Obj_Fall(r->x, r->a, r->y);
}

static void Run_ClimbDown(Regs *r)
{
    r->x = Obj_ClimbDown(r->x, r->a, r->y);
}

static void Run_ClimbUp(Regs *r)
{
    r->x = Obj_ClimbUp(r->x, r->a, r->y);
}

static void Run_Rise(Regs *r)
{
    r->x = Obj_Rise(r->x, r->a, r->y);
}

static void Run_Despawn(Regs *r)
{
    Obj_Despawn(r->x);
}

static void Run_TouchBlockH(Regs *r)
{
    SetCarry(r, !Obj_TouchBlockH(r->x));
}

static void Run_TouchBlockV(Regs *r)
{
    SetCarry(r, !Obj_TouchBlockV(r->x));
}

static void Run_FaceScrooge(Regs *r)
{
    uint8_t x = r->x;
    Obj_FaceScrooge(x);
    bool borrow = gObjects->worldX[x] < gObjects->worldX[P_];
    r->a = (uint8_t)(gObjects->worldPage[x] - gObjects->worldPage[P_] - borrow);
    if (gObjects->dir[x] == OBJ_DIR_LEFT) {
        r->a = OBJ_DIR_LEFT;
    }
}

static void Run_DistanceX(Regs *r)
{
    r->a = Obj_DistanceX(r->x);
}

static void Run_DistanceY(Regs *r)
{
    r->a = Obj_DistanceY(r->x);
}

static void Run_MarkConsumed(Regs *r)
{
    Obj_MarkConsumed(r->x);
    r->y = (uint8_t)(gObjects->record[r->x] >> 3);
    r->a = RAM_U8(DT_ADDR_CONSUMED + r->y);
}

static void Run_DifficultAction(Regs *r)
{
    Obj_DifficultAction(r->x);
}

static void Run_SetSpeedIndex(Regs *r)
{
    Obj_SetSpeedIndex(r->x, r->a);
    r->y = (uint8_t)(r->a << 1);
    r->a = gObjects->speed[r->x];
}

static void Run_FindFreeSlot(Regs *r)
{
    uint8_t slot;
    SetCarry(r, !Obj_FindFreeSlot(&slot));
    r->x = slot;
}

const DecompRoutine gObjCommonRoutines[] = {
    { DT_CODE_OBJ_TOUCH_SCROOGE, "ducktales.decomp.F436", "Obj_TouchScrooge", Run_TouchScrooge, OUT_X | OUT_C },
    { DT_CODE_MOVE_WITH_WALLS, "ducktales.decomp.F0F8", "Obj_WalkRight", Run_WalkRight, OUT_X },
    { DT_CODE_OBJ_WALK_LEFT, "ducktales.decomp.F13B", "Obj_WalkLeft", Run_WalkLeft, OUT_X },
    { DT_CODE_OBJ_FALL, "ducktales.decomp.F1BE", "Obj_Fall", Run_Fall, OUT_X },
    { DT_CODE_OBJ_RISE, "ducktales.decomp.F1F9", "Obj_Rise", Run_Rise, OUT_X },
    { DT_CODE_CLIMB_DOWN, "ducktales.decomp.F192", "Obj_ClimbDown", Run_ClimbDown, OUT_X },
    { DT_CODE_CLIMB_UP, "ducktales.decomp.F1A8", "Obj_ClimbUp", Run_ClimbUp, OUT_X },
    { DT_CODE_OBJ_DESPAWN, NULL, "Obj_Despawn", Run_Despawn, 0 },
    { DT_CODE_OBJ_TOUCH_BLOCK_H, NULL, "Obj_TouchBlockH", Run_TouchBlockH, OUT_C },
    { DT_CODE_OBJ_TOUCH_BLOCK_V, NULL, "Obj_TouchBlockV", Run_TouchBlockV, OUT_C },
    { DT_CODE_OBJ_FACE_SCROOGE, "ducktales.decomp.F6BD", "Obj_FaceScrooge", Run_FaceScrooge, OUT_A },
    { DT_CODE_OBJ_DISTANCE_X, "ducktales.decomp.F7DA", "Obj_DistanceX", Run_DistanceX, OUT_A },
    { DT_CODE_OBJ_DISTANCE_Y, "ducktales.decomp.F7CC", "Obj_DistanceY", Run_DistanceY, OUT_A },
    { DT_CODE_OBJ_MARK_CONSUMED, "ducktales.decomp.F7F0", "Obj_MarkConsumed", Run_MarkConsumed, OUT_A | OUT_Y },
    { DT_CODE_OBJ_DIFFICULT_ACTION, NULL, "Obj_DifficultAction", Run_DifficultAction, 0 },
    { DT_CODE_OBJ_SET_SPEED, "ducktales.decomp.F312", "Obj_SetSpeedIndex", Run_SetSpeedIndex, OUT_A | OUT_Y },
    { DT_CODE_OBJ_FIND_FREE, "ducktales.decomp.F7BE", "Obj_FindFreeSlot", Run_FindFreeSlot, OUT_X | OUT_C },
};
const int gObjCommonRoutineCount = sizeof gObjCommonRoutines / sizeof gObjCommonRoutines[0];
