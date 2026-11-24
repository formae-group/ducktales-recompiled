/*
 * collision.c - the background probe and its snaps, decompiled from the
 * fixed bank (docs/PLAYER.md, docs/LEVELS.md, docs/DECOMPILATION.md).
 *
 * Every object tests the background through probe points: offsets from its
 * world X and screen Y (tables at $D433 and $D3CE, read from the ROM) that
 * select a 16x16 metatile quadrant of the collision map $0580. Class 1 is
 * solid, 2 climbable, 3 special: the metatile under the point (its id >> 4)
 * picks a handler from the table at $D1E2, which can make it solid, draw the
 * object behind the background, or raise a hazard for Scrooge ($70). A snap
 * then moves the object out of the tile the last probe hit, using the
 * point's position the probe left in $06/$07.
 *
 * The metatile lookup ($CDE2, level.c) switches banks and restores them, so
 * the probe's contract has OUT_HW.
 */
#include "decomp/collision.h"
#include "decomp/decomp.h"
#include "decomp/level.h"

#include "ducktales/game.h"
#include "ducktales/level.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#define gScratch0      RAM_U8(DT_ADDR_SCRATCH0)
#define gScratch1      RAM_U8(DT_ADDR_SCRATCH1)
#define gScratch2      RAM_U8(DT_ADDR_SCRATCH2)
#define gProbePage     RAM_U8(DT_ADDR_SCRATCH3)
#define gProbeHits     RAM_U8(DT_ADDR_PROBE_HITS)
#define gProbeFirst    RAM_U8(DT_ADDR_PROBE_FIRST)
#define gProbeCount    RAM_U8(DT_ADDR_PROBE_COUNT)
#define gProbeClass(i) RAM_U8(DT_ADDR_PROBE_CLASS + (i))

#define CLASS_SOLID   1
#define CLASS_SPECIAL 3
#define FLAG_BEHIND   0x04 /* $0600 bit 2: drawn behind the background */
#define HAZARD_SPIKES 6    /* hypothesis: what $70 asks of Scrooge's code */
#define HAZARD_POGO   7
#define HAZARD_PIT    8

uint8_t gProbeExitY;
#define PIT_Y           0xD0
#define POINT_POGO_FEET 0x07 /* the probe points the hazards look at */
#define POINT_POGO_TIP  0x0A

static uint8_t rom(uint16_t addr)
{
    return Game_ReadRom(addr);
}

/* $D350: the collision class (0-3) under probe point "point" of the object
 * in slot. A point left of page 0 is empty; one above the row is solid and
 * one below it empty. carry is the C flag the original returns with: the
 * page or row sum's, or the last bit its shifts dropped. */
static uint8_t Bg_ClassAt(int slot, uint8_t point, bool *carry)
{
    uint8_t dx = rom((uint16_t)(DT_CODE_PROBE_DX + point));
    gScratch2 = (dx & 0x80) ? 0xFF : 0;
    unsigned x = (unsigned)gObjects->worldX[slot] + dx;
    gProbeX = (uint8_t)x;
    unsigned page = gObjects->worldPage[slot] + gScratch2 + (x >> 8);
    gProbePage = (uint8_t)page;
    if (gProbePage & 0x80) {
        *carry = page > 0xFF;
        return 0;
    }
    gProbeQuadrant = gProbeX >> 4 & 1;
    gProbeCell = gProbeX >> 5 & 7;
    uint8_t dy = rom((uint16_t)(DT_CODE_PROBE_DY + point));
    gScratch2 = (dy & 0x80) ? 0xFF : 0;
    unsigned y = (unsigned)gObjects->y[slot] + dy;
    gProbeY = (uint8_t)y;
    uint8_t probeRow = (uint8_t)(gMapRow + gScratch2 + (y >> 8));
    if (probeRow != gMapRow) {
        *carry = probeRow >= gMapRow;
        return gScratch2 & 1;
    }
    uint8_t row = (uint8_t)(gProbeY >> 2);
    gProbeCell |= (uint8_t)((row & 0x38) | rom((uint16_t)(DT_CODE_PROBE_PAGE_BIT + (gProbePage & 1))));
    gProbeQuadrant |= row >> 1 & 2;
    uint8_t classes = gCollisionMap[gProbeCell];
    *carry = gProbeQuadrant ? classes >> (2 * gProbeQuadrant - 1) & 1 : row & 1;
    return classes >> 2 * gProbeQuadrant & 3;
}

