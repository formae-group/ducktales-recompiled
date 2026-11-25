/*
 * objects/dispatch.c - the object system of bank 6, decompiled
 * (docs/DECOMPILATION.md "Objects"): the per-frame update, the handler
 * dispatch and the land record spawn.
 *
 * Obj_UpdateAll runs once per frame from the frame loop ($DB78, bank 6
 * mapped): it loads the objects' gravity and runs the handler of every live
 * slot 1-15 (status bit 7), chosen by its kind through the handler table at
 * $802C/$806F. The slot cursor lives in RAM ($4A) because the original keeps
 * it there across the handler call. Every kind in the table has a C case;
 * kind 0 has no action, matching its RTS. Kinds $43 and up have no entry.
 *
 * Each file of the object system ends with the adapters that call its
 * routines with the registers of the original, and its table. Registered
 * routines start with Decomp_Enter. Routines also called by original code
 * may have a hook site, including the dispatch and the fixed-bank movement
 * helpers.
 */
#include "decomp/objects/objects_internal.h"

/* The part of $AF2B a land record's spawn shares with a mod's (dt.spawn,
 * docs/MODS.md): slot x takes a 4-byte template of $BE16 (timer and status,
 * pose, kind, hitbox), with its counters and animation cleared. */
void Obj_SpawnTemplate(uint8_t x, const uint8_t template[4])
{
    gObjects->unk_0690[x] = 0;
    gObjects->unk_06C0[x] = 0;
    gObjects->bossCooldown[x] = 0;
    gObjects->timer[x] = template[0] & 0x0F;
    gObjects->state[x] = gObjects->flags[x] = template[0] & 0xF0;
    gObjects->action[x] = template[1];
    gObjects->kind[x] = template[2];
    gObjects->hitbox[x] = template[3];
    ResetAnim(x);
}

/* $AF2B: spawn land record y (the spawner $AEA0 calls it for the records
 * entering the window $00-$03) unless its consumed bit is set, it lies left
 * of $01:$00, or a live slot already holds it. The object goes to the
 * highest free slot with the record's world X and Y and the 4-byte template
 * of its type ($BE16: timer and status, pose, kind, hitbox). A hidden object
 * (status bit 4) shown outside a row scroll gets pose bit 7 moved to
 * $06C0 = $FF, and a hidden block or dropper draws its tiles ($F4D8). In a
 * boss fight the boss hits are reset to 5, and in the final arena, or with
 * every treasure, the object becomes the final boss's stand-in (kind $3A,
 * pose $84, hidden). With the Amazon's treasure, the land's hidden items,
 * chests and pose-$1B blocks are not spawned again. $10 holds the record's
 * bit and $04/$05 the template's address. */
uint8_t Obj_SpawnRecord(uint8_t y)
{
    Decomp_Enter(DT_CODE_SPAWN_RECORD, 0, 0, y);
    gProbeHits = (uint8_t)(1 << (y & 7));
    if (RAM_U8(DT_ADDR_CONSUMED + (y >> 3)) & gProbeHits) {
        return y >> 3;
    }
    uint8_t page = Rom((uint16_t)(SpawnTable(0) + y));
    uint8_t wx = Rom((uint16_t)(SpawnTable(1) + y));
    if ((uint16_t)(page << 8 | wx) < (uint16_t)(gScratch(1) << 8 | gScratch(0))) {
        return y >> 3;
    }
    uint8_t x = 0x0F;
    while (x != 0 && gObjects->record[x] != y) {
        x--;
    }
    if (x != 0 && (gObjects->state[x] & OBJ_STATUS_LIVE)) {
        return x;
    }
    if (!Obj_FindFreeSlot(&x)) {
        return x;
    }
    gObjects->record[x] = y;
    gObjects->worldPage[x] = page;
    gObjects->worldX[x] = wx;
    gObjects->unk_0740[x] = gObjects->y[x] = Rom((uint16_t)(SpawnTable(3) + y));
    uint8_t type = Rom((uint16_t)(SpawnTable(4) + y));
    uint8_t t = (uint8_t)(type << 2);
    gScratch(4) = (uint8_t)DT_CODE_SPAWN_TEMPLATES;
    gScratch(5) = (uint8_t)((DT_CODE_SPAWN_TEMPLATES >> 8) + ((type >> 6) & 1));
    uint16_t at = (uint16_t)((gScratch(5) << 8 | gScratch(4)) + t);
    const uint8_t template[4] = { Rom(at), Rom((uint16_t)(at + 1)), Rom((uint16_t)(at + 2)), Rom((uint16_t)(at + 3)) };
    Obj_SpawnTemplate(x, template);
    if (gRowScroll == 0 && (gObjects->state[x] & OBJ_STATUS_HIDDEN)) {
        if (gObjects->action[x] & 0x80) {
            gObjects->action[x] &= 0x7F;
            gObjects->unk_06C0[x] = 0xFF;
        }
        uint8_t kind = gObjects->kind[x];
        if (kind == OBJ_KIND_BLOCK || kind == OBJ_KIND_DROPPER) {
            Tile_EditByAction(x, (uint8_t)(t + 3));
            return x;
        }
    }
    if (gBossFight & 0x80) {
        gBossHits = 5;
        if (gFinalArena != 0 || (gTreasures & 0x1F) == 0x1F) {
            gObjects->kind[x] = OBJ_KIND_FINAL_STAND_IN;
            gObjects->hitbox[x] = 0;
            gObjects->state[x] |= OBJ_STATUS_HIDDEN;
            gObjects->action[x] = 0x84;
        }
    }
    if (gLand != 1 || !(gTreasures & 2)) {
        return x;
    }
    uint8_t kind = gObjects->kind[x];
    if (kind == OBJ_KIND_HIDDEN_ITEM || kind == OBJ_KIND_CHEST ||
        (kind == OBJ_KIND_BLOCK && gObjects->action[x] == 0x1B)) {
        gObjects->state[x] = 0;
    }
    return x;
}

