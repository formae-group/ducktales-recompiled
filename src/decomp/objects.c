/*
 * objects.c - the object system of bank 6, decompiled (docs/decompilation.md
 * "Objects").
 *
 * Obj_UpdateAll runs once per frame from the frame loop ($DB78, bank 6
 * mapped): it loads the objects' gravity and runs the handler of every live
 * slot 1-15 (status bit 7), chosen by its kind through the handler table at
 * $802C/$806F. The slot cursor lives in RAM ($4A) because the original keeps
 * it there across the handler call. Kinds whose handler is decompiled run as
 * C; the others run the original handler through Decomp_CallP.
 *
 * Decompiled kinds: the Amazon's enemies (1-5, 7, $0A, $0C, $0D) and its
 * swinging hold ($0E), the Himalayas' dangler (6), Transylvania's swooper
 * ($11), blocks (8) and launched blocks (9), beaten enemies ($0B), the lift
 * ($0F), pickups ($13) with what taking each item does, hidden items
 * ($14), chests ($15), doors ($2F) and talking characters ($35). Inner
 * routines are plain C calls; only the dispatch, the first handlers and
 * the shared movers have hook sites (NESRecomp registers at most 128).
 * Not decompiled yet and run through Decomp_Call: the tile edit $F506, the
 * background probe $D120 with its snaps (in objcommon.c) and the climbing
 * step $F1A8.
 */
#include "decomp/decomp.h"
#include "decomp/motion.h"
#include "decomp/objcommon.h"
#include "decomp/sound.h"

#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define P_ PLAYER_SLOT

#define OBJ_GRAVITY 0x40

#define SFX_DIAMOND  0x15
#define SFX_BREAK    0x16
#define SFX_DEFEAT   0x35
#define SFX_TREASURE 0x37
#define SFX_THROW    0x1B
#define SFX_SNAP     0x19
#define SFX_SPIT     0x1A
#define SFX_SWING    0x26
#define SFX_CRAWLER  0x27
#define SFX_WATCHER  0x2A
#define SFX_FLYER    0x2B
#define SFX_SWOOP    0x30

#define gObjCursor        RAM_U8(DT_ADDR_OBJ_CURSOR)
#define gScratch0         RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch1         RAM_U8(DT_ADDR_SCRATCH1)
#define gScratch(i)       RAM_U8(DT_ADDR_SCRATCH0 + (i))
#define gProbeHits        RAM_U8(DT_ADDR_PROBE_HITS)
#define gContactDepth     RAM_U8(DT_ADDR_CONTACT_DEPTH)
#define gBlockOpened      RAM_U8(DT_ADDR_BLOCK_OPENED)
#define gMoneyDigit(i)    RAM_U8(DT_ADDR_MONEY_DIGITS + (i))
#define gMoneyChanged     RAM_U8(DT_ADDR_MONEY_CHANGED)
#define gExtraDotTaken    RAM_U8(DT_ADDR_EXTRA_DOT_TAKEN)
#define gPogoCeiling      RAM_U8(DT_ADDR_POGO_CEILING)
#define gSceneStep        RAM_U8(DT_ADDR_SCENE_STEP)
#define gMessageSlot      RAM_U8(DT_ADDR_MESSAGE_SLOT)
#define gMessagesShown(i) RAM_U8(DT_ADDR_MESSAGES_SHOWN + (i))
#define gLaunchpadRides   RAM_U8(DT_ADDR_LAUNCHPAD_RIDES)
#define gRandom(i)        RAM_U8(DT_ADDR_RANDOM + (i))
#define gPaletteUpload    RAM_U8(DT_ADDR_PALETTE_UPLOAD)
#define gHeldSlot         RAM_U8(DT_ADDR_HELD_SLOT)

/* ---- shared pieces of the handlers ---- */

static uint8_t Rom(uint16_t addr) { return Game_ReadRom(addr); }
static bool OnScreen(uint8_t x) { return gObjects->flags[x] & OBJ_FLAG_ON_SCREEN; }
static uint8_t Step(uint8_t x) { return gObjects->state[x] & OBJ_STATE_STEP; }
static void ResetAnim(uint8_t x)
{
    gObjects->animTimer[x] = 0;
    gObjects->animFrame[x] = 0;
}

/* $F270 / $F284: Y += or -= the vertical velocity, without gravity. */
static void Obj_MoveDown(uint8_t x)
{
    uint8_t p = 0;
    gObjects->yFrac[x] = Alu_Adc(gObjects->yFrac[x], gObjects->vyFrac[x], &p);
    gObjects->y[x] = Alu_Adc(gObjects->y[x], gObjects->vy[x], &p);
}
static void Obj_MoveUp(uint8_t x)
{
    uint8_t p = P_C;
    gObjects->yFrac[x] = Alu_Sbc(gObjects->yFrac[x], gObjects->vyFrac[x], &p);
    gObjects->y[x] = Alu_Sbc(gObjects->y[x], gObjects->vy[x], &p);
}

/* $F2A1 with the carry it leaves: true when Y did not borrow. */
static bool Obj_ApplyVelocityYCarry(uint8_t x)
{
    bool carry = (uint16_t)(gObjects->y[x] << 8 | gObjects->yFrac[x]) >= Obj_GetVelocityY(x);
    Obj_ApplyVelocityY(x);
    return carry;
}
/* $F234 / $F252 with the carry they leave: true when the move left the
 * world (page $FF to 0 moving right; for the left move the original's carry
 * is the opposite). */
static bool Obj_MoveRightOff(uint8_t x)
{
    uint8_t page = gObjects->worldPage[x];
    Obj_MoveRight(x);
    return page == 0xFF && gObjects->worldPage[x] == 0;
}
static bool Obj_MoveLeftOff(uint8_t x)
{
    uint8_t page = gObjects->worldPage[x];
    Obj_MoveLeft(x);
    return page == 0 && gObjects->worldPage[x] == 0xFF;
}

/* The original's tile edit at the slot ($F506), not decompiled yet. It
 * leaves the caller's Y in $13 when it spawns nothing, so Y is passed as the
 * original has it. */
static uint8_t Obj_EditTile(uint8_t x, uint8_t a, uint8_t y) { return Decomp_Call(DT_CODE_OBJ_EDIT_TILE, a, x, y).x; }

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
static bool Obj_HitOthers(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_HIT_OTHERS, 0, x, 0);
    if (!OnScreen(x)) return false;
    gScratch(0) = x;
    gScratch(1) = gObjects->y[x];
    gScratch(2) = gObjects->worldX[x];
    gScratch(3) = gObjects->worldPage[x];
    uint8_t box = gObjects->hitbox[x] & 0x7F;
    uint16_t wx = (uint16_t)(gScratch(3) << 8 | gScratch(2));
    for (uint8_t o = 0x0F; o != 0; o--) {
        if (o == x || !OnScreen(o)) continue;
        if (AbsDiff(gScratch(1), gObjects->y[o]) >= Rom((uint16_t)(DT_CODE_HIT_HEIGHT + box))) continue;
        if (AbsDiff16(wx, Obj_GetWorldX(o)) > Rom((uint16_t)(DT_CODE_HIT_WIDTH + box))) continue;
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
static void Obj_Break(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_BREAK, 0, x, 0);
    gObjects->action[x] = OBJ_ACTION_BREAK;
    Obj_Shatter(x);
}

