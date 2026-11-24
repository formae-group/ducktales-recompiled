/*
 * level/map.c - a map's header, its screens' pointers and the decoding of a
 * 32x32 block into tiles, an attribute and collision byte, and the metatile
 * under a point (docs/LEVELS.md).
 */
#include "decomp/level/level_internal.h"

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
uint8_t Level_DecodeBlock(void)
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