/* ---- the dispatch ---- */

/* One live slot: the dispatch leaves the handler's address in $00/$01 (the
 * JMP ($0000) vector), A = $22 (the low byte of the return it pushed),
 * X = slot and Y = kind. The flags are those of the loop: the caller's carry
 * for slot 1, the CPX of the loop for the others, and the V the previous
 * handler left.
 * Kinds $43 and up read past the table in the original; here they do nothing. */
static void Obj_RunHandler(uint8_t slot, Regs *r)
{
    uint8_t kind = gObjects->kind[slot];
    gScratch0 = Rom((uint16_t)(DT_CODE_OBJ_HANDLER_LO + kind));
    gScratch1 = Rom((uint16_t)(DT_CODE_OBJ_HANDLER_HI + kind));
    r->a = 0x22;
    r->y = kind;
    r->p = Flags_NZ(r->p, r->a);
    switch (kind) {
        case OBJ_KIND_NONE:
            break;
        case OBJ_KIND_HOPPER:
            Obj_Hopper(slot);
            break;
        case OBJ_KIND_WALKER:
            Obj_Walker(slot);
            break;
        case OBJ_KIND_THROWER:
            Obj_Thrower(slot);
            break;
        case OBJ_KIND_SHOT:
            Obj_Shot(slot);
            break;
        case OBJ_KIND_WATCHER:
            Obj_Watcher(slot);
            break;
        case OBJ_KIND_DANGLER:
            Obj_Dangler(slot);
            break;
        case OBJ_KIND_SNAPPER:
            Obj_Snapper(slot);
            break;
        case OBJ_KIND_BLOCK:
            Obj_Block(slot, kind);
            break;
        case OBJ_KIND_THROWN:
            Obj_Thrown(slot);
            break;
        case OBJ_KIND_DEFEATED:
            Obj_Defeated(slot);
            break;
        case OBJ_KIND_DROPPER:
            Obj_Dropper(slot);
            break;
        case OBJ_KIND_CRAWLER:
            Obj_Crawler(slot);
            break;
        case OBJ_KIND_FLYER:
            Obj_Flyer(slot);
            break;
        case OBJ_KIND_SWING:
            Obj_Swing(slot);
            break;
        case OBJ_KIND_LIFT:
            Obj_Lift(slot);
            break;
        case OBJ_KIND_CHARGER:
            Obj_Charger(slot);
            break;
        case OBJ_KIND_SWOOPER:
            Obj_Swooper(slot);
            break;
        case OBJ_KIND_BLOCK_DWELLER:
            Obj_BlockDweller(slot);
            break;
        case OBJ_KIND_PICKUP:
            Obj_Pickup(slot);
            break;
        case OBJ_KIND_HIDDEN_ITEM:
            Obj_HiddenItem(slot);
            break;
        case OBJ_KIND_CHEST:
            Obj_Chest(slot);
            break;
        case OBJ_KIND_CHEST_OPENING:
            Obj_ChestOpening(slot);
            break;
        case OBJ_KIND_CHEST_SLIDER:
            Obj_ChestSlider(slot);
            break;
        case OBJ_KIND_PATROLLER:
            Obj_Patroller(slot);
            break;
        case OBJ_KIND_POUNCER:
            Obj_Pouncer(slot);
            break;
        case OBJ_KIND_PIT_JUMPER:
            Obj_PitJumper(slot);
            break;
        case OBJ_KIND_BOUNCER:
            Obj_Bouncer(slot);
            break;
        case OBJ_KIND_LEAPER:
            Obj_Leaper(slot);
            break;
        case OBJ_KIND_BLOCK_KICKER:
            Obj_BlockKicker(slot);
            break;
        case OBJ_KIND_LEDGE_WALKER:
            Obj_LedgeWalker(slot);
            break;
        case OBJ_KIND_PACER:
            Obj_Pacer(slot);
            break;
        case OBJ_KIND_BOBBER:
            Obj_Bobber(slot);
            break;
        case OBJ_KIND_ROLLER:
            Obj_Roller(slot);
            break;
        case OBJ_KIND_BOULDER:
            Obj_Boulder(slot);
            break;
        case OBJ_KIND_CEILING_HOPPER:
            Obj_CeilingHopper(slot);
            break;
        case OBJ_KIND_TRANSYLVANIA_BOSS:
            Obj_TransylvaniaBoss(slot);
            break;
        case OBJ_KIND_RISING_SHOT:
            Obj_RisingShot(slot);
            break;
        case OBJ_KIND_MINES_BOSS:
            Obj_MinesBoss(slot);
            break;
        case OBJ_KIND_MOON_BOSS:
            Obj_MoonBoss(slot, r->p & P_C);
            break;
        case OBJ_KIND_HIMALAYAS_BOSS:
            Obj_HimalayasBoss(slot);
            break;
        case OBJ_KIND_DROPPING:
            Obj_Dropping(slot);
            break;
        case OBJ_KIND_TREAT_SPOUT:
            Obj_TreatSpout(slot);
            break;
        case OBJ_KIND_TREAT:
            Obj_Treat(slot);
            break;
        case OBJ_KIND_AMAZON_BOSS:
            Obj_AmazonBoss(slot, r->p & P_C);
            break;
        case OBJ_KIND_DOOR:
            Obj_Door(slot);
            break;
        case OBJ_KIND_THIEF:
        case OBJ_KIND_SHORT_THIEF:
            Obj_Thief(slot);
            break;
        case OBJ_KIND_PICKPOCKET:
            Obj_Pickpocket(slot);
            break;
        case OBJ_KIND_TARGET:
            Obj_Target(slot);
            break;
        case OBJ_KIND_LAUNCHER:
            Obj_Launcher(slot);
            break;
        case OBJ_KIND_TALKER:
            Obj_Talker(slot);
            break;
        case OBJ_KIND_LAUNCHPAD_RIDE:
            Obj_LaunchpadRide(slot);
            break;
        case OBJ_KIND_SKY_FLYER:
            Obj_SkyFlyer(slot);
            break;
        case OBJ_KIND_SKY_ROPE:
            Obj_SkyRope(slot);
            break;
        case OBJ_KIND_FINAL_STAND_IN:
            Obj_FinalStandIn(slot, r->p & P_C);
            break;
        case OBJ_KIND_STAND_IN_BEATEN:
            Obj_StandInBeaten(slot);
            break;
        case OBJ_KIND_CLIMBER:
            Obj_Climber(slot);
            break;
        case OBJ_KIND_HIT_TALKER:
            Obj_HitTalker(slot);
            break;
        case OBJ_KIND_FINAL_TREASURE:
            Obj_FinalTreasure(slot);
            break;
        case OBJ_KIND_MOVING_PLATFORM:
            Obj_MovingPlatform(slot);
            break;
        case OBJ_KIND_VISITOR:
            Obj_Visitor(slot);
            break;
        case OBJ_KIND_PATH_OPENER:
            Obj_PathOpener(slot);
            break;
        case OBJ_KIND_KEY_SPOT:
            Obj_KeySpot(slot);
            break;
        case OBJ_KIND_CART:
            Obj_Cart(slot);
            break;
        case OBJ_KIND_HOP_WALKER:
            Obj_HopWalker(slot);
            break;
        case OBJ_KIND_CHEST_HEALTH:
            Obj_ChestHealth(slot);
            break;
    }
}

