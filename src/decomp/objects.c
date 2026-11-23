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
 * ($14), chests ($15), doors ($2F) and talking characters ($35). Every
 * routine starts with Decomp_Enter and the C calls it as plain C; only
 * those original code also calls have a hook site (the dispatch, the
 * movers and the helpers original handlers call). Not decompiled yet and
 * run through Decomp_Call: the tile edit $F506.
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

#define SFX_DIAMOND      0x15
#define SFX_BREAK        0x16
#define SFX_DEFEAT       0x35
#define SFX_TREASURE     0x37
#define SFX_THROW        0x1B
#define SFX_SNAP         0x19
#define SFX_SPIT         0x1A
#define SFX_SWING        0x26
#define SFX_CRAWLER      0x27
#define SFX_WATCHER      0x2A
#define SFX_FLYER        0x2B
#define SFX_SWOOP        0x30
#define SFX_CHARGE       0x31
#define SFX_BOUNCE       0x1D
#define SFX_LEAP         0x34
#define SFX_HOP          0x24
#define SFX_DWELLER_HIT  0x2C
#define SFX_POUNCER_FLY  0x32
#define SFX_POUNCE       0x1F
#define SFX_POUNCER_LAND 0x20
#define SFX_SPLASH       0x21
#define SFX_ROLL         0x27
#define SFX_SPOUT        0x25
#define SFX_BOSS_HOP     0x22
#define SFX_BOSS_LAND    0x1E
#define SFX_BOSS_TURN    0x23

#define gObjCursor        RAM_U8(DT_ADDR_OBJ_CURSOR)
#define gScratch0         RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch1         RAM_U8(DT_ADDR_SCRATCH1)
#define gScratch(i)       RAM_U8(DT_ADDR_SCRATCH0 + (i))
#define gProbeHits        RAM_U8(DT_ADDR_PROBE_HITS)
#define gContactDepth     RAM_U8(DT_ADDR_CONTACT_DEPTH)
#define gBlockOpened      RAM_U8(DT_ADDR_BLOCK_OPENED)
#define gContactSlot      RAM_U8(DT_ADDR_CONTACT_SLOT)
#define gMoneyDigit(i)    RAM_U8(DT_ADDR_MONEY_DIGITS + (i))
#define gMoneyChanged     RAM_U8(DT_ADDR_HUD_MONEY_DIRTY)
#define gExtraDotTaken    RAM_U8(DT_ADDR_EXTRA_DOT_TAKEN)
#define gPogoCeiling      RAM_U8(DT_ADDR_POGO_CEILING)
#define gSceneStep        RAM_U8(DT_ADDR_SCENE_STEP)
#define gMessageSlot      RAM_U8(DT_ADDR_MESSAGE_SLOT)
#define gMessagesShown(i) RAM_U8(DT_ADDR_MESSAGES_SHOWN + (i))
#define gLaunchpadRides   RAM_U8(DT_ADDR_LAUNCHPAD_RIDES)
#define gRandom(i)        RAM_U8(DT_ADDR_RANDOM + (i))
#define gPaletteUpload    RAM_U8(DT_ADDR_PALETTE_REQUEST)
#define gHeldSlot         RAM_U8(DT_ADDR_HELD_SLOT)
#define gProbeCount       RAM_U8(DT_ADDR_PROBE_COUNT)
#define gPaletteShown(i)  RAM_U8(DT_ADDR_PALETTE_SHOWN + (i))

/* ---- shared pieces of the handlers ---- */

static uint8_t Rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

static bool OnScreen(uint8_t x)
{
    return gObjects->flags[x] & OBJ_FLAG_ON_SCREEN;
}

static uint8_t Step(uint8_t x)
{
    return gObjects->state[x] & OBJ_STATE_STEP;
}

static void ResetAnim(uint8_t x)
{
    gObjects->animTimer[x] = 0;
    gObjects->animFrame[x] = 0;
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
static uint8_t Obj_EditTile(uint8_t x, uint8_t a, uint8_t y)
{
    return Decomp_Call(DT_CODE_OBJ_EDIT_TILE, a, x, y).x;
}

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
static void Obj_HitBlock(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_OBJ_HIT_BLOCK, 0, x, y);
    if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
        if (gBlockOpened) {
            return;
        }
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
        if (gBlockOpened) {
            return;
        }
        gBlockOpened++;
        if (gObjects->state[x] & OBJ_STATUS_HIDDEN) {
            gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
            x = Obj_EditTile(x, gObjects->state[x], gObjects->hitbox[x] & 0x7F);
        }
        Obj_LaunchBlock(x);
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
        default:
            Decomp_Call(at, gScratch(1), x, step);
            return;
    }
}

/* $F83F: money is capped at 9,999,990 (the four high digits set to 9). */
static void Money_Max(void)
{
    Decomp_Enter(DT_CODE_MONEY_MAX, 0, 0, 0);
    for (int i = 3; i >= 0; i--) {
        gMoneyDigit(i) = 9;
    }
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
        if (a == DT_MONEY_TILE_BLANK) {
            break;
        }
        if (a > DT_MONEY_TILE_BLANK) {
            gMoneyDigit(y) = a = (uint8_t)(a - DT_MONEY_TILE_BLANK);
        }
        if (a < 10) {
            break;
        }
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
    Decomp_Enter(DT_CODE_HP_REFILL, 0, 0, 0);
    gPlayerEasyHits = 0;
    for (int y = 0x10; y >= 0; y -= 4) {
        HUD_RECORD(y / 4)[2] = 0;
    }
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
    if (gLand == 1) {
        gExtraDotTaken = 1;
    }
    uint8_t y = 0x0C;
    if (!(HUD_RECORD(3)[0] & 0x80)) {
        y = 0x10;
        if (!(HUD_RECORD(4)[0] & 0x80)) {
            return;
        }
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
        case 0x8B71:
            Money_Add(2, 3);
            break;
        case 0x8B82:
            Hud_RestoreDot();
            break;
        case 0x8B9C:
            Hud_RefillHp();
            break;
        case 0x8B9F:
            gPlayerInvulnerable = 0;
            gPlayerPower = 1;
            RAM_U8(DT_ADDR_POWER_FRAMES) = 0xE0;
            RAM_U8(DT_ADDR_POWER_FRAMES + 1) = 0x01;
            break;
        case 0x8BB0:
            if (gLives != 9) {
                gLives++;
            }
            break;
        case 0x8BBB:
            Money_Add(1, 2);
            break;
        case 0x8BE7:
            gKeyItems |= gLand == 4 ? 4 : 2;
            gSceneStep = gLand == 4 ? 0x10 : 0x0E;
            gPlayerMode = PLAYER_MODE_MESSAGE;
            break;
        default:
            Decomp_Call(at, gScratch(1), x, i);
            break;
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
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
            return;
        }
        gObjects->action[x] = gObjects->timer[x];
        Obj_DifficultAction(x);
        ResetAnim(x);
    }
    if (Step(x) == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (!(gObjects->vy[x] & 0x80)) {
            return;
        }
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
            if (gObjects->timer[x] == 0x78) {
                gObjects->flags[x] |= OBJ_FLAG_BLINK;
            }
            gObjects->timer[x]--;
        }
    }
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK) {
        return;
    }
    if (gPlayerMode == PLAYER_MODE_DEATH) {
        gObjects->state[x] = 0;
        return;
    }
    if (Obj_TouchScrooge(x)) {
        Pickup_Take(x);
    }
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
    if (!(gBossFight & 0x80)) {
        return;
    }
    gObjects->timer[x] = (uint8_t)(gLand + 6);
    if (gObjects->timer[x] != 0x0A) {
        return;
    }
    RAM_U8(DT_ADDR_PALETTE_SHOWN + 0x1D) = RAM_U8(DT_ADDR_PALETTE + 0x1D) = 0x0F;
    RAM_U8(DT_ADDR_PALETTE_SHOWN + 0x1F) = RAM_U8(DT_ADDR_PALETTE + 0x1F) = 0x29;
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
        if (gPlayerInvulnerable || !Obj_TouchScrooge(x)) {
            return;
        }
        Obj_MarkConsumed(x);
        gObjects->state[x]++;
        gObjects->unk_06C0[x] = 0x1E;
    }
    if (!(gObjects->state[x] & 2)) {
        if (--gObjects->unk_06C0[x] != 0) {
            return;
        }
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = gObjects->action[x];
        gObjects->action[x] = OBJ_ACTION_BREAK;
        gObjects->state[x]++;
        return;
    }
    if (gObjects->action[x] == OBJ_ACTION_BREAK) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
            return;
        }
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
    if (gLand != 1 || gObjects->timer[x] < 2) {
        return;
    }
    gObjects->state[x] &= 0xE0;
    if (gObjects->action[P_] == PLAYER_ACTION_CANE) {
        return;
    }
    gObjects->action[x] = 0x5B;
    gObjects->hitbox[x] = 0x82;
    if (AbsDiff(gObjects->y[x], gObjects->y[P_]) >= 0x18) {
        gObjects->hitbox[x]++;
    }
}

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
    if ((gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) {
        gObjects->state[x] = 0;
    }
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
static void Obj_Door(uint8_t x)
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
static void Obj_Talker(uint8_t x)
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
static uint8_t Enemy_Walk(uint8_t x)
{
    Decomp_Enter(DT_CODE_ENEMY_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        return Obj_WalkRight(x, 0x18, 2);
    }
    return Obj_WalkLeft(x, 0x1B, 2);
}

/* Kind 1 ($8225): an enemy that walks towards where Scrooge was at 1 px a
 * frame and jumps ($0400) whenever it lands. Hit: pose $0C, beaten. */
static void Obj_Hopper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HOPPER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0x0C)) {
        return;
    }
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
    if (!fall || (x = Obj_Fall(x, 0x1E, 2), !(gProbeHits & 1))) {
        ResetAnim(x);
    }
    x = Enemy_Walk(x);
    if (gObjects->state[x] & 2) {
        return;
    }
    if (!(gProbeHits & 1) || Obj_GetVelocityY(x) != 0) {
        return;
    }
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
    if (!(gProbeHits & 1)) {
        ResetAnim(x);
    }
    x = Enemy_Walk(x);
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
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
    if (Enemy_Beaten(x, 0)) {
        return;
    }
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
        if (--gObjects->timer[x] != 0) {
            return;
        }
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
    if (gObjects->state[x] & 2) {
        return;
    }
    if (Obj_GetVelocityY(x) != 0) {
        return;
    }
    if (gProbeHits & 1) {
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x0400);
        return;
    }
    if (gObjects->unk_06C0[x] != 0 || Obj_DistanceX(x) >= 0x51) {
        return;
    }
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
    if (gObjects->flags[x] & OBJ_FLAG_FACING_RIGHT) {
        x = Obj_WalkRight(x, 0x24, 0);
    } else {
        x = Obj_WalkLeft(x, 0x25, 0);
    }
    (void)Obj_TouchScrooge(x);
    if (!(gProbeHits & 1)) {
        return;
    }
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
        if (gObjects->action[x] == 0x0E) {
            Sound_Request(SFX_WATCHER, x);
        }
        ResetAnim(x);
    }
    if (Obj_GetWorldX(x) < Obj_GetWorldX(P_)) {
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
    } else {
        gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
    }
}