static void Obj_Pickup(uint8_t x);

/* $8A32: become a pickup (kind $13) that jumps out with 4 px/frame, and run
 * the pickup's handler now; the item is in $06B0. */
static void Obj_BecomePickupNow(uint8_t x)
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
    if (gObjects->action[x] == OBJ_ACTION_LAND_CHEST)
        item = Rom((uint16_t)(DT_CODE_ITEM_BY_LAND + gLand * 3 + gObjects->timer[x]));
    if (item == 0) item = Rom((uint16_t)(DT_CODE_ITEM_BY_INDEX + gObjects->timer[x]));
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
    if (a == OBJ_ACTION_CHEST || a == OBJ_ACTION_LAND_CHEST) Obj_OpenBlock(x);
    else if (a != OBJ_ACTION_STAY) Obj_Break(x);
}

/* $86F7: a block hit by a counted blow ($0690): uncover its tile first
 * (once per frame, $5B), then resolve the hit. y is the caller's Y. */
static void Obj_HitBlock(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_OBJ_HIT_BLOCK, 0, x, y);
    if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
        if (gBlockOpened) return;
        x = Obj_EditTile(x, gBlockOpened, y);
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
    }
    Obj_ResolveBlock(x);
}

/* Kind 8 ($8668): a block. Off screen it stays only in poses $1D and $1E.
 * Scrooge's cane launches it (one block a frame, $5B) or, in pose $3F,
 * opens it; a counted blow ($0690) opens or breaks it. y is the kind the
 * dispatch leaves in Y. */
static void Obj_Block(uint8_t x, uint8_t y)
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
        if (gBlockOpened) return;
        gBlockOpened++;
        if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
            gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
            x = Obj_EditTile(x, gObjects->state[x], gObjects->hitbox[x] & 0x7F);
        }
        Obj_LaunchBlock(x);
        return;
    }
    if (gObjects->unk_0690[x] == 0) return;
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
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) return Obj_WalkRight(x, 0x29, 1);
    return Obj_WalkLeft(x, 0x2B, 1);
}

/* Kind 9 ($8722): a launched block, by step ($0700 bits 0-3) through the
 * table at $88FC/$8905. It rises then falls (vdir), moves, and breaks
 * against a wall or the floor or on the first object it hits; some steps
 * become a pickup or a block again. */
static void Obj_Thrown(uint8_t x)
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
        if ((gProbeHits | gContactDepth) & 1) Obj_Break(x);
        else if (Obj_HitOthers(x)) Obj_Shatter(x);
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
        x = Decomp_Call(DT_CODE_CLIMB_UP, 0x2F, x, 1).x;
        if (gProbeHits & 1) {
            Obj_Break(x);
            return;
        }
        x = Obj_ThrownWalk(x);
        if (gProbeHits & 1) {
            Obj_Break(x);
            return;
        }
        if (!Obj_HitOthers(x)) return;
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
        if (Obj_HitOthers(x)) Obj_Shatter(x);
        return;
    case 0x87FF:
        x = Obj_ThrownFall(x);
        if (!(gProbeHits & 1)) gObjects->vdir[x] = 1;
        else if (gObjects->vdir[x] == 0) x = Obj_ThrownWalk(x);
        if (gObjects->vdir[x] == 0 && Obj_HitOthers(x)) {
            gObjects->action[x] = OBJ_ACTION_STAY;
            uint8_t o = gContactDepth;
            if (gObjects->kind[o] == OBJ_KIND_BLOCK) {
                Obj_HitBlock(o, o);
                return;
            }
        }
        if (gProbeHits & 1) gObjects->kind[x] = OBJ_KIND_BLOCK;
        return;
    default:
        Decomp_Call(at, gScratch(1), x, step);
        return;
    }
}

/* $F83F: money is capped at 9,999,990 (the four high digits set to 9). */
static void Money_Max(void)
{
    Decomp_Enter(DT_CODE_MONEY_MAX, 0, 0, 0);
    for (int i = 3; i >= 0; i--) gMoneyDigit(i) = 9;
}

/* $8BBF: add amount to money digit y (0 = most significant) with decimal
 * carries; a blank digit ($24) counts as 0 once something is added. */
static void Money_Add(uint8_t amount, uint8_t y)
{
    Decomp_Enter(DT_CODE_MONEY_ADD, amount, 0, y);
    uint8_t a = amount, c = 0;
    for (;;) {
        a = (uint8_t)(a + gMoneyDigit(y) + c);
        gMoneyDigit(y) = a;
        if (a == DT_MONEY_TILE_BLANK) break;
        if (a > DT_MONEY_TILE_BLANK) gMoneyDigit(y) = a = (uint8_t)(a - DT_MONEY_TILE_BLANK);
        if (a < 10) break;
        gMoneyDigit(y) = (uint8_t)(a - 10);
        a = 0;
        c = 1;
        if (y-- == 0) {
            Money_Max();
            break;
        }
    }
    gMoneyChanged++;
}

/* $F812: every HP dot full again. */
static void Hud_RefillHp(void)
{
    Decomp_Enter(DT_CODE_HUD_REFILL_HP, 0, 0, 0);
    gPlayerEasyHits = 0;
    for (int y = 0x10; y >= 0; y -= 4) HUD_RECORD(y / 4)[2] = 0;
}

/* $8B82: the first lost HP dot becomes full. */
static void Hud_RestoreDot(void)
{
    Decomp_Enter(DT_CODE_HUD_RESTORE_DOT, 0, 0, 0);
    gPlayerEasyHits = 0;
    for (uint8_t y = 0; !(HUD_RECORD(y / 4)[0] & 0x80); y = (uint8_t)(y + 4)) {
        if (HUD_RECORD(y / 4)[2] != 0) {
            HUD_RECORD(y / 4)[2] = 0;
            return;
        }
    }
}

/* $8B38: the extra HP dot (item $2B): one more dot in the HP group when it
 * has fewer than five, then full HP. In the Himalayas ($F6 = 1) it is
 * remembered in $AE. */
static void Pickup_ExtraDot(void)
{
    Decomp_Enter(DT_CODE_PICKUP_EXTRA_DOT, 0, 0, 0);
    if (gLand == 1) gExtraDotTaken = 1;
    uint8_t y = 0x0C;
    if (!(HUD_RECORD(3)[0] & 0x80)) {
        y = 0x10;
        if (!(HUD_RECORD(4)[0] & 0x80)) return;
    }
    HUD_RECORD(y / 4)[0] = 0x20;
    HUD_RECORD(y / 4 + 1)[0] = DT_HUD_RECORD_END_Y;
    Hud_RefillHp();
}

