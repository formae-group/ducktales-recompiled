/*
 * flow_internal.h - what the modules of the game flow (src/decomp/flow/,
 * docs/DECOMPILATION.md "Task code") share: the RAM fields and constants more
 * than one of them uses, the helpers for calls, hand-offs and yields, and the
 * routines one module's C reaches in another or the routine table names. It
 * owns no settings or options.
 *
 * The main task runs the title, Land Select, the land load, the frame loop
 * and the ending. Its task code returns or hands off to another routine
 * (task_yield, the fades, the PPU loaders); the RTS observer resumes the C
 * at the address after the original JSR. The reset's VBlank waits and the
 * credits' raster split poll the PPU (DecompRoutine.polls: their bus
 * operations at the original's cycles).
 * Values the original keeps across a frame go on the 6502 stack.
 * "r" holds the registers of the original at that point.
 */
#pragma once

#include "decomp/decomp.h"
#include "decomp/devices.h"
#include "decomp/motion.h"
#include "decomp/objcommon.h"
#include "decomp/player.h"
#include "decomp/sound.h"
#include "decomp/system.h"

#include <string.h>

#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define P_                  PLAYER_SLOT
#define MUSIC_BOSS          0x06
#define LAND_ARENA          5
#define SPLIT_ON            0x40
#define ROW_SCROLL_ADDR     0xC0
#define SCROOGE_RESPAWN     0xA0 /* $0700 of Scrooge after a respawn */
#define NT_FILL_ADDR        0x20 /* $C203: nametables from $2000 */
#define NT_FILL_TILE        0x24
#define ALL_TREASURES       0x1F
#define SFX_CURSOR          0x2F
#define ENDING_STREAM_STEPS 0x40
#define gScratch0           RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch(i)         RAM_U8(DT_ADDR_SCRATCH0 + (i))
#define gStreamSteps        RAM_U8(DT_ADDR_SCRATCH4)
#define gStreamQueueEnd     RAM_U8(DT_ADDR_SCRATCH5)
#define gVramPending        RAM_U8(DT_ADDR_VRAM_PENDING)
#define gVramQueueReady     RAM_U8(DT_ADDR_VRAM_QUEUE_READY)
#define gStreamPage         RAM_U8(DT_ADDR_STREAM_PAGE)
#define gStreamRequest      RAM_U8(DT_ADDR_MOVE_DIR)
#define gHeldSlot           RAM_U8(DT_ADDR_HELD_SLOT)
#define gRowChrState        RAM_U8(DT_ADDR_ROW_CHR_STATE)
#define gLandLeft           RAM_U8(DT_ADDR_LAND_LEFT)
#define gHudMoneyDirty      RAM_U8(DT_ADDR_HUD_MONEY_DIRTY)
#define gHudTotalDirty      RAM_U8(DT_ADDR_HUD_TOTAL_DIRTY)
#define gScene              RAM_U8(DT_ADDR_SCENE)
#define gEnding             RAM_U8(DT_ADDR_ENDING)
#define gNoSpriteDma        RAM_U8(DT_ADDR_NO_SPRITE_DMA)
#define gSpritePhase        RAM_U8(DT_ADDR_SPR_PHASE)
#define gRowScroll          RAM_U8(DT_ADDR_ROW_SCROLL)
#define gRowScrollAddr      RAM_U8(DT_ADDR_ROW_SCROLL_ADDR)
#define gHudSplit           RAM_U8(DT_ADDR_HUD_SPLIT)
#define gPrevCameraX        RAM_U8(DT_ADDR_PREV_CAMERA_X)
#define gSprite0Y           RAM_U8(DT_ADDR_SPRITE0_Y)
#define gHudGate            RAM_U8(DT_ADDR_HUD_GATE)
#define gSelectMessage      RAM_U8(DT_ADDR_SELECT_MESSAGE)
#define gTimeDigit(i)       RAM_U8(DT_ADDR_TIME_DIGITS + 4 * (i))

static inline uint8_t Rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

static inline void Call(Regs *r, uint16_t routine)
{
    *r = Decomp_Call(routine, r->a, r->x, r->y);
}

static inline void SelectBank(Regs *r, uint8_t bank)
{
    Decomp_SelectBank(bank);
    r->a = r->x = bank;
}

