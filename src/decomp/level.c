/*
 * level.c - the level's map and screen decoding, the column streaming, the
 * VRAM queue uploads and the camera, decompiled from the fixed bank
 * (docs/levels.md, docs/rendering.md, docs/decompilation.md).
 *
 * The camera follows Scrooge inside a map row and stops at its ends; every 4
 * pixels it moves stream one 32x32 block of the screen at its edge into the
 * $0400 queue, which the NMI uploads (or the producer itself, while the
 * screen is blanked). A block is decoded from the screen data in bank 4 and
 * the metatiles of the tileset bank ($F7) into the tile buffer $03F0, an
 * attribute byte and a collision byte. Every table (maps, screens, blocks,
 * metatiles, queue layout, respawn entries) is read from the loaded ROM.
 *
 * These routines switch PRG banks ($FFDE) and write the PPU, so their
 * contracts have OUT_HW: the C makes those writes with Decomp_BusWrite
 * (decomp.h). The block decoder leaves the tileset bank mapped, as the
 * original does.
 */
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

static uint8_t rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

/* $FFDE bank_select: map PRG bank at $8000 and remember it in $E7. */
static void Bank_Select(uint8_t bank)
{
    gBank = bank;
    Decomp_BusWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
}

/* $C7B0: rows, stride and screen grid of a map (0-4 the lands). */
void Map_LoadHeader(uint8_t map)
{
    Decomp_Enter(DT_CODE_MAP_HEADER, 0, map, 0);
    gMapRows = rom((uint16_t)(DT_CODE_MAP_ROWS + map));
    gMapStride = rom((uint16_t)(DT_CODE_MAP_STRIDE + map));
    Ram_SetU16(
        DT_ADDR_MAP_GRID, DT_ADDR_MAP_GRID + 1,
        (uint16_t)(rom((uint16_t)(DT_CODE_MAP_GRID_LO + map)) | rom((uint16_t)(DT_CODE_MAP_GRID_HI + map)) << 8));
}

/* $CE2C: ($22) = the data of the screen in grid cell index: bank 4,
 * $8000 + id * 72. The products id * 8 and id * 64 it adds are left in the
 * scratch bytes. Returns id >> 5, which the original leaves in X. */
uint8_t Level_ScreenPointer(uint8_t index)
{
    Decomp_Enter(DT_CODE_SCREEN_POINTER, 0, 0, index);
    uint8_t id = Map_Cell(index);
    gScratch0 = (uint8_t)(id >> 2);
    gScratch1 = (uint8_t)(id << 3);
    gScratch2 = (uint8_t)(id >> 5);
    Ram_SetU16(DT_ADDR_SCREEN_DATA, DT_ADDR_SCREEN_DATA + 1, (uint16_t)(DT_SCREEN_BASE + id * 72));
    return gScratch2;
}

/* $CDAF: ($00) = the entry of block gBlockCell of the screen at ($22) in
 * the tileset bank; the block number has a 9th bit per cell after the 64
 * block bytes. Leaves bank 4 mapped. */
static void Level_BlockPointer(void)
{
    Bank_Select(DT_SCREEN_BANK);
    uint16_t screen = gScreenData;
    uint8_t ninthBits = rom((uint16_t)(screen + DT_SCREEN_NINTH_BITS + (gScratch0 & 7)));
    unsigned block = rom((uint16_t)(screen + gBlockCell));
    if (ninthBits & rom((uint16_t)(DT_CODE_NINTH_BIT_MASK + (gBlockCell & 7)))) {
        block |= 0x100;
    }
    Ram_SetU16(DT_ADDR_SCRATCH0, DT_ADDR_SCRATCH1, (uint16_t)(DT_BLOCK_BASE + block * 4));
}

static void Level_DrawMetatile(uint8_t offset, uint8_t metatile)
{
    gTileBuffer[offset] = rom((uint16_t)(DT_METATILE_TILES + metatile));
    gTileBuffer[offset + 1] = rom((uint16_t)(DT_METATILE_TILES + 0x100 + metatile));
    gTileBuffer[offset + 4] = rom((uint16_t)(DT_METATILE_TILES + 0x200 + metatile));
    gTileBuffer[offset + 5] = rom((uint16_t)(DT_METATILE_TILES + 0x300 + metatile));
}

/* $CD24: opened passages drawn over the map (hypothesis): in the Himalayas
 * two blocks of screen $28, in the Moon two block columns of screen $27. */
static uint8_t Level_Substitute(int quadrant, uint8_t metatile)
{
    if (gLand == LAND_MOON) {
        if (!(gProgressA1 & 0x80) || gStreamScreen != 0x27 || gBlockCell < 0x20) {
            return metatile;
        }
        uint8_t column = gStreamPos & 0x38;
        if (column == 0x18 && (quadrant & 1)) {
            return 0xD6;
        }
        if (column == 0x20 && !(quadrant & 1)) {
            return 0xD7;
        }
        return metatile;
    }
    if (gLand != LAND_HIMALAYAS || !gHimalayasOpen || gStreamScreen != 0x28) {
        return metatile;
    }
    if (gBlockCell != 0x21 && gBlockCell != 0x29) {
        return metatile;
    }
    gBlockAttr = 0;
    gBlockClasses = 0;
    return 0x88;
}

/* $CCBD: decode block $26 (row in bits 0-2, column in bits 3-5) of the
 * screen at ($22): the tiles of its four metatiles into the buffer, their
 * palettes into the attribute byte $27 and their collision classes into $28
 * and the collision map. The bottom block row is half a block: its lower
 * quadrants redraw the last metatile (at first the tileset bank number, which
 * the bank switch leaves in the original's X) at the last buffer offset, and
 * the queue never uploads those tiles. Leaves the tileset bank mapped.
 * Returns the block's collision map index, which the original leaves in X. */
static uint8_t Level_DecodeBlock(void)
{
    Decomp_Enter(DT_CODE_DECODE_BLOCK, 0, 0, 0);
    uint8_t pos = gStreamPos;
    gBlockCell = (uint8_t)((pos & 7) << 3 | (pos >> 3 & 7));
    gScratch0 = pos;
    Level_BlockPointer();
    Bank_Select(gTilesetBank);
    uint8_t metatile = gTilesetBank;
    uint16_t block = gPointer;
    gBlockAttr = 0;
    gBlockClasses = 0;
    for (int quadrant = 3; quadrant >= 0; quadrant--) {
        gScratch2 = (uint8_t)quadrant;
        if (quadrant < 2 || gBlockCell < LAST_BLOCK_ROW) {
            gScratch3 = rom((uint16_t)(DT_CODE_QUADRANT_OFFSET + quadrant));
            metatile = rom((uint16_t)(block + quadrant));
            uint8_t attr = rom((uint16_t)(DT_METATILE_ATTR + metatile));
            gScratch0F = (uint8_t)((attr >> 4 & 3) << 2 * quadrant);
            gBlockAttr |= (uint8_t)((attr & 3) << 2 * quadrant);
            gBlockClasses |= gScratch0F;
            if (!gRowScroll) {
                metatile = Level_Substitute(quadrant, metatile);
            }
        }
        Level_DrawMetatile(gScratch3, metatile);
    }
    gScratch2 = 0xFF;
    uint8_t cell = (uint8_t)((gStreamPage & 1) << 6 | gBlockCell);
    gCollisionMap[cell] = gBlockClasses;
    return cell;
}