/* $8C13: a land treasure (items $6F and up): 100,000 more money, the
 * treasure bit in $A0, and Scrooge holds it up (mode $10), or for the
 * items $74 and up shows message $0D (mode $0F). */
static void Pickup_Treasure(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKUP_TREASURE, 0, x, 0);
    Sound_Request(SFX_TREASURE, x);
    gPogoCeiling = 0;
    Money_Add(1, 0);
    uint8_t item = gObjects->action[x];
    gTreasures |= Rom((uint16_t)(DT_CODE_BIT_TABLE + (uint8_t)(item - OBJ_ITEM_TREASURE)));
    if (item >= OBJ_ITEM_TREASURE_MESSAGE) {
        gPlayerMode = PLAYER_MODE_MESSAGE;
        gSceneStep = 0x0D;
    } else {
        gPlayerMode = PLAYER_MODE_TREASURE;
    }
}

/* $8B21: Scrooge takes the pickup; the item is its pose. */
static void Pickup_Take(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKUP_TAKE, 0, x, 0);
    gObjects->state[x] = 0;
    uint8_t item = gObjects->action[x];
    if (item == OBJ_ITEM_EXTRA_DOT) {
        Pickup_ExtraDot();
        return;
    }
    if (item == OBJ_ITEM_BIG_DIAMOND) {
        Sound_Request(SFX_DIAMOND, x);
        Money_Add(5, 2);
        return;
    }
    if (item >= OBJ_ITEM_TREASURE) {
        Pickup_Treasure(x);
        return;
    }
    uint8_t i = (uint8_t)(item - OBJ_ITEM_FIRST);
    Sound_Request(Rom((uint16_t)(DT_CODE_ITEM_SOUND + i)), x);
    gScratch(0) = Rom((uint16_t)(DT_CODE_ITEM_TAKE_LO + i));
    gScratch(1) = Rom((uint16_t)(DT_CODE_ITEM_TAKE_HI + i));
    uint16_t at = (uint16_t)(gScratch(1) << 8 | gScratch(0));
    switch (at) {
    case 0x8B71: Money_Add(2, 3); break;
    case 0x8B82: Hud_RestoreDot(); break;
    case 0x8B9C: Hud_RefillHp(); break;
    case 0x8B9F:
        gPlayerInvulnerable = 0;
        gPlayerPower = 1;
        RAM_U8(DT_ADDR_POWER_FRAMES) = 0xE0;
        RAM_U8(DT_ADDR_POWER_FRAMES + 1) = 0x01;
        break;
    case 0x8BB0:
        if (gLives != 9) gLives++;
        break;
    case 0x8BBB: Money_Add(1, 2); break;
    case 0x8BE7:
        gKeyItems |= gLand == 4 ? 4 : 2;
        gSceneStep = gLand == 4 ? 0x10 : 0x0E;
        gPlayerMode = PLAYER_MODE_MESSAGE;
        break;
    default: Decomp_Call(at, gScratch(1), x, i); break;
    }
}

/* Kind $13 ($8A59): a pickup. While in the jump-out pose ($2A) it shows
 * its item when the animation reaches frame 1, timer 5. Step 0 rises, step
 * 1 falls to the floor, step 2 waits; the timed items blink at $78 and
 * vanish at 0. Gone off screen, an item in pose $29 gives its record back
 * to the spawner. */
static void Obj_Pickup(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PICKUP, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        if (gObjects->action[x] == OBJ_ITEM_RESPAWNS) {
            uint8_t record = gObjects->record[x];
            gScratch(0) = (uint8_t)~Rom((uint16_t)(DT_CODE_BIT_TABLE + (record & 7)));
            RAM_U8(DT_ADDR_CONSUMED + (record >> 3)) &= gScratch(0);
        }
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->action[x] == OBJ_ACTION_BREAK) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) return;
        gObjects->action[x] = gObjects->timer[x];
        Obj_DifficultAction(x);
        ResetAnim(x);
    }
    if (Step(x) == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (!(gObjects->vy[x] & 0x80)) return;
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0);
    }
    if (!(gObjects->state[x] & 2)) {
        x = Obj_Fall(x, 0x2D, 1);
        if (gProbeHits & 1) {
            gObjects->state[x]++;
            gObjects->timer[x] = 0xFF;
        }
    } else {
        uint8_t item = gObjects->action[x];
        if (item == OBJ_ITEM_BIG_DIAMOND || item < OBJ_ITEM_RESPAWNS) {
            if (gObjects->timer[x] == 0) {
                gObjects->state[x] = 0;
                return;
            }
            if (gObjects->timer[x] == 0x78) gObjects->flags[x] |= OBJ_FLAG_BLINK;
            gObjects->timer[x]--;
        }
    }
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK) return;
    if (gPlayerMode == PLAYER_MODE_DEATH) {
        gObjects->state[x] = 0;
        return;
    }
    if (Obj_TouchScrooge(x)) Pickup_Take(x);
}

/* $8CE1: the slot becomes a block (kind 8). In a boss fight it gets
 * $06B0 = land + 6, and the Moon's ($F6 = 4) also changes two sprite
 * colours. */
static void Obj_BecomeBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_BECOME_BLOCK, 0, x, 0);
    gObjects->kind[x] = OBJ_KIND_BLOCK;
    gObjects->hitbox[x] = 0x80;
    gObjects->timer[x] = 0;
    gObjects->unk_06C0[x] = 0;
    gObjects->vdir[x] = 0;
    if (!(gBossFight & 0x80)) return;
    gObjects->timer[x] = (uint8_t)(gLand + 6);
    if (gObjects->timer[x] != 0x0A) return;
    RAM_U8(DT_ADDR_PALETTE + 0x1D) = RAM_U8(DT_ADDR_MASTER_PALETTE + 0x1D) = 0x0F;
    RAM_U8(DT_ADDR_PALETTE + 0x1F) = RAM_U8(DT_ADDR_MASTER_PALETTE + 0x1F) = 0x29;
    gPaletteUpload = 0x29;
}

/* $8CAC: the slot shows item: a pickup (kind $13, step 1, falling), or for
 * poses $1B and $22 a block again. */
static void Obj_ShowItem(uint8_t x, uint8_t item)
{
    Decomp_Enter(DT_CODE_OBJ_SHOW_ITEM, item, x, 0);
    gObjects->action[x] = item;
    Obj_DifficultAction(x);
    ResetAnim(x);
    gObjects->hitbox[x] = 0;
    item = gObjects->action[x];
    if (item == OBJ_ACTION_CHEST || item == OBJ_ACTION_STAY) {
        Obj_BecomeBlock(x);
        return;
    }
    gObjects->kind[x] = OBJ_KIND_PICKUP;
    gObjects->state[x] = OBJ_STATUS_LIVE | OBJ_STATUS_WORLD | 1;
    Obj_SetVelocityY(x, 0);
    gObjects->timer[x] = 0;
    gObjects->unk_06C0[x] = 0;
}