static inline void Jsr(uint16_t routine, uint16_t resume, uint8_t uses)
{
    Decomp_Handoff(routine, resume, uses);
}

static inline void Jump(uint16_t target, uint8_t uses)
{
    Decomp_Handoff(target, 0, uses);
}

static inline void Yield(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_YIELD, resume, OUT_X | OUT_Y);
}

/* JSR from task code to task code in C: true when the callee returned. */
static inline bool Gosub(void (*routine)(Regs *), Regs *r, uint16_t resume)
{
    Decomp_PushReturn(resume);
    routine(r);
    if (Decomp_HandedOff()) {
        return false;
    }
    Decomp_PullReturn();
    return true;
}

/* The sprite list rebuilt with the NMI's sprite DMA held off. */
static inline void BuildSprites(Regs *r, bool clear)
{
    gNoSpriteDma++;
    if (clear) {
        Call(r, DT_CODE_OAM_CLEAR);
    }
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    gNoSpriteDma--;
}

static inline void SoundRequest(Regs *r, uint8_t id)
{
    Sound_Request(id, r->x);
    r->a = id;
}

static inline void MusicRequest(Regs *r, uint8_t id)
{
    Music_Request(id, r->x);
    r->a = id;
}

static inline void Copy(uint16_t dst, uint16_t src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        gRam[dst + i] = gRam[src + i];
    }
}

static inline void CopyRom(uint16_t dst, uint16_t src, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        gRam[dst + i] = Rom((uint16_t)(src + i));
    }
}

/* flow_frame.c */
void Text_Lines(Regs *r);
void Text_LineNext(Regs *r);
void PauseText(Regs *r);
void PauseText_Framed(Regs *r);
void FrameLoop(Regs *r);
void FrameLoop_Paused(Regs *r);
void FrameLoop_AfterPlayer(Regs *r);
void FrameLoop_AfterObjects(Regs *r);
void FrameLoop_AfterCamera(Regs *r);
void FrameLoop_AfterBossTrigger(Regs *r);
void FrameLoop_AfterDeath(Regs *r);
void FrameLoop_Sprites(Regs *r);
void FrameLoop_AfterSpawn(Regs *r);
void FrameLoop_Next(Regs *r);
void Reset(Regs *r);
void Reset_WaitVblank(Regs *r);
void Reset_WaitVblankEnd(Regs *r);
void Reset_SecondNametable(Regs *r);
void Reset_CreateMainTask(Regs *r);
void Reset_StartScheduler(Regs *r);
void BossTrigger(Regs *r);
void BossTrigger_Streamed(Regs *r);
void BossTrigger_Next(Regs *r);

/* flow_title.c */
void MainTask(Regs *r);
void Title_Start(Regs *r);
void Title_Faded(Regs *r);
void Title_Black(Regs *r);
void Title_ScreenOff(Regs *r);
void Title_Filled(Regs *r);
void Title_Strings(Regs *r);
void Title_Chr(Regs *r);
void Title_Shown(Regs *r);
void Title_FadeIn(Regs *r);
void Title_Lit(Regs *r);
void Title_Ready(Regs *r);
void Title_Loop(Regs *r);
void Title_Frame(Regs *r);
void Title_FrameDone(Regs *r);

/* flow_select.c */
void Select_Message(Regs *r);
void Select_ResultMessage(Regs *r);
void Records_Consumed(Regs *r);
void BankTransfer(Regs *r);
void Total_Max(Regs *r);
void Ending_BankTransfer(Regs *r);
void BankTransfer_Next(Regs *r);
void WaitA(Regs *r);
void WaitA_Next(Regs *r);
void LandSelect(Regs *r);
void LandSelect_Faded(Regs *r);
void LandSelect_ScreenOff(Regs *r);
void LandSelect_Chr(Regs *r);
void LandSelect_Drawn(Regs *r);
void LandSelect_Shown(Regs *r);
void LandSelect_FadeIn(Regs *r);
void LandSelect_Lit(Regs *r);
void LandSelect_Waited(Regs *r);
void LandSelect_Banked(Regs *r);
void LandSelect_FinalWait(Regs *r);
void LandSelect_FinalScene(Regs *r);
void LandSelect_FinalRead(Regs *r);
void LandSelect_ResultRead(Regs *r);
void LandSelect_Cursor(Regs *r);
void LandSelect_Next(Regs *r);