/* Kind 7 ($8567): a plant that snaps at Scrooge within $0B px. Caught
 * (within 7 px), he is held hurt (mode 5, $7D = $44) at its mouth; when
 * $7D reaches 8 it spits him out, and the plant reopens when $7D is 0.
 * Touched from further away, it knocks him back. Hit: beaten. */
static void Obj_Snapper(uint8_t x)
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
static void Obj_Dropper(uint8_t x)
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
            x = Obj_EditTile(x, 0, y);
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

/* Kind $0C ($90EA): an enemy that waits until Scrooge is 16 px past it to
 * the right, then walks at $0180 and turns at walls. */
static void Obj_Crawler(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CRAWLER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        uint16_t d = (uint16_t)(Obj_GetWorldX(P_) - Obj_GetWorldX(x));
        gScratch(0) = (uint8_t)d;
        if (Obj_GetWorldX(P_) < Obj_GetWorldX(x) || gScratch(0) < 0x10) {
            return;
        }
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0180);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, 0x31, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    x = (gObjects->dir[x] & OBJ_DIR_RIGHT) ? Obj_WalkRight(x, 0x34, 2) : Obj_WalkLeft(x, 0x37, 2);
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_CRAWLER, x);
    }
}

/* Kind $0D ($916C): a flyer that heads towards where Scrooge was and
 * weaves through 14 motion entries ($9215), 5 frames each, without
 * touching the background. Hit or off screen: beaten. */
static void Obj_Flyer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_FLYER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        }
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_FLYER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) {
            Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        }
        if (++gObjects->unk_06C0[x] == 0x0E) {
            gObjects->unk_06C0[x] = 0;
        }
        gObjects->timer[x] = 5;
    }
    gObjects->timer[x]--;
    Obj_AddVelocityY(x);
    if (!(gObjects->dir[x] & OBJ_DIR_LEFT) ? Obj_MoveRightOff(x) : Obj_MoveLeftOff(x)) {
        Obj_Despawn(x);
        return;
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_FLYER, x);
    }
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
        x = Obj_ClimbUp(x, 0x27, 0);
        if (!(gProbeHits & 1)) {
            return;
        }
        if (Obj_DistanceX(x) >= 0x32) {
            gObjects->timer[x] = 0;
        } else if (gObjects->timer[x] == 0) {
            gObjects->state[x]++;
        } else {
            gObjects->timer[x]--;
        }
        return;
    }
    Obj_SetVelocityY(x, 0x0300);
    Obj_AddVelocityY(x);
    if (gObjects->y[x] < 0xB0 && Obj_DistanceY(x) >= 0x18) {
        return;
    }
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
            if (Obj_DistanceX(x) >= 0x60) {
                return;
            }
            Sound_Request(SFX_SWOOP, x);
        }
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        Obj_SetVelocityY(x, 0x0300);
        Obj_FaceScrooge(x);
        if (!(gObjects->dir[x] & OBJ_DIR_LEFT)) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        }
    }
    (void)Obj_TouchScrooge(x);
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->y[x] >= gObjects->y[P_] && (uint8_t)(gObjects->y[x] - gObjects->y[P_]) >= 0x10) {
            gObjects->state[x]++;
        }
        Obj_AddVelocityY(x);
        return;
    }
    if (gObjects->timer[x] == 0) {
        Obj_SetMotionIndex(x, Rom((uint16_t)(DT_CODE_SWOOPER_PATH + gObjects->unk_06C0[x])));
        if (gObjects->speed[x] & 0x80) {
            Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
        }
        gObjects->timer[x] = (gObjects->unk_06C0[x] >= 7 && (gObjects->unk_06D0[x] & 1)) ? 8 : 3;
        if (++gObjects->unk_06C0[x] == 0x0E) {
            gObjects->unk_06C0[x] = 0;
            gObjects->unk_06D0[x]++;
        }
    }
    bool tick = true;
    if (!(gObjects->state[x] & 1)) {
        if (gObjects->y[P_] >= gObjects->y[x] && (uint8_t)(gObjects->y[P_] - gObjects->y[x]) >= 0x20) {
            gObjects->state[x]++;
        } else {
            tick = false;
        }
    }
    if (tick) {
        gObjects->timer[x]--;
    }
    Obj_AddVelocityY(x);
    if (gObjects->dir[x] & OBJ_DIR_LEFT) {
        Obj_MoveLeft(x);
    } else {
        Obj_MoveRight(x);
    }
}

/* Kind $18 ($96D5): an enemy that walks a ledge at $0080 a frame and turns
 * back at its edges (no floor under probe $2D or $2E) and at walls. A hit
 * beats it. */
static void Obj_Patroller(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PATROLLER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0080);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    x = Obj_Fall(x, (uint8_t)(0x2D + (gObjects->dir[x] & OBJ_DIR_RIGHT)), 0);
    if (!(gProbeHits & 1)) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x29, 0);
    } else {
        x = Obj_WalkLeft(x, 0x2B, 0);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* Kind $10 ($9313): an enemy that faces Scrooge, waits 120 frames, then
 * charges at 2 px a frame. Against a wall or a block it turns, knocks the
 * block ($86F7) and bounces up ($06C0 counts the bounce, negative once it
 * falls); landing from the bounce leaves it dazed (pose $42) for 30 frames
 * before it charges again. A hit beats it. */
static void Obj_Charger(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHARGER, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0);
        gObjects->unk_06C0[x] = 0;
        Obj_SetSpeed(x, 0x0200);
        gObjects->timer[x] = 0x78;
        Obj_FaceScrooge(x);
    }
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->timer[x] != 0) {
            gObjects->timer[x]--;
            gObjects->animTimer[x] = 0;
            return;
        }
        if (gObjects->animFrame[x] != 3) {
            return;
        }
        ResetAnim(x);
        gObjects->action[x] = 0x2D;
        gObjects->hitbox[x] |= 0x80;
        gObjects->state[x]++;
    }
    uint8_t bounce = gObjects->unk_06C0[x];
    if (bounce != 0) {
        if (gObjects->animFrame[x] == 3) {
            gObjects->animTimer[x] = 0;
        }
        if (!(bounce & 0x80)) {
            x = Obj_Rise(x, 9, 2);
            if (!(gObjects->vy[x] & 0x80)) {
                return;
            }
            gObjects->unk_06C0[x] = gObjects->vy[x];
            Obj_SetVelocityY(x, 0);
            return;
        }
    }
    x = Obj_Fall(x, 6, 2);
    if (!(gProbeHits & 1)) {
        return;
    }
    if (gObjects->unk_06C0[x] & 0x80) {
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->action[x] = 0x42;
        gObjects->timer[x] = 0x1E;
        gObjects->state[x]--;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0, 2);
    } else {
        x = Obj_WalkLeft(x, 3, 2);
    }
    if (gObjects->animTimer[x] == 0) {
        Sound_Request(SFX_CHARGE, x);
    }
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (!(gProbeHits & 1)) {
        return;
    }
    uint8_t block = gContactSlot;
    if (gObjects->kind[block] == OBJ_KIND_BLOCK && (gObjects->state[block] & OBJ_STATUS_HIDDEN) && !gBlockOpened) {
        /* The tile edit needs the Y the walls move left, which the C does
         * not model: the original runs this call. */
        Decomp_RunOriginal();
        return;
    }
    gObjects->dir[x] ^= 3;
    gObjects->action[x] = 0x2E;
    ResetAnim(x);
    gObjects->hitbox[x] &= 0x7F;
    if (gObjects->kind[block] == OBJ_KIND_BLOCK) {
        Obj_HitBlock(block, 0);
    }
    Sound_Request(SFX_BOUNCE, x);
    gObjects->unk_06C0[x]++;
    Obj_SetVelocityY(x, 0x02D4);
}

