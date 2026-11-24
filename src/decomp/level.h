/*
 * decomp/level.h - the level routines other decompiled files call
 * (level.c, collision.c).
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