/* flow_land.c */
void Land_LoadPalette(Regs *r, uint8_t set);
void Land_ApplyTables(Regs *r);
void LandLoad(Regs *r);
void LandLoad_Faded(Regs *r);
void LandLoad_Black(Regs *r);
void LandLoad_ScreenOff(Regs *r);
void LandLoad_Filled(Regs *r);
void LandLoad_Drawn(Regs *r);
void LandLoad_Strings(Regs *r);
void LandLoad_Chr(Regs *r);
void LandLoad_Redrawn(Regs *r);
void LandLoad_RowChr(Regs *r);
void LandLoad_Shown(Regs *r);
void LandLoad_FadeIn(Regs *r);
void LandLoad_Done(Regs *r);

/* flow_hud.c */
void Timer_Tick(Regs *r);
void Hud_RefreshMoney(Regs *r);
void Hud_RefreshTotal(Regs *r);
void Hud_Reset(Regs *r);

/* flow_death.c */
void Respawn_Drawn(Regs *r);
void Respawn_PlaceAt(Regs *r, uint8_t entry);
void Respawn_Place(Regs *r);
void DeathPause(Regs *r);
void DeathPause_Next(Regs *r);
void GameOver(Regs *r);
void GameOver_Next(Regs *r);
void LandExit(Regs *r);
void LandExit_Next(Regs *r);
void DeathOrWarp(Regs *r);
void Death_Fade(Regs *r);
void Death_Blank(Regs *r);
void Death_Place(Regs *r);
void Death_Placed(Regs *r);
void Death_RowChr(Regs *r);
void Death_Shown(Regs *r);
void Death_FadeIn(Regs *r);
void Death_Resume(Regs *r);
void Death_Spawned(Regs *r);
void Death_Done(Regs *r);

/* flow_scenes.c */
void Scene(Regs *r);
void Scene_Faded(Regs *r);
void Scene_ScreenOff(Regs *r);
void Scene_Chr(Regs *r);
void Scene_Placed(Regs *r);
void Scene_Shown(Regs *r);
void Scene_FadeIn(Regs *r);
void Scene_Done(Regs *r);
void SpritesFrame(Regs *r);
void SpritesFrame_Next(Regs *r);
void FinalTrip(Regs *r);
void FinalTrip_Next(Regs *r);
void FinalTrip_Dark(Regs *r);
void FinalTrip_Flashed(Regs *r);
void Mode15_Redraw(Regs *r);
void Mode15_RowChr(Regs *r);
void Mode15_Streamed(Regs *r);
void Mode15_Next(Regs *r);
void Mode15_ArenaChr(Regs *r);
void Mode15_Spawned(Regs *r);

/* flow_ending.c */
void EndingObjects_Place(Regs *r);
void EndingObjects(Regs *r);
void EndingObjects_Hop(Regs *r);
void EndingObjects_Walk(Regs *r);
void EndingObjects_Wait(Regs *r);
void EndingObjects_Exit(Regs *r);
void EndingObjects_Done(Regs *r);
void EndingObjects_Update(Regs *r);
void Ending(Regs *r);
void Ending_Banked(Regs *r);
void Ending_Waited(Regs *r);
void Ending_Shown(Regs *r);
void Ending_Split(Regs *r);
void Ending_Second(Regs *r);
void Ending_OverNext(Regs *r);
void EndingScreen(Regs *r);
void EndingScreen_Faded(Regs *r);
void EndingScreen_Black(Regs *r);
void EndingScreen_ScreenOff(Regs *r);
void EndingScreen_Filled(Regs *r);
void EndingScreen_Palette(Regs *r);
void EndingScreen_Streamed(Regs *r);
void EndingScreen_NextString(Regs *r);
void EndingScreen_Chr(Regs *r);
void EndingScreen_Shown(Regs *r);
void EndingScreen_Next(Regs *r);
void EndingScreen_Lit(Regs *r);

/* flow_table.c */
void Run_RespawnEntry(Regs *r);