/* $CDE2: the metatile under a probed point (collision.c): screen page $03
 * of the current row, cell $05, quadrant $04. Keeps the bank mapped. */
uint8_t Level_MetatileAt(uint8_t x)
{
    Decomp_Enter(DT_CODE_METATILE_AT, 0, x, 0);
    uint8_t bank = gBank;
    uint8_t screen = gScratch3;
    uint8_t row = gMapRow;
    while ((int8_t)--row >= 0) {
        screen += gMapStride;
    }
    Level_ScreenPointer(screen);
    uint8_t cell = gBlockCell;
    gBlockCell = gProbeCell & 0x3F;
    gScratch0 = (uint8_t)(gBlockCell >> 3);
    Level_BlockPointer();
    Bank_Select(gTilesetBank);
    gScratch0 = rom((uint16_t)(gPointer + gProbeQuadrant));
    gBlockCell = cell;
    Bank_Select(bank);
    return gScratch0;
}

/* $C139/$C15E: upload a queue to the PPU. Records of address high, low,
 * count - 1 and the bytes, until an address high byte with bit 7 (or the
 * index wraps). Returns the index where it stopped, the original's X. */
static uint8_t Vram_Upload(uint16_t queue)
{
    uint8_t i = 0;
    do {
        uint8_t hi = RAM_U8(queue + i);
        if (hi & QUEUE_END) {
            return i;
        }
        Decomp_BusWrite(DT_PPU_ADDR, hi);
        Decomp_BusWrite(DT_PPU_ADDR, RAM_U8(queue + i + 1));
        uint8_t count = RAM_U8(queue + i + 2);
        do {
            Decomp_BusWrite(DT_PPU_DATA, RAM_U8(queue + i + 3));
            i++;
        } while ((int8_t)--count >= 0);
        i += 3;
    } while (i);
    return i;
}

uint8_t Vram_FlushQueue(void)
{
    Decomp_Enter(DT_CODE_VRAM_FLUSH, 0, 0, 0);
    gVramQueueReady = 0;
    return Vram_Upload(DT_ADDR_VRAM_QUEUE);
}

static void Vram_FlushTileEdits(void)
{
    Decomp_Enter(DT_CODE_TILE_EDIT_FLUSH, 0, 0, 0);
    gTileEditReady = 0;
    Vram_Upload(DT_ADDR_TILE_EDIT_QUEUE);
}

/* $C84E/$C959: close the queue at offset end; upload it now while the
 * screen is blanked, else let the NMI do it. Returns the original's X: the
 * upload's, else x. */
static uint8_t Vram_EndQueue(unsigned end, uint8_t x)
{
    gVramQueue[end] = 0xFF;
    if (gBlanked) {
        return Vram_FlushQueue();
    }
    gVramQueueReady = 0xFF;
    return x;
}

/* $C7FB: the next step in the direction of the last move ($38). A change of
 * direction moves the streamed screen by two instead. */
static void Scroll_Advance(void)
{
    uint8_t step;
    if (gPlayerMoveDir != gStreamDir) {
        gStreamDir = gPlayerMoveDir & 3;
        step = (uint8_t)((gStreamDir & 2) + 1);
    } else {
        step = gStreamDir & 2;
        uint8_t pos = (uint8_t)(gStreamPos + rom((uint16_t)(DT_CODE_STREAM_STEP + step)));
        gStreamPos = pos & 0x3F;
        if (!(pos & 0xC0)) {
            return;
        }
    }
    gStreamPage += rom((uint16_t)(DT_CODE_STREAM_STEP + step));
    gStreamScreen += rom((uint16_t)(DT_CODE_STREAM_STEP + step));
}

/* $C867: queue the decoded block at gQueueEnd: four entries of 4 tiles (one
 * per tile row) and its attribute byte, also kept in the attribute shadow.
 * In nametable 0 the HUD covers block row 0 and the top half of row 1, and
 * the bottom row is half a block. Returns the offset of the attribute
 * entry. */
static uint8_t Scroll_QueueBlock(void)
{
    uint8_t *q = gVramQueue + gQueueEnd;
    uint8_t row = gStreamPos & 7;
    uint8_t hi = (uint8_t)((gStreamPage & 1) << 2 | rom((uint16_t)(DT_CODE_QUEUE_TILE_HI + row)));
    uint8_t lo = (uint8_t)((gStreamPos >> 1 & 0x1C) | rom((uint16_t)(DT_CODE_QUEUE_TILE_LO + row)));
    for (int line = 0; line < 4; line++) {
        q[QUEUE_ENTRY * line] = hi;
        q[QUEUE_ENTRY * line + 1] = (uint8_t)(lo + 0x20 * line);
        q[QUEUE_ENTRY * line + 2] = 3;
    }
    for (int quadrant = 3; quadrant >= 0; quadrant--) {
        gScratch0 = (uint8_t)quadrant;
        uint8_t at = (uint8_t)(rom((uint16_t)(DT_CODE_QUEUE_QUADRANT + quadrant)) + gQueueEnd);
        uint8_t from = rom((uint16_t)(DT_CODE_QUADRANT_OFFSET + quadrant));
        gVramQueue[at + 3] = gTileBuffer[from];
        gVramQueue[at + 4] = gTileBuffer[from + 1];
        gVramQueue[at + QUEUE_ENTRY + 3] = gTileBuffer[from + 4];
        gVramQueue[at + QUEUE_ENTRY + 4] = gTileBuffer[from + 5];
    }
    gScratch0 = 0xFF;
    uint8_t attrAt = 4 * QUEUE_ENTRY;
    if (gLand <= LAND_MOON && !(gStreamPage & 1) && row == 1) {
        for (uint8_t k = 0; k < 2 * QUEUE_ENTRY; k++) {
            gVramQueue[(uint8_t)(gQueueEnd + k)] = gVramQueue[(uint8_t)(gQueueEnd + k) + 2 * QUEUE_ENTRY];
        }
        gBlockAttr &= 0xF0;
        attrAt = 2 * QUEUE_ENTRY;
    } else if (gBlockCell >= LAST_BLOCK_ROW) {
        gBlockAttr &= 0x0F;
        attrAt = 2 * QUEUE_ENTRY;
    }
    uint8_t at = (uint8_t)(attrAt + gQueueEnd);
    gVramQueue[at] = ATTR_TABLE_HI | gVramQueue[at - QUEUE_ENTRY];
    gVramQueue[at + 1] = ATTR_TABLE_LO | gBlockCell;
    gVramQueue[at + 2] = 0;
    gVramQueue[at + 3] = gBlockAttr;
    gAttrShadow[(gStreamPage & 1) << 6 | gBlockCell] = gBlockAttr;
    return at;
}

/* $C830: stream gSteps steps into the queue from gQueueEnd, starting with
 * block $26 of screen $24. A step whose screen is missing, or whose block
 * the HUD covers, queues nothing. Returns the X the original leaves, which
 * the frame loop carries past the camera. */
