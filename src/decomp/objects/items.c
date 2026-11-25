/*
 * objects/items.c - what Scrooge collects: money and HP, the pickup (kind $13),
 * the hidden item ($14) and the chest with its variants ($15-$17, $42).
 */
#include "decomp/objects/objects_internal.h"

/* $F83F: money is capped at 9,999,990 (the four high digits set to 9). */
void Money_Max(void)
{
    Decomp_Enter(DT_CODE_MONEY_MAX, 0, 0, 0);
    for (int i = 3; i >= 0; i--) {
        gMoneyDigit(i) = 9;
    }
}

/* $8BBF: add amount to money digit y (0 = most significant) with decimal
 * carries; a blank digit ($24) counts as 0 once something is added. */
static void Money_Add(uint8_t amount, uint8_t y)
{
    Decomp_Enter(DT_CODE_MONEY_ADD, amount, 0, y);
    uint8_t a = amount, c = 0;
    for (;;) {
        a = (uint8_t)(a + gMoneyDigit(y) + c);
        gMoneyDigit(y) = a;
        if (a == DT_MONEY_TILE_BLANK) {
            break;
        }
        if (a > DT_MONEY_TILE_BLANK) {
            gMoneyDigit(y) = a = (uint8_t)(a - DT_MONEY_TILE_BLANK);
        }
        if (a < 10) {
            break;
        }
        gMoneyDigit(y) = (uint8_t)(a - 10);
        a = 0;
        c = 1;
        if (y-- == 0) {
            Money_Max();
            break;
        }
    }
    gMoneyChanged++;
}

/* $F812: every HP dot full again. */
void Hud_RefillHp(void)
{
    Decomp_Enter(DT_CODE_HP_REFILL, 0, 0, 0);
    gPlayerEasyHits = 0;
    for (int y = 0x10; y >= 0; y -= 4) {
        HUD_RECORD(y / 4)[2] = 0;
    }
}

/* $8B82: the first lost HP dot becomes full. */
void Hud_RestoreDot(void)
{
    Decomp_Enter(DT_CODE_HUD_RESTORE_DOT, 0, 0, 0);
    gPlayerEasyHits = 0;
    for (uint8_t y = 0; !(HUD_RECORD(y / 4)[0] & 0x80); y = (uint8_t)(y + 4)) {
        if (HUD_RECORD(y / 4)[2] != 0) {
            HUD_RECORD(y / 4)[2] = 0;
            return;
        }
    }
}

/* $8B38: the extra HP dot (item $2B): one more dot in the HP group when it
 * has fewer than five, then full HP. In the Himalayas ($F6 = 1) it is
 * remembered in $AE. */
static void Pickup_ExtraDot(void)
{
    Decomp_Enter(DT_CODE_PICKUP_EXTRA_DOT, 0, 0, 0);
    if (gLand == 1) {
        gExtraDotTaken = 1;
    }
    uint8_t y = 0x0C;
    if (!(HUD_RECORD(3)[0] & 0x80)) {
        y = 0x10;
        if (!(HUD_RECORD(4)[0] & 0x80)) {
            return;
        }
    }
    HUD_RECORD(y / 4)[0] = 0x20;
    HUD_RECORD(y / 4 + 1)[0] = DT_HUD_RECORD_END_Y;
    Hud_RefillHp();
}

/* $8C13: a land treasure (items $6F and up): 100,000 more money, the
 * treasure bit in $A0, and Scrooge holds it up (mode $10), or for the
 * items $74 and up shows message $0D (mode $0F). */
static void Pickup_Treasure(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKUP_TREASURE, 0, x, 0);
    Sound_Request(SFX_TREASURE, x);
    gPogoCeiling = 0;
    Money_Add(1, 0);
    uint8_t item = gObjects->action[x];
    gTreasures |= Rom((uint16_t)(DT_CODE_BIT_TABLE + (uint8_t)(item - OBJ_ITEM_TREASURE)));
    if (item >= OBJ_ITEM_TREASURE_MESSAGE) {
        gPlayerMode = PLAYER_MODE_MESSAGE;
        gSceneStep = 0x0D;
    } else {
        gPlayerMode = PLAYER_MODE_TREASURE;
    }
}

/* $8B21: Scrooge takes the pickup (EVENT_ITEM_COLLECTED); the item is its
 * pose. */
