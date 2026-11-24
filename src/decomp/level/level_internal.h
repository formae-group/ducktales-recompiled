/*
 * level/level_internal.h - what the files of src/decomp/level/ share: the
 * scratch and stream RAM names, the layout constants, the ROM and bank helpers
 * and the routines one file calls in another. Not for code outside level/.
 */
#pragma once

#include "decomp/decomp.h"
#include "decomp/level.h"
#include "decomp/objcommon.h"
#include "decomp/sound.h"
#include "ducktales/game.h"
#include "ducktales/level.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define P_ PLAYER_SLOT

#define gScratch0     RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch1     RAM_U8(DT_ADDR_SCRATCH1)
#define gScratch2     RAM_U8(DT_ADDR_SCRATCH2)
#define gScratch3     RAM_U8(DT_ADDR_SCRATCH3)
#define gScratch0F    RAM_U8(DT_ADDR_SCRATCH0F)
#define gScratch8     RAM_U8(DT_ADDR_TEMP08) /* tile edit: replacement entry */
#define gScratch9     RAM_U8(DT_ADDR_TEMP09)
#define gScratch0A    RAM_U8(DT_ADDR_TEMP09 + 1)
#define gScratch0B    RAM_U8(DT_ADDR_TEMP09 + 2)
#define gTileEditEnd  RAM_U8(DT_ADDR_TILE_EDIT_END)
#define gChrStream    RAM_U8(DT_ADDR_CHR_STREAM)
#define gChrBank      RAM_U8(DT_ADDR_CHR_BANK)
#define gChrRecord    RAM_U8(DT_ADDR_CHR_RECORD)
#define gChrOffset    RAM_U8(DT_ADDR_CHR_OFFSET)
#define gChrPages     RAM_U8(DT_ADDR_CHR_PAGES)
#define CHR_CHUNK     0x40
#define gSteps        RAM_U8(DT_ADDR_STEPS)
#define gQueueEnd     RAM_U8(DT_ADDR_QUEUE_END)
#define gBlockCell    RAM_U8(DT_ADDR_TARGET_X) /* row * 8 + column of the decoded block */
#define gTargetX      RAM_U8(DT_ADDR_TARGET_X)
#define gTargetPage   RAM_U8(DT_ADDR_TARGET_PAGE)
#define gTargetScreen RAM_U8(DT_ADDR_TARGET_SCREEN)
#define gDrawSteps    RAM_U8(DT_ADDR_TARGET_PAGE) /* steps left of a start draw, minus one */
#define gPointer      Ram_GetU16(DT_ADDR_SCRATCH0, DT_ADDR_SCRATCH1)

#define LAST_BLOCK_ROW    0x38 /* cells at or past this are the half block row at the bottom */
#define SCROLL_EDGE_RIGHT 0xF0 /* Scrooge's X when the camera stops at a row end */
#define SCROLL_EDGE_LEFT  0x10
#define CAMERA_CENTER     0x80
#define STREAM_COLUMNS    0x40 /* steps per screen: 8 block columns of 8 block rows */
#define QUEUE_ENTRY       7    /* a 4-tile entry: address high, low, count - 1, 4 tiles */
#define ATTR_TABLE_HI     0x23
#define ATTR_TABLE_LO     0xC0

#define DEBRIS_BANK 4 /* bank of $BD80 */
#define gEditX      RAM_U8(DT_ADDR_TILE_EDIT_X)
#define gEditY      RAM_U8(DT_ADDR_TILE_EDIT_Y)
#define gScratch4   RAM_U8(DT_ADDR_SCRATCH4)
#define gScratch5   RAM_U8(DT_ADDR_SCRATCH5)
#define gScratch6   RAM_U8(DT_ADDR_SCRATCH5 + 1)
#define gScratch7   RAM_U8(DT_ADDR_SCRATCH5 + 2)
#define ROW_LOCALS  2 /* $26 and $2C */

static inline uint8_t rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

/* $FFDE bank_select: map PRG bank at $8000 and remember it in $E7. */
static inline void Bank_Select(uint8_t bank)
{
    gBank = bank;
    Decomp_BusWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

uint8_t Level_DecodeBlock(void);
uint8_t Vram_EndQueue(unsigned end, uint8_t x);
void Level_DrawStartColumns(void);
void Level_DrawScreen(void);
uint8_t Chr_StreamRow(uint8_t x);
void Level_TileEdit(int slot);
void Row_QueueHeaders(uint8_t row, uint8_t side);
void Row_Draw(void);
void Row_DrawStep(void);
void Row_KeptStep(void);
void Row_LastStep(void);
void Row_DrawHalf(void);
void Row_RightStep(void);
void Row_LeftStep(void);
void Row_Scroll(Regs *r);
void Row_ScrollFrame(Regs *r);
void Row_ScrollStepped(Regs *r);
void Row_ScrollNext(Regs *r);
void Row_ScrollEnd(Regs *r);
void Block_Debris(void);
uint8_t Spawn_TablesOf(uint8_t x);
uint8_t Spawn_Tables(void);
uint8_t Spawn_Reset(void);
void Spawn_TimedAtEdge(uint8_t *xp, uint8_t *yp);
void Spawn_Update(uint8_t *xp, uint8_t *yp);
