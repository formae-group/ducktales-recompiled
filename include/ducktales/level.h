/*
 * ducktales/level.h - the level's map, screen streaming, VRAM queues and
 * collision map (docs/LEVELS.md, docs/RENDERING.md).
 *
 * A land is a grid of screens (gMapGrid, one screen id per cell, $FF = none).
 * A screen is 8x8 blocks of 32x32 px in bank 4; a block is 2x2 metatiles in
 * the tileset bank gTilesetBank; a metatile is 2x2 tiles plus an attribute
 * byte with its palette and collision class. The camera (game.h) streams one
 * block per step into the $0400 queue, which the NMI uploads.
 */
#pragma once
#include <stdint.h>

#include "ducktales/ram.h"

#ifdef __cplusplus
extern "C" {
#endif

#define gMapGrid        Ram_GetU16(DT_ADDR_MAP_GRID, DT_ADDR_MAP_GRID + 1)
#define gStreamPage     RAM_U8(DT_ADDR_STREAM_PAGE)
#define gCameraXPrev    RAM_U8(DT_ADDR_CAMERA_X_PREV)
#define gCameraDelta    RAM_U8(DT_ADDR_CAMERA_DELTA)
#define gScreenData     Ram_GetU16(DT_ADDR_SCREEN_DATA, DT_ADDR_SCREEN_DATA + 1)
#define gBlockAttr      RAM_U8(DT_ADDR_BLOCK_ATTR)
#define gBlockClasses   RAM_U8(DT_ADDR_BLOCK_CLASSES)
#define gTilesetBank    RAM_U8(DT_ADDR_TILESET_BANK)
#define gBank           RAM_U8(DT_ADDR_BANK)
#define gBlanked        RAM_U8(DT_ADDR_BLANKED)
#define gRowScroll      RAM_U8(DT_ADDR_ROW_SCROLL)
#define gScene          RAM_U8(DT_ADDR_SCENE)          /* hypothesis */
#define gSceneScroll    RAM_U8(DT_ADDR_SCENE_SCROLL)   /* hypothesis */
#define gHimalayasOpen  RAM_U8(DT_ADDR_HIMALAYAS_OPEN) /* hypothesis */
#define gVramQueueReady RAM_U8(DT_ADDR_VRAM_QUEUE_READY)
#define gTileEditReady  RAM_U8(DT_ADDR_TILE_EDIT_READY)
#define gVramQueue      (&RAM_U8(DT_ADDR_VRAM_QUEUE))
#define gTileEditQueue  (&RAM_U8(DT_ADDR_TILE_EDIT_QUEUE))
#define gTileBuffer     (&RAM_U8(DT_ADDR_TILE_BUFFER))   /* 4 rows of 4 tiles */
#define gAttrShadow     (&RAM_U8(DT_ADDR_ATTR_SHADOW))   /* 64 bytes per nametable */
#define gCollisionMap   (&RAM_U8(DT_ADDR_COLLISION_MAP)) /* one byte per block, 64 per nametable */

#define MAP_NO_SCREEN 0xFF
#define QUEUE_END     0x80 /* a PPU address high byte with bit 7 ends a queue */

/* The screen id in cell index of the current map's grid (the grid is ROM). */
static inline uint8_t Map_Cell(uint8_t index)
{
    uint16_t addr = (uint16_t)(gMapGrid + index);
    return addr < GAME_RAM_SIZE ? gRam[addr] : Game_ReadRom(addr);
}

#ifdef __cplusplus
}
#endif
