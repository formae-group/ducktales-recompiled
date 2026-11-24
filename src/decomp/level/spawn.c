/*
 * level/spawn.c - the object spawner of bank 6: its tables, its reset, the
 * timed spawns at the camera's edge and the per-frame window update
 * (docs/DECOMPILATION.md).
 */
#include "decomp/level/level_internal.h"

/* ---- the object spawner (bank 6) ----
 *
 * A land's objects are records in five tables ($60-$69: page, world X, the
 * first record of each map row, Y, type), sorted by world X within a row.
 * $AE60 puts both ends of the spawn window ($4C right, $4D left) on the
 * first record at or right of the camera page; every frame $AEA0 moves the
 * end on the side the camera went ($4B) over the records the window
 * ($F3:$F2 to 255 pixels right of it) reaches, spawning each one ($AF2B).
 * $B0AB first spawns the land's timed objects at the far edge. */

#define gSpawnSide     RAM_U8(DT_ADDR_MOVE_SIDE)
#define gSpawnRight    RAM_U8(DT_ADDR_SPAWN_RIGHT)
#define gSpawnLeft     RAM_U8(DT_ADDR_SPAWN_LEFT)
#define gSpawnFirst    RAM_U8(DT_ADDR_SPAWN_FIRST)
#define gSpawnTimer    RAM_U8(DT_ADDR_HELD_SLOT)
#define SPAWN_TABLES   10
#define SPAWN_MAPS     9 /* entries per pointer table: the lands and the scenes */
#define SPAWN_TABLE(i) Ram_GetU16(DT_ADDR_SPAWN_TABLES + 2 * (i), DT_ADDR_SPAWN_TABLES + 2 * (i) + 1)
#define SPAWN_PAGE     0
#define SPAWN_X        1
#define SPAWN_ROWS     2

static uint8_t Spawn_Read(int table, uint8_t index)
{
    uint16_t addr = (uint16_t)(SPAWN_TABLE(table) + index);
    return addr < GAME_RAM_SIZE ? gRam[addr] : rom(addr);
}

/* bank 6 $AE22: the spawn table pointers of map x; $AE20 with the land.
 * Returns the last byte loaded, the original's A. */
uint8_t Spawn_TablesOf(uint8_t x)
{
    Decomp_Enter(DT_B6_SPAWN_TABLES_MAP, 0, x, 0);
    for (int i = 0; i < SPAWN_TABLES; i++) {
        RAM_U8(DT_ADDR_SPAWN_TABLES + i) = rom((uint16_t)(DT_B6_SPAWN_POINTERS + SPAWN_MAPS * i + x));
    }
    return RAM_U8(DT_ADDR_SPAWN_TABLES + SPAWN_TABLES - 1);
}

uint8_t Spawn_Tables(void)
{
    Decomp_Enter(DT_B6_SPAWN_TABLES, 0, 0, 0);
    return Spawn_TablesOf(gLand);
}

/* bank 6 $AE60: both window ends on the first record of the map row at or
 * right of the camera page (or with page bit 7). Returns that index. */
uint8_t Spawn_Reset(void)
{
    Decomp_Enter(DT_B6_SPAWN_RESET, 0, 0, 0);
    gSpawnFirst = Spawn_Read(SPAWN_ROWS, gMapRow);
    uint8_t y = gSpawnFirst;
    for (;;) {
        uint8_t page = Spawn_Read(SPAWN_PAGE, y);
        if ((page & 0x80) || page <= gCameraPage || !++y) {
            break;
        }
    }
    gSpawnRight = y;
    gSpawnLeft = y;
    gSpawnSide = 1;
    return y;
}

/* bank 6 $B105: a timed object of entry *y at world X $03:$02 in the
 * highest free slot, from the type's 4-byte template at $BE16. Returns the
 * slot (0 when every one is taken, which spawns nothing); *y is left as the
 * original's Y, the template's last offset. */
static uint8_t Spawn_Timed(uint8_t *yp)
{
    uint8_t slot;
    uint8_t y = *yp;
    if (!Obj_FindFreeSlot(&slot)) {
        return slot;
    }
    gObjects->y[slot] = rom((uint16_t)(DT_B6_TIMED_Y + y));
    uint8_t type = rom((uint16_t)(DT_B6_TIMED_TYPE + y));
    gScratch4 = (uint8_t)DT_B6_SPAWN_TEMPLATES;
    gScratch5 = (uint8_t)((DT_B6_SPAWN_TEMPLATES >> 8) + (type >> 6 & 1));
    uint16_t t = (uint16_t)(Ram_GetU16(DT_ADDR_SCRATCH4, DT_ADDR_SCRATCH5) + (uint8_t)(type << 2));
    *yp = (uint8_t)((type << 2) + 3);
    gObjects->state[slot] = gObjects->flags[slot] = rom(t);
    gObjects->action[slot] = rom((uint16_t)(t + 1));
    gObjects->kind[slot] = rom((uint16_t)(t + 2));
    gObjects->hitbox[slot] = rom((uint16_t)(t + 3));
    gObjects->animTimer[slot] = 0;
    gObjects->animFrame[slot] = 0;
    gObjects->unk_0690[slot] = 0;
    gObjects->timer[slot] = 0;
    gObjects->unk_06C0[slot] = 0;
    gObjects->worldX[slot] = gScratch2;
    gObjects->worldPage[slot] = gScratch3;
    gObjects->record[slot] = 0xFF;
    gSpawnTimer = slot | 0x80;
    return slot;
}