/* Kind $20 ($9E51): an enemy that bobs up and down at 1 px a frame, first
 * for 22 frames, then turning every 44; it faces Scrooge as it appears. A
 * hit beats it. */
static void Obj_Bobber(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BOBBER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0x0100);
        gObjects->timer[x] = 0x16;
        gObjects->state[x]++;
        Obj_FaceScrooge(x);
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        } else {
            gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
        }
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        Obj_SubVelocityY(x);
    } else {
        Obj_AddVelocityY(x);
    }
    if (--gObjects->timer[x] == 0) {
        gObjects->dir[x] ^= 3;
        gObjects->timer[x] = 0x2C;
    }
}

/* $9C0A: a puff of dust (action $57, kind 0, freed when its animation
 * ends) 4 px below the slot, in the highest free slot; none when every slot
 * is taken. */
static void Obj_SpawnDust(uint8_t x)
{
    Decomp_Enter(DT_CODE_LEAPER_DUST, 0, x, 0);
    uint8_t y;
    if (!Obj_FindFreeSlotY(&y)) {
        return;
    }
    gObjects->state[y] = 0xA8;
    gObjects->flags[y] = 0xA8;
    ResetAnim(y);
    gObjects->kind[y] = OBJ_KIND_NONE;
    gObjects->hitbox[y] = 0;
    gObjects->action[y] = 0x57;
    gObjects->y[y] = (uint8_t)(gObjects->y[x] + 4);
    gObjects->worldX[y] = gObjects->worldX[x];
    gObjects->worldPage[y] = gObjects->worldPage[x];
}

/* Kind $1C ($9B10): an enemy that wakes when Scrooge is within 80 px, walks
 * a ledge at $0133 a frame, and after 80 frames, at a wall or with nothing
 * under it turns and leaps (4 px a frame up) with a puff of dust; landing
 * it walks again. A hit beats it (pose $56). */
static void Obj_Leaper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LEAPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        gObjects->action[x] = 0x56;
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x50) {
            return;
        }
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0133);
        gObjects->timer[x] = 0x50;
        gObjects->state[x] = (uint8_t)((gObjects->state[x] & (uint8_t)~OBJ_STATUS_HIDDEN) | 1);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->state[x] & 2) {
        if (!(gObjects->state[x] & 1)) {
            x = Obj_Rise(x, 0x2F, 1);
            if (!(gObjects->vy[x] & 0x80)) {
                return;
            }
            gObjects->animFrame[x]++;
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
            return;
        }
        x = Obj_Fall(x, 0x2D, 1);
        if (!(gProbeHits & 1)) {
            return;
        }
        ResetAnim(x);
        gObjects->action[x] = 0x55;
        gObjects->y[x]++;
        gObjects->state[x] -= 2;
        return;
    }
    x = Obj_Fall(x, (uint8_t)(0x2D + (gObjects->dir[x] & OBJ_DIR_RIGHT)), 0);
    if (gProbeHits != 0) {
        gObjects->y[x] += 2;
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            x = Obj_WalkRight(x, 0x29, 0);
        } else {
            x = Obj_WalkLeft(x, 0x2B, 0);
        }
        if (!(gProbeHits & 1) && --gObjects->timer[x] != 0) {
            return;
        }
    }
    gObjects->dir[x] ^= 3;
    gObjects->timer[x] = 0x50;
    gObjects->state[x]++;
    Obj_SetVelocityY(x, 0x0400);
    gObjects->action[x] = 0x56;
    gObjects->animFrame[x] = 0;
    gObjects->animTimer[x] = 0x80;
    Sound_Request(SFX_LEAP, x);
    Obj_SpawnDust(x);
}

/* Kind $27 ($A5BE): a shot that flies up and sideways by its velocity and
 * speed, harming Scrooge on contact; it is freed when it leaves the top of
 * the screen. */
static void Obj_RisingShot(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_RISING_SHOT, 0x22, x, gObjects->kind[x]);
    (void)Obj_TouchScrooge(x);
    bool off = (uint16_t)(gObjects->y[x] << 8 | gObjects->yFrac[x]) < Obj_GetVelocityY(x);
    Obj_SubVelocityY(x);
    if (off) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        Obj_MoveRight(x);
    } else {
        Obj_MoveLeft(x);
    }
}

/* Kind $2B ($AAC8): something that drops by its velocity (probe $2D),
 * harming Scrooge on contact, and is freed when it hits the floor. */
static void Obj_Dropping(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_DROPPING, 0x22, x, gObjects->kind[x]);
    x = Obj_ClimbDown(x, 0x2D, 0);
    if (gProbeHits & 1) {
        gObjects->state[x] = 0;
        gObjects->flags[x] = 0;
    }
    (void)Obj_TouchScrooge(x);
}

/* $A1D4: the ceiling hopper's walk, in its direction with probes $18 or $1B. */
static uint8_t Obj_CeilingWalk(uint8_t x)
{
    Decomp_Enter(DT_CODE_CEILING_HOPPER_WALK, 0, x, 0);
    if (gObjects->dir[x] & OBJ_DIR_LEFT) {
        return Obj_WalkLeft(x, 0x1B, 2);
    }
    return Obj_WalkRight(x, 0x18, 2);
}

static void Obj_CeilingJump(uint8_t x)
{
    gObjects->vdir[x] = 8;
    Obj_SetVelocityY(x, 0x02D4);
    gObjects->speedFrac[x] = 0;
}

static void Obj_CeilingClimb(uint8_t x)
{
    gObjects->state[x]++;
    gObjects->vdir[x] = 0;
    ResetAnim(x);
    gObjects->action[x] = 0x4C;
    Obj_SetVelocityY(x, 0x01CC);
    Obj_SetSpeed(x, 0x01CC);
}

/* Kind $25 ($A065): an enemy that waits until Scrooge is within 64 px,
 * climbs down to the floor and hops towards him (action $4D, sound $24),
 * walking while airborne. When he gets behind it after a landing, or a hop
 * runs into a wall, it climbs back up to the ceiling (action $4C) and walks
 * there. Off screen or hit, it is beaten. */
static void Obj_CeilingHopper(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CEILING_HOPPER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        gObjects->vdir[x] = 0;
        Obj_SetSpeed(x, 0x01CC);
        Obj_FaceScrooge(x);
        gObjects->state[x]++;
    }
    if (Step(x) == 3) {
        if (gObjects->vdir[x] == 0) {
            x = Obj_ClimbUp(x, 0x21, 2);
            if ((gProbeHits & 1) || gObjects->y[x] < 0x88) {
                gObjects->vdir[x]++;
            }
            return;
        }
        x = Obj_CeilingWalk(x);
        if (gProbeHits & 1) {
            (void)Obj_ClimbUp(x, 0x21, 2);
        }
        return;
    }
    if (!(Step(x) & 2)) {
        if (Obj_DistanceX(x) >= 0x40) {
            (void)Obj_CeilingWalk(x);
            return;
        }
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0x01CC);
    }
    if (gObjects->vdir[x] == 0) {
        x = Obj_ClimbDown(x, 0x1E, 2);
        if (!(gProbeHits & 1)) {
            return;
        }
        gObjects->animTimer[x] = 0;
        gObjects->animFrame[x] = 0;
        gObjects->action[x] = 0x4D;
        Sound_Request(SFX_HOP, x);
        Obj_CeilingJump(x);
        return;
    }
    if (gObjects->vdir[x] & 8) {
        if (gObjects->action[x] == 0x4D) {
            if (gObjects->animFrame[x] != 2) {
                return;
            }
            gObjects->animTimer[x] = 0x80;
        }
        x = Obj_CeilingWalk(x);
        uint8_t hits = gProbeHits;
        x = Obj_Rise(x, 0x21, 2);
        if (!(gObjects->vy[x] & 0x80)) {
            return;
        }
        if (hits & 1) {
            Obj_CeilingClimb(x);
            return;
        }
        gObjects->vdir[x] = 4;
        Obj_SetVelocityY(x, 0);
        return;
    }
    x = Obj_Fall(x, 0x1E, 2);
    if (!(gProbeHits & 1)) {
        (void)Obj_CeilingWalk(x);
        return;
    }
    if (gObjects->action[x] != 0x4E) {
        Sound_Request(SFX_HOP, x);
        gObjects->action[x] = 0x4E;
        gObjects->animFrame[x] = 0;
        gObjects->animTimer[x] = 1;
    }
    if ((gObjects->animFrame[x] | gObjects->animTimer[x]) != 0) {
        return;
    }
    uint8_t dir = gObjects->dir[x];
    Obj_FaceScrooge(x);
    bool turned = gObjects->dir[x] != dir;
    gObjects->dir[x] = dir;
    if (turned) {
        Obj_CeilingClimb(x);
        return;
    }
    gObjects->action[x] = 0x4D;
    gObjects->animFrame[x] = 2;
    gObjects->animTimer[x] = 0;
    Obj_CeilingJump(x);
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
static void Obj_BlockDweller(uint8_t x)
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

