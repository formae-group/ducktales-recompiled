/*
 * level/scroll.c - the $0400 VRAM queue uploads, the column streaming that
 * follows the camera, and the camera itself, inside a map row or by a scene's
 * script (docs/LEVELS.md, docs/RENDERING.md).
 */
#include "decomp/level/level_internal.h"

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

void Vram_FlushTileEdits(void)
{
    Decomp_Enter(DT_CODE_TILE_EDIT_FLUSH, 0, 0, 0);
    gTileEditReady = 0;
    Vram_Upload(DT_ADDR_TILE_EDIT_QUEUE);
}

/* $C84E/$C959: close the queue at offset end; upload it now while the
 * screen is blanked, else let the NMI do it. Returns the original's X: the
 * upload's, else x. */
uint8_t Vram_EndQueue(unsigned end, uint8_t x)
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
