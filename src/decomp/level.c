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

/* $DD4E: place Scrooge and the camera from respawn/warp entry $7E & $1F
 * (9 bytes at $DDC1: row, camera X and page, steps to draw, streamed
 * screen, first block, Scrooge's Y, X and page | screen row) and draw the
 * columns around him. */
static void Level_PlaceFromEntry(void)
{
    Decomp_Enter(DT_CODE_PLACE_FROM_ENTRY, 0, 0, 0);
    uint8_t index = gRespawn & 0x1F;
    gScratch0 = index;
    uint16_t entry = (uint16_t)(DT_CODE_RESPAWN_ENTRIES + (uint8_t)(index * 9));
    gMapRow = rom(entry);
    gCameraX = rom((uint16_t)(entry + 1));
    gCameraPage = rom((uint16_t)(entry + 2));
    gDrawSteps = rom((uint16_t)(entry + 3));
    gStreamScreen = rom((uint16_t)(entry + 4));
    gStreamPage = gStreamScreen & 7;
    gStreamPos = rom((uint16_t)(entry + 5));
    gObjects->y[P_] = rom((uint16_t)(entry + 6));
    gObjects->worldX[P_] = rom((uint16_t)(entry + 7));
    uint8_t screen = rom((uint16_t)(entry + 8));
    gObjects->worldPage[P_] = screen & 7;
    gCameraScreen = (uint8_t)((screen & 0xF8) | gCameraPage);
    for (;;) {
        gSteps = 1;
        gQueueEnd = 0;
        if (Map_Cell(gStreamScreen) != MAP_NO_SCREEN) {
            Scroll_StreamBlocks();
        }
        if ((int8_t)--gDrawSteps < 0) {
            return;
        }
        gStreamPos = (uint8_t)((gStreamPos + 1) & (STREAM_COLUMNS - 1));
        if (!gStreamPos) {
            gStreamScreen++;
            gStreamPage++;
        }
    }
}

/* $C42C, called until $50 goes negative at land load, respawn and in the
 * boss arena: stream the sprite CHR of the current row ($0900-$0FFF) 64 bytes
 * per call through the $0400 queue, from the list the row picks (by camera
 * page where the row has two), then load the row's sprite palette 3. */
static void Chr_StreamRow(void)
{
    Decomp_Enter(DT_CODE_ROW_CHR_STREAM, 0, 0, 0);
    if (gChrStream & 0x80) {
        return;
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
                RAM_U8(DT_ADDR_PALETTE + 0x1C + i) = color;
                RAM_U8(DT_ADDR_PALETTE_MASTER + 0x1C + i) = color;
            }
            return;
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
    Vram_EndQueue(3 + CHR_CHUNK, CHR_CHUNK);
}

/* $CF93, every frame after the camera: the boss room starts when Scrooge
 * reaches the land's boss row and page (his X taken 8 pixels ahead in the
 * scroll direction). In Transylvania without the other treasures but with
 * the Mines' one it sends him back to Land Select instead. The scroll into
 * the arena waits for frames, so it runs as the original. */
static void Boss_CheckTrigger(void)
{
    Decomp_Enter(DT_CODE_BOSS_TRIGGER, 0, 0, 0);
    uint8_t land = gLand;
    if (gMapRow != rom((uint16_t)(DT_CODE_BOSS_ROW + land))) {
        return;
    }
    uint8_t dir = rom((uint16_t)(DT_CODE_BOSS_SCROLL + land)) & 2;
    uint16_t ahead = (uint16_t)(rom((uint16_t)(DT_CODE_BOSS_LOOK_AHEAD + dir)) |
                                rom((uint16_t)(DT_CODE_BOSS_LOOK_AHEAD + dir + 1)) << 8);
    if ((uint8_t)((Obj_GetWorldX(P_) + ahead) >> 8) != rom((uint16_t)(DT_CODE_BOSS_PAGE + land))) {
        return;
    }
    if (land == LAND_TRANSYLVANIA && (gTreasures & 0x1F) != 0x1F && (gTreasures & 0x02)) {
        gLandRedirect = 0x80;
        return;
    }
    Decomp_RunOriginal();
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

static void Run_PlaceFromEntry(Regs *r)
{
    (void)r;
    Level_PlaceFromEntry();
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

static void Run_BossCheckTrigger(Regs *r)
{
    (void)r;
    Boss_CheckTrigger();
}

static void Run_TileEdit(Regs *r)
{
    Level_TileEdit(r->x);
}

static void Run_ChrStreamRow(Regs *r)
{
    (void)r;
    Chr_StreamRow();
}

const DecompRoutine gLevelRoutines[] = {
    { DT_CODE_MAP_HEADER, "ducktales.decomp.C7B0", "Map_LoadHeader", Run_MapLoadHeader, OUT_X },
    { DT_CODE_STREAM_BLOCKS, "ducktales.decomp.C830", "Scroll_StreamBlocks", Run_StreamBlocks, OUT_X | OUT_HW },
    { DT_CODE_DRAW_START, "ducktales.decomp.C71D", "Level_DrawStartColumns", Run_DrawStartColumns, OUT_HW },
    { DT_CODE_DRAW_SCREEN, "ducktales.decomp.C78F", "Level_DrawScreen", Run_DrawScreen, OUT_HW },
    { DT_CODE_PLACE_FROM_ENTRY, "ducktales.decomp.DD4E", "Level_PlaceFromEntry", Run_PlaceFromEntry, OUT_HW },
    { DT_CODE_VRAM_FLUSH, "ducktales.decomp.C139", "Vram_FlushQueue", Run_VramFlush, OUT_X | OUT_HW },
    { DT_CODE_TILE_EDIT_FLUSH, "ducktales.decomp.C15E", "Vram_FlushTileEdits", Run_TileEditFlush, OUT_HW },
    { DT_CODE_CAMERA_UPDATE, "ducktales.decomp.EFE2", "Camera_Update", Run_CameraUpdate, OUT_X | OUT_HW },
    { DT_CODE_BOSS_TRIGGER, "ducktales.decomp.CF93", "Boss_CheckTrigger", Run_BossCheckTrigger, 0 },
    { DT_CODE_TILE_EDIT, "ducktales.decomp.F574", "Level_TileEdit", Run_TileEdit, OUT_HW },
    { DT_CODE_SCREEN_POINTER, NULL, "Level_ScreenPointer", Run_ScreenPointer, OUT_X },
    { DT_CODE_DECODE_BLOCK, NULL, "Level_DecodeBlock", Run_DecodeBlock, OUT_X },
    { DT_CODE_METATILE_AT, NULL, "Level_MetatileAt", Run_MetatileAt, OUT_A | OUT_X },
    { DT_CODE_ROW_CHR_STREAM, "ducktales.decomp.C42C", "Chr_StreamRow", Run_ChrStreamRow, OUT_HW },
};
const int gLevelRoutineCount = sizeof gLevelRoutines / sizeof gLevelRoutines[0];
