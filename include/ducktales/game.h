/*
 * ducktales/game.h - game-wide state (docs/game-state.md, docs/levels.md).
 */
#pragma once
#include <stdint.h>

#include "ducktales/ram.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Difficulty { DIFFICULTY_EASY = 0, DIFFICULTY_NORMAL = 1, DIFFICULTY_DIFFICULT = 2 } Difficulty;

/* Land Select order; $F6 (verified). 5 while Land Select loads and during the
 * boss scroll, 9 in the ending. */
typedef enum Land {
    LAND_AMAZON = 0,
    LAND_TRANSYLVANIA = 1,
    LAND_AFRICAN_MINES = 2,
    LAND_HIMALAYAS = 3,
    LAND_MOON = 4,
} Land;

#define gPadNew          RAM_U8(DT_ADDR_PAD_NEW)
#define gPadHeld         RAM_U8(DT_ADDR_PAD_HELD)
#define gPad2Held        RAM_U8(DT_ADDR_PAD2_HELD)      /* hypothesis */
#define gDifficulty      RAM_U8(DT_ADDR_DIFFICULTY)
#define gLand            RAM_U8(DT_ADDR_LAND)
#define gMapRow          RAM_U8(DT_ADDR_MAP_ROW)
#define gMapRows         RAM_U8(DT_ADDR_MAP_ROWS)
#define gMapStride       RAM_U8(DT_ADDR_MAP_STRIDE)
#define gCameraScreen    RAM_U8(DT_ADDR_CAMERA_SCREEN)
#define gCameraX         RAM_U8(DT_ADDR_CAMERA_X)
#define gCameraPage      RAM_U8(DT_ADDR_CAMERA_PAGE)
#define gStreamScreen    RAM_U8(DT_ADDR_STREAM_SCREEN)
#define gStreamPos       RAM_U8(DT_ADDR_STREAM_POS)
#define gStreamDir       RAM_U8(DT_ADDR_STREAM_DIR)     /* hypothesis */
#define gGravity         RAM_U8(DT_ADDR_GRAVITY)
#define gPaused          RAM_U8(DT_ADDR_PAUSE)
#define gTickFrames      RAM_U8(DT_ADDR_TICK_FRAMES)    /* frames to the next HUD second */
#define gBossHits        RAM_U8(DT_ADDR_BOSS_HITS)      /* verified for Amazon and Himalayas */
#define gBossFight       RAM_U8(DT_ADDR_BOSS_FIGHT)     /* bit 7 */
#define gBossCheckpoint  RAM_U8(DT_ADDR_BOSS_CHECKPOINT)
#define gFinalArena      RAM_U8(DT_ADDR_FINAL_ARENA)
#define gRespawn         RAM_U8(DT_ADDR_RESPAWN)
#define gTreasures       RAM_U8(DT_ADDR_TREASURES)      /* bits 0-4 verified, 5-6 hypothesis */
#define gProgressA1      RAM_U8(DT_ADDR_PROGRESS_A1)    /* hypothesis */
#define gKeyItems        RAM_U8(DT_ADDR_KEY_ITEMS)      /* bit 1 verified, bit 2 hypothesis */
#define gLandRedirect    RAM_U8(DT_ADDR_LAND_REDIRECT)
#define gMusic           RAM_U8(DT_ADDR_MUSIC)

#ifdef __cplusplus
}
#endif