/* $D350 entered on its own: Player_ContactV falls into it, with X = the
 * slot touched and Y = its hitbox as the point, and reads the carry. */
uint8_t Bg_ProbePoint(int slot, uint8_t point, bool *carry)
{
    Decomp_Enter(DT_CODE_PROBE_POINT, 0, (uint8_t)slot, point);
    return Bg_ClassAt(slot, point, carry);
}

static uint8_t Bg_Solid(void)
{
    return gProbeHits |= CLASS_SOLID;
}

/* $D1BF: a pit: Scrooge below PIT_Y asks for the pit death. Other objects
 * end the probe: the original tests the value left in A, the handler's high
 * byte, which is odd. */
static uint8_t Bg_PitTile(int slot, uint8_t left)
{
    if (slot != PLAYER_SLOT) {
        return left;
    }
    if (gObjects->y[PLAYER_SLOT] >= PIT_Y) {
        gPlayerHazard = HAZARD_PIT;
    }
    return 0;
}

/* $D1B1: slippery floor under Scrooge's middle feet probe (solid), counted
 * in $71; when the count wraps to 0 the tile acts as a pit instead. */
static uint8_t Bg_SlipperyTile(int slot)
{
    if (slot != PLAYER_SLOT || gProbeCount != 1) {
        return Bg_Solid();
    }
    if (++gPlayerSlippery) {
        return Bg_Solid();
    }
    return Bg_PitTile(slot, 0);
}

/* $D14D: a class-3 metatile: run the handler its id picks (the original
 * jumps through $00/$01; every entry of the table is one of the seven
 * handlers below, $D162 the last). Returns what the probe loop tests for
 * other objects (bit 0 ends the probe). */
static uint8_t Bg_SpecialTile(int slot)
{
    uint8_t kind = Level_MetatileAt((uint8_t)slot) >> 4;
    gProbeExitY = kind;
    gScratch0 = rom((uint16_t)(DT_CODE_SPECIAL_LO + kind));
    gScratch1 = rom((uint16_t)(DT_CODE_SPECIAL_HI + kind));
    uint16_t handler = (uint16_t)(gScratch1 << 8 | gScratch0);
    bool scrooge = slot == PLAYER_SLOT;
    switch (handler) {
        case DT_CODE_SPECIAL_MARK:
            gObjects->flags[slot] |= FLAG_BEHIND;
            return gObjects->flags[slot];
        case DT_CODE_SPECIAL_MARK_SLIPPERY:
            gObjects->flags[slot] |= FLAG_BEHIND;
            return scrooge ? Bg_SlipperyTile(slot) : Bg_Solid();
        case DT_CODE_SPECIAL_HAZARD6:
            if (scrooge && !(gPlayerPower | gPlayerInvulnerable) &&
                (gObjects->action[PLAYER_SLOT] != PLAYER_ACTION_POGO || gProbeFirst == POINT_POGO_TIP) &&
                gProbeCount == 1) {
                gPlayerHazard = HAZARD_SPIKES;
            }
            return Bg_Solid();
        case DT_CODE_SPECIAL_HAZARD7:
            if (!scrooge || gProbeFirst != POINT_POGO_FEET || gObjects->action[PLAYER_SLOT] != PLAYER_ACTION_POGO) {
                return Bg_Solid();
            }
            return gPlayerHazard = HAZARD_POGO;
        case DT_CODE_SPECIAL_SLIPPERY:
            return Bg_SlipperyTile(slot);
        case DT_CODE_SPECIAL_PIT:
            return Bg_PitTile(slot, (uint8_t)(handler >> 8));
        default:
            return Bg_Solid();
    }
}

/* $D120: test gProbeCount + 1 probe points from gProbeFirst for the object
 * in slot; their classes go to $42 + count left, solid hits to bit 0 of
 * $10. Other objects than Scrooge stop at the first solid point. */