static void Pickup_Take(uint8_t x)
{
    Decomp_Enter(DT_CODE_PICKUP_TAKE, 0, x, 0);
    Decomp_RaiseSlotEvent(EVENT_ITEM_COLLECTED, x);
    gObjects->state[x] = 0;
    uint8_t item = gObjects->action[x];
    if (item == OBJ_ITEM_EXTRA_DOT) {
        Pickup_ExtraDot();
        return;
    }
    if (item == OBJ_ITEM_BIG_DIAMOND) {
        Sound_Request(SFX_DIAMOND, x);
        Money_Add(5, 2);
        return;
    }
    if (item >= OBJ_ITEM_TREASURE) {
        Pickup_Treasure(x);
        return;
    }
    uint8_t i = (uint8_t)(item - OBJ_ITEM_FIRST);
    Sound_Request(Rom((uint16_t)(DT_CODE_ITEM_SOUND + i)), x);
    gScratch(0) = Rom((uint16_t)(DT_CODE_ITEM_TAKE_LO + i));
    gScratch(1) = Rom((uint16_t)(DT_CODE_ITEM_TAKE_HI + i));
    uint16_t at = (uint16_t)(gScratch(1) << 8 | gScratch(0));
    switch (at) {
        case 0x8B71:
            Money_Add(2, 3);
            break;
        case 0x8B82:
            Hud_RestoreDot();
            break;
        case 0x8B9C:
            Hud_RefillHp();
            break;
        case 0x8B9F:
            gPlayerInvulnerable = 0;
            gPlayerPower = 1;
            RAM_U8(DT_ADDR_POWER_FRAMES) = 0xE0;
            RAM_U8(DT_ADDR_POWER_FRAMES + 1) = 0x01;
            break;
        case 0x8BB0:
            if (gLives != 9) {
                gLives++;
            }
            break;
        case 0x8BBB:
            Money_Add(1, 2);
            break;
        case 0x8BE7:
            gKeyItems |= gLand == 4 ? 4 : 2;
            gSceneStep = gLand == 4 ? 0x10 : 0x0E;
            gPlayerMode = PLAYER_MODE_MESSAGE;
            break;
    }
}

static void Pickup_TryTake(uint8_t x)
{
    if (gPlayerMode == PLAYER_MODE_KNOCKBACK) {
        return;
    }
    if (gPlayerMode == PLAYER_MODE_DEATH) {
        gObjects->state[x] = 0;
        return;
    }
    if (Obj_TouchScrooge(x)) {
        Pickup_Take(x);
    }
}

/* Kind $13 ($8A59): a pickup. While in the jump-out pose ($2A) it shows
 * its item when the animation reaches frame 1, timer 5. Step 0 rises, step
 * 1 falls to the floor, step 2 waits; the timed items blink at $78 and
 * vanish at 0. Gone off screen, an item in pose $29 gives its record back
 * to the spawner. */
void Obj_Pickup(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_PICKUP, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        if (gObjects->action[x] == OBJ_ITEM_RESPAWNS) {
            uint8_t record = gObjects->record[x];
            gScratch(0) = (uint8_t)~Rom((uint16_t)(DT_CODE_BIT_TABLE + (record & 7)));
            RAM_U8(DT_ADDR_CONSUMED + (record >> 3)) &= gScratch(0);
        }
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->action[x] == OBJ_ACTION_BREAK) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
            return;
        }
        gObjects->action[x] = gObjects->timer[x];
        Obj_DifficultAction(x);
        ResetAnim(x);
    }
    if (Step(x) == 0) {
        Obj_ApplyVelocityY(x);
        Obj_ApplyGravity(x);
        if (!(gObjects->vy[x] & 0x80)) {
            return;
        }
        gObjects->state[x]++;
        Obj_SetVelocityY(x, 0);
    }
    if (!(gObjects->state[x] & 2)) {
        x = Obj_Fall(x, 0x2D, 1);
        if (gProbeHits & 1) {
            gObjects->state[x]++;
            gObjects->timer[x] = 0xFF;
        }
    } else {
        uint8_t item = gObjects->action[x];
        if (item == OBJ_ITEM_BIG_DIAMOND || item < OBJ_ITEM_RESPAWNS) {
            if (gObjects->timer[x] == 0) {
                gObjects->state[x] = 0;
                return;
            }
            if (gObjects->timer[x] == 0x78) {
                gObjects->flags[x] |= OBJ_FLAG_BLINK;
            }
            gObjects->timer[x]--;
        }
    }
    Pickup_TryTake(x);
}

/* $8CE1: the slot becomes a block (kind 8). In a boss fight it gets
 * $06B0 = land + 6, and the Moon's ($F6 = 4) also changes two sprite
 * colours. */
