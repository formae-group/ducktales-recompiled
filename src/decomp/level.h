/*
 * decomp/level.h - the level routines other decompiled files call
 * (level/, collision.c).
 */
#pragma once
#include <stdint.h>

#include "ducktales/ram.h"

/* Scratch bytes of a probed point, read by the metatile lookup. */
#define gProbeQuadrant RAM_U8(DT_ADDR_STEPS)     /* quadrant: bit 0 right half, bit 1 lower half */
#define gProbeCell     RAM_U8(DT_ADDR_QUEUE_END) /* collision map index: page bit 6, row * 8 + column */
#define gProbeX        RAM_U8(DT_ADDR_PROBE_X)
#define gProbeY        RAM_U8(DT_ADDR_PROBE_Y)

void Map_LoadHeader(uint8_t map);
uint8_t Level_ScreenPointer(uint8_t index);
uint8_t Level_MetatileAt(uint8_t x);
uint8_t Scroll_StreamBlocks(void);
uint8_t Vram_FlushQueue(void);
void Vram_FlushTileEdits(void);
uint8_t Camera_Update(uint8_t x);
void Block_Draw(uint8_t record);
void Tile_EditByAction(uint8_t x, uint8_t y);
void Tile_EditBySlot(uint8_t x, uint8_t y);

/* A cell Level_EditCell changed: the metatile's quadrant of its 32x32 block
 * (bit 0 right, bit 1 lower), the block's index in the collision map and
 * attribute shadow (page bit 6, row * 8 + column), and the collision and
 * palette bits it put there (shifted to the quadrant). */
typedef struct {
    uint8_t quadrant;
    uint8_t cell;
    uint8_t collision;
    uint8_t palette;
} LevelCell;

LevelCell Level_EditCell(uint8_t x, uint8_t page, uint8_t y, const uint8_t metatile[5]);