/* Kind $14 ($8C45): a hidden item. Touched by Scrooge (when he is not
 * invulnerable) it marks its record consumed and after 30 frames turns into
 * the jump-out pose; at its frame 1, timer 5 it shows its item. */
static void Obj_HiddenItem(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HIDDEN_ITEM, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (Step(x) == 0) {
        if (gPlayerInvulnerable || !Obj_TouchScrooge(x)) return;
        Obj_MarkConsumed(x);
        gObjects->state[x]++;
        gObjects->unk_06C0[x] = 0x1E;
    }
    if (!(gObjects->state[x] & 2)) {
        if (--gObjects->unk_06C0[x] != 0) return;
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = gObjects->action[x];
        gObjects->action[x] = OBJ_ACTION_BREAK;
        gObjects->state[x]++;
        return;
    }
    if (gObjects->action[x] == OBJ_ACTION_BREAK) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) return;
        Obj_ShowItem(x, gObjects->timer[x]);
        return;
    }
    Obj_BecomeBlock(x);
}

/* Kind $15 ($8D19): a chest (hitbox $81). Scrooge's cane opens it: its
 * record is consumed, it moves up by the land's offset, and it shows the
 * land's item, or becomes a pickup at once when the table has none. In the
 * Himalayas ($F6 = 1) a chest with $06B0 >= 2 is a solid block (pose $5B)
 * while Scrooge does not swing at it. */
static void Obj_Chest(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHEST, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    gObjects->hitbox[x] = 0x81;
    if (gObjects->animTimer[P_] == 0 && gObjects->action[P_] == PLAYER_ACTION_CANE && gObjects->animFrame[P_] == 3 &&
        Obj_TouchScrooge(x)) {
        Obj_MarkConsumed(x);
        uint8_t y = (uint8_t)((gLand << 2) + gObjects->timer[x]);
        gObjects->y[x] -= Rom((uint16_t)(DT_CODE_CHEST_RISE + y));
        if (Rom((uint16_t)(DT_CODE_CHEST_ITEM + y)) == 0) {
            gObjects->timer[x] = 0x28;
            Obj_BecomePickupNow(x);
            return;
        }
        gObjects->hitbox[x] = 0;
        ResetAnim(x);
        gObjects->unk_0690[x] = 0;
        gObjects->state[x] &= 0xE0;
        if (y == 6) {
            gObjects->kind[x] = 0x17;
            gObjects->hitbox[x] = 0x10;
            gObjects->action[x] = 0x21;
            return;
        }
        gObjects->action[x] = OBJ_ACTION_BREAK;
        gObjects->kind[x]++;
        return;
    }
    if (gLand != 1 || gObjects->timer[x] < 2) return;
    gObjects->state[x] &= 0xE0;
    if (gObjects->action[P_] == PLAYER_ACTION_CANE) return;
    gObjects->action[x] = 0x5B;
    gObjects->hitbox[x] = 0x82;
    if (AbsDiff(gObjects->y[x], gObjects->y[P_]) >= 0x18) gObjects->hitbox[x]++;
}

/* $89DA: a defeated enemy may drop an item: one time in four by the random
 * bytes ($C1 += $C0 + carry), when its kind has one ($9025) and a slot is
 * free; the item appears where the enemy is. carry is the carry the
 * caller's sound request left. */
static void Obj_Drop(uint8_t x, bool carry)
{
    gScratch(0) = x;
    gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + carry);
    if (gRandom(1) & 3) return;
    uint8_t item = Rom((uint16_t)(DT_CODE_DROP_BY_KIND + gObjects->kind[x]));
    if (item == 0) return;
    gScratch(1) = item;
    uint8_t o;
    if (!Obj_FindFreeSlot(&o)) return;
    Obj_ShowItem(o, gScratch(1));
    uint8_t from = gScratch(0);
    gObjects->y[o] = gObjects->y[from];
    gObjects->worldX[o] = gObjects->worldX[from];
    gObjects->worldPage[o] = gObjects->worldPage[from];
}

/* Kind $0B ($897B): a defeated enemy flies off: up at $02D4, then down
 * (vdir), sideways at $0080 in $07E0's direction, freed when it leaves the
 * screen or the world. */
static void Obj_Defeated(uint8_t x)
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
    if ((gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) gObjects->state[x] = 0;
}

/* $8932: an enemy is beaten: sound $35, maybe a drop, then it flies off
 * away from Scrooge as kind $0B. Off screen it is just freed. */
static void Obj_Defeat(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_DEFEAT, 0, x, 0);
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

/* Kind $0F ($9093): a platform that rises to Y $68 then sinks to $A8 at
 * 1.5 px a frame, carrying Scrooge down 2 px a frame when he stands near
 * it. */
static void Obj_Lift(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LIFT, 0x22, x, gObjects->kind[x]);
    Obj_SetVelocityY(x, 0x0180);
    if (Step(x) == 0) {
        Obj_MoveUp(x);
        if (gObjects->y[x] >= 0x68) return;
        gObjects->state[x]++;
    } else {
        Obj_MoveDown(x);
    }
    if (Obj_DistanceX(x) < 0x1D && Obj_DistanceY(x) < 0x18) gObjects->y[P_] += 2;
    if (gObjects->y[x] >= 0xA8) gObjects->state[x]--;
}

/* Kind $2F ($9065): a door. Touched, it requests warp $06B0 ($7E). Door 0
 * opens only with message bit 3 of $A8, door 4 only with every land's
 * treasure, else it sends Scrooge to Land Select ($A4). */
static void Obj_Door(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DOOR, 0x22, x, gObjects->kind[x]);
    if (!Obj_TouchScrooge(x)) return;
    uint8_t warp = gObjects->timer[x];
    if (warp == 0) {
        if (!(gMessagesShown(0) & 8)) return;
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
static void Obj_Talker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_TALKER, 0x22, x, gObjects->kind[x]);
    if (gPlayerMode == PLAYER_MODE_DEATH || gPlayerMode == PLAYER_MODE_KNOCKBACK) return;
    if (Step(x) != 0 || !Obj_TouchScrooge(x)) return;
    if (gObjects->action[x] == 0x33 && (gLaunchpadRides & Rom((uint16_t)(DT_CODE_LAND_BIT + gLand)))) return;
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

/* ---- enemies ---- */

/* $818A: vertical velocity and speed from the table by index (8.8 each). */
static void Obj_SetMotionIndex(uint8_t x, uint8_t index)
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
static bool Enemy_Beaten(uint8_t x, uint8_t hit_pose)
{
    if (OnScreen(x)) {
        if (gObjects->unk_0690[x] == 0) return false;
        if (hit_pose) gObjects->action[x] = hit_pose;
    }
    gHeldSlot = 0x3C;
    Obj_Defeat(x);
    return true;
}