static void Obj_BecomeBlock(uint8_t x)
{
    Decomp_Enter(DT_CODE_OBJ_BECOME_BLOCK, 0, x, 0);
    gObjects->kind[x] = OBJ_KIND_BLOCK;
    gObjects->hitbox[x] = 0x80;
    gObjects->timer[x] = 0;
    gObjects->unk_06C0[x] = 0;
    gObjects->vdir[x] = 0;
    if (!(gBossFight & 0x80)) {
        return;
    }
    gObjects->timer[x] = (uint8_t)(gLand + 6);
    if (gObjects->timer[x] != 0x0A) {
        return;
    }
    RAM_U8(DT_ADDR_PALETTE_SHOWN + 0x1D) = RAM_U8(DT_ADDR_PALETTE + 0x1D) = 0x0F;
    RAM_U8(DT_ADDR_PALETTE_SHOWN + 0x1F) = RAM_U8(DT_ADDR_PALETTE + 0x1F) = 0x29;
    gPaletteUpload = 0x29;
}

/* $8CAC: the slot shows item: a pickup (kind $13, step 1, falling), or for
 * poses $1B and $22 a block again. */
void Obj_ShowItem(uint8_t x, uint8_t item)
{
    Decomp_Enter(DT_CODE_OBJ_SHOW_ITEM, item, x, 0);
    gObjects->action[x] = item;
    Obj_DifficultAction(x);
    ResetAnim(x);
    gObjects->hitbox[x] = 0;
    item = gObjects->action[x];
    if (item == OBJ_ACTION_CHEST || item == OBJ_ACTION_STAY) {
        Obj_BecomeBlock(x);
        return;
    }
    gObjects->kind[x] = OBJ_KIND_PICKUP;
    gObjects->state[x] = OBJ_STATUS_LIVE | OBJ_STATUS_WORLD | 1;
    Obj_SetVelocityY(x, 0);
    gObjects->timer[x] = 0;
    gObjects->unk_06C0[x] = 0;
}

/* Kind $14 ($8C45): a hidden item. Touched by Scrooge (when he is not
 * invulnerable) it marks its record consumed and after 30 frames turns into
 * the jump-out pose; at its frame 1, timer 5 it shows its item. */
void Obj_HiddenItem(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_HIDDEN_ITEM, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (Step(x) == 0) {
        if (gPlayerInvulnerable || !Obj_TouchScrooge(x)) {
            return;
        }
        Obj_MarkConsumed(x);
        gObjects->state[x]++;
        gObjects->unk_06C0[x] = 0x1E;
    }
    if (!(gObjects->state[x] & 2)) {
        if (--gObjects->unk_06C0[x] != 0) {
            return;
        }
        gObjects->state[x] &= (uint8_t)~OBJ_STATUS_HIDDEN;
        Obj_SetVelocityY(x, 0);
        gObjects->timer[x] = gObjects->action[x];
        gObjects->action[x] = OBJ_ACTION_BREAK;
        gObjects->state[x]++;
        return;
    }
    if (gObjects->action[x] == OBJ_ACTION_BREAK) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
            return;
        }
        Obj_ShowItem(x, gObjects->timer[x]);
        return;
    }
    Obj_BecomeBlock(x);
}

/* Kind $15 ($8D19): a chest (hitbox $81). Scrooge's cane opens it: its
 * record is consumed, it moves up by the land's offset, and it shows the
 * land's item, or becomes a pickup at once when the table has none. In the
 * Transylvania ($F6 = 1) a chest with $06B0 >= 2 is a solid block (pose $5B)
 * while Scrooge does not swing at it. */
void Obj_Chest(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHEST, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    gObjects->hitbox[x] = 0x81;
    if (gObjects->animTimer[P_] == 0 && gObjects->action[P_] == PLAYER_ACTION_CANE && gObjects->animFrame[P_] == 3 &&
        Obj_TouchScrooge(x)) {
        Obj_MarkConsumed(x);
        uint8_t y = (uint8_t)((gLand << 2) + gObjects->timer[x]);
        gObjects->y[x] -= Rom((uint16_t)(DT_CODE_CHEST_RISE + y));
        if (Rom((uint16_t)(DT_CODE_CHEST_ITEM + y)) == 0) {
            gObjects->timer[x] = 0x28;
            Obj_BecomePickupNow(x);
            return;
        }
        gObjects->hitbox[x] = 0;
        ResetAnim(x);
        gObjects->unk_0690[x] = 0;
        gObjects->state[x] &= 0xE0;
        if (y == 6) {
            gObjects->kind[x] = OBJ_KIND_CHEST_SLIDER;
            gObjects->hitbox[x] = 0x10;
            gObjects->action[x] = 0x21;
            return;
        }
        gObjects->action[x] = OBJ_ACTION_BREAK;
        gObjects->kind[x] = OBJ_KIND_CHEST_OPENING;
        return;
    }
    if (gLand != 1 || gObjects->timer[x] < 2) {
        return;
    }
    gObjects->state[x] &= 0xE0;
    if (gObjects->action[P_] == PLAYER_ACTION_CANE) {
        return;
    }
    gObjects->action[x] = 0x5B;
    gObjects->hitbox[x] = 0x82;
    if (AbsDiff(gObjects->y[x], gObjects->y[P_]) >= 0x18) {
        gObjects->hitbox[x]++;
    }
}