uint8_t Scroll_StreamBlocks(void)
{
    Decomp_Enter(DT_CODE_STREAM_BLOCKS, 0, 0, 0);
    for (;;) {
        uint8_t x = Level_ScreenPointer(gStreamScreen);
        bool queued = false;
        if (Map_Cell(gStreamScreen) != MAP_NO_SCREEN && gStreamPage < gMapStride) {
            x = Level_DecodeBlock();
            queued = gLand > LAND_MOON || (gStreamPage & 1) || (gStreamPos & 7);
        }
        if (queued) {
            uint8_t at = Scroll_QueueBlock();
            if (!--gSteps) {
                return Vram_EndQueue(at + 4u, x);
            }
            gQueueEnd = (uint8_t)(at + 4);
        } else if (!--gSteps) {
            if (gQueueEnd) {
                return Vram_EndQueue(gQueueEnd, x);
            }
            return x;
        }
        Scroll_Advance();
    }
}

/* $C7C5: one step per 4 pixels the camera moved, counted from the previous
 * camera X so that the steps follow the 4-pixel grid in the scroll
 * direction. Returns the original's X, x when there is nothing to stream. */
static uint8_t Scroll_Stream(uint8_t x)
{
    uint8_t moved = (uint8_t)(gCameraX - gCameraXPrev);
    if (moved & 0x80) {
        moved = (uint8_t)-moved;
    }
    gCameraDelta = moved;
    uint8_t carried = (gPlayerMoveDir & 2) ? (uint8_t)(~gCameraXPrev & 3) : (uint8_t)(gCameraXPrev & 3);
    gSteps = (uint8_t)(carried + moved) >> 2;
    if (!gSteps) {
        return x;
    }
    gQueueEnd = 0;
    Scroll_Advance();
    return Scroll_StreamBlocks();
}

/* $F0C4: the camera moved by gCameraDelta: target X, page and screen.
 * Returns the direction's table index, which the original leaves in X. */
static uint8_t Camera_Target(void)
{
    uint8_t index = gPlayerMoveDir & 2;
    uint8_t carry = rom((uint16_t)(DT_CODE_CAMERA_PAGE_STEP + index));
    unsigned x = (unsigned)gCameraX + gCameraDelta;
    gTargetX = (uint8_t)x;
    gTargetPage = (uint8_t)(gCameraPage + carry + (x >> 8));
    gTargetScreen = (uint8_t)(gCameraScreen + carry + (x >> 8));
    return index;
}

static void Camera_MoveTo(uint8_t x)
{
    gCameraX = x;
    gCameraPage = gTargetPage;
    gCameraScreen = gTargetScreen;
}

/* $F067: scroll right unless the next screen of the row is missing (or a
 * boss fight holds the camera): then the camera stops on the page and
 * Scrooge stops at the right edge. */
static uint8_t Camera_ScrollRight(void)
{
    uint8_t x = Camera_Target();
    if (!(gBossFight & 0x80) && (uint8_t)(gTargetPage + 1) != gMapStride && (uint8_t)(gTargetScreen + 1) != 0 &&
        Map_Cell((uint8_t)(gTargetScreen + 1)) != MAP_NO_SCREEN) {
        Camera_MoveTo(gTargetX);
    } else {
        gCameraX = 0;
        if (gObjects->worldX[P_] > SCROLL_EDGE_RIGHT) {
            gObjects->worldX[P_] = SCROLL_EDGE_RIGHT;
        }
        gCameraPage = gTargetPage;
        gCameraScreen = gTargetScreen;
    }
    return Scroll_Stream(x);
}

/* $F091: scroll left unless the screen there is missing (or before page 0,
 * or a boss fight): then the camera stops and Scrooge stops at the left
 * edge. */
static uint8_t Camera_ScrollLeft(void)
{
    uint8_t x = Camera_Target();
    if (!(gBossFight & 0x80) && !(gTargetPage & 0x80) && Map_Cell(gTargetScreen) != MAP_NO_SCREEN) {
        Camera_MoveTo(gTargetX);
    } else {
        gCameraX = 0;
        if (gObjects->worldX[P_] <= SCROLL_EDGE_LEFT) {
            gObjects->worldX[P_] = SCROLL_EDGE_LEFT;
        }
    }
    return Scroll_Stream(x);
}

/* $EFE8: scenes move the camera by script (one pixel left per frame while
 * $79 bit 7 is set) and push Scrooge back from the screen edges. */
static uint8_t Camera_FollowScript(uint8_t x)
{
    uint16_t camera = (uint16_t)(gCameraPage << 8 | gCameraX);
    if ((gSceneScroll & 0x80) && camera) {
        camera--;
        gCameraX = (uint8_t)camera;
        gCameraPage = (uint8_t)(camera >> 8);
    }
    uint8_t screenX = (uint8_t)(gObjects->worldX[P_] - gCameraX);
    int push = -1;
    if (gPadHeld & DT_PAD_RIGHT) {
        if (screenX >= SCROLL_EDGE_RIGHT) {
            push = 0;
        }
    } else if (gPadHeld & DT_PAD_LEFT) {
        if (screenX < SCROLL_EDGE_LEFT) {
            push = 3;
        }
    }
    if (push >= 0) {
        uint32_t x = (uint32_t)gObjects->worldPage[P_] << 16 | (uint32_t)gObjects->worldX[P_] << 8 |
                     gObjects->xFrac[P_];
        uint32_t by = (uint32_t)rom((uint16_t)(DT_CODE_SCRIPT_PUSH + push + 2)) << 16 |
                      (uint32_t)rom((uint16_t)(DT_CODE_SCRIPT_PUSH + push + 1)) << 8 |
                      rom((uint16_t)(DT_CODE_SCRIPT_PUSH + push));
        x += by;
        gObjects->xFrac[P_] = (uint8_t)x;
        gObjects->worldX[P_] = (uint8_t)(x >> 8);
        gObjects->worldPage[P_] = (uint8_t)(x >> 16);
    }
    if (!(gCameraX | gCameraPage)) {
        return x;
    }
    gPlayerMoveDir = DT_PAD_LEFT;
    return Scroll_Stream(x);
}

/* $EFE2, from the frame loop: keep Scrooge at screen X $80 while the row
 * lets the camera move, then stream the columns it uncovered. Returns the
 * X the original leaves (x when the camera stays): the frame loop keeps it
 * in X past $CF93, so it reaches the code after. */
uint8_t Camera_Update(uint8_t x)
{
    Decomp_Enter(DT_CODE_CAMERA_UPDATE, 0, x, 0);
    if (gScene & 2) {
        return Camera_FollowScript(x);
    }
    if (!(gPlayerMoveDir & 3)) {
        return x;
    }
    gPlayerMoveDir = DT_PAD_RIGHT;
    uint8_t offset = (uint8_t)(gObjects->worldX[P_] - gCameraX - CAMERA_CENTER);
    if (!offset) {
        return x;
    }
    gCameraDelta = offset;
    if (!(offset & 0x80)) {
        return Camera_ScrollRight();
    }
    gPlayerMoveDir = DT_PAD_LEFT;
    return Camera_ScrollLeft();
}

/* $C71D, land load: draw the start screen's columns, with the screens on
 * either side when they exist (tables at $C786: steps, first block, screen
 * offset for none, left, or both). */