/* $82F9: walk in the slot's direction with probes $18 or $1B. */
static uint8_t Enemy_Walk(uint8_t x)
{
    Decomp_Enter(DT_CODE_ENEMY_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) return Obj_WalkRight(x, 0x18, 2);
    return Obj_WalkLeft(x, 0x1B, 2);
}

/* Kind 1 ($8225): an enemy that walks towards where Scrooge was at 1 px a
 * frame and jumps ($0400) whenever it lands. Hit: pose $0C, beaten. */
static void Obj_Hopper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HOPPER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0x0C)) return;
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->speedFrac[x] = 0;
        gObjects->state[x]++;
        gObjects->speed[x] = 1;
        Obj_FaceScrooge(x);
    }
    bool fall = true;
    if (gObjects->state[x] & 2) {
        x = Obj_Rise(x, 0x21, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            fall = false;
        } else {
            gObjects->state[x]--;
            Obj_SetVelocityY(x, 0);
        }
    }
    if (!fall || (x = Obj_Fall(x, 0x1E, 2), !(gProbeHits & 1))) ResetAnim(x);
    x = Enemy_Walk(x);
    if (gObjects->state[x] & 2) return;
    if (!(gProbeHits & 1) || Obj_GetVelocityY(x) != 0) return;
    gObjects->state[x]++;
    Obj_SetVelocityY(x, 0x0400);
}

/* Kind 2 ($82AF): an enemy that walks at speed 0 of the table and turns at
 * walls. Hit: pose $0C, beaten. */
static void Obj_Walker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_WALKER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x0C;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->state[x]++;
        Obj_SetSpeedIndex(x, 0);
        Obj_FaceScrooge(x);
    }
    x = Obj_Fall(x, 0x1E, 2);
    if (!(gProbeHits & 1)) ResetAnim(x);
    x = Enemy_Walk(x);
    if (gProbeHits & 1) gObjects->dir[x] ^= 3;
}

/* $80B3: the highest free slot 15..1 (the bank 6 copy, which answers in Y). */
static bool Obj_FindFreeSlotY(uint8_t *slot)
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

/* Kind 3 ($830E): an enemy that walks and hops like kind 1, and within $50
 * px of Scrooge stops (step 3) to throw one kind-4 shot when its animation
 * gets there; after $3C frames it walks on. Hit: beaten. */
static void Obj_Thrower(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_THROWER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) return;
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->state[x]++;
        Obj_SetSpeedIndex(x, 0);
        Obj_FaceScrooge(x);
    }
    if (Step(x) == 3) {
        uint8_t y;
        if (gObjects->unk_06C0[x] == 0 && (gObjects->animFrame[x] & 0x0F) && Obj_FindFreeSlotY(&y)) {
            gObjects->animTimer[y] = 0;
            gObjects->animFrame[y] = 0;
            gObjects->speedFrac[y] = 0;
            gObjects->hitbox[y] = 0;
            gObjects->state[y] = gObjects->state[x];
            gObjects->flags[y] = gObjects->flags[x];
            gObjects->action[y] = 0x17;
            gObjects->kind[y] = OBJ_KIND_SHOT;
            gObjects->speed[y] = 2;
            gObjects->worldX[y] = gObjects->worldX[x];
            gObjects->worldPage[y] = gObjects->worldPage[x];
            gObjects->y[y] = (uint8_t)(gObjects->y[x] - 8);
            gObjects->hitbox[y] = 0x17;
            Sound_Request(SFX_THROW, x);
            gObjects->unk_06C0[x]++;
        }
        if (--gObjects->timer[x] != 0) return;
        ResetAnim(x);
        gObjects->action[x] = 0x14;
        gObjects->state[x] &= 0xF1;
        return;
    }
    bool fall = true;
    if (Step(x) & 2) {
        x = Obj_Rise(x, 0x09, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            fall = false;
        } else {
            gObjects->state[x]--;
            Obj_SetVelocityY(x, 0);
        }
    }
    if (!fall || (x = Obj_Fall(x, 0x06, 2), !(gProbeHits & 1))) {
        gObjects->action[x] = 0x15;
        ResetAnim(x);
    } else {
        gObjects->action[x] = 0x14;
    }
    x = (gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_WalkRight(x, 0x00, 2) : Obj_WalkLeft(x, 0x03, 2);
    if (gObjects->state[x] & 2) return;
    if (Obj_GetVelocityY(x) != 0) return;
    if (gProbeHits & 1) {
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x0400);
        return;
    }
    if (gObjects->unk_06C0[x] != 0 || Obj_DistanceX(x) >= 0x51) return;
    gObjects->animTimer[x] = 0;
    gObjects->animFrame[x] = 0x80;
    gObjects->pose[x] = 1;
    gObjects->action[x] = 0x16;
    gObjects->state[x] = (uint8_t)((gObjects->state[x] & 0xF0) | 3);
    gObjects->timer[x] = 0x3C;
}

/* Kind 4 ($8462): a shot flying in its facing direction; it vanishes
 * against a wall. */
static void Obj_Shot(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SHOT, 0x22, x, gObjects->kind[x]);
    if (gObjects->flags[x] & OBJ_FLAG_FACING_RIGHT) x = Obj_WalkRight(x, 0x24, 0);
    else x = Obj_WalkLeft(x, 0x25, 0);
    (void)Obj_TouchScrooge(x);
    if (!(gProbeHits & 1)) return;
    gObjects->flags[x] = 0;
    gObjects->state[x] = 0;
    gObjects->record[x] = 0xFF;
}

/* Kind 5 ($8491): an enemy that stays and faces Scrooge; at the end of
 * each animation (frame $0C) it picks pose $0D or $0E by the random bytes
 * ($0E with sound $2A). Hit: pose $0F, beaten. */
static void Obj_Watcher(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_WATCHER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x0F;
        Obj_Defeat(x);
        return;
    }
    if (gObjects->animFrame[x] == 0x0C) {
        gRandom(2) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
        gObjects->action[x] = (uint8_t)((gRandom(2) & 1) + 0x0D);
        if (gObjects->action[x] == 0x0E) Sound_Request(SFX_WATCHER, x);
        ResetAnim(x);
    }
    if (Obj_GetWorldX(x) < Obj_GetWorldX(P_)) gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    else gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
}

/* Kind 7 ($8567): a plant that snaps at Scrooge within $0B px. Caught
 * (within 7 px), he is held hurt (mode 5, $7D = $44) at its mouth; when
 * $7D reaches 8 it spits him out, and the plant reopens when $7D is 0.
 * Touched from further away, it knocks him back. Hit: beaten. */