/* Kind $16 ($8DD1): at frame 1 or later, timer 5 of the opening pose,
 * show the chest's contents. Transylvania's fourth variant moves down
 * $18 px; its first plays sound $30. */
void Obj_ChestOpening(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHEST_OPENING, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
        return;
    }
    uint8_t i = (uint8_t)((gLand << 2) + gObjects->timer[x]);
    if (i == 7) {
        gObjects->y[x] += 0x18;
    }
    gObjects->action[x] = Rom((uint16_t)(DT_CODE_CHEST_CONTENT_ACTION + i));
    gObjects->kind[x] = Rom((uint16_t)(DT_CODE_CHEST_ITEM + i));
    gObjects->hitbox[x] = Rom((uint16_t)(DT_CODE_CHEST_CONTENT_HITBOX + i));
    ResetAnim(x);
    gObjects->unk_0690[x] = 0;
    if (i == 4) {
        Sound_Request(SFX_SWOOP, x);
    }
}

/* Kind $17 ($8E23): a block that shakes in place for eight sprite phases,
 * slides toward Scrooge, then turns into a regular block on the floor
 * when it stops or hits something. */
void Obj_ChestSlider(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHEST_SLIDER, 0x22, x, gObjects->kind[x]);
    if (!OnScreen(x)) {
        gObjects->state[x] = 0;
        return;
    }
    if (Step(x) == 0) {
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0200);
        gObjects->timer[x] = 0;
        Obj_FaceScrooge(x);
        gObjects->state[x]++;
    }
    if (gObjects->timer[x] != 8) {
        if (!(gSprPhase & 3)) {
            Obj_SetWorldX(x, (uint16_t)(Obj_GetWorldX(x) + (int8_t)Rom((uint16_t)(DT_CODE_CHEST_SLIDER_SHAKE +
                                                                                  (gObjects->timer[x] & 3)))));
            gObjects->timer[x]++;
        }
        return;
    }
    x = Obj_Fall(x, 0x2D, 1);
    gObjects->unk_06C0[x] = gProbeHits;
    uint16_t speed = Obj_GetSpeed(x);
    if ((gProbeHits & 1) && speed != 0) {
        Obj_SetSpeed(x, (uint16_t)(speed - 0x10));
    }
    if (!(gObjects->state[x] & 2)) {
        if (gObjects->dir[x] & OBJ_DIR_LEFT) {
            x = Obj_WalkLeft(x, 0x2B, 1);
        } else {
            x = Obj_WalkRight(x, 0x29, 1);
        }
        (void)Obj_TouchScrooge(x);
        if (Obj_GetSpeed(x) != 0 && !(gProbeHits & 1)) {
            return;
        }
    }
    if (gObjects->unk_06C0[x] & 1) {
        gObjects->kind[x] = OBJ_KIND_BLOCK;
        gObjects->hitbox[x] = 0x80;
    }
}

/* Kind $42 ($8EE6): a solid block the cane opens a second time. It releases
 * a sliding health pickup that moves until it has fallen and landed. */