static void Level_DrawStartColumns(void)
{
    Decomp_Enter(DT_CODE_DRAW_START, 0, 0, 0);
    gStreamScreen = gCameraScreen;
    Map_LoadHeader(gLand);
    uint8_t layout = 0;
    if (gStreamPage) {
        uint8_t left = (uint8_t)(gStreamScreen - 1);
        if (Map_Cell(left) != MAP_NO_SCREEN) {
            layout++;
            if ((uint8_t)(left - 1) != gMapStride && Map_Cell((uint8_t)(gStreamScreen + 1)) != MAP_NO_SCREEN) {
                layout++;
            }
        }
    }
    gDrawSteps = rom((uint16_t)(DT_CODE_START_STEPS + layout));
    gStreamPos = rom((uint16_t)(DT_CODE_START_FIRST_POS + layout));
    gStreamScreen += rom((uint16_t)(DT_CODE_START_SCREEN + layout));
    gStreamPage = gStreamScreen & 7;
    do {
        gSteps = 1;
        gQueueEnd = 0;
        Scroll_StreamBlocks();
        gStreamPos = (uint8_t)((gStreamPos + 1) & (STREAM_COLUMNS - 1));
        if (!gStreamPos) {
            gStreamScreen++;
            gStreamPage = gStreamScreen & 7;
        }
    } while ((int8_t)--gDrawSteps >= 0);
    gStreamDir = DT_PAD_RIGHT;
    gStreamPos = 0x1F;
}

/* $C78F, Land Select: draw the camera's whole screen. */
static void Level_DrawScreen(void)
{
    Decomp_Enter(DT_CODE_DRAW_SCREEN, 0, 0, 0);
    gStreamScreen = gCameraScreen;
    Map_LoadHeader(gLand);
    gStreamPos = 0;
    do {
        gSteps = 1;
        gQueueEnd = 0;
        Scroll_StreamBlocks();
    } while (++gStreamPos != STREAM_COLUMNS);
}

/* $C42C, called until $50 goes negative at land load, respawn and in the
 * boss arena: stream the sprite CHR of the current row ($0900-$0FFF) 64 bytes
 * per call through the $0400 queue, from the list the row picks (by camera
 * page where the row has two), then load the row's sprite palette 3. */
static uint8_t Chr_StreamRow(uint8_t x)
{
    Decomp_Enter(DT_CODE_ROW_CHR_STREAM, 0, x, 0);
    if (gChrStream & 0x80) {
        return x;
    }
    uint8_t row = (uint8_t)(rom((uint16_t)(DT_CODE_ROW_CHR_BASE + gLand)) + gMapRow);
    if (!gChrStream) {
        uint8_t list = rom((uint16_t)(DT_CODE_ROW_CHR_LIST + row));
        if (list & 0x80) {
            list &= 0x7F;
            if (gCameraPage >= (gLand == LAND_MOON ? 3 : 4)) {
                list++;
            }
        }
        gChrStream++;
        Ram_SetU16(
            DT_ADDR_CHR_LIST, DT_ADDR_CHR_LIST + 1,
            (uint16_t)(rom((uint16_t)(DT_CODE_CHR_LIST_LO + list)) | rom((uint16_t)(DT_CODE_CHR_LIST_HI + list)) << 8));
        gChrRecord = 0;
        RAM_U8(DT_ADDR_CHR_SOURCE) = 0;
        RAM_U8(DT_ADDR_CHR_TARGET) = 0;
    }
    if (!(gChrStream & 2)) {
        gChrStream++;
        uint16_t record = (uint16_t)(Ram_GetU16(DT_ADDR_CHR_LIST, DT_ADDR_CHR_LIST + 1) + gChrRecord);
        uint8_t head = rom(record);
        if (head & 0x80) {
            gChrStream = head;
            uint8_t palette = (uint8_t)(rom((uint16_t)(DT_CODE_ROW_PALETTE + row)) * 4);
            for (int i = 0; i < 4; i++) {
                uint8_t color = rom((uint16_t)(DT_CODE_SPRITE_PALETTES + (uint8_t)(palette + i)));
                RAM_U8(DT_ADDR_PALETTE_SHOWN + 0x1C + i) = color;
                RAM_U8(DT_ADDR_PALETTE + 0x1C + i) = color;
            }
            return 4;
        }
        gChrBank = head >> 4;
        gChrPages = head & 0x0F;
        RAM_U8(DT_ADDR_CHR_SOURCE + 1) = rom((uint16_t)(record + 1));
        RAM_U8(DT_ADDR_CHR_TARGET + 1) = rom((uint16_t)(record + 2));
        gChrRecord += 3;
        gChrOffset = 0;
    }
    Bank_Select(gChrBank);
    uint16_t source = Ram_GetU16(DT_ADDR_CHR_SOURCE, DT_ADDR_CHR_SOURCE + 1);
    for (uint8_t i = 0; i < CHR_CHUNK; i++) {
        gVramQueue[3 + i] = rom((uint16_t)(source + (uint8_t)(gChrOffset + i)));
    }
    gChrOffset += CHR_CHUNK;
    uint16_t target = Ram_GetU16(DT_ADDR_CHR_TARGET, DT_ADDR_CHR_TARGET + 1);
    gVramQueue[2] = CHR_CHUNK - 1;
    gVramQueue[0] = (uint8_t)(target >> 8);
    gVramQueue[1] = (uint8_t)target;
    Ram_SetU16(DT_ADDR_CHR_TARGET, DT_ADDR_CHR_TARGET + 1, (uint16_t)(target + CHR_CHUNK));
    if (!gChrOffset) {
        RAM_U8(DT_ADDR_CHR_SOURCE + 1)++;
        if ((int8_t)--gChrPages < 0) {
            gChrStream--;
        }
    }
    return Vram_EndQueue(3 + CHR_CHUNK, CHR_CHUNK);
}

/* $F574: replace the metatile under the object in slot (its world X and
 * the Y in $0740) by entry $08 of the table at $F653: two 2-tile entries and
 * an attribute entry in the $0440 queue at $5C, and the new collision class
 * and palette in the collision map and attribute shadow (the HUD half of
 * the top attribute bytes is kept out of the queue). */
static void Level_TileEdit(int slot)
{
    Decomp_Enter(DT_CODE_TILE_EDIT, 0, (uint8_t)slot, 0);
    uint8_t y = gObjects->unk_0740[slot];
    uint8_t x = gObjects->worldX[slot];
    uint8_t page = gObjects->worldPage[slot];
    gScratch9 = y;
    gScratch0A = x;
    gScratch0B = page;
    uint8_t *q = gTileEditQueue + gTileEditEnd;
    q[12] = 0;
    q[2] = 1;
    q[7] = 1;
    uint8_t blockRow = y >> 5;
    uint8_t hi = (uint8_t)((page & 1) << 2 | rom((uint16_t)(DT_CODE_TILE_EDIT_HI + blockRow)));
    q[0] = hi;
    q[5] = hi;
    q[10] = hi | 3;
    uint8_t quadrant = (uint8_t)((y >> 3 & 2) | (x >> 4 & 1));
    gTargetPage = quadrant;
    q[1] = (uint8_t)(rom((uint16_t)(DT_CODE_TILE_EDIT_LO + blockRow)) | (x >> 3 & 0x1E) |
                     rom((uint16_t)(DT_CODE_TILE_EDIT_HALF + quadrant)));
    q[6] = q[1] | 0x20;
    uint8_t cell = (uint8_t)((page & 1) << 6 | (y >> 2 & 0x38) | (x >> 5 & 7));
    gBlockCell = cell;
    q[11] = ATTR_TABLE_LO | cell;
    uint8_t metatile = rom((uint16_t)(DT_CODE_TILE_EDIT_METATILE + gScratch8));
    Bank_Select(gTilesetBank);
    q[3] = rom((uint16_t)(DT_METATILE_TILES + metatile));
    q[4] = rom((uint16_t)(DT_METATILE_TILES + 0x100 + metatile));
    q[8] = rom((uint16_t)(DT_METATILE_TILES + 0x200 + metatile));
    q[9] = rom((uint16_t)(DT_METATILE_TILES + 0x300 + metatile));
    q[13] = rom((uint16_t)(DT_METATILE_ATTR + metatile));
    gScratch0A = (uint8_t)((q[13] >> 4 & 3) << 2 * quadrant);
    gScratch0B = (uint8_t)((q[13] & 3) << 2 * quadrant);
    uint8_t keep = rom((uint16_t)(DT_CODE_QUADRANT_KEEP + quadrant));
    gCollisionMap[cell] = (gCollisionMap[cell] & keep) | gScratch0A;
    uint8_t attr = gAttrShadow[cell] = (gAttrShadow[cell] & keep) | gScratch0B;
    q[13] = cell < 0x10 ? attr & 0xF0 : attr;
    q[14] = 0xFF;
}

