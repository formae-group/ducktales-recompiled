/*
 * level/row_scroll.c - the vertical row scroll between map rows: its drawing
 * steps and its task code, one step per frame (docs/LEVELS.md,
 * docs/DECOMPILATION.md).
 */
#include "decomp/level/level_internal.h"

/* ---- the row scroll's steps ----
 *
 * The row scroll ($C976) runs one step routine a frame through the pointers
 * at $CA2A; an RTS to $C9FB ends each. A block row of the incoming row
 * reaches the screen in two halves: $CAA1 decodes its 8 blocks, queues two
 * of their tile rows and keeps the other two at $0460/$0480, which $CA4E
 * queues on a later frame. $CB5A and $CB6E draw half a block row of the
 * screens beside the camera's. The side ($2F, 0 or 4) picks each table's
 * entry. The step routines run with the scroll's state and keep no other. */

#define gRowSide    RAM_U8(DT_ADDR_ROW_SCROLL_SIDE)
#define gRowKept    (&RAM_U8(DT_ADDR_ROW_KEPT)) /* two tile rows of 32, queued later */
#define ROW_TILES   0x20
#define ROW_RECORD  (3 + ROW_TILES) /* a tile row record of the queue: address high, low, count - 1, tiles */
#define HALF_RECORD (3 + ROW_TILES / 2)
#define ATTR_SECOND 0x40 /* attribute shadow of the second nametable */
#define LAST_ROW    7    /* the half block row at the bottom */

/* $CB1E: the headers of a block row's queue: two tile rows of 32 from block
 * row row (the lower two with side 0) and its 8 attribute bytes. */
void Row_QueueHeaders(uint8_t row, uint8_t side)
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
void Row_Draw(void)
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
void Row_DrawStep(void)
{
    Decomp_Enter(DT_CODE_ROW_DRAW_STEP, 0, 0, 0);
    Row_Draw();
}

/* $CA4E: the step that queues the kept half of the last row drawn, with the
 * attribute bytes of block row $26 from the shadow, and moves $26 a row on. */
void Row_KeptStep(void)
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
void Row_LastStep(void)
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
void Row_DrawHalf(void)
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

void Row_RightStep(void)
{
    Decomp_Enter(DT_CODE_ROW_RIGHT_STEP, 0, 0, 0);
    uint8_t screen = (uint8_t)(gCameraScreen + 1);
    if ((uint8_t)(gMapStride - 1) != gCameraPage && Map_Cell(screen) != 0xFF) {
        Row_DrawBeside(screen, 0x00);
    }
    gStreamPos &= 7;
}

void Row_LeftStep(void)
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

void Row_Scroll(Regs *r)
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
    r->a = Obj_ClearSlots(&r->x);
    Row_Call(r, DT_CODE_OAM_CLEAR);
    Row_Yield(DT_CODE_ROW_SCROLL_FRAME);
}

void Row_ScrollFrame(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_FRAME, r->a, r->x, r->y);
    Row_Frame(r);
}

/* $C9FB: after the step, the sprites, and the next frame. */
void Row_ScrollStepped(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_STEPPED, r->a, r->x, r->y);
    Row_Call(r, DT_CODE_OAM_CLEAR);
    Row_SelectBank(r, DT_SPR_BANK);
    Row_Call(r, DT_B5_BUILD_OAM);
    Row_Yield(DT_CODE_ROW_SCROLL_NEXT);
}

/* $CA09: the next frame, or the end: $26 and $2C back, the object spawner
 * reset and run. */
void Row_ScrollNext(Regs *r)
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
void Row_ScrollEnd(Regs *r)
{
    Decomp_Enter(DT_CODE_ROW_SCROLL_END, r->a, r->x, r->y);
    gRowScroll = 0;
    r->y = 1;
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = r->y;
}