/* Kind $41 (fixed bank $FE50): a spot that, while Scrooge holds the key
 * item the land asks for ($A3 & $F6), sets his special-tile bit ($0600 bit
 * 2) when he touches it. */
static void Obj_KeySpot(uint8_t x)
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
static void Obj_MovingPlatform(uint8_t x)
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

/* Kind $1F ($9DF7): an enemy that paces 1 px a frame, right first for 22
 * frames, then turning every 32, facing its way. A hit beats it. */
static void Obj_Pacer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PACER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetSpeed(x, 0x0100);
        gObjects->timer[x] = 0x16;
        gObjects->state[x]++;
        gObjects->dir[x] = OBJ_DIR_RIGHT;
    }
    (void)Obj_TouchScrooge(x);
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
        Obj_MoveRight(x);
    } else {
        gObjects->flags[x] &= (uint8_t)~OBJ_FLAG_FACING_RIGHT;
        Obj_MoveLeft(x);
    }
    if (--gObjects->timer[x] == 0) {
        gObjects->dir[x] ^= 3;
        gObjects->timer[x] = 0x20;
    }
}

/* Kind $1E ($9D8A): an enemy that walks a ledge at $0133 a frame, right
 * first, turning at its edges (probe $1E or $20 finds no floor) and at
 * walls. A hit beats it. */
static void Obj_LedgeWalker(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_LEDGE_WALKER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0133);
        gObjects->dir[x] = OBJ_DIR_RIGHT;
        gObjects->state[x]++;
    }
    (void)Obj_TouchScrooge(x);
    x = Obj_Fall(x, (gObjects->dir[x] & OBJ_DIR_RIGHT) ? 0x20 : 0x1E, 0);
    if (!(gProbeHits & 1)) {
        gObjects->dir[x] ^= 3;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x19, 0);
    } else {
        x = Obj_WalkLeft(x, 0x1C, 0);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
    }
}

/* Kind $19 ($973C): an enemy that waits until Scrooge is within 12 px,
 * drops (pose $3C, then $3B) until it lands or is 40 px above him, faces
 * him and flies in arcs by a cycle of 14 motions (sound $32), its step
 * moving on once it is 80 px or more above him. Within 12 px of him it
 * pounces (vertical velocity $0280, sound $1F) and, landed, sits 120 frames
 * in pose $39 (sound $20) before flying again. Off screen or hit, it is
 * beaten. Each step's address in the jump table is left in $00/$01. */
static void Obj_Pouncer(uint8_t x)
{
    static const uint8_t motions[14] = { 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19,
                                         0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11 };
    static const uint16_t steps[5] = { 0x9768, 0x9787, 0x97F3, 0x97F3, 0x98C8 };

    Decomp_Enter(DT_CODE_KIND_POUNCER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x) || gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    if (gObjects->action[x] != 0x39) {
        (void)Obj_TouchScrooge(x);
    }
    uint8_t step = Step(x);
    gScratch(0) = (uint8_t)steps[step];
    gScratch(1) = (uint8_t)(steps[step] >> 8);
    switch (step) {
        case 0:
            if (Obj_DistanceX(x) >= 0x0C) {
                return;
            }
            gObjects->state[x]++;
            gObjects->timer[x] = 0x1E;
            gObjects->unk_06C0[x] = 0;
            Obj_SetVelocityY(x, 0x0280);
            return;
        case 1:
            if (gObjects->timer[x] != 0) {
                if (--gObjects->timer[x] == 0) {
                    gObjects->action[x] = 0x3C;
                    ResetAnim(x);
                }
                return;
            }
            if (gObjects->action[x] == 0x3C && gObjects->animTimer[x] == 8) {
                gObjects->action[x]--;
                ResetAnim(x);
            }
            x = Obj_ClimbDown(x, 0x40, 1);
            if (!(gProbeHits & 1) && (uint8_t)(gObjects->y[P_] - gObjects->y[x]) >= 0x28) {
                return;
            }
            gObjects->state[x]++;
            Obj_FaceScrooge(x);
            if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
                gObjects->flags[x] |= OBJ_FLAG_FACING_RIGHT;
            }
            gObjects->action[x] = 0x3B;
            ResetAnim(x);
            return;
        case 2:
        case 3:
            if (gObjects->timer[x] == 0) {
                gObjects->vdir[x] = 0;
                Obj_SetMotionIndex(x, motions[gObjects->unk_06C0[x]]);
                if (gObjects->speed[x] & 0x80) {
                    Obj_SetSpeed(x, (uint16_t)-Obj_GetSpeed(x));
                }
                if (gObjects->vy[x] & 0x80) {
                    gObjects->vdir[x] = 8;
                    Obj_SetVelocityY(x, (uint16_t)-Obj_GetVelocityY(x));
                }
                if (++gObjects->unk_06C0[x] == 0x0E) {
                    gObjects->unk_06C0[x] = 0;
                }
                gObjects->timer[x] = 3;
            }
            if (gObjects->vdir[x] == 0) {
                x = Obj_ClimbDown(x, 0x40, 1);
            } else {
                x = Obj_ClimbUp(x, 0x42, 1);
            }
            if (gObjects->dir[x] & OBJ_DIR_LEFT) {
                x = Obj_WalkLeft(x, 0x3D, 2);
            } else {
                x = Obj_WalkRight(x, 0x3A, 2);
            }
            if (gObjects->animTimer[x] == 0) {
                Sound_Request(SFX_POUNCER_FLY, x);
            }
            if (gObjects->state[x] & 1) {
                gObjects->timer[x]--;
            } else if (gObjects->y[P_] >= gObjects->y[x] && ((gObjects->y[P_] - gObjects->y[x]) & 0xF0) >= 0x50) {
                gObjects->state[x]++;
                gObjects->timer[x]--;
            }
            if (Obj_DistanceX(x) >= 0x0C) {
                return;
            }
            gObjects->action[x] = 0x3C;
            ResetAnim(x);
            gObjects->state[x]++;
            Obj_SetVelocityY(x, 0x0280);
            Sound_Request(SFX_POUNCE, x);
            return;
        default:
            break;
    }
    if (gObjects->action[x] == 0x39) {
        if (--gObjects->timer[x] != 0) {
            return;
        }
        gObjects->action[x] = 0x3B;
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->state[x] = (uint8_t)((gObjects->state[x] & 0xF0) | 2);
        gObjects->hitbox[x] &= 0x7F;
        return;
    }
    if (gObjects->action[x] == 0x3C && gObjects->animTimer[x] == 8) {
        gObjects->action[x] = 0x5C;
        ResetAnim(x);
    }
    x = Obj_ClimbDown(x, 0x40, 1);
    if (!(gProbeHits & 1)) {
        return;
    }
    gObjects->action[x] = 0x39;
    ResetAnim(x);
    gObjects->timer[x] = 0x78;
    gObjects->hitbox[x] |= 0x80;
    Sound_Request(SFX_POUNCER_LAND, x);
}

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
static void Obj_Pickpocket(uint8_t x)
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

/* The splash of kind $1A: palette entries $1E/$1F of the sprites, sound
 * $21, a palette upload, and a splash (pose $37, kind 0, freed when its
 * animation ends) at Y $BC in the highest free slot, if any. */
static void Obj_PitSplash(uint8_t x, uint8_t c2, uint8_t c3)
{
    gPaletteShown(0x1E) = c2;
    gPaletteShown(0x1F) = c3;
    Sound_Request(SFX_SPLASH, x);
    gPaletteUpload = 0xFF;
    uint8_t y = 0x0F;
    while (gObjects->state[y] & OBJ_STATUS_LIVE) {
        if (--y == 0) {
            return;
        }
    }
    gObjects->state[y] = 0xA8;
    gObjects->flags[y] = 0xA8;
    gObjects->y[y] = 0xBC;
    gObjects->worldX[y] = gObjects->worldX[x];
    gObjects->worldPage[y] = gObjects->worldPage[x];
    gObjects->action[y] = 0x37;
    ResetAnim(y);
    gObjects->hitbox[y] = 0;
    gObjects->kind[y] = OBJ_KIND_NONE;
}