/* ---- the row scroll's steps ----
 *
 * The row scroll ($C976) runs one step routine a frame through the pointers
 * at $CA2A; an RTS to $C9FB ends each. A block row of the incoming row
 * reaches the screen in two halves: $CAA1 decodes its 8 blocks, queues two
 * of their tile rows and keeps the other two at $0460/$0480, which $CA4E
 * queues on a later frame. $CB5A and $CB6E draw half a block row of the
 * screens beside the camera's. The side ($2F, 0 or 4) picks each table's
 * entry. The step routines run with the scroll's state and keep no other. */

#define gScratch4   RAM_U8(DT_ADDR_SCRATCH4)
#define gScratch5   RAM_U8(DT_ADDR_SCRATCH5)
#define gScratch6   RAM_U8(DT_ADDR_SCRATCH5 + 1)
#define gScratch7   RAM_U8(DT_ADDR_SCRATCH5 + 2)
#define gRowSide    RAM_U8(DT_ADDR_ROW_SCROLL_SIDE)
#define gRowKept    (&RAM_U8(DT_ADDR_ROW_KEPT)) /* two tile rows of 32, queued later */
#define ROW_TILES   0x20
#define ROW_RECORD  (3 + ROW_TILES) /* a tile row record of the queue: address high, low, count - 1, tiles */
#define HALF_RECORD (3 + ROW_TILES / 2)
#define ATTR_SECOND 0x40 /* attribute shadow of the second nametable */
#define LAST_ROW    7    /* the half block row at the bottom */

/* $CB1E: the headers of a block row's queue: two tile rows of 32 from block
 * row row (the lower two with side 0) and its 8 attribute bytes. */
static void Row_QueueHeaders(uint8_t row, uint8_t side)
{
    Decomp_Enter(DT_CODE_ROW_HEADERS, 0, side, row);
    uint8_t hi = rom((uint16_t)(DT_CODE_QUEUE_TILE_HI + row)) | 0x04;
    gVramQueue[0] = hi;
    gVramQueue[ROW_RECORD] = hi;
    gVramQueue[2 * ROW_RECORD] = hi | 0x03;
    uint8_t lo = rom((uint16_t)(DT_CODE_QUEUE_TILE_LO + row)) | rom((uint16_t)(DT_CODE_ROW_ADDR_LO + side));
    gVramQueue[1] = lo;
    gVramQueue[ROW_RECORD + 1] = lo | 0x20;
    gVramQueue[2 * ROW_RECORD + 1] = (uint8_t)(row << 3) | ATTR_TABLE_LO;
    gVramQueue[2] = ROW_TILES - 1;
    gVramQueue[ROW_RECORD + 2] = ROW_TILES - 1;
    gVramQueue[2 * ROW_RECORD + 2] = 7;
    gVramQueue[2 * ROW_RECORD + 3 + 8] = 0xFF;
    gVramQueueReady = 0xFF;
}

/* $CAA1: block row $26 of the camera's screen, decoded into the queue's two
 * tile row records (the half the side's $CC87 offset names) and the kept
 * half; its attribute bytes are merged into the shadow of the second
 * nametable and the queue. Ends with the headers of row $26 & 7. */
static void Row_Draw(void)
{
    Decomp_Enter(DT_CODE_ROW_DRAW, 0, 0, 0);
    Level_ScreenPointer(gCameraScreen);
    gScratch4 = 0;
    gScratch8 = 0;
    for (;;) {
        gStreamPage = 1;
        Level_DecodeBlock();
        gScratch5 = rom((uint16_t)(DT_CODE_ROW_BUFFER_HALF + gRowSide));
        gScratch6 = gScratch5 ^ 0x08;
        gScratch7 = 4;
        do {
            gVramQueue[3 + gScratch4] = gTileBuffer[gScratch5];
            gVramQueue[ROW_RECORD + 3 + gScratch4] = gTileBuffer[gScratch5 + 4];
            gRowKept[gScratch4] = gTileBuffer[gScratch6];
            gRowKept[ROW_TILES + gScratch4] = gTileBuffer[gScratch6 + 4];
            gScratch4++;
            gScratch5++;
            gScratch6++;
        } while (--gScratch7);
        uint8_t cell = gBlockCell | ATTR_SECOND;
        gScratch9 = gAttrShadow[cell] & rom((uint16_t)(DT_CODE_ROW_ATTR_KEEP + gRowSide));
        gAttrShadow[cell] = gBlockAttr;
        gVramQueue[2 * ROW_RECORD + 3 + gScratch8] = (gBlockAttr & rom((uint16_t)(DT_CODE_ROW_ATTR_NEW + gRowSide))) |
                                                     gScratch9;
        gScratch8++;
        uint8_t pos = (uint8_t)(gStreamPos + 8);
        if (pos >= STREAM_COLUMNS) {
            gStreamPos = pos & 7;
            break;
        }
        gStreamPos = pos;
    }
    Row_QueueHeaders(gStreamPos, gRowSide);
}

/* $CA4A: the step that draws block row $26. */
static void Row_DrawStep(void)
{
    Decomp_Enter(DT_CODE_ROW_DRAW_STEP, 0, 0, 0);
    Row_Draw();
}

/* $CA4E: the step that queues the kept half of the last row drawn, with the
 * attribute bytes of block row $26 from the shadow, and moves $26 a row on. */
static void Row_KeptStep(void)
{
    Decomp_Enter(DT_CODE_ROW_KEPT_STEP, 0, 0, 0);
    for (int i = ROW_TILES - 1; i >= 0; i--) {
        gVramQueue[3 + i] = gRowKept[i];
        gVramQueue[ROW_RECORD + 3 + i] = gRowKept[ROW_TILES + i];
    }
    uint8_t cell = (uint8_t)((gStreamPos & 7) << 3 | ATTR_SECOND);
    for (int i = 0; i < 8; i++) {
        gVramQueue[2 * ROW_RECORD + 3 + i] = gAttrShadow[cell + i];
    }
    uint8_t row = gStreamPos;
    gStreamPos = (uint8_t)(row + rom((uint16_t)(DT_CODE_ROW_POS_STEP + gRowSide))) & 7;
    Row_QueueHeaders(row, gRowSide ^ 4);
}

/* $CA8B: at the bottom block row, draws block row 7 as the other side
 * would and moves $26 up; else the step of $CA4E. */
