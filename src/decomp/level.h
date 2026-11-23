/*
 * decomp/level.h - the level routines other decompiled files call
 * (level.c, collision.c).
 */
#pragma once
#include <stdint.h>

#include "ducktales/ram.h"

/* Scratch bytes of a probed point, read by the metatile lookup. */
#define gProbeQuadrant RAM_U8(DT_ADDR_STEPS)       /* quadrant: bit 0 right half, bit 1 lower half */
#define gProbeCell     RAM_U8(DT_ADDR_QUEUE_END)   /* collision map index: page bit 6, row * 8 + column */
#define gProbeX        RAM_U8(DT_ADDR_PROBE_X)
#define gProbeY        RAM_U8(DT_ADDR_PROBE_Y)

void Map_LoadHeader(uint8_t map);
void Level_ScreenPointer(uint8_t index);
uint8_t Level_MetatileAt(void);
void Scroll_StreamBlocks(void);
void Vram_FlushQueue(void);
void Camera_Update(void);
