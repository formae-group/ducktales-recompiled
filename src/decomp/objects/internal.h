/*
 * objects/internal.h - what the files of the object system share
 * (src/decomp/objects/, docs/DECOMPILATION.md "Objects"): the sound ids and
 * RAM names the handlers use, the small helpers every family calls, and the
 * routines one file calls in another.
 */
#pragma once
#include "decomp/collision.h"
#include "decomp/decomp.h"
#include "decomp/level.h"
#include "decomp/motion.h"
#include "decomp/objcommon.h"
#include "decomp/player.h"
#include "decomp/sound.h"
#include "decomp/system.h"

#include "ducktales/events.h"
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
#define SFX_LAUNCH       0x28
#define SFX_BOSS_SHOTS   0x1C
#define SFX_CART         0x33

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
#define gRowScroll        RAM_U8(DT_ADDR_ROW_SCROLL)
#define gRowScrollAddr    RAM_U8(DT_ADDR_ROW_SCROLL_ADDR)
#define gSprPhase         RAM_U8(DT_ADDR_SPR_PHASE)
#define SpawnTable(i) \
    ((uint16_t)(RAM_U8(DT_ADDR_SPAWN_TABLES + 2 * (i)) | RAM_U8(DT_ADDR_SPAWN_TABLES + 2 * (i) + 1) << 8))
#define gPaletteShown(i) RAM_U8(DT_ADDR_PALETTE_SHOWN + (i))

static inline uint8_t Rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

static inline bool OnScreen(uint8_t x)
{
    return gObjects->flags[x] & OBJ_FLAG_ON_SCREEN;
}

static inline uint8_t Step(uint8_t x)
{
    return gObjects->state[x] & OBJ_STATE_STEP;
}

static inline void ResetAnim(uint8_t x)
{
    gObjects->animTimer[x] = 0;
    gObjects->animFrame[x] = 0;
}

/* $F2A1 with the carry it leaves: true when Y did not borrow. */

static inline bool Obj_ApplyVelocityYCarry(uint8_t x)
{
    bool carry = (uint16_t)(gObjects->y[x] << 8 | gObjects->yFrac[x]) >= Obj_GetVelocityY(x);
    Obj_ApplyVelocityY(x);
    return carry;
}

/* $F234 / $F252 with the carry they leave: true when the move left the
 * world (page $FF to 0 moving right; for the left move the original's carry
 * is the opposite). */

static inline bool Obj_MoveRightOff(uint8_t x)
{
    uint8_t page = gObjects->worldPage[x];
    Obj_MoveRight(x);
    return page == 0xFF && gObjects->worldPage[x] == 0;
}

static inline bool Obj_MoveLeftOff(uint8_t x)
{
    uint8_t page = gObjects->worldPage[x];
    Obj_MoveLeft(x);
    return page == 0 && gObjects->worldPage[x] == 0xFF;
}

static inline void SetCarry(Regs *r, bool carry)
{
    r->p = (uint8_t)(carry ? r->p | P_C : r->p & ~P_C);
}

/* blocks.c */
bool Obj_HitOthers(uint8_t x);
void Obj_Break(uint8_t x);
void Obj_BecomePickupNow(uint8_t x);
void Obj_HitBlock(uint8_t x, uint8_t y);
void Obj_KickBlock(uint8_t x, uint8_t y);
void Obj_Block(uint8_t x, uint8_t y);
void Obj_Thrown(uint8_t x);

/* items.c */
void Hud_RestoreDot(void);
void Obj_Pickup(uint8_t x);
void Obj_ShowItem(uint8_t x, uint8_t item);
void Obj_HiddenItem(uint8_t x);
void Obj_Chest(uint8_t x);
void Obj_ChestOpening(uint8_t x);
void Obj_ChestSlider(uint8_t x);
void Obj_ChestHealth(uint8_t x);

/* enemies.c */
void Obj_Defeated(uint8_t x);
void Obj_Defeat(uint8_t x);
void Obj_SetMotionIndex(uint8_t x, uint8_t index);
bool Enemy_Beaten(uint8_t x, uint8_t hit_pose);
uint8_t Enemy_Walk(uint8_t x);
bool Obj_FindFreeSlotY(uint8_t *slot);

/* props.c */
void Obj_Lift(uint8_t x);
void Obj_Door(uint8_t x);
void Obj_Talker(uint8_t x);
void Obj_Swing(uint8_t x);
void Obj_KeySpot(uint8_t x);
void Obj_MovingPlatform(uint8_t x);
void Obj_PathOpener(uint8_t x);
void Obj_Visitor(uint8_t x);
void Obj_Cart(uint8_t x);

/* walkers.c */
void Obj_Hopper(uint8_t x);
void Obj_Walker(uint8_t x);
void Obj_Thrower(uint8_t x);
void Obj_Shot(uint8_t x);
void Obj_Watcher(uint8_t x);
void Obj_Crawler(uint8_t x);
void Obj_Patroller(uint8_t x);
void Obj_Charger(uint8_t x);
void Obj_Leaper(uint8_t x);
void Obj_Pacer(uint8_t x);
void Obj_LedgeWalker(uint8_t x);
void Obj_Bouncer(uint8_t x);
void Obj_BlockKicker(uint8_t x);

/* thieves.c */
void Obj_Pickpocket(uint8_t x);
void Obj_Thief(uint8_t x);

/* flyers.c */
void Obj_Flyer(uint8_t x);
void Obj_Dangler(uint8_t x);
void Obj_Swooper(uint8_t x);
void Obj_Bobber(uint8_t x);
void Obj_RisingShot(uint8_t x);
void Obj_Dropping(uint8_t x);
void Obj_CeilingHopper(uint8_t x);
void Obj_Pouncer(uint8_t x);
void Obj_PitJumper(uint8_t x);
void Obj_HopWalker(uint8_t x);

/* hazards.c */
void Obj_Snapper(uint8_t x);
void Obj_Dropper(uint8_t x);
void Obj_BlockDweller(uint8_t x);
void Obj_Boulder(uint8_t x);
void Obj_Roller(uint8_t x);
void Obj_TreatSpout(uint8_t x);
void Obj_Treat(uint8_t x);

/* bosses.c */
bool Boss_Hit(uint8_t *x);
void Obj_MinesBoss(uint8_t x);
void Obj_HimalayasBoss(uint8_t x);
void Obj_MoonBoss(uint8_t x, bool carry);
void Obj_Target(uint8_t x);
void Obj_Launcher(uint8_t x);
void Obj_AmazonBoss(uint8_t x, bool carry);
void Obj_TransylvaniaBoss(uint8_t x);

/* scenes.c */
void Obj_LaunchpadRide(uint8_t x);
void Obj_SkyFlyer(uint8_t x);
void Obj_SkyRope(uint8_t x);
void Obj_FinalStandIn(uint8_t x, bool carry);
void Obj_StandInBeaten(uint8_t x);
void Obj_Climber(uint8_t x);
void Obj_HitTalker(uint8_t x);
void Obj_FinalTreasure(uint8_t x);