static void Row_LastStep(void)
{
    Decomp_Enter(DT_CODE_ROW_LAST_STEP, 0, 0, 0);
    if (gStreamPos != LAST_ROW) {
        Row_KeptStep();
        return;
    }
    uint8_t side = gRowSide;
    gRowSide = side ^ 4;
    Row_Draw();
    gStreamPos--;
    gRowSide = side;
}

/* $CB94: 4 blocks of the screen at ($22) from $26 on, one block column
 * apart, into four tile row records of 16 at PPU column $07 and their
 * attribute bytes. Nothing is queued when $26 names block row 0. The half
 * block row at the bottom keeps its top tile rows, and row 1 drops the two
 * tile rows that row 0 has, with the attribute halves the screen shows. */
static void Row_DrawHalf(void)
{
    Decomp_Enter(DT_CODE_ROW_DRAW_HALF, 0, 0, 0);
    gScratch4 = 0;
    gScratch5 = 0;
    gScratch6 = 4;
    for (;;) {
        gStreamPage = 0;
        Level_DecodeBlock();
        if (!(gStreamPos & 7)) {
            gStreamPos += 8;
            if (--gScratch6) {
                continue;
            }
            return;
        }
        for (int i = 0; i < 4; i++) {
            for (int k = 0; k < 4; k++) {
                gVramQueue[k * HALF_RECORD + 3 + gScratch4] = gTileBuffer[4 * k + i];
            }
            gScratch4++;
        }
        gAttrShadow[gBlockCell] = gBlockAttr;
        gVramQueue[4 * HALF_RECORD + 3 + gScratch5] = gBlockAttr;
        gScratch5++;
        gStreamPos += 8;
        if (!--gScratch6) {
            break;
        }
    }
    uint8_t row = gStreamPos & 7;
    uint8_t hi = rom((uint16_t)(DT_CODE_QUEUE_TILE_HI + row));
    unsigned lo = rom((uint16_t)(DT_CODE_QUEUE_TILE_LO + row)) | gScratch7;
    for (int k = 0; k < 4; k++) {
        gVramQueue[k * HALF_RECORD] = hi;
        gVramQueue[k * HALF_RECORD + 1] = (uint8_t)lo;
        gVramQueue[k * HALF_RECORD + 2] = 0x0F;
        lo = (lo & 0xFF) + 0x20 + (lo >> 8);
    }
    gVramQueue[4 * HALF_RECORD] = hi | 0x03;
    gVramQueue[4 * HALF_RECORD + 1] = (gBlockCell & 0x3C) | ATTR_TABLE_LO;
    gVramQueue[4 * HALF_RECORD + 2] = 3;
    gVramQueue[4 * HALF_RECORD + 3 + 4] = 0xFF;
    if (row == 1) {
        for (int i = 0; i < 2 * HALF_RECORD + 8; i++) {
            gVramQueue[i] = gVramQueue[2 * HALF_RECORD + i];
        }
        for (int i = 3; i >= 0; i--) {
            gVramQueue[2 * HALF_RECORD + 3 + i] &= 0xF0;
        }
    } else if (row == LAST_ROW) {
        for (int i = LAST_ROW; i >= 0; i--) {
            gVramQueue[2 * HALF_RECORD + i] = gVramQueue[4 * HALF_RECORD + i];
        }
        for (int i = 3; i >= 0; i--) {
            gVramQueue[2 * HALF_RECORD + 3 + i] &= 0x0F;
        }
    }
    gVramQueueReady = 0xFF;
}

/* $CB5A/$CB6E: half a block row of the screen beside the camera's, unless
 * the camera is at that end of the map row or no screen is there: from
 * block column 0 of the screen to the right, at PPU column 0, or from
 * column 4 of the screen to the left, at PPU column 16. */
static void Row_DrawBeside(uint8_t screen, uint8_t column)
{
    gScratch7 = column;
    Level_ScreenPointer(screen);
    Row_DrawHalf();
}

static void Row_RightStep(void)
{
    Decomp_Enter(DT_CODE_ROW_RIGHT_STEP, 0, 0, 0);
    uint8_t screen = (uint8_t)(gCameraScreen + 1);
    if ((uint8_t)(gMapStride - 1) != gCameraPage && Map_Cell(screen) != 0xFF) {
        Row_DrawBeside(screen, 0x00);
    }
    gStreamPos &= 7;
}

static void Row_LeftStep(void)
{
    Decomp_Enter(DT_CODE_ROW_LEFT_STEP, 0, 0, 0);
    uint8_t screen = (uint8_t)(gCameraScreen - 1);
    if (gCameraPage && Map_Cell(screen) != 0xFF) {
        gStreamPos = (gStreamPos & 7) | 0x20;
        Row_DrawBeside(screen, 0x10);
    }
    gStreamPos &= 7;
}

/* ---- the row scroll ----
 *
 * $C976, task code: the row change ($E8C0) hands off to it. Over 60 frames
 * it moves the $2006 address the NMI writes after the sprite-0 split
 * ($ED/$EE, while $E9 is set) and Scrooge's Y, runs one step routine a frame
 * (above, or the sprite CHR stream $C42C) and rebuilds the sprites. It keeps
 * $26 and $2C on the stack throughout, and ends with the object spawner. */

#define gRowFrame   RAM_U8(DT_ADDR_ROW_SCROLL_FRAME)
#define gSplitAddr  RAM_U8(DT_ADDR_ROW_SCROLL_ADDR)
#define gSplitAddrH RAM_U8(DT_ADDR_ROW_SCROLL_ADDR_HI)
#define ROW_FRAMES  0x3C
#define ROW_LOCALS  2 /* $26 and $2C */

static void Row_Call(Regs *r, uint16_t routine)
{
    *r = Decomp_Call(routine, r->a, r->x, r->y);
}

static void Row_SelectBank(Regs *r, uint8_t bank)
{
    Decomp_SelectBank(bank);
    r->a = r->x = bank;
}

static void Row_Yield(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_YIELD, resume, OUT_X | OUT_Y);
}

/* $C99D: one frame: Scrooge's Y and, on even frames, the split address
 * move (wrapping once at the side's frame); then the frame's step routine,
 * which returns to $C9FB. */
static void Row_Frame(Regs *r)
{
    uint8_t side = gRowSide;
    gObjects->y[P_] += rom((uint16_t)(DT_CODE_ROW_Y_STEP + side));
    uint8_t step = gRowFrame & 7;
    Decomp_Push(step);
    if (!(step & 1)) {
        bool wrapped = false;
        if (gRowFrame == rom((uint16_t)(DT_CODE_ROW_WRAP_FRAME + side))) {
            gSplitAddr = rom((uint16_t)(DT_CODE_ROW_ADDR_WRAP + side));
            gSplitAddrH = (gSplitAddrH & 0x24) | rom((uint16_t)(DT_CODE_ROW_ADDR_WRAP + 1 + side));
            wrapped = gSplitAddrH != 0;
        }
        if (!wrapped) {
            unsigned lo = gSplitAddr + rom((uint16_t)(DT_CODE_ROW_ADDR_STEP + side));
            gSplitAddr = (uint8_t)lo;
            gSplitAddrH = (uint8_t)(gSplitAddrH + rom((uint16_t)(DT_CODE_ROW_ADDR_STEP + 1 + side)) + (lo >> 8));
            gObjects->y[P_] += rom((uint16_t)(DT_CODE_ROW_Y_EXTRA + side));
        }
    }
    uint8_t index = (uint8_t)((Decomp_Pull() << 1) + rom((uint16_t)(DT_CODE_ROW_STEP_BASE + side)));
    gScratch0 = rom((uint16_t)(DT_CODE_ROW_STEPS + index));
    gScratch1 = rom((uint16_t)(DT_CODE_ROW_STEPS + 1 + index));
    r->a = (uint8_t)(DT_CODE_ROW_SCROLL_STEPPED - 1);
    r->x = side;
    r->y = index;
    Decomp_Handoff(gPointer, DT_CODE_ROW_SCROLL_STEPPED, OUT_A | OUT_X | OUT_Y);
}

