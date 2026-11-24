/*
 * level/level.c - the level's map and screen decoding, the column streaming,
 * the VRAM queue uploads and the camera, decompiled from the fixed bank
 * (docs/LEVELS.md, docs/RENDERING.md, docs/DECOMPILATION.md): the site
 * adapters and the routine table. The other files of level/ hold the map and
 * block decoding (map.c), the VRAM queue, column streaming and camera
 * (scroll.c), the land-load draws, row CHR and tile edit (screen.c), the row
 * scroll (row_scroll.c), the block draw and object tile edits (block.c) and
 * the object spawner of bank 6 (spawn.c); level_internal.h is what they
 * share.
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
#include "decomp/level/level_internal.h"

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

static void Run_SpawnTables(Regs *r)
{
    r->a = Spawn_Tables();
    r->x = gLand;
}

static void Run_SpawnTablesOf(Regs *r)
{
    r->a = Spawn_TablesOf(r->x);
}

static void Run_SpawnReset(Regs *r)
{
    r->y = Spawn_Reset();
    r->a = 1;
}

static void Run_SpawnTimedAtEdge(Regs *r)
{
    Spawn_TimedAtEdge(&r->x, &r->y);
}

static void Run_SpawnUpdate(Regs *r)
{
    Spawn_Update(&r->x, &r->y);
}

static void Run_TileEditByAction(Regs *r)
{
    Tile_EditByAction(r->x, r->y);
    r->x = gEditX;
    r->y = gEditY;
}

static void Run_TileEditBySlot(Regs *r)
{
    Tile_EditBySlot(r->x, r->y);
    r->x = gEditX;
    r->y = gEditY;
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
    { DT_B6_SPAWN_TABLES, "ducktales.decomp.6.AE20", "Spawn_Tables", Run_SpawnTables, OUT_A | OUT_X,
      .bank = DT_BANK_OBJECTS },
    { DT_B6_SPAWN_TABLES_MAP, "ducktales.decomp.6.AE22", "Spawn_TablesOf", Run_SpawnTablesOf, OUT_A,
      .bank = DT_BANK_OBJECTS },
    { DT_B6_SPAWN_RESET, "ducktales.decomp.6.AE60", "Spawn_Reset", Run_SpawnReset, OUT_A | OUT_Y,
      .bank = DT_BANK_OBJECTS },
    { DT_B6_SPAWN_TIMED, NULL, "Spawn_TimedAtEdge", Run_SpawnTimedAtEdge, OUT_X | OUT_Y, .bank = DT_BANK_OBJECTS },
    { DT_B6_SPAWN, "ducktales.decomp.6.AEA0", "Spawn_Update", Run_SpawnUpdate, OUT_X | OUT_Y, .bank = DT_BANK_OBJECTS },
    { DT_CODE_TILE_EDIT_BY_ACTION, "ducktales.decomp.F4D8", "Tile_EditByAction", Run_TileEditByAction,
      OUT_X | OUT_Y | OUT_HW },
    { DT_CODE_OBJ_EDIT_TILE, "ducktales.decomp.F506", "Tile_EditBySlot", Run_TileEditBySlot, OUT_X | OUT_Y | OUT_HW },
    { DT_CODE_BLOCK_DRAW, "ducktales.decomp.D068", "Block_Draw", Run_BlockDraw, OUT_HW },
    { DT_B4_BLOCK_DEBRIS, NULL, "Block_Debris", Run_BlockDebris, 0, .bank = DEBRIS_BANK },
    { DT_CODE_ROW_SCROLL, "ducktales.decomp.C976", "Row_Scroll", Row_Scroll, 0, true, 0, false },
    { DT_CODE_ROW_SCROLL_FRAME, NULL, "Row_ScrollFrame", Row_ScrollFrame, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_STEPPED, NULL, "Row_ScrollStepped", Row_ScrollStepped, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_NEXT, NULL, "Row_ScrollNext", Row_ScrollNext, 0, true, ROW_LOCALS, true },
    { DT_CODE_ROW_SCROLL_END, NULL, "Row_ScrollEnd", Row_ScrollEnd, 0, true, 0, true },
};
const int gLevelRoutineCount = sizeof gLevelRoutines / sizeof gLevelRoutines[0];