/* $9A1E: the splash as the jumper leaves the pit. */
static void Obj_PitSplashOut(uint8_t x)
{
    Decomp_Enter(DT_CODE_PIT_SPLASH_OUT, 0, x, 0);
    Obj_PitSplash(x, 0x19, 0x27);
}

/* $9A2A: the splash as it falls back in. */
static void Obj_PitSplashIn(uint8_t x)
{
    Decomp_Enter(DT_CODE_PIT_SPLASH_IN, 0, x, 0);
    Obj_PitSplash(x, 0x21, 0x17);
}

/* Kind $1A ($9956): an enemy that, once Scrooge is within 32 px, jumps out
 * of a pit from the bottom of the screen (Y $EF) at the velocity its
 * variant ($06B0) gives, falls back, and jumps again 120 frames after it
 * passes Y $F0, with a splash at the frame counts ($06D0) its variant
 * gives. It harms him (hitbox $91) only on screen above Y $D0. A hit beats
 * it. */
static void Obj_PitJumper(uint8_t x)
{
    static const uint16_t jumps[3] = { 0x0652, 0x07A5, 0x07A5 };
    static const uint8_t splashOut[3] = { 0x08, 0x07, 0x00 };
    static const uint8_t splashIn[3] = { 0x2A, 0x37, 0x00 };

    Decomp_Enter(DT_CODE_KIND_PIT_JUMPER, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        Obj_Defeat(x);
        return;
    }
    gObjects->hitbox[x] = 0;
    if (gObjects->y[x] != 0 && gObjects->y[x] < 0xD0) {
        gObjects->hitbox[x] = 0x91;
    }
    if (Step(x) == 0) {
        if (Obj_DistanceX(x) >= 0x20) {
            return;
        }
        gObjects->vdir[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        gObjects->y[x] = 0xEF;
        Obj_SetVelocityY(x, jumps[gObjects->timer[x]]);
        gObjects->animTimer[x] = 0x80;
        gObjects->animFrame[x] = 0;
        gObjects->state[x]++;
    }
    gObjects->unk_06D0[x]++;
    uint8_t variant = gObjects->timer[x];
    if (gObjects->vdir[x] == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (gObjects->vy[x] & 0x80) {
            gObjects->vdir[x]++;
            Obj_SetVelocityY(x, 0);
            gObjects->animFrame[x] = 1;
        }
        if (splashOut[variant] != 0 && splashOut[variant] == gObjects->unk_06D0[x]) {
            Obj_PitSplashOut(x);
        }
        return;
    }
    if (gObjects->unk_06C0[x] == 0) {
        Obj_ApplyGravity(x);
        Obj_ApplyVelocityY(x);
        if (gObjects->y[x] >= 0xF0) {
            gObjects->yFrac[x] = 0;
            gObjects->y[x] = 0;
            gObjects->unk_06C0[x] = 0x78;
        }
    } else if (--gObjects->unk_06C0[x] == 0) {
        gObjects->state[x] &= 0xF0;
        return;
    }
    if (splashIn[variant] != 0 && splashIn[variant] == gObjects->unk_06D0[x]) {
        Obj_PitSplashIn(x);
    }
}

/* Kind $1B ($9A81): an enemy that bounces towards Scrooge, walking 1 px a
 * frame and jumping again (vertical velocity $04E5) each time it lands. Off
 * screen or hit, it is beaten. */
static void Obj_Bouncer(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_BOUNCER, 0x22, x, gObjects->kind[x]);
    if (Enemy_Beaten(x, 0)) {
        return;
    }
    if (Step(x) == 0) {
        Obj_SetSpeed(x, 0x0100);
        Obj_FaceScrooge(x);
        gObjects->state[x] |= 2;
    }
    (void)Obj_TouchScrooge(x);
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->animFrame[x] != 0) {
            gObjects->animTimer[x] = 0x80;
        }
        x = Obj_Rise(x, 0x21, 2);
        if (gObjects->vy[x] & 0x80) {
            Obj_SetVelocityY(x, 0);
            gObjects->state[x]++;
        }
    } else {
        x = Obj_Fall(x, 0x1E, 2);
        if (gProbeHits & 1) {
            gObjects->state[x]--;
            ResetAnim(x);
            Obj_SetVelocityY(x, 0x04E5);
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        (void)Obj_WalkRight(x, 0x18, 2);
    } else {
        (void)Obj_WalkLeft(x, 0x1B, 2);
    }
}

/* Kind $24 ($A024): a boulder that, once Scrooge is within 4 px, rolls
 * right at $0180 a frame with probe $35 (the walls move skips the block test
 * for this kind). At a wall it walks once more and stops for good (kind 0).
 * Within $15 px of him both ways it pushes him right ($73). The second walk
 * takes probe 1 and the Y the first one left: the probes left to test, 0
 * after a solid hit; a hit on a special tile would leave that tile's handler
 * index instead, which the C does not model. */
static void Obj_Boulder(uint8_t x)
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
        x = Obj_WalkRight(x, 1, gProbeCount);
        gObjects->kind[x] = OBJ_KIND_NONE;
    }
    if (Obj_DistanceX(x) < 0x15 && Obj_DistanceY(x) < 0x15) {
        gPlayerPushRight++;
    }
}

/* Kind $23 ($9FEC): something that rolls left at $0180 a frame on the
 * floor (probe $31), harming Scrooge on contact, with sound $27 at the start
 * of each animation cycle; in the air it only falls. */
static void Obj_Roller(uint8_t x)
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
static void Obj_TreatSpout(uint8_t x)
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
static void Obj_Treat(uint8_t x)
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

/* Kind $30 ($AC3D, also kind $31's handler): an enemy that paces 1 px a
 * frame, right first, for its kind's span ($16 or $0B frames, doubled after
 * the first turn), resting 120 frames at each turn and facing its way; each
 * frame it touches Scrooge it takes 1 from the money's third digit (which
 * also adds to its span left). A hit beats it. */
static void Obj_Thief(uint8_t x)
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

/* $F6D7, not decompiled yet: a counted hit on a boss (the boss-hit event's
 * site is in it). Carry set when the hit beat it. */
static bool Boss_Hit(uint8_t *x)
{
    Regs r = Decomp_Call(DT_CODE_BOSS_HIT, 0, *x, 0);
    *x = r.x;
    return r.p & P_C;
}

/* Kind $28 ($A6D3): the African Mines boss. It lands in the middle of the
 * arena (world X $80, Y $B0) and hops three times (vertical velocity $02D4,
 * sound $22), then, facing a random way (pose $2A, harmless pose $62 to
 * $63), runs a 20-step pattern of climbs up and down and walks at $0398
 * ($A8AC: direction and vertical way), stepping 4 px on the probes the step
 * expects ($A8C0). Off the expected probes after a full pattern, within 3 px
 * of the middle, it starts over. A counted hit goes to the boss hit $F6D7;
 * the last one beats it. */