static void Row_Scroll(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL, r->a, r->x, r->y);
    uint8_t side = gPadHeld & 0x04;
    gRowSide = side;
    gRowScroll = 0x0C;
    Decomp_Push(gStreamPos);
    Decomp_Push(gStreamPage);
    gStreamPos = rom((uint16_t)(DT_CODE_ROW_FIRST_POS + side));
    gRowFrame = 0;
    gChrStream = 0;
    r->a = 0;
    r->x = side;
    Row_Call(r, DT_CODE_CLEAR_OBJECTS);
    Row_Call(r, DT_CODE_OAM_CLEAR);
    Row_Yield(DT_CODE_ROW_SCROLL_FRAME);
}

static void Row_ScrollFrame(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_FRAME, r->a, r->x, r->y);
    Row_Frame(r);
}

/* $C9FB: after the step, the sprites, and the next frame. */
static void Row_ScrollStepped(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_STEPPED, r->a, r->x, r->y);
    Row_Call(r, DT_CODE_OAM_CLEAR);
    Row_SelectBank(r, DT_SPR_BANK);
    Row_Call(r, DT_B5_BUILD_OAM);
    Row_Yield(DT_CODE_ROW_SCROLL_NEXT);
}

/* $CA09: the next frame, or the end: $26 and $2C back, the object spawner
 * reset and run. */
static void Row_ScrollNext(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_NEXT, r->a, r->x, r->y);
    if (++gRowFrame != ROW_FRAMES) {
        Row_Frame(r);
        return;
    }
    gStreamPage = Decomp_Pull();
    gStreamPos = Decomp_Pull();
    Row_SelectBank(r, DT_BANK_OBJECTS);
    Row_Call(r, DT_B6_SPAWN_RESET);
    Decomp_Handoff(DT_B6_SPAWN, DT_CODE_ROW_SCROLL_END, 0);
}

/* $CA22: the split address no longer written; the palette uploaded. */
static void Row_ScrollEnd(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_END, r->a, r->x, r->y);
    gRowScroll = 0;
    r->y = 1;
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = r->y;
}

/* ---- block draw ----
 *
 * $D068 queues a block record (the table at $D110, from offset Y) over the
 * screen: a tile pair record of its metatiles' top and bottom tiles, copied
 * down two tile rows at a time; then it throws the four debris pieces of a
 * broken block from bank 4. The message box ($ED05, message $06) and the
 * breaking object at $FD88 use it. */

#define gBlockRecord RAM_U8(DT_ADDR_SCRATCH0) /* offset of the next metatile in the record */
#define gBlockOut    RAM_U8(DT_ADDR_SCRATCH1) /* offset of its tiles in the first record */
#define gBlockFirst  RAM_U8(DT_ADDR_SCRATCH3) /* the record's offset */
#define gBlockCopies RAM_U8(DT_ADDR_TARGET_X)
#define gDebrisY     RAM_U8(DT_ADDR_TARGET_X)
#define gDebrisX     RAM_U8(DT_ADDR_TARGET_X + 1)
#define PAIR_RECORD  7 /* address high, low, count - 1, 4 tiles */
#define DEBRIS       4
#define DEBRIS_BANK  4 /* bank of $BD80 */
#define DEBRIS_STATE (OBJ_STATUS_LIVE | 0x20)
#define SFX_BREAK    0x16

/* bank 4 $BD80: four debris pieces around ($11, $10) in free slots, then the
 * break sound. */
static void Block_Debris(void)
{
    Decomp_Enter(DT_B4_BLOCK_DEBRIS, 0, 0, 0);
    uint8_t slot = 0;
    for (int i = DEBRIS - 1; i >= 0 && Obj_FindFreeSlot(&slot); i--) {
        gObjects->state[slot] = DEBRIS_STATE;
        gObjects->flags[slot] = DEBRIS_STATE | OBJ_FLAG_DELETE_AT_END;
        gObjects->action[slot] = OBJ_ACTION_BREAK;
        gObjects->animFrame[slot] = 0;
        gObjects->hitbox[slot] = 0;
        gObjects->kind[slot] = 0;
        unsigned y = gDebrisY + rom((uint16_t)(DT_B4_DEBRIS_DY + i));
        gObjects->y[slot] = (uint8_t)y;
        gObjects->worldX[slot] = (uint8_t)(gDebrisX + rom((uint16_t)(DT_B4_DEBRIS_DX + i)) + (y >> 8));
        gObjects->worldPage[slot] = gObjects->worldPage[P_];
        gObjects->animTimer[slot] = rom((uint16_t)(DT_B4_DEBRIS_TIMER + i));
    }
    Sound_Request(SFX_BREAK, slot);
}

static void Block_Draw(uint8_t record)
{
    Decomp_Enter(DT_CODE_BLOCK_DRAW, 0, 0, record);
    gBlockRecord = record;
    gBlockFirst = record;
    Bank_Select(gTilesetBank);
    gBlockCopies = rom((uint16_t)(DT_CODE_BLOCK_RECORDS + record));
    uint8_t hi = rom((uint16_t)(DT_CODE_BLOCK_RECORDS + 1 + record));
    uint8_t lo = rom((uint16_t)(DT_CODE_BLOCK_RECORDS + 2 + record));
    gVramQueue[0] = hi;
    gVramQueue[PAIR_RECORD] = hi;
    gVramQueue[1] = lo;
    gVramQueue[PAIR_RECORD + 1] = lo | 0x20;
    gVramQueue[2] = 3;
    gVramQueue[PAIR_RECORD + 2] = 3;
    gBlockOut = 0;
    for (;;) {
        uint8_t metatile = rom((uint16_t)(DT_CODE_BLOCK_RECORDS + 3 + gBlockRecord));
        if (!metatile) {
            break;
        }
        uint8_t i = gBlockOut;
        gVramQueue[3 + i] = rom((uint16_t)(DT_METATILE_TILES + metatile));
        gVramQueue[4 + i] = rom((uint16_t)(DT_METATILE_TILES + 0x100 + metatile));
        gVramQueue[PAIR_RECORD + 3 + i] = rom((uint16_t)(DT_METATILE_TILES + 0x200 + metatile));
        gVramQueue[PAIR_RECORD + 4 + i] = rom((uint16_t)(DT_METATILE_TILES + 0x300 + metatile));
        gBlockOut = (uint8_t)(i + 2);
        if (!++gBlockRecord) {
            break;
        }
    }
    uint8_t y = 0;
    do {
        unsigned low = gVramQueue[1 + y] + 0x40u;
        gVramQueue[2 * PAIR_RECORD + 1 + y] = (uint8_t)low;
        gVramQueue[3 * PAIR_RECORD + 1 + y] = (uint8_t)low | 0x20;
        uint8_t high = (uint8_t)(gVramQueue[y] + (low >> 8));
        gVramQueue[2 * PAIR_RECORD + y] = high;
        gVramQueue[3 * PAIR_RECORD + y] = high;
        for (int k = 0; k < 5; k++, y++) {
            gVramQueue[2 * PAIR_RECORD + 2 + y] = gVramQueue[2 + y];
            gVramQueue[3 * PAIR_RECORD + 2 + y] = gVramQueue[2 + y];
        }
        y = (uint8_t)(y + 9);
    } while (--gBlockCopies);
    gVramQueue[2 * PAIR_RECORD + y] = 0xFF;
    gVramQueueReady = 0xFF;
    uint8_t side = gBlockFirst & 0x02;
    gDebrisY = rom((uint16_t)(DT_CODE_BLOCK_DEBRIS_POS + side));
    gDebrisX = rom((uint16_t)(DT_CODE_BLOCK_DEBRIS_POS + 1 + side));
    Bank_Select(DEBRIS_BANK);
    Block_Debris();
}