/* bank 6 $B0AB: outside a boss fight, while no timed object is out ($4F
 * bit 7) and its countdown is done, the timed object the map row has for
 * the page at the far edge of the screen (bit 7 of the entry: a pair, the
 * second at Y $C0 and X mirrored). */
void Spawn_TimedAtEdge(uint8_t *xp, uint8_t *yp)
{
    Decomp_Enter(DT_B6_SPAWN_TIMED, 0, *xp, *yp);
    if ((gBossFight & 0x80) || (gSpawnTimer & 0x80)) {
        return;
    }
    if (gSpawnTimer) {
        gSpawnTimer--;
        return;
    }
    uint8_t row = (uint8_t)(rom((uint16_t)(DT_B6_TIMED_LAND + gLand)) + gMapRow);
    uint8_t y = rom((uint16_t)(DT_B6_TIMED_ROW + row));
    uint8_t edge = *xp = gSpawnSide & 2;
    unsigned x = gCameraX + rom((uint16_t)(DT_B6_TIMED_EDGE + edge));
    gScratch2 = (uint8_t)x;
    gScratch3 = (uint8_t)(gCameraPage + rom((uint16_t)(DT_B6_TIMED_EDGE + 1 + edge)) + (x >> 8));
    for (;; y++) {
        uint8_t entry = rom((uint16_t)(DT_B6_TIMED_PAGE + y));
        *yp = y;
        if (entry == 0xFF) {
            return;
        }
        gScratch6 = entry & 0x80;
        if ((entry & 0x7F) == gScratch3) {
            break;
        }
        if (y == 0xFF) {
            *yp = 0;
            return;
        }
    }
    if (gScratch6) {
        gScratch7 = y;
        gObjects->y[Spawn_Timed(yp)] = 0xC0;
        *yp = gScratch7;
        gScratch2 ^= 0xF0;
    }
    *xp = Spawn_Timed(yp);
}

/* bank 6 $AEA0. X and Y are left as the original leaves them (a caller's
 * task_yield keeps them): X the last record spawn's or the timed spawn's, Y
 * the right index ($4C) after a move left, the left one ($4D) after a move
 * right, else the timed spawn's. */
void Spawn_Update(uint8_t *xp, uint8_t *yp)
{
    Decomp_Enter(DT_B6_SPAWN, 0, *xp, *yp);
    Spawn_TimedAtEdge(xp, yp);
    unsigned right = (unsigned)(gCameraPage << 8 | gCameraX) + 0xFF;
    gScratch0 = gCameraX;
    gScratch1 = gCameraPage;
    gScratch2 = (uint8_t)right;
    gScratch3 = (uint8_t)(right >> 8);
    uint16_t left = (uint16_t)(gScratch1 << 8 | gScratch0);
    uint16_t edge = (uint16_t)(gScratch3 << 8 | gScratch2);
#define RECORD_X(y) ((uint16_t)(Spawn_Read(SPAWN_PAGE, (y)) << 8 | Spawn_Read(SPAWN_X, (y))))
    if (!(gSpawnSide & 3)) {
        return;
    }
    if (!(gSpawnSide & 1)) {
        while (gSpawnLeft != gSpawnFirst && RECORD_X((uint8_t)(gSpawnLeft - 1)) >= left) {
            *xp = Obj_SpawnRecord((uint8_t)(gSpawnLeft - 1));
            if (!--gSpawnLeft) {
                break;
            }
        }
        while (gSpawnRight != gSpawnFirst && RECORD_X((uint8_t)(gSpawnRight - 1)) >= edge) {
            if (--gSpawnRight == gSpawnFirst) {
                break;
            }
        }
        *yp = gSpawnRight;
        return;
    }
    while (RECORD_X(gSpawnRight) <= edge) {
        *xp = Obj_SpawnRecord(gSpawnRight);
        if (!++gSpawnRight) {
            break;
        }
    }
    uint8_t y = gSpawnLeft;
    while (RECORD_X(y) <= left && ++y) {
    }
    gSpawnLeft = *yp = y;
#undef RECORD_X
}