static void Obj_MinesBoss(uint8_t x)
{
    static const uint8_t pattern[20] = { 0x05, 0x09, 0x05, 0x09, 0x05, 0x09, 0x0A, 0x09, 0x0A, 0x09,
                                         0x0A, 0x06, 0x0A, 0x06, 0x0A, 0x06, 0x05, 0x06, 0x05, 0x06 };
    static const uint8_t expected[20] = { 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x02, 0x00, 0x02,
                                          0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00, 0x02, 0x00, 0x02 };

    Decomp_Enter(DT_CODE_KIND_MINES_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0 && Boss_Hit(&x)) {
        Obj_Defeat(x);
        return;
    }
    uint8_t step;
    if (Step(x) == 0) {
        if (gObjects->action[x] == 0x2A) {
            if (gObjects->animFrame[x] != 0 && gObjects->animTimer[x] == 5) {
                gObjects->action[x] = 0x62;
                ResetAnim(x);
                gObjects->hitbox[x] |= 0x80;
            }
            return;
        }
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = 0;
        gObjects->vdir[x] = 0;
        gObjects->y[x] = 0xB0;
        gObjects->worldX[x] = 0x80;
        RAM_U8(DT_ADDR_MINES_BOSS_05E7) = 0x55;
        gObjects->state[x]++;
    }
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->vdir[x] != 0) {
            x = Obj_Rise(x, 0x52, 2);
            if (gObjects->vy[x] & 0x80) {
                Obj_SetVelocityY(x, 0);
                gObjects->vdir[x]--;
            }
            return;
        }
        x = Obj_Fall(x, 0x31, 2);
        if (!(gProbeHits & 1)) {
            return;
        }
        Sound_Request(SFX_BOSS_HOP, x);
        Obj_SetVelocityY(x, 0x02D4);
        gObjects->vdir[x]++;
        if (++gObjects->timer[x] != 3) {
            return;
        }
        gObjects->state[x]++;
        gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
        gObjects->dir[x] = (uint8_t)((gRandom(1) & 1) + 1);
        gObjects->timer[x] = gObjects->dir[x];
        gObjects->action[x] = 0x2A;
        ResetAnim(x);
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        Obj_SetSpeed(x, 0x0398);
        Obj_SetVelocityY(x, 0x0398);
        step = 0;
    } else {
        if (gObjects->action[x] == 0x2A) {
            if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
                return;
            }
            gObjects->action[x] = 0x63;
            ResetAnim(x);
            gObjects->hitbox[x] &= 0x7F;
        }
        (void)Obj_TouchScrooge(x);
        if (gObjects->vdir[x] & 4) {
            x = Obj_ClimbDown(x, 0x31, 2);
        } else {
            x = Obj_ClimbUp(x, 0x52, 2);
        }
        uint8_t vertical = (uint8_t)((gProbeHits & 1) << 1);
        if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
            x = Obj_WalkRight(x, 0x34, 2);
        } else {
            x = Obj_WalkLeft(x, 0x37, 2);
        }
        gProbeHits = (uint8_t)(vertical + (gProbeHits & 1));
        uint8_t want = expected[gObjects->unk_06C0[x]];
        if (want != 0) {
            gProbeHits &= want;
        }
        if (gProbeHits != want) {
            if (gObjects->unk_06D0[x] == 0) {
                return;
            }
            uint8_t off = (uint8_t)(gObjects->worldX[x] - 0x80);
            if (gObjects->worldX[x] < 0x80) {
                off = (uint8_t)-off;
            }
            if (off >= 3) {
                return;
            }
            gObjects->action[x] = 0x2A;
            ResetAnim(x);
            gObjects->state[x] &= 0xF0;
            return;
        }
        if (gProbeHits == 0) {
            gObjects->y[x] = (uint8_t)(gObjects->y[x] + ((gObjects->vdir[x] & 8) ? 0xFC : 0x04));
        }
        step = (uint8_t)(gObjects->unk_06C0[x] + 1);
        if (step == 0x14) {
            gObjects->unk_06D0[x]++;
            step = 0;
        }
        gObjects->unk_06C0[x] = step;
    }
    uint8_t p = pattern[step];
    gObjects->dir[x] = (uint8_t)(p & 3);
    if (gObjects->timer[x] != 1) {
        gObjects->dir[x] ^= 3;
    }
    gObjects->vdir[x] = (uint8_t)(p & 0x0C);
}

/* Kind $2A ($A8D6): the Himalayas boss. It jumps (vertical velocity $06EE,
 * frame 2 of its pose), lands (sound $1E) and, at animation frame 7, rests
 * 30 frames before the next jump, walking at $01CC while airborne. A wall
 * hit while resting turns it (pose $65, sound $23) and starts a stomp: at
 * frame 7 of the pose (pose $64) the camera shakes 4 px every 4 frames and
 * one of two patterns ($AAA8, random) drops a row of falling objects (kind
 * $2B, pose $47) into slots 8 on, each at its frame count ($AAB6). When the
 * last one is gone it jumps again. A counted hit goes to the boss hit $F6D7;
 * the last one beats it. The spawning slot is left in $00. */
static void Obj_HimalayasBoss(uint8_t x)
{
    static const uint8_t dropX[14] = { 0x70, 0xD8, 0x48, 0xA8, 0x18, 0x88, 0x00,
                                       0x68, 0x98, 0x28, 0xD8, 0x48, 0x88, 0x00 };
    static const uint8_t dropAt[14] = { 0x00, 0x08, 0x18, 0x18, 0x20, 0x30, 0x00,
                                        0x00, 0x08, 0x18, 0x18, 0x28, 0x28, 0x00 };
    static const uint8_t patterns[2] = { 0x00, 0x07 };
    static const uint8_t shakes[2] = { 0x00, 0x04 };

    Decomp_Enter(DT_CODE_KIND_HIMALAYAS_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0 && Boss_Hit(&x)) {
        Obj_Defeat(x);
        return;
    }
    if (Step(x) == 0) {
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        ResetAnim(x);
        Obj_SetSpeed(x, 0x01CC);
        gObjects->state[x]++;
        gObjects->dir[x] = OBJ_DIR_LEFT;
        RAM_U8(DT_ADDR_HIMALAYAS_BOSS_05A0) = 0x11;
        Obj_SetVelocityY(x, 0x06EE);
    }
    if (gObjects->state[x] & 2) {
        if (gObjects->action[x] != 0x64) {
            if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 7) {
                return;
            }
            gObjects->action[x] = 0x64;
            gObjects->animFrame[x] = 0;
            gObjects->timer[x] = 0;
            gRandom(1) = (uint8_t)(gRandom(0) + gRandom(1) + 1);
            gObjects->unk_06C0[x] = patterns[gRandom(1) & 1];
            gObjects->unk_06D0[x] = 8;
        }
        for (;;) {
            gObjects->animTimer[x] = 0;
            gCameraX = shakes[(RAM_U8(DT_ADDR_SPR_PHASE) >> 2) & 1];
            uint8_t i = gObjects->unk_06C0[x];
            if (dropX[i] == 0) {
                break;
            }
            if (gObjects->timer[x] != dropAt[i]) {
                gObjects->timer[x]++;
                return;
            }
            gScratch0 = x;
            uint8_t s = gObjects->unk_06D0[x];
            gObjects->worldX[s] = dropX[i];
            gObjects->worldPage[s] = 4;
            gObjects->y[s] = 0x38;
            gObjects->state[s] = 0xA0;
            gObjects->flags[s] = 0xA0;
            gObjects->action[s] = 0x47;
            ResetAnim(s);
            gObjects->hitbox[s] = 0x10;
            gObjects->kind[s] = OBJ_KIND_DROPPING;
            Obj_SetVelocityY(s, 0x0180);
            gObjects->unk_06C0[x]++;
            gObjects->unk_06D0[x]++;
        }
        if (gObjects->state[gObjects->unk_06D0[x] - 1] != 0) {
            return;
        }
        gObjects->state[x]--;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        gObjects->unk_06D0[x] = 0;
        gCameraX = 0;
        Obj_SetVelocityY(x, 0x06EE);
    }
    if (gObjects->unk_06C0[x] != 0) {
        gObjects->animTimer[x] = 0;
        if (--gObjects->unk_06C0[x] == 0) {
            gObjects->animFrame[x]++;
        }
        return;
    }
    if (gObjects->timer[x] == 0) {
        if (gObjects->animFrame[x] != 2) {
            return;
        }
        gObjects->animTimer[x] = 0;
        x = Obj_Rise(x, 0x52, 2);
        if (gObjects->vy[x] & 0x80) {
            Obj_SetVelocityY(x, 0);
            gObjects->timer[x]++;
            return;
        }
    } else {
        if (gObjects->animFrame[x] == 2) {
            gObjects->animTimer[x] = 0;
        }
        x = Obj_Fall(x, 0x31, 2);
        if (gProbeHits & 1) {
            if (gObjects->animFrame[x] != 1) {
                gObjects->animFrame[x] = 1;
            }
            if (gObjects->animTimer[x] == 0) {
                Sound_Request(SFX_BOSS_LAND, x);
            }
            if (gObjects->animTimer[x] != 7) {
                return;
            }
            gObjects->unk_06C0[x] = 0x1E;
            Obj_SetVelocityY(x, 0x06EE);
            gObjects->timer[x] = 0;
            gObjects->animFrame[x] = 0;
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x34, 2);
    } else {
        x = Obj_WalkLeft(x, 0x37, 2);
    }
    if (!(gProbeHits & 1) || gObjects->unk_06C0[x] == 0) {
        return;
    }
    gObjects->dir[x] ^= 3;
    gObjects->state[x]++;
    gObjects->action[x] = 0x65;
    ResetAnim(x);
    Sound_Request(SFX_BOSS_TURN, x);
}

/* The Moon boss back in the middle: pose $67, 30 frames facing Scrooge. */
static void Obj_MoonBossRestart(uint8_t x)
{
    gObjects->worldX[x] = 0x80;
    gObjects->unk_06C0[x]--;
    gObjects->state[x]--;
    gObjects->action[x] = 0x67;
    ResetAnim(x);
    gObjects->unk_06D0[x] = 0x1E;
}

/* Kind $29 ($AAE1): the Moon boss. It enters at world X $80 (pose $67) and
 * for 15 frames faces Scrooge, then (pose $66) walks a random way at a
 * random speed, $03C0 or $01CC, the slow one jumping first (pose $68,
 * vertical velocity $06EE). Landing from a jump it jumps again ($0548),
 * turned round; walls turn it too. After a turn, back within 4 px of the
 * middle on the floor, or landing after its second jump, it starts over
 * from the middle with 30 frames facing Scrooge. Its random steps add the
 * carry the dispatch left. A counted hit goes to the boss hit $F6D7; the
 * last one beats it. */