static void Obj_Snapper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SNAPPER, 0x22, x, gObjects->kind[x]);
    if (gPlayerMode == PLAYER_MODE_DEATH) return;
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
            if (gPlayerInvulnerable != 8) return;
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
    if (a == 2 && gObjects->animTimer[x] == 0) Sound_Request(SFX_SNAP, x);
    gObjects->hitbox[x] = Rom((uint16_t)(DT_CODE_SNAPPER_HITBOX + gObjects->animFrame[x]));
    if (!Obj_TouchScrooge(x)) return;
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
static void Obj_Dropper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DROPPER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (Step(x) == 0) {
        if (gBlockOpened) return;
        uint8_t d = Obj_DistanceX(x);
        uint8_t y = gObjects->action[x];
        if (d >= (y == 0x87 ? 0x20 : 0x04)) return;
        Obj_SetVelocityY(x, 0);
        gObjects->state[x]++;
        if (gObjects->action[x] != 0x87) {
            gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
            gObjects->flags[x] = gObjects->state[x] & 0xF0;
            Obj_SetVelocityY(x, 0);
            x = Obj_EditTile(x, 0, y);
            if (gObjects->y[x] < gObjects->y[P_]) gObjects->hitbox[x] = 0x10;
        }
    }
    if (!Obj_TouchScrooge(x)) {
        x = Obj_Fall(x, 0x2D, 0);
        if (!(gProbeHits & 1)) return;
    }
    Obj_Break(x);
}

/* Kind $0C ($90EA): an enemy that waits until Scrooge is 16 px past it to
 * the right, then walks at $0180 and turns at walls. */
static void Obj_Crawler(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CRAWLER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        uint16_t d = (uint16_t)(Obj_GetWorldX(P_) - Obj_GetWorldX(x));
        gScratch(0) = (uint8_t)d;
        if (Obj_GetWorldX(P_) < Obj_GetWorldX(x) || gScratch(0) < 0x10) return;
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0180);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, 0x31, 2);
    if (!(gProbeHits & 1)) return;
    x = (gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_WalkRight(x, 0x34, 2) : Obj_WalkLeft(x, 0x37, 2);
    if (gProbeHits & 1) gObjects->dir[x] ^= 3;
    if (gObjects->animTimer[x] == 0) Sound_Request(SFX_CRAWLER, x);
}

/* Kind $0D ($916C): a flyer that heads towards where Scrooge was and
 * weaves through 14 motion entries ($9215), 5 frames each, without
 * touching the background. Hit or off screen: beaten. */
static void Obj_Flyer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_FLYER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) return;
    if (Step(x) == 0) {
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_FLYER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        if (++gObjects->unk_06C0[x] == 0x0E) gObjects->unk_06C0[x] = 0;
        gObjects->timer[x] = 5;
    }
    gObjects->timer[x]--;
    Obj_MoveDown(x);
    if (!(gObjects->dir[x] & OBJ_DIR_LEFT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) {
        Obj_Despawn(x);
        return;
    }
    if (gObjects->animTimer[x] == 0) Sound_Request(SFX_FLYER, x);
    (void)Obj_TouchScrooge(x);
}

/* Kind $0E ($9225): a swinging hold (a vine or rope end) moving through 36
 * motion entries ($92ED, bit 7: 15 frames instead of 5) with signed speed.
 * Scrooge touching it with Up held grabs it (mode 9, climbing pose) and
 * moves with it. */
static void Obj_Swing(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SWING, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->yFrac[x] = 0;
        gObjects->xFrac[x] = 0;
        gObjects->state[x]++;
    }
    if (gObjects->animTimer[x] == 0) Sound_Request(SFX_SWING, x);
    if (gObjects->timer[x] == 0) {
        uint8_t e = Rom((uint16_t)(DT_CODE_SWING_PATH + gObjects->unk_06C0[x]));
        Obj_SetMotionIndex(x, e & 0x1F);
        gObjects->timer[x] = Rom((uint16_t)(DT_CODE_SWING_FRAMES + (e >> 7)));
        if (++gObjects->unk_06C0[x] == 0x24) gObjects->unk_06C0[x] = 0;
    }
    gObjects->timer[x]--;
    Obj_MoveDown(x);
    gScratch(0) = (gObjects->speed[x] & 0x80) ? 0xFF : 0x00;
    uint8_t p = 0;
    gObjects->xFrac[x] = Alu_Adc(gObjects->xFrac[x], gObjects->speedFrac[x], &p);
    gObjects->worldX[x] = Alu_Adc(gObjects->worldX[x], gObjects->speed[x], &p);
    gObjects->worldPage[x] = Alu_Adc(gObjects->worldPage[x], gScratch(0), &p);
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK || gPlayerMode == PLAYER_MODE_DEATH) return;
    if (gPlayerMode != PLAYER_MODE_9) {
        if (!Obj_TouchScrooge(x) || !(gPadHeld & DT_PAD_UP)) return;
        gPlayerMode = PLAYER_MODE_9;
        gObjects->action[P_] = PLAYER_ACTION_CLIMB;
        gObjects->animTimer[P_] = 0;
        gObjects->animFrame[P_] = 0;
    }
    gObjects->y[P_] = gObjects->y[x];
    gObjects->worldX[P_] = gObjects->worldX[x];
    gObjects->worldPage[P_] = gObjects->worldPage[x];
}

/* Kind 6 ($84E5): an enemy that climbs up at 2 px a frame to the ceiling,
 * waits there while Scrooge is within $31 px, then drops at 3 px a frame
 * to Y $B0 or to Scrooge's height and climbs back ($96 frames later). Hit:
 * pose $11, beaten. */
static void Obj_Dangler(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DANGLER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x11;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->state[x]++;
    }
    if (!(gObjects->state[x] & 2)) {
        Obj_SetVelocityY(x, 0x0200);
        x = Decomp_Call(DT_CODE_CLIMB_UP, 0x27, x, 0).x;
        if (!(gProbeHits & 1)) return;
        if (Obj_DistanceX(x) >= 0x32) gObjects->timer[x] = 0;
        else if (gObjects->timer[x] == 0) gObjects->state[x]++;
        else gObjects->timer[x]--;
        return;
    }
    Obj_SetVelocityY(x, 0x0300);
    Obj_MoveDown(x);
    if (gObjects->y[x] < 0xB0 && Obj_DistanceY(x) >= 0x18) return;
    gObjects->state[x]--;
    gObjects->timer[x] = 0x96;
}

/* Kind $11 ($9430): a flyer that starts within $5F px of Scrooge (pose $85
 * at once), drops at 3 px a frame to 16 px below his height, then weaves
 * through 14 motion entries ($9544; every other pass the second half slows
 * to 8 frames a step), rising again whenever it is $20 px above him. In
 * pose $85 a hit beats it. */
