/*
 * level/screen.c - the whole-screen draws of a land load and of Land Select,
 * the sprite CHR streaming of the current row and the tile edit the object
 * code queues (docs/LEVELS.md, docs/RENDERING.md).
 */
#include "decomp/level/level_internal.h"

/* $C71D, land load: draw the start screen's columns, with the screens on
 * either side when they exist (tables at $C786: steps, first block, screen
 * offset for none, left, or both). */
void Level_DrawStartColumns(void)
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
void Level_DrawScreen(void)
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
uint8_t Chr_StreamRow(uint8_t x)
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
void Level_TileEdit(int slot)
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