static void Obj_MoonBoss(uint8_t x, bool carry)
{
    static const uint16_t speeds[2] = { 0x03C0, 0x01CC };

    Decomp_Enter(DT_CODE_KIND_MOON_BOSS, 0x22, x, gObjects->kind[x]);
    if (gObjects->unk_0690[x] != 0) {
        if (Boss_Hit(&x)) {
            Obj_Defeat(x);
            return;
        }
        carry = false;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        gObjects->worldX[x] = 0x80;
        RAM_U8(DT_ADDR_BOSS_05AF) = 0x44;
        gObjects->state[x]++;
        gObjects->unk_06D0[x] = 0x0F;
        gObjects->action[x] = 0x67;
    }
    if (gObjects->unk_06D0[x] != 0) {
        gObjects->unk_06D0[x]--;
        uint8_t facing = gObjects->worldX[x] >= gObjects->worldX[P_] ? 0 : OBJ_FLAG_FACING_RIGHT;
        gObjects->flags[x] = (uint8_t)((gObjects->flags[x] & (uint8_t)~OBJ_FLAG_FACING_RIGHT) | facing);
        return;
    }
    if (!(gObjects->state[x] & 2)) {
        gObjects->state[x]++;
        gObjects->timer[x] = 0;
        gObjects->unk_06C0[x] = 0;
        ResetAnim(x);
        gObjects->action[x] = 0x66;
        unsigned sum = gRandom(0) + gRandom(1) + carry;
        gRandom(3) = (uint8_t)sum;
        gObjects->dir[x] = (uint8_t)((sum & 1) + 1);
        sum = gRandom(2) + gRandom(3) + (sum >> 8);
        gRandom(1) = (uint8_t)sum;
        uint8_t slow = sum & 1;
        gObjects->timer[x] = slow;
        Obj_SetSpeed(x, speeds[slow]);
        if (slow) {
            gObjects->timer[x]++;
            Obj_SetVelocityY(x, 0x06EE);
            gObjects->action[x] = 0x68;
        }
    }
    if (gObjects->timer[x] != 0) {
        if (!(gObjects->timer[x] & 1)) {
            x = Obj_Rise(x, 0x5E, 2);
            if (gObjects->vy[x] & 0x80) {
                gObjects->timer[x]--;
                Obj_SetVelocityY(x, 0);
            }
        } else {
            x = Obj_Fall(x, 0x5B, 2);
            if (gProbeHits & 1) {
                if (gObjects->unk_06C0[x] != 0) {
                    Obj_MoonBossRestart(x);
                    return;
                }
                Obj_SetVelocityY(x, 0x0548);
                gObjects->timer[x]++;
                gObjects->unk_06C0[x]++;
                gObjects->dir[x] ^= 3;
                return;
            }
        }
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        x = Obj_WalkRight(x, 0x55, 2);
    } else {
        x = Obj_WalkLeft(x, 0x58, 2);
    }
    if (gProbeHits & 1) {
        gObjects->dir[x] ^= 3;
        gObjects->unk_06C0[x]++;
        return;
    }
    if (gObjects->timer[x] != 0 || gObjects->unk_06C0[x] == 0) {
        return;
    }
    uint8_t off = (uint8_t)(gObjects->worldX[x] - 0x80);
    if (gObjects->worldX[x] < 0x80) {
        off = (uint8_t)-off;
    }
    if (off < 4) {
        Obj_MoonBossRestart(x);
    }
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
        case OBJ_KIND_NONE:
            break;
        case OBJ_KIND_HOPPER:
            Obj_Hopper(slot);
            break;
        case OBJ_KIND_WALKER:
            Obj_Walker(slot);
            break;
        case OBJ_KIND_THROWER:
            Obj_Thrower(slot);
            break;
        case OBJ_KIND_SHOT:
            Obj_Shot(slot);
            break;
        case OBJ_KIND_WATCHER:
            Obj_Watcher(slot);
            break;
        case OBJ_KIND_DANGLER:
            Obj_Dangler(slot);
            break;
        case OBJ_KIND_SNAPPER:
            Obj_Snapper(slot);
            break;
        case OBJ_KIND_BLOCK:
            Obj_Block(slot, kind);
            break;
        case OBJ_KIND_THROWN:
            Obj_Thrown(slot);
            break;
        case OBJ_KIND_DEFEATED:
            Obj_Defeated(slot);
            break;
        case OBJ_KIND_DROPPER:
            Obj_Dropper(slot);
            break;
        case OBJ_KIND_CRAWLER:
            Obj_Crawler(slot);
            break;
        case OBJ_KIND_FLYER:
            Obj_Flyer(slot);
            break;
        case OBJ_KIND_SWING:
            Obj_Swing(slot);
            break;
        case OBJ_KIND_LIFT:
            Obj_Lift(slot);
            break;
        case OBJ_KIND_CHARGER:
            Obj_Charger(slot);
            break;
        case OBJ_KIND_SWOOPER:
            Obj_Swooper(slot);
            break;
        case OBJ_KIND_BLOCK_DWELLER:
            Obj_BlockDweller(slot);
            break;
        case OBJ_KIND_PICKUP:
            Obj_Pickup(slot);
            break;
        case OBJ_KIND_HIDDEN_ITEM:
            Obj_HiddenItem(slot);
            break;
        case OBJ_KIND_CHEST:
            Obj_Chest(slot);
            break;
        case OBJ_KIND_PATROLLER:
            Obj_Patroller(slot);
            break;
        case OBJ_KIND_POUNCER:
            Obj_Pouncer(slot);
            break;
        case OBJ_KIND_PIT_JUMPER:
            Obj_PitJumper(slot);
            break;
        case OBJ_KIND_BOUNCER:
            Obj_Bouncer(slot);
            break;
        case OBJ_KIND_LEAPER:
            Obj_Leaper(slot);
            break;
        case OBJ_KIND_LEDGE_WALKER:
            Obj_LedgeWalker(slot);
            break;
        case OBJ_KIND_PACER:
            Obj_Pacer(slot);
            break;
        case OBJ_KIND_BOBBER:
            Obj_Bobber(slot);
            break;
        case OBJ_KIND_ROLLER:
            Obj_Roller(slot);
            break;
        case OBJ_KIND_BOULDER:
            Obj_Boulder(slot);
            break;
        case OBJ_KIND_CEILING_HOPPER:
            Obj_CeilingHopper(slot);
            break;
        case OBJ_KIND_RISING_SHOT:
            Obj_RisingShot(slot);
            break;
        case OBJ_KIND_MINES_BOSS:
            Obj_MinesBoss(slot);
            break;
        case OBJ_KIND_MOON_BOSS:
            Obj_MoonBoss(slot, r->p & P_C);
            break;
        case OBJ_KIND_HIMALAYAS_BOSS:
            Obj_HimalayasBoss(slot);
            break;
        case OBJ_KIND_DROPPING:
            Obj_Dropping(slot);
            break;
        case OBJ_KIND_TREAT_SPOUT:
            Obj_TreatSpout(slot);
            break;
        case OBJ_KIND_TREAT:
            Obj_Treat(slot);
            break;
        case OBJ_KIND_DOOR:
            Obj_Door(slot);
            break;
        case OBJ_KIND_THIEF:
        case OBJ_KIND_SHORT_THIEF:
            Obj_Thief(slot);
            break;
        case OBJ_KIND_PICKPOCKET:
            Obj_Pickpocket(slot);
            break;
        case OBJ_KIND_TALKER:
            Obj_Talker(slot);
            break;
        case OBJ_KIND_MOVING_PLATFORM:
            Obj_MovingPlatform(slot);
            break;
        case OBJ_KIND_KEY_SPOT:
            Obj_KeySpot(slot);
            break;
        default:
            *r = Decomp_CallP((uint16_t)(gScratch1 << 8 | gScratch0), r->a, slot, r->y, r->p);
            break;
    }
}

/* Bank 6 $8000: once per frame from the frame loop. Returns with X = 16,
 * which the code after it passes on (a sound request keeps it in $00). */