static void Obj_Swooper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_SWOOPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->action[x] == 0x85 && gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
        gHeldSlot = 0x3C;
        return;
    }
    if (Step(x) == 0) {
        if (gObjects->action[x] != 0x85) {
            if (Obj_DistanceX(x) >= 0x60) return;
            Sound_Request(SFX_SWOOP, x);
        }
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        Obj_SetVelocityY(x, 0x0300);
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    }
    (void)Obj_TouchScrooge(x);
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->y[x] >= gObjects->y[P_] && (uint8_t)(gObjects->y[x] - gObjects->y[P_]) >= 0x10) gObjects->state[x]++;
        Obj_MoveDown(x);
        return;
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_SWOOPER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        gObjects->timer[x] = (gObjects->unk_06C0[x] >= 7 && (gObjects->unk_06D0[x] & 1)) ? 8 : 3;
        if (++gObjects->unk_06C0[x] == 0x0E) {
            gObjects->unk_06C0[x] = 0;
            gObjects->unk_06D0[x]++;
        }
    }
    bool tick = true;
    if (!(gObjects->state[x] & 1)) {
        if (gObjects->y[P_] >= gObjects->y[x] && (uint8_t)(gObjects->y[P_] - gObjects->y[x]) >= 0x20) gObjects->state[x]++;
        else tick = false;
    }
    if (tick) gObjects->timer[x]--;
    Obj_MoveDown(x);
    if (gObjects->dir[x] & OBJ_DIR_LEFT) Obj_MoveLeft(x);
    else Obj_MoveRight(x);
}

/* ---- the dispatch ---- */

/* One live slot: the dispatch leaves the handler's address in $00/$01 (the
 * JMP ($0000) vector), A = $22 (the low byte of the return it pushed),
 * X = slot and Y = kind. The flags are those of the loop: the caller's carry
 * for slot 1, the CPX of the loop for the others, and the V the previous
 * handler left. *r gets the registers an original handler returns with. */
static void Obj_RunHandler(uint8_t slot, Regs *r)
{
    uint8_t kind = gObjects->kind[slot];
    gScratch0 = Rom((uint16_t)(DT_CODE_OBJ_HANDLER_LO + kind));
    gScratch1 = Rom((uint16_t)(DT_CODE_OBJ_HANDLER_HI + kind));
    r->a = 0x22;
    r->y = kind;
    r->p = Flags_NZ(r->p, r->a);
    switch (kind) {
    case OBJ_KIND_NONE: break;
    case OBJ_KIND_HOPPER: Obj_Hopper(slot); break;
    case OBJ_KIND_WALKER: Obj_Walker(slot); break;
    case OBJ_KIND_THROWER: Obj_Thrower(slot); break;
    case OBJ_KIND_SHOT: Obj_Shot(slot); break;
    case OBJ_KIND_WATCHER: Obj_Watcher(slot); break;
    case OBJ_KIND_DANGLER: Obj_Dangler(slot); break;
    case OBJ_KIND_SNAPPER: Obj_Snapper(slot); break;
    case OBJ_KIND_BLOCK: Obj_Block(slot, kind); break;
    case OBJ_KIND_THROWN: Obj_Thrown(slot); break;
    case OBJ_KIND_DEFEATED: Obj_Defeated(slot); break;
    case OBJ_KIND_DROPPER: Obj_Dropper(slot); break;
    case OBJ_KIND_CRAWLER: Obj_Crawler(slot); break;
    case OBJ_KIND_FLYER: Obj_Flyer(slot); break;
    case OBJ_KIND_SWING: Obj_Swing(slot); break;
    case OBJ_KIND_LIFT: Obj_Lift(slot); break;
    case OBJ_KIND_SWOOPER: Obj_Swooper(slot); break;
    case OBJ_KIND_PICKUP: Obj_Pickup(slot); break;
    case OBJ_KIND_HIDDEN_ITEM: Obj_HiddenItem(slot); break;
    case OBJ_KIND_CHEST: Obj_Chest(slot); break;
    case OBJ_KIND_DOOR: Obj_Door(slot); break;
    case OBJ_KIND_TALKER: Obj_Talker(slot); break;
    default: *r = Decomp_CallP((uint16_t)(gScratch1 << 8 | gScratch0), r->a, slot, r->y, r->p); break;
    }
}

/* Bank 6 $8000: once per frame from the frame loop. Returns with X = 16,
 * which the code after it passes on (a sound request keeps it in $00). */
static void Obj_UpdateAll(Regs *r)
{
    Decomp_Enter(DT_CODE_OBJ_UPDATE, r->a, r->x, r->y);
    gGravity = OBJ_GRAVITY;
    r->a = OBJ_GRAVITY;
    for (gObjCursor = 1; gObjCursor != OBJ_SLOTS; gObjCursor++) {
        r->a = gObjects->state[gObjCursor];
        if (r->a & OBJ_STATUS_LIVE) Obj_RunHandler(gObjCursor, r);
        r->x = (uint8_t)(gObjCursor + 1);
        r->p = Flags_Cmp(r->p, r->x, OBJ_SLOTS);
    }
}

/* ---- adapters ---- */

static void SetCarry(Regs *r, bool carry)
{
    r->p = (uint8_t)(carry ? r->p | P_C : r->p & ~P_C);
}

static void Run_UpdateAll(Regs *r)
{
    Obj_UpdateAll(r);
}

static void Run_Block(Regs *r)
{
    Obj_Block(r->x, r->y);
}

static void Run_Hopper(Regs *r)
{
    Obj_Hopper(r->x);
}

static void Run_Walker(Regs *r)
{
    Obj_Walker(r->x);
}

static void Run_Thrower(Regs *r)
{
    Obj_Thrower(r->x);
}

static void Run_Shot(Regs *r)
{
    Obj_Shot(r->x);
}

static void Run_Watcher(Regs *r)
{
    Obj_Watcher(r->x);
}

static void Run_Dangler(Regs *r)
{
    Obj_Dangler(r->x);
}

static void Run_Snapper(Regs *r)
{
    Obj_Snapper(r->x);
}

static void Run_Thrown(Regs *r)
{
    Obj_Thrown(r->x);
}

static void Run_Defeated(Regs *r)
{
    Obj_Defeated(r->x);
}

static void Run_Dropper(Regs *r)
{
    Obj_Dropper(r->x);
}

static void Run_Crawler(Regs *r)
{
    Obj_Crawler(r->x);
}

static void Run_Flyer(Regs *r)
{
    Obj_Flyer(r->x);
}

static void Run_Swing(Regs *r)
{
    Obj_Swing(r->x);
}

static void Run_Lift(Regs *r)
{
    Obj_Lift(r->x);
}

static void Run_Swooper(Regs *r)
{
    Obj_Swooper(r->x);
}

static void Run_Pickup(Regs *r)
{
    Obj_Pickup(r->x);
}

static void Run_HiddenItem(Regs *r)
{
    Obj_HiddenItem(r->x);
}

static void Run_Chest(Regs *r)
{
    Obj_Chest(r->x);
}

static void Run_Door(Regs *r)
{
    Obj_Door(r->x);
}

static void Run_Talker(Regs *r)
{
    Obj_Talker(r->x);
}

static void Run_FindFreeSlotY(Regs *r)
{
    uint8_t slot;
    SetCarry(r, !Obj_FindFreeSlotY(&slot));
    r->y = slot;
}