/* Bank 6 $8000: once per frame from the frame loop. Returns with X = 16,
 * which the code after it passes on (a sound request keeps it in $00). */
static void Obj_UpdateAll(Regs *r)
{
    Decomp_Enter(DT_B6_OBJECTS, r->a, r->x, r->y);
    gGravity = OBJ_GRAVITY;
    r->a = OBJ_GRAVITY;
    for (gObjCursor = 1; gObjCursor != OBJ_SLOTS; gObjCursor++) {
        r->a = gObjects->state[gObjCursor];
        if (r->a & OBJ_STATUS_LIVE) {
            Obj_RunHandler(gObjCursor, r);
        }
        r->x = (uint8_t)(gObjCursor + 1);
        r->p = Flags_Cmp(r->p, r->x, OBJ_SLOTS);
    }
}

/* ---- adapters ---- */

static void Run_UpdateAll(Regs *r)
{
    Obj_UpdateAll(r);
}

static void Run_SpawnRecord(Regs *r)
{
    r->x = Obj_SpawnRecord(r->y);
}

const DecompRoutine gObjDispatchRoutines[] = {
    { DT_B6_OBJECTS, "ducktales.decomp.6.8000", "Obj_UpdateAll", Run_UpdateAll, OUT_X, .bank = DT_BANK_OBJECTS },
    { DT_CODE_SPAWN_RECORD, "ducktales.decomp.6.AF2B", "Obj_SpawnRecord", Run_SpawnRecord, OUT_X,
      .bank = DT_BANK_OBJECTS },
};
const int gObjDispatchRoutineCount = sizeof gObjDispatchRoutines / sizeof gObjDispatchRoutines[0];