static void Obj_UpdateAll(Regs *r)
{
    Decomp_Enter(DT_B6_OBJECTS, r->a, r->x, r->y);
    gGravity = OBJ_GRAVITY;
    r->a = OBJ_GRAVITY;
    for (gObjCursor = 1; gObjCursor != OBJ_SLOTS; gObjCursor++) {
        r->a = gObjects->state[gObjCursor];
        if (r->a & OBJ_STATUS_LIVE) {
            Obj_RunHandler(gObjCursor, r);
        }
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

static void Run_Patroller(Regs *r)
{
    Obj_Patroller(r->x);
}

static void Run_Charger(Regs *r)
{
    Obj_Charger(r->x);
}

static void Run_Bobber(Regs *r)
{
    Obj_Bobber(r->x);
}

static void Run_Leaper(Regs *r)
{
    Obj_Leaper(r->x);
}

static void Run_SpawnDust(Regs *r)
{
    Obj_SpawnDust(r->x);
}

static void Run_RisingShot(Regs *r)
{
    Obj_RisingShot(r->x);
}

static void Run_Dropping(Regs *r)
{
    Obj_Dropping(r->x);
}

static void Run_CeilingHopper(Regs *r)
{
    Obj_CeilingHopper(r->x);
}

static void Run_CeilingWalk(Regs *r)
{
    Obj_CeilingWalk(r->x);
}

static void Run_BlockDweller(Regs *r)
{
    Obj_BlockDweller(r->x);
}

static void Run_DwellerLinkBlock(Regs *r)
{
    Obj_DwellerLinkBlock(r->x);
}

static void Run_KeySpot(Regs *r)
{
    Obj_KeySpot(r->x);
}

static void Run_MovingPlatform(Regs *r)
{
    Obj_MovingPlatform(r->x);
}

static void Run_Pacer(Regs *r)
{
    Obj_Pacer(r->x);
}

static void Run_LedgeWalker(Regs *r)
{
    Obj_LedgeWalker(r->x);
}

static void Run_Pouncer(Regs *r)
{
    Obj_Pouncer(r->x);
}

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

static void Run_PitJumper(Regs *r)
{
    Obj_PitJumper(r->x);
}

static void Run_PitSplashOut(Regs *r)
{
    Obj_PitSplashOut(r->x);
}

static void Run_PitSplashIn(Regs *r)
{
    Obj_PitSplashIn(r->x);
}

static void Run_Bouncer(Regs *r)
{
    Obj_Bouncer(r->x);
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

static void Run_Thief(Regs *r)
{
    Obj_Thief(r->x);
}

static void Run_MinesBoss(Regs *r)
{
    Obj_MinesBoss(r->x);
}

static void Run_HimalayasBoss(Regs *r)
{
    Obj_HimalayasBoss(r->x);
}

static void Run_MoonBoss(Regs *r)
{
    Obj_MoonBoss(r->x, r->p & P_C);
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
    { DT_B6_OBJECTS, "ducktales.decomp.6.8000", "Obj_UpdateAll", Run_UpdateAll, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HOPPER, NULL, "Obj_Hopper", Run_Hopper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_WALKER, NULL, "Obj_Walker", Run_Walker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_THROWER, NULL, "Obj_Thrower", Run_Thrower, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SHOT, NULL, "Obj_Shot", Run_Shot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_WATCHER, NULL, "Obj_Watcher", Run_Watcher, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DANGLER, NULL, "Obj_Dangler", Run_Dangler, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SNAPPER, NULL, "Obj_Snapper", Run_Snapper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BLOCK, NULL, "Obj_Block", Run_Block, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_THROWN, NULL, "Obj_Thrown", Run_Thrown, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DEFEATED, NULL, "Obj_Defeated", Run_Defeated, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DROPPER, NULL, "Obj_Dropper", Run_Dropper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CRAWLER, NULL, "Obj_Crawler", Run_Crawler, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_FLYER, NULL, "Obj_Flyer", Run_Flyer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SWING, NULL, "Obj_Swing", Run_Swing, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LIFT, NULL, "Obj_Lift", Run_Lift, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_SWOOPER, NULL, "Obj_Swooper", Run_Swooper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_MOON_BOSS, NULL, "Obj_MoonBoss", Run_MoonBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HIMALAYAS_BOSS, NULL, "Obj_HimalayasBoss", Run_HimalayasBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_MINES_BOSS, NULL, "Obj_MinesBoss", Run_MinesBoss, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_THIEF, NULL, "Obj_Thief", Run_Thief, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TREAT, NULL, "Obj_Treat", Run_Treat, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TREAT_SPOUT, NULL, "Obj_TreatSpout", Run_TreatSpout, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_ROLLER, NULL, "Obj_Roller", Run_Roller, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOULDER, NULL, "Obj_Boulder", Run_Boulder, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOUNCER, NULL, "Obj_Bouncer", Run_Bouncer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PIT_JUMPER, NULL, "Obj_PitJumper", Run_PitJumper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PIT_SPLASH_OUT, NULL, "Obj_PitSplashOut", Run_PitSplashOut, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PIT_SPLASH_IN, NULL, "Obj_PitSplashIn", Run_PitSplashIn, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PICKPOCKET, NULL, "Obj_Pickpocket", Run_Pickpocket, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKPOCKET_WALK, NULL, "Obj_PickpocketWalk", Run_PickpocketWalk, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKPOCKET_STEAL, NULL, "Obj_StealMoney", Run_StealMoney, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_POUNCER, NULL, "Obj_Pouncer", Run_Pouncer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LEDGE_WALKER, NULL, "Obj_LedgeWalker", Run_LedgeWalker, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PACER, NULL, "Obj_Pacer", Run_Pacer, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_MOVING_PLATFORM, NULL, "Obj_MovingPlatform", Run_MovingPlatform, 0 },
    { DT_CODE_KIND_KEY_SPOT, NULL, "Obj_KeySpot", Run_KeySpot, 0 },
    { DT_CODE_KIND_BLOCK_DWELLER, NULL, "Obj_BlockDweller", Run_BlockDweller, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_DWELLER_LINK_BLOCK, NULL, "Obj_DwellerLinkBlock", Run_DwellerLinkBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CEILING_HOPPER, NULL, "Obj_CeilingHopper", Run_CeilingHopper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_CEILING_HOPPER_WALK, NULL, "Obj_CeilingWalk", Run_CeilingWalk, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DROPPING, NULL, "Obj_Dropping", Run_Dropping, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_RISING_SHOT, NULL, "Obj_RisingShot", Run_RisingShot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_LEAPER, NULL, "Obj_Leaper", Run_Leaper, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_LEAPER_DUST, NULL, "Obj_SpawnDust", Run_SpawnDust, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_BOBBER, NULL, "Obj_Bobber", Run_Bobber, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHARGER, NULL, "Obj_Charger", Run_Charger, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PATROLLER, NULL, "Obj_Patroller", Run_Patroller, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_PICKUP, NULL, "Obj_Pickup", Run_Pickup, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HIDDEN_ITEM, NULL, "Obj_HiddenItem", Run_HiddenItem, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHEST, NULL, "Obj_Chest", Run_Chest, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_DOOR, NULL, "Obj_Door", Run_Door, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_TALKER, NULL, "Obj_Talker", Run_Talker, 0 },
    { DT_CODE_OBJ_FIND_FREE_Y, NULL, "Obj_FindFreeSlotY", Run_FindFreeSlotY, OUT_Y | OUT_C, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_HIT_OTHERS, NULL, "Obj_HitOthers", Run_HitOthers, OUT_C, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SET_MOTION, NULL, "Obj_SetMotionIndex", Run_SetMotionIndex, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_ENEMY_WALK, NULL, "Enemy_Walk", Run_EnemyWalk, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_LAUNCH_BLOCK, NULL, "Obj_LaunchBlock", Run_LaunchBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_RESOLVE_BLOCK, NULL, "Obj_ResolveBlock", Run_ResolveBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BREAK, NULL, "Obj_Break", Run_Break, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SHATTER, NULL, "Obj_Shatter", Run_Shatter, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_DEFEAT, NULL, "Obj_Defeat", Run_Defeat, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_OPEN_BLOCK, NULL, "Obj_OpenBlock", Run_OpenBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BECOME_PICKUP, NULL, "Obj_BecomePickupNow", Run_BecomePickupNow, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BECOME_BLOCK, NULL, "Obj_BecomeBlock", Run_BecomeBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_HIT_BLOCK, NULL, "Obj_HitBlock", Run_HitBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_THROWN_FALL, NULL, "Obj_ThrownFall", Run_ThrownFall, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_THROWN_WALK, NULL, "Obj_ThrownWalk", Run_ThrownWalk, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SHOW_ITEM, NULL, "Obj_ShowItem", Run_ShowItem, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_TAKE, NULL, "Pickup_Take", Run_PickupTake, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_TREASURE, NULL, "Pickup_Treasure", Run_PickupTreasure, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_EXTRA_DOT, NULL, "Pickup_ExtraDot", Run_PickupExtraDot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_HUD_RESTORE_DOT, NULL, "Hud_RestoreDot", Run_RestoreDot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_MONEY_ADD, NULL, "Money_Add", Run_MoneyAdd, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_HP_REFILL, "ducktales.decomp.F812", "Hud_RefillHp", Run_RefillHp, OUT_A | OUT_Y },
    { DT_CODE_MONEY_MAX, "ducktales.decomp.F83F", "Money_Max", Run_MoneyMax, OUT_A | OUT_Y },
};
const int gObjectRoutineCount = sizeof gObjectRoutines / sizeof gObjectRoutines[0];