void Obj_ChestHealth(uint8_t x)
{
    Decomp_Enter(DT_CODE_KIND_CHEST_HEALTH, 0x22, x, gObjects->kind[x]);
    if (Step(x) == 0) {
        bool swinging = gObjects->action[P_] == PLAYER_ACTION_CANE;
        if (swinging && gObjects->animFrame[P_] == 3) {
            gObjects->hitbox[x] = 0x81;
            if (Obj_TouchScrooge(x)) {
                gObjects->state[x]++;
                gObjects->action[x] = OBJ_ACTION_BREAK;
                ResetAnim(x);
                gObjects->hitbox[x] = 0;
                gObjects->y[x] -= 0x18;
                return;
            }
        }
        gObjects->hitbox[x] = 0x83;
        if (swinging && (uint8_t)(gObjects->y[x] - gObjects->y[P_]) >= 0x18) {
            gObjects->hitbox[x]++;
        }
        return;
    }
    if (gObjects->action[x] != 0x25 && gObjects->action[x] != 0x28) {
        if (gObjects->animFrame[x] == 0 || gObjects->animTimer[x] != 5) {
            return;
        }
        gObjects->action[x] = 0x25;
        Obj_DifficultAction(x);
        ResetAnim(x);
        gObjects->timer[x] = 0;
        Obj_SetVelocityY(x, 0);
        Obj_SetSpeed(x, 0x0120);
        Obj_FaceScrooge(x);
        return;
    }
    x = Obj_Fall(x, 0x2D, 1);
    if (Obj_TouchScrooge(x)) {
        Pickup_Take(x);
        return;
    }
    if (!(gProbeHits & 1)) {
        gObjects->timer[x]++;
    } else if (gObjects->timer[x] != 0) {
        gObjects->state[x] = OBJ_STATUS_LIVE | OBJ_STATUS_WORLD | 2;
        gObjects->timer[x] = 0xFF;
        Pickup_TryTake(x);
        return;
    }
    if (gObjects->dir[x] & OBJ_DIR_RIGHT) {
        (void)Obj_WalkRight(x, 0x29, 0);
    } else {
        (void)Obj_WalkLeft(x, 0x2B, 0);
    }
}

/* ---- adapters ---- */

static void Run_Pickup(Regs *r)
{
    Obj_Pickup(r->x);
}

static void Run_HiddenItem(Regs *r)
{
    Obj_HiddenItem(r->x);
}

static void Run_Chest(Regs *r)
{
    Obj_Chest(r->x);
}

static void Run_ChestOpening(Regs *r)
{
    Obj_ChestOpening(r->x);
}

static void Run_ChestSlider(Regs *r)
{
    Obj_ChestSlider(r->x);
}

static void Run_ChestHealth(Regs *r)
{
    Obj_ChestHealth(r->x);
}

static void Run_BecomeBlock(Regs *r)
{
    Obj_BecomeBlock(r->x);
}

static void Run_ShowItem(Regs *r)
{
    Obj_ShowItem(r->x, r->a);
}

static void Run_PickupTake(Regs *r)
{
    Pickup_Take(r->x);
}

static void Run_PickupTreasure(Regs *r)
{
    Pickup_Treasure(r->x);
}

static void Run_PickupExtraDot(Regs *r)
{
    Pickup_ExtraDot();
}

static void Run_RestoreDot(Regs *r)
{
    Hud_RestoreDot();
}

static void Run_MoneyAdd(Regs *r)
{
    Money_Add(r->a, r->y);
}

static void Run_RefillHp(Regs *r)
{
    Hud_RefillHp();
    r->a = 0;
    r->y = 0xFC;
}

static void Run_MoneyMax(Regs *r)
{
    Money_Max();
    r->a = 9;
    r->y = 0xFF;
}

const DecompRoutine gObjItemRoutines[] = {
    { DT_CODE_KIND_PICKUP, NULL, "Obj_Pickup", Run_Pickup, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_HIDDEN_ITEM, NULL, "Obj_HiddenItem", Run_HiddenItem, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHEST, NULL, "Obj_Chest", Run_Chest, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHEST_OPENING, NULL, "Obj_ChestOpening", Run_ChestOpening, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHEST_SLIDER, NULL, "Obj_ChestSlider", Run_ChestSlider, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_KIND_CHEST_HEALTH, NULL, "Obj_ChestHealth", Run_ChestHealth, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_BECOME_BLOCK, NULL, "Obj_BecomeBlock", Run_BecomeBlock, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_OBJ_SHOW_ITEM, NULL, "Obj_ShowItem", Run_ShowItem, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_TAKE, NULL, "Pickup_Take", Run_PickupTake, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_TREASURE, NULL, "Pickup_Treasure", Run_PickupTreasure, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_PICKUP_EXTRA_DOT, NULL, "Pickup_ExtraDot", Run_PickupExtraDot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_HUD_RESTORE_DOT, NULL, "Hud_RestoreDot", Run_RestoreDot, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_MONEY_ADD, NULL, "Money_Add", Run_MoneyAdd, 0, .bank = DT_BANK_OBJECTS },
    { DT_CODE_HP_REFILL, "ducktales.decomp.F812", "Hud_RefillHp", Run_RefillHp, OUT_A | OUT_Y },
    { DT_CODE_MONEY_MAX, "ducktales.decomp.F83F", "Money_Max", Run_MoneyMax, OUT_A | OUT_Y },
};
const int gObjItemRoutineCount = sizeof gObjItemRoutines / sizeof gObjItemRoutines[0];