static void Run_HitOthers(Regs *r)
{
    SetCarry(r, !Obj_HitOthers(r->x));
}

static void Run_SetMotionIndex(Regs *r)
{
    Obj_SetMotionIndex(r->x, r->a);
}

static void Run_EnemyWalk(Regs *r)
{
    r->x = Enemy_Walk(r->x);
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

static void Run_Defeat(Regs *r)
{
    Obj_Defeat(r->x);
}

static void Run_OpenBlock(Regs *r)
{
    Obj_OpenBlock(r->x);
}

static void Run_BecomePickupNow(Regs *r)
{
    Obj_BecomePickupNow(r->x);
}

static void Run_BecomeBlock(Regs *r)
{
    Obj_BecomeBlock(r->x);
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

static void Run_ShowItem(Regs *r)
{
    Obj_ShowItem(r->x, r->a);
}

static void Run_PickupTake(Regs *r)
{
    Pickup_Take(r->x);
}

static void Run_PickupTreasure(Regs *r)
{
    Pickup_Treasure(r->x);
}

static void Run_PickupExtraDot(Regs *r)
{
    Pickup_ExtraDot();
}

static void Run_RestoreDot(Regs *r)
{
    Hud_RestoreDot();
}

static void Run_MoneyAdd(Regs *r)
{
    Money_Add(r->a, r->y);
}

static void Run_RefillHp(Regs *r)
{
    Hud_RefillHp();
    r->a = 0;
    r->y = 0xFC;
}

static void Run_MoneyMax(Regs *r)
{
    Money_Max();
    r->a = 9;
    r->y = 0xFF;
}

const DecompRoutine gObjectRoutines[] = {
    { DT_CODE_OBJ_UPDATE, "ducktales.decomp.6.8000", "Obj_UpdateAll", Run_UpdateAll, OUT_X, DT_OBJ_BANK },
    { DT_CODE_KIND_HOPPER, NULL, "Obj_Hopper", Run_Hopper, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_WALKER, NULL, "Obj_Walker", Run_Walker, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_THROWER, NULL, "Obj_Thrower", Run_Thrower, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_SHOT, NULL, "Obj_Shot", Run_Shot, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_WATCHER, NULL, "Obj_Watcher", Run_Watcher, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_DANGLER, NULL, "Obj_Dangler", Run_Dangler, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_SNAPPER, NULL, "Obj_Snapper", Run_Snapper, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_BLOCK, NULL, "Obj_Block", Run_Block, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_THROWN, NULL, "Obj_Thrown", Run_Thrown, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_DEFEATED, NULL, "Obj_Defeated", Run_Defeated, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_DROPPER, NULL, "Obj_Dropper", Run_Dropper, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_CRAWLER, NULL, "Obj_Crawler", Run_Crawler, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_FLYER, NULL, "Obj_Flyer", Run_Flyer, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_SWING, NULL, "Obj_Swing", Run_Swing, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_LIFT, NULL, "Obj_Lift", Run_Lift, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_SWOOPER, NULL, "Obj_Swooper", Run_Swooper, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_PICKUP, NULL, "Obj_Pickup", Run_Pickup, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_HIDDEN_ITEM, NULL, "Obj_HiddenItem", Run_HiddenItem, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_CHEST, NULL, "Obj_Chest", Run_Chest, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_DOOR, NULL, "Obj_Door", Run_Door, 0, DT_OBJ_BANK },
    { DT_CODE_KIND_TALKER, NULL, "Obj_Talker", Run_Talker, 0 },
    { DT_CODE_OBJ_FIND_FREE_Y, NULL, "Obj_FindFreeSlotY", Run_FindFreeSlotY, OUT_Y | OUT_C, DT_OBJ_BANK },
    { DT_CODE_OBJ_HIT_OTHERS, NULL, "Obj_HitOthers", Run_HitOthers, OUT_C, DT_OBJ_BANK },
    { DT_CODE_OBJ_SET_MOTION, NULL, "Obj_SetMotionIndex", Run_SetMotionIndex, 0, DT_OBJ_BANK },
    { DT_CODE_ENEMY_WALK, NULL, "Enemy_Walk", Run_EnemyWalk, OUT_X, DT_OBJ_BANK },
    { DT_CODE_OBJ_LAUNCH_BLOCK, NULL, "Obj_LaunchBlock", Run_LaunchBlock, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_RESOLVE_BLOCK, NULL, "Obj_ResolveBlock", Run_ResolveBlock, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_BREAK, NULL, "Obj_Break", Run_Break, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_SHATTER, NULL, "Obj_Shatter", Run_Shatter, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_DEFEAT, NULL, "Obj_Defeat", Run_Defeat, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_OPEN_BLOCK, NULL, "Obj_OpenBlock", Run_OpenBlock, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_BECOME_PICKUP, NULL, "Obj_BecomePickupNow", Run_BecomePickupNow, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_BECOME_BLOCK, NULL, "Obj_BecomeBlock", Run_BecomeBlock, 0, DT_OBJ_BANK },
    { DT_CODE_OBJ_HIT_BLOCK, NULL, "Obj_HitBlock", Run_HitBlock, 0, DT_OBJ_BANK },
    { DT_CODE_THROWN_FALL, NULL, "Obj_ThrownFall", Run_ThrownFall, OUT_X, DT_OBJ_BANK },
    { DT_CODE_THROWN_WALK, NULL, "Obj_ThrownWalk", Run_ThrownWalk, OUT_X, DT_OBJ_BANK },
    { DT_CODE_OBJ_SHOW_ITEM, NULL, "Obj_ShowItem", Run_ShowItem, 0, DT_OBJ_BANK },
    { DT_CODE_PICKUP_TAKE, NULL, "Pickup_Take", Run_PickupTake, 0, DT_OBJ_BANK },
    { DT_CODE_PICKUP_TREASURE, NULL, "Pickup_Treasure", Run_PickupTreasure, 0, DT_OBJ_BANK },
    { DT_CODE_PICKUP_EXTRA_DOT, NULL, "Pickup_ExtraDot", Run_PickupExtraDot, 0, DT_OBJ_BANK },
    { DT_CODE_HUD_RESTORE_DOT, NULL, "Hud_RestoreDot", Run_RestoreDot, 0, DT_OBJ_BANK },
    { DT_CODE_MONEY_ADD, NULL, "Money_Add", Run_MoneyAdd, 0, DT_OBJ_BANK },
    { DT_CODE_HUD_REFILL_HP, "ducktales.decomp.F812", "Hud_RefillHp", Run_RefillHp, OUT_A | OUT_Y },
    { DT_CODE_MONEY_MAX, "ducktales.decomp.F83F", "Money_Max", Run_MoneyMax, OUT_A | OUT_Y },
};
const int gObjectRoutineCount = sizeof gObjectRoutines / sizeof gObjectRoutines[0];