void Bg_Probe(int slot)
{
    Decomp_Enter(DT_CODE_BG_PROBE, 0, (uint8_t)slot, 0);
    gObjects->flags[slot] &= (uint8_t)~FLAG_BEHIND;
    gProbeHits = 0;
    for (;;) {
        bool carry;
        uint8_t cls = Bg_ClassAt(slot, gProbeFirst, &carry);
        gProbeClass(gProbeCount) = cls;
        gProbeExitY = gProbeCount;
        uint8_t tested = cls == CLASS_SPECIAL ? Bg_SpecialTile(slot) : (gProbeHits |= cls);
        if (slot != PLAYER_SLOT && (tested & 1)) {
            return;
        }
        gProbeFirst++;
        if ((int8_t)--gProbeCount < 0) {
            return;
        }
    }
}

/* $D49A: after a hit moving right, back world X out of the tile. */
void Bg_SnapRight(int slot)
{
    Decomp_Enter(DT_CODE_SNAP_RIGHT, 0, (uint8_t)slot, 0);
    gProbeX &= 0x0F;
    Obj_SetWorldX(slot, (uint16_t)(Obj_GetWorldX(slot) - gProbeX));
}

/* $D4B2: after a hit moving left, push world X to the next tile. */
void Bg_SnapLeft(int slot)
{
    Decomp_Enter(DT_CODE_SNAP_LEFT, 0, (uint8_t)slot, 0);
    Obj_SetWorldX(slot, (uint16_t)(Obj_GetWorldX(slot) + 16 - (gProbeX & 0x0F)));
}

/* $D4C8: after a head hit, push Y down below the tile. */
void Bg_SnapHead(int slot)
{
    Decomp_Enter(DT_CODE_SNAP_HEAD, 0, (uint8_t)slot, 0);
    gObjects->y[slot] = (uint8_t)(gObjects->y[slot] + 16 - (gProbeY & 0x0F));
}

/* $D4D6: after a feet hit, put Y on top of the tile. */
void Bg_SnapFeet(int slot)
{
    Decomp_Enter(DT_CODE_SNAP_FEET, 0, (uint8_t)slot, 0);
    gProbeY &= 0x0F;
    gObjects->y[slot] = (uint8_t)(gObjects->y[slot] - gProbeY);
}

/* ---- site adapters: X = slot, which callers keep using ---- */

static void Run_Probe(Regs *r)
{
    Bg_Probe(r->x);
    r->y = gProbeExitY;
}

static void Run_SnapRight(Regs *r)
{
    Bg_SnapRight(r->x);
}

static void Run_SnapLeft(Regs *r)
{
    Bg_SnapLeft(r->x);
}

static void Run_SnapHead(Regs *r)
{
    Bg_SnapHead(r->x);
}

static void Run_SnapFeet(Regs *r)
{
    Bg_SnapFeet(r->x);
}

static void Run_ProbePoint(Regs *r)
{
    bool carry;
    r->a = Bg_ProbePoint(r->x, r->y, &carry);
    r->p = (uint8_t)((r->p & ~P_C) | (carry ? P_C : 0));
}

const DecompRoutine gCollisionRoutines[] = {
    { DT_CODE_BG_PROBE, "ducktales.decomp.D120", "Bg_Probe", Run_Probe, OUT_X | OUT_Y | OUT_HW },
    { DT_CODE_SNAP_RIGHT, "ducktales.decomp.D49A", "Bg_SnapRight", Run_SnapRight, OUT_X },
    { DT_CODE_SNAP_LEFT, "ducktales.decomp.D4B2", "Bg_SnapLeft", Run_SnapLeft, OUT_X },
    { DT_CODE_SNAP_HEAD, "ducktales.decomp.D4C8", "Bg_SnapHead", Run_SnapHead, OUT_X },
    { DT_CODE_SNAP_FEET, "ducktales.decomp.D4D6", "Bg_SnapFeet", Run_SnapFeet, OUT_X },
    { DT_CODE_PROBE_POINT, NULL, "Bg_ProbePoint", Run_ProbePoint, OUT_A | OUT_C },
};
const int gCollisionRoutineCount = sizeof gCollisionRoutines / sizeof gCollisionRoutines[0];
