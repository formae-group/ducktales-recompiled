/*
 * level/block.c - the block draw $D068 with the debris it throws, and the
 * tile edits the objects ask for by action or by slot (docs/LEVELS.md,
 * docs/DECOMPILATION.md).
 */
#include "decomp/level/level_internal.h"

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
#define DEBRIS_STATE (OBJ_STATUS_LIVE | 0x20)
#define SFX_BREAK    0x16

/* bank 4 $BD80: four debris pieces around ($11, $10) in free slots, then the
 * break sound. */
void Block_Debris(void)
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

void Block_Draw(uint8_t record)
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

/* ---- tile edits for objects ----
 *
 * Bank 6 runs the tile edit $F574 for the object in slot X through these
 * two: $F4D8 takes the replacement from the object's action, $F506 from its
 * $06B0 and may first spawn an object of action $F67B[$06B0] at the same
 * place. Both close the 14-byte queue entry and map the objects' bank back,
 * and return X and Y from $12/$13: X as it came, Y as it came for $F4D8 and
 * the slot $F506 looked at (0 when every slot was taken). */

#define TILE_EDIT_ENTRY   14
#define EDIT_FIRST_ACTION 0x1A
#define EDIT_BY_ACTION    0x13 /* entry of the first action's replacement */
#define EDIT_SPAWNS       0x80 /* $06C0 bit 7: the edit spawns an object */
#define EDIT_SPAWN_STATE  (OBJ_STATUS_LIVE | OBJ_STATUS_WORLD | 1)
#define EDIT_SPAWN_KIND   0x13

/* The land's replacement entry plus entry (ADC with the carry the second
 * ASL of the land leaves), the edit, and the queue entry closed. */
static void Tile_EditFor(uint8_t x, uint8_t entry)
{
    gScratch8 = (uint8_t)((gLand << 2) + entry + (gLand >> 6 & 1));
    Level_TileEdit(x);
    gTileEditEnd += TILE_EDIT_ENTRY;
    Bank_Select(DT_BANK_OBJECTS);
}

/* $F4D8: entry $13 + (action - $1A) & 3, plus 1 from action $1A on. */
void Tile_EditByAction(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_TILE_EDIT_BY_ACTION, 0, x, y);
    gEditX = x;
    gEditY = y;
    uint8_t action = gObjects->action[x] & 0x7F;
    gScratch8 = (uint8_t)(((action - EDIT_FIRST_ACTION) & 3) + EDIT_BY_ACTION + (action >= EDIT_FIRST_ACTION));
    Tile_EditFor(x, gScratch8);
}

/* $F506 */
void Tile_EditBySlot(uint8_t x, uint8_t y)
{
    Decomp_Enter(DT_CODE_OBJ_EDIT_TILE, 0, x, y);
    if (gObjects->unk_06C0[x] & EDIT_SPAWNS) {
        y = 0x0F;
        while (gObjects->state[y] & OBJ_STATUS_LIVE) {
            if (!--y) {
                break;
            }
        }
        if (y) {
            gObjects->state[y] = EDIT_SPAWN_STATE;
            gObjects->animTimer[y] = 0;
            gObjects->animFrame[y] = 0;
            gObjects->hitbox[y] = 0;
            gObjects->vyFrac[y] = 0;
            gObjects->vy[y] = 0;
            gObjects->kind[y] = EDIT_SPAWN_KIND;
            gObjects->y[y] = gObjects->y[x];
            gObjects->worldX[y] = gObjects->worldX[x];
            gObjects->worldPage[y] = gObjects->worldPage[x];
            gScratch0 = y;
            gObjects->action[y] = rom((uint16_t)(DT_CODE_TILE_EDIT_SPAWN + gObjects->timer[x]));
        }
    }
    gEditX = x;
    gEditY = y;
    RAM_U8(DT_ADDR_BLOCK_OPENED)++;
    Tile_EditFor(x, gObjects->timer[x]);
}
