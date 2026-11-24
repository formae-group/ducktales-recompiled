/*
 * devices.h - the console's device registers as decompiled code uses them
 * (docs/DECOMPILATION.md). Every access goes through Decomp_BusWrite,
 * Decomp_BusRead or Decomp_BusLoad, so a routine using these needs OUT_HW in
 * its table entry.
 */
#pragma once
#include <stdint.h>

#include "decomp/decomp.h"

#define PPU_CTRL   0x2000
#define PPU_MASK   0x2001
#define PPU_STATUS 0x2002
#define OAM_ADDR   0x2003
#define PPU_SCROLL 0x2005
#define PPU_ADDR   0x2006
#define PPU_DATA   0x2007
#define APU_BASE   0x4000
#define OAM_DMA    0x4014
#define APU_STATUS 0x4015
#define JOYPAD1    0x4016
#define JOYPAD2    0x4017
#define APU_FRAME  0x4017 /* the frame counter, when written */

/* A read of PPUSTATUS for its side effect: it resets the address latch. */
static inline void Ppu_ResetLatch(void)
{
    Decomp_BusRead(PPU_STATUS);
}

static inline void Ppu_SetCtrl(uint8_t v)
{
    Decomp_BusWrite(PPU_CTRL, v);
}

static inline void Ppu_SetMask(uint8_t v)
{
    Decomp_BusWrite(PPU_MASK, v);
}

static inline void Ppu_SetAddress(uint16_t addr)
{
    Decomp_BusWrite(PPU_ADDR, (uint8_t)(addr >> 8));
    Decomp_BusWrite(PPU_ADDR, (uint8_t)addr);
}

static inline void Ppu_Write(uint8_t v)
{
    Decomp_BusWrite(PPU_DATA, v);
}