/* ---- site adapters ---- */

static void Run_MapLoadHeader(Regs *r)
{
    Map_LoadHeader(r->x);
}

static void Run_StreamBlocks(Regs *r)
{
    r->x = Scroll_StreamBlocks();
}

static void Run_DrawStartColumns(Regs *r)
{
    (void)r;
    Level_DrawStartColumns();
}

static void Run_DrawScreen(Regs *r)
{
    (void)r;
    Level_DrawScreen();
}

static void Run_VramFlush(Regs *r)
{
    r->x = Vram_FlushQueue();
}

static void Run_TileEditFlush(Regs *r)
{
    (void)r;
    Vram_FlushTileEdits();
}

static void Run_ScreenPointer(Regs *r)
{
    r->x = Level_ScreenPointer(r->y);
}

static void Run_DecodeBlock(Regs *r)
{
    r->x = Level_DecodeBlock();
}

static void Run_MetatileAt(Regs *r)
{
    r->a = Level_MetatileAt(r->x);
}

static void Run_CameraUpdate(Regs *r)
{
    r->x = Camera_Update(r->x);
}

static void Run_TileEdit(Regs *r)
{
    Level_TileEdit(r->x);
}

static void Run_ChrStreamRow(Regs *r)
{
    r->x = Chr_StreamRow(r->x);
}

static void Run_BlockDraw(Regs *r)
{
    Block_Draw(r->y);
}

static void Run_BlockDebris(Regs *r)
{
    (void)r;
    Block_Debris();
}

static void Run_RowHeaders(Regs *r)
{
    Row_QueueHeaders(r->y, r->x);
}

static void Run_RowDraw(Regs *r)
{
    (void)r;
    Row_Draw();
}

static void Run_RowDrawStep(Regs *r)
{
    (void)r;
    Row_DrawStep();
}

static void Run_RowKeptStep(Regs *r)
{
    (void)r;
    Row_KeptStep();
}

static void Run_RowLastStep(Regs *r)
{
    (void)r;
    Row_LastStep();
}

static void Run_RowDrawHalf(Regs *r)
{
    (void)r;
    Row_DrawHalf();
}

static void Run_RowRightStep(Regs *r)
{
    (void)r;
    Row_RightStep();
}

static void Run_RowLeftStep(Regs *r)
{
    (void)r;
    Row_LeftStep();
}

const DecompRoutine gLevelRoutines[] = {
    { DT_CODE_MAP_HEADER, "ducktales.decomp.C7B0", "Map_LoadHeader", Run_MapLoadHeader, OUT_X },
    { DT_CODE_STREAM_BLOCKS, "ducktales.decomp.C830", "Scroll_StreamBlocks", Run_StreamBlocks, OUT_X | OUT_HW },
    { DT_CODE_DRAW_START, "ducktales.decomp.C71D", "Level_DrawStartColumns", Run_DrawStartColumns, OUT_HW },
    { DT_CODE_DRAW_SCREEN, "ducktales.decomp.C78F", "Level_DrawScreen", Run_DrawScreen, OUT_HW },
    { DT_CODE_VRAM_FLUSH, "ducktales.decomp.C139", "Vram_FlushQueue", Run_VramFlush, OUT_X | OUT_HW },
    { DT_CODE_TILE_EDIT_FLUSH, "ducktales.decomp.C15E", "Vram_FlushTileEdits", Run_TileEditFlush, OUT_HW },
    { DT_CODE_CAMERA_UPDATE, "ducktales.decomp.EFE2", "Camera_Update", Run_CameraUpdate, OUT_X | OUT_HW },
    { DT_CODE_TILE_EDIT, "ducktales.decomp.F574", "Level_TileEdit", Run_TileEdit, OUT_HW },
    { DT_CODE_SCREEN_POINTER, NULL, "Level_ScreenPointer", Run_ScreenPointer, OUT_X },
    { DT_CODE_DECODE_BLOCK, NULL, "Level_DecodeBlock", Run_DecodeBlock, OUT_X },
    { DT_CODE_METATILE_AT, NULL, "Level_MetatileAt", Run_MetatileAt, OUT_A | OUT_X },
    { DT_CODE_ROW_CHR_STREAM, "ducktales.decomp.C42C", "Chr_StreamRow", Run_ChrStreamRow, OUT_X | OUT_HW },
    { DT_CODE_ROW_HEADERS, NULL, "Row_QueueHeaders", Run_RowHeaders, 0 },
    { DT_CODE_ROW_DRAW, NULL, "Row_Draw", Run_RowDraw, OUT_HW },
    { DT_CODE_ROW_DRAW_HALF, NULL, "Row_DrawHalf", Run_RowDrawHalf, OUT_HW },
    { DT_CODE_ROW_DRAW_STEP, "ducktales.decomp.CA4A", "Row_DrawStep", Run_RowDrawStep, OUT_HW },
    { DT_CODE_ROW_KEPT_STEP, "ducktales.decomp.CA4E", "Row_KeptStep", Run_RowKeptStep, 0 },
    { DT_CODE_ROW_LAST_STEP, "ducktales.decomp.CA8B", "Row_LastStep", Run_RowLastStep, OUT_HW },
    { DT_CODE_ROW_RIGHT_STEP, "ducktales.decomp.CB5A", "Row_RightStep", Run_RowRightStep, OUT_HW },
    { DT_CODE_ROW_LEFT_STEP, "ducktales.decomp.CB6E", "Row_LeftStep", Run_RowLeftStep, OUT_HW },
    { DT_CODE_BLOCK_DRAW, "ducktales.decomp.D068", "Block_Draw", Run_BlockDraw, OUT_HW },
    { DT_B4_BLOCK_DEBRIS, NULL, "Block_Debris", Run_BlockDebris, 0, .bank = DEBRIS_BANK },
    { DT_CODE_ROW_SCROLL, "ducktales.decomp.C976", "Row_Scroll", Row_Scroll, 0, true, 0, false },
    { DT_CODE_ROW_SCROLL_FRAME, NULL, "Row_ScrollFrame", Row_ScrollFrame, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_STEPPED, NULL, "Row_ScrollStepped", Row_ScrollStepped, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_NEXT, NULL, "Row_ScrollNext", Row_ScrollNext, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_END, NULL, "Row_ScrollEnd", Row_ScrollEnd, 0, true, 0, true },
};
const int gLevelRoutineCount = sizeof gLevelRoutines / sizeof gLevelRoutines[0];
