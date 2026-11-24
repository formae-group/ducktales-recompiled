/*
 * flow_select.c - Land Select: its screen, cursor and messages (bank 5), the
 * bank transfer of money with the TOTAL cap, the wait for A, the consumed-
 * record bits (bank 5) and the way to the final trip. It owns no settings or
 * options.
 */
#include "decomp/flow/flow_internal.h"

#define LAND_COUNT            5
#define LAND_SELECT_SCREEN    5 /* $F6 while the Land Select screen loads */
#define SELECT_OBJECTS        8
#define SELECT_WAIT           0x3C
#define SELECT_PAD_MOVE       (DT_PAD_UP | DT_PAD_DOWN)
#define SELECT_LAND_NAME      0x6F /* action of slots 2 and 4: the land's name */
#define SELECT_MESSAGE_PROMPT 0
#define SELECT_LAND_STRING    2 /* string of land 0's name in the Land Select messages */
#define CONSUMED_BYTES        0x20
#define SELECT_MESSAGE_ALL    3 /* all five treasures held */
#define SELECT_MESSAGE_FINAL  4
#define SFX_BANK_STEP         0x15
#define ENDING_TOTAL_PPU      0x206C
#define ENDING_MONEY_PPU      0x2043
#define BANK_STEPS            10 /* steps of the transfer per two frames */
#define BANK_WAIT             2
#define BANK_TOTAL_PPU        0x220A
#define BANK_MONEY_PPU        0x226B
#define BANK_PACKET           12 /* the money packet's offset in the VRAM queue */
#define BANK_MONEY_STEP_DIGIT 3  /* the money digit a step takes one from */
#define TOTAL_DIGITS          5  /* TOTAL's counted digits; two zeros follow */
#define gSelectMove           RAM_U8(DT_ADDR_SCRATCH10)
#define gBankSteps            RAM_U8(DT_ADDR_SCRATCH10)

/* ---- bank 5: Land Select messages and the consumed records ---- */

/* bank 5 $B81C: Land Select message X into the VRAM queue, which is then
 * marked ready. A message (its first entry at DT_B5_MESSAGE_LIST) is entries
 * of three bytes, a PPU address and a string (0: the land's name), up to a
 * negative byte; a string (pointers at DT_B5_STRING_LO/HI) is its length - 1
 * and its bytes. */
void Select_Message(Regs *r)
{
    Decomp_Enter(DT_B5_MESSAGE, r->a, r->x, r->y);
    uint8_t *q = &gRam[DT_ADDR_VRAM_QUEUE];
    uint8_t x = 0;
    uint8_t y = Rom((uint16_t)(DT_B5_MESSAGE_LIST + r->x));
    for (;;) {
        r->a = q[x] = Rom((uint16_t)(DT_B5_MESSAGE_ENTRIES + y));
        if (r->a & 0x80) {
            break;
        }
        q[x + 1] = Rom((uint16_t)(DT_B5_MESSAGE_ENTRIES + 1 + y));
        gScratch(0) = x;
        gScratch(1) = y;
        uint8_t string = Rom((uint16_t)(DT_B5_MESSAGE_ENTRIES + 2 + y));
        if (!string) {
            string = (uint8_t)(SELECT_LAND_STRING + gLand);
        }
        gScratch(2) = Rom((uint16_t)(DT_B5_STRING_LO + string));
        gScratch(3) = Rom((uint16_t)(DT_B5_STRING_HI + string));
        uint16_t text = (uint16_t)(gScratch(2) | gScratch(3) << 8);
        uint8_t i = 0;
        q[x + 2] = gScratch(4) = Rom(text);
        do {
            q[x + 3] = Rom((uint16_t)(text + ++i));
            x++;
        } while (!(--gScratch(4) & 0x80));
        x = (uint8_t)(x + 3);
        y = (uint8_t)(gScratch(1) + 3);
    }
    gVramQueueReady = r->a;
    r->x = x;
    r->y = y;
}

/* bank 5 $B800: the message Land Select shows on its way in: the final trip's
 * ($5E), else after a land its result (with a scene, the one that ends the
 * land), else the land list. The first two clear the objects' sub-states. */
void Select_ResultMessage(Regs *r)
{
    Decomp_Enter(DT_B5_SELECT_MESSAGE, r->a, r->x, r->y);
    r->x = gSelectMessage;
    if (!r->x) {
        r->a = gLandLeft;
        if (!r->a) {
            Select_Message(r);
            return;
        }
        r->x = gScene ? 2 : 1;
        r->a = gScene;
    }
    r->a = 0;
    gObjects->state[0] = gObjects->state[2] = gObjects->state[3] = gObjects->state[4] = 0;
    Select_Message(r);
}

/* bank 5 $B760: the land's consumed-record bits cleared, then set again for
 * the records whose progress flag is set (Records_MarkTaken, player.c). */
void Records_Consumed(Regs *r)
{
    Decomp_Enter(DT_B5_CONSUMED_BITS, r->a, r->x, r->y);
    for (int i = 0; i < CONSUMED_BYTES; i++) {
        RAM_U8(DT_ADDR_CONSUMED + i) = 0;
    }
    r->a = 0;
    r->x = 0xFF;
    Records_MarkTaken(r);
}

/* ---- Land Select ---- */

static void LandSelect_FinalTrip(Regs *r);
static void LandSelect_Loop(Regs *r);

/* $D8D9: the money Scrooge brought back is banked into TOTAL: both are shown
 * through the VRAM queue (TOTAL at $220A, the money at $226B), and each step
 * moves one unit of the money's fourth digit, ten steps every two frames with
 * a sound, until the money is blank. TOTAL stops at 99999. Only the queue's
 * copy of the money counts down; TOTAL's digits are kept in its packet. */
static void BankTransfer_Steps(Regs *r);

static void BankTransfer_To(Regs *r, uint16_t total_ppu, uint16_t money_ppu)
{
    uint8_t *q = &gRam[DT_ADDR_VRAM_QUEUE];
    q[0] = total_ppu >> 8;
    q[1] = (uint8_t)total_ppu;
    q[BANK_PACKET] = money_ppu >> 8;
    q[BANK_PACKET + 1] = (uint8_t)money_ppu;
    r->a = gRam[DT_ADDR_MONEY_DIGITS + DT_MONEY_DIGIT_COUNT - 1];
    if (r->a == DT_MONEY_TILE_BLANK) {
        return;
    }
    uint8_t x = gRam[DT_ADDR_HUD_TOTAL_PACKET + 2];
    uint8_t y = 0;
    q[2] = x;
    do {
        q[3 + y] = gRam[DT_ADDR_HUD_TOTAL_PACKET + 3 + y];
        y++;
        x--;
    } while (!(x & 0x80));
    x = gRam[DT_ADDR_HUD_MONEY_PACKET + 2];
    y = 0;
    q[BANK_PACKET + 2] = x;
    do {
        q[BANK_PACKET + 3 + y] = gRam[DT_ADDR_HUD_MONEY_PACKET + 3 + y];
        y++;
        x--;
    } while (!(x & 0x80));
    q[BANK_PACKET + 3 + y] = x;
    r->a = x;
    r->x = x;
    r->y = y;
    BankTransfer_Steps(r);
}

void BankTransfer(Regs *r)
{
    Decomp_Enter(DT_CODE_BANK_TRANSFER, r->a, r->x, r->y);
    BankTransfer_To(r, BANK_TOTAL_PPU, BANK_MONEY_PPU);
}

/* $F84A: TOTAL's five digits set to 9. */
void Total_Max(Regs *r)
{
    Decomp_Enter(DT_CODE_TOTAL_MAX, r->a, r->x, r->y);
    for (int i = TOTAL_DIGITS - 1; i >= 0; i--) {
        RAM_U8(DT_ADDR_TOTAL_DIGITS + i) = 9;
    }
    r->a = 9;
    r->y = 0xFF;
}

/* $FFC7: the same transfer as the ending starts, shown in the HUD (TOTAL at
 * $206C, the money at $2043). */
void Ending_BankTransfer(Regs *r)
{
    Decomp_Enter(DT_CODE_ENDING_BANK, r->a, r->x, r->y);
    BankTransfer_To(r, ENDING_TOTAL_PPU, ENDING_MONEY_PPU);
}

static void BankTransfer_Steps(Regs *r)
{
    uint8_t *q = &gRam[DT_ADDR_VRAM_QUEUE];
    uint8_t *money = &q[BANK_PACKET + 4];
    uint8_t *total = &q[4];
    gBankSteps = BANK_STEPS;
    for (;;) {
        uint8_t borrow = 1;
        for (int i = BANK_MONEY_STEP_DIGIT; i >= 0; i--) {
            int digit = money[i] == DT_MONEY_TILE_BLANK ? 0 : money[i];
            digit -= borrow;
            borrow = digit < 0;
            money[i] = (uint8_t)(digit < 0 ? digit + 10 : digit);
        }
        for (int i = 0; i < DT_MONEY_DIGIT_COUNT; i++) {
            if (money[i] == DT_MONEY_TILE_BLANK) {
                continue;
            }
            if (money[i] != 0) {
                break;
            }
            money[i] = DT_MONEY_TILE_BLANK;
        }
        uint8_t carry = 1;
        for (int i = TOTAL_DIGITS - 1; i >= 0; i--) {
            uint8_t digit = (uint8_t)(total[i] + carry);
            carry = 0;
            if (digit != DT_MONEY_TILE_BLANK) {
                if (digit > DT_MONEY_TILE_BLANK) {
                    digit -= DT_MONEY_TILE_BLANK;
                }
                if (digit >= 10) {
                    digit -= 10;
                    carry = 1;
                }
            }
            total[i] = digit;
            gRam[DT_ADDR_HUD_TOTAL_PACKET + 4 + i] = digit;
        }
        gScratch0 = 0;
        total[TOTAL_DIGITS] = total[TOTAL_DIGITS + 1] = 0;
        gRam[DT_ADDR_HUD_TOTAL_PACKET + 4 + TOTAL_DIGITS] = 0;
        gRam[DT_ADDR_HUD_TOTAL_PACKET + 5 + TOTAL_DIGITS] = 0;
        r->a = 0;
        r->x = 0xFF;
        if (carry) {
            Total_Max(r);
            for (int i = TOTAL_DIGITS - 1; i >= 0; i--) {
                total[i] = r->a;
            }
            r->x = 0xFF;
        }
        r->a = money[DT_MONEY_DIGIT_COUNT - 1];
        if (r->a == DT_MONEY_TILE_BLANK) {
            gVramPending = r->x;
            return;
        }
        if (--gBankSteps) {
            continue;
        }
        gVramPending = r->x;
        SoundRequest(r, SFX_BANK_STEP);
        r->x = BANK_WAIT;
        Jsr(DT_CODE_TASK_WAIT, DT_FLOW_BANK_NEXT, OUT_X | OUT_Y);
        return;
    }
}

/* $D9B8 */
void BankTransfer_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_BANK_NEXT, r->a, r->x, r->y);
    BankTransfer_Steps(r);
}

/* $D9BE: until A is pressed, a frame at a time; then X goes to $19 (the
 * bank transfer's exit, $D9BB). */
void WaitA(Regs *r)
{
    Decomp_Enter(DT_CODE_WAIT_A, r->a, r->x, r->y);
    r->a = gPadNew & DT_PAD_A;
    if (r->a) {
        gVramPending = r->x;
        return;
    }
    Yield(DT_FLOW_WAIT_A_NEXT);
}

/* $D9C7 */
void WaitA_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_WAIT_A_NEXT, r->a, r->x, r->y);
    WaitA(r);
}

/* $D630: the Land Select screen. The picture fades out and is redrawn with
 * rendering off from the land-5 entries of the per-land tables ($F6 is 5
 * meanwhile, the land kept on the stack), with its eight objects. */
void LandSelect(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_SELECT, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_SELECT_FADED, 0);
}

/* $D633 */
void LandSelect_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    r->a = 0;
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_SELECT_SCREEN_OFF, 0);
}

/* $D63A: the screen's CHR. */
void LandSelect_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_SCREEN_OFF, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    r->a = Obj_ClearSlots(&r->x);
    r->a = gLand;
    Decomp_Push(gLand);
    r->x = LAND_SELECT_SCREEN;
    gLand = LAND_SELECT_SCREEN;
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_SELECT_CHR, OUT_X);
}

/* $D64A (the land on the stack): the tables and the screen. */
void LandSelect_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_CHR, r->a, r->x, r->y);
    Land_ApplyTables(r);
    Jsr(DT_CODE_DRAW_SCREEN, DT_FLOW_SELECT_DRAWN, 0);
}

/* $D650 (the land on the stack): the land back, and the screen's objects. */
void LandSelect_Drawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_DRAWN, r->a, r->x, r->y);
    gLand = Decomp_Pull();
    for (int slot = SELECT_OBJECTS - 1; slot >= 0; slot--) {
        uint16_t e = (uint16_t)(DT_DATA_SELECT_OBJECTS + 3 * slot);
        gObjects->state[slot] = 0x80;
        gObjects->flags[slot] = 0x90;
        gObjects->action[slot] = Rom(e);
        gObjects->y[slot] = Rom((uint16_t)(e + 1));
        gObjects->worldX[slot] = Rom((uint16_t)(e + 2));
        gObjects->animTimer[slot] = 0;
        gObjects->animFrame[slot] = 0;
        gObjects->worldPage[slot] = 0;
    }
    r->a = 0;
    r->x = 0xFF;
    r->y = 0xFD;
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_SELECT_SHOWN, 0);
}

/* $D68A: the message for the way here (after a land, its result). */
void LandSelect_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_SHOWN, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_SPRITES);
    Select_ResultMessage(r);
    Yield(DT_FLOW_SELECT_FADE_IN);
}

/* $D695 */
void LandSelect_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_FADE_IN, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_IN, DT_FLOW_SELECT_LIT, 0);
}

/* $D698: straight to the final trip when $5E says so; after a land, a
 * second and the money banked into TOTAL; otherwise the cursor. */
void LandSelect_Lit(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_LIT, r->a, r->x, r->y);
    gHudGate = 0;
    r->a = gSelectMessage;
    if (gSelectMessage) {
        LandSelect_FinalTrip(r);
        return;
    }
    r->a = gLandLeft;
    if (!gLandLeft) {
        LandSelect_Cursor(r);
        return;
    }
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    r->x = SELECT_WAIT;
    Jsr(DT_CODE_TASK_WAIT, DT_FLOW_SELECT_WAITED, OUT_X | OUT_Y);
}

/* $D6B1 */
void LandSelect_Waited(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_WAITED, r->a, r->x, r->y);
    if (Gosub(BankTransfer, r, DT_FLOW_SELECT_BANKED)) {
        LandSelect_Banked(r);
    }
}

/* $D6B4: with all five treasures held, their message and the final trip;
 * otherwise A after the land's result, the prompt and the cursor. */
void LandSelect_Banked(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_BANKED, r->a, r->x, r->y);
    r->a = gTreasures & ALL_TREASURES;
    if (r->a != ALL_TREASURES) {
        if (Gosub(WaitA, r, DT_FLOW_SELECT_RESULT_READ)) {
            LandSelect_ResultRead(r);
        }
        return;
    }
    r->x = SELECT_MESSAGE_ALL;
    Select_Message(r);
    LandSelect_FinalTrip(r);
}

/* $D6C1: a second, then the take-off scene. */
static void LandSelect_FinalTrip(Regs *r)
{
    r->x = SELECT_WAIT;
    Jsr(DT_CODE_TASK_WAIT, DT_FLOW_SELECT_FINAL_WAIT, OUT_X | OUT_Y);
}

/* $D6C6 */
void LandSelect_FinalWait(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_FINAL_WAIT, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_FINAL_TRIP);
    if (Gosub(FinalTrip, r, DT_FLOW_SELECT_FINAL_SCENE)) {
        LandSelect_FinalScene(r);
    }
}

/* $D6CE: the final trip's message and A. */
void LandSelect_FinalScene(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_FINAL_SCENE, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_SPRITES);
    r->x = SELECT_MESSAGE_FINAL;
    Select_Message(r);
    if (Gosub(WaitA, r, DT_FLOW_SELECT_FINAL_READ)) {
        LandSelect_FinalRead(r);
    }
}

/* $D6DB: Transylvania loads, its arena counted as the final one. */
void LandSelect_FinalRead(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_FINAL_READ, r->a, r->x, r->y);
    r->a = LAND_TRANSYLVANIA;
    gLand = LAND_TRANSYLVANIA;
    gFinalArena++;
    LandLoad(r);
}

/* $D6E7 */
void LandSelect_ResultRead(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_RESULT_READ, r->a, r->x, r->y);
    r->x = SELECT_MESSAGE_PROMPT;
    Select_Message(r);
    Yield(DT_FLOW_SELECT_CURSOR);
}

/* The cursor (slot 0), the land's name (slots 2 and 4) and the map marker
 * (slot 3) on a land. */
static void LandSelect_Mark(uint8_t land)
{
    gObjects->y[0] = Rom((uint16_t)(DT_DATA_SELECT_CURSOR_Y + land));
    gObjects->action[2] = (uint8_t)(land + SELECT_LAND_NAME);
    gObjects->action[4] = (uint8_t)(land + SELECT_LAND_NAME);
    gObjects->y[3] = Rom((uint16_t)(DT_DATA_SELECT_MARK_Y + land));
    gObjects->worldX[3] = Rom((uint16_t)(DT_DATA_SELECT_MARK_X + land));
}

/* $D6EF: the cursor starts on the first land whose treasure is not held. */
void LandSelect_Cursor(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_CURSOR, r->a, r->x, r->y);
    uint8_t land = 0;
    while (gTreasures & Rom((uint16_t)(DT_DATA_LAND_BIT + land))) {
        land++;
        if (land == 0) {
            break;
        }
    }
    gLand = land;
    r->y = land;
    LandSelect_Mark(land);
    gObjects->state[0] = 0x80;
    gObjects->state[2] = 0x80;
    gObjects->state[4] = 0x80;
    gObjects->state[3] = 0x80;
    gObjects->flags[3] = 0x90;
    r->a = 0x90;
    LandSelect_Loop(r);
}

/* $D784: the sprites, and the next frame. */
static void LandSelect_Frame(Regs *r)
{
    Call(r, DT_CODE_OAM_CLEAR);
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    Yield(DT_FLOW_SELECT_NEXT);
}

/* $D72C: Up and Down move the cursor through the five lands (wrapping),
 * past the lands whose treasure is held; A loads the land. */
static void LandSelect_Loop(Regs *r)
{
    r->a = gPadNew & SELECT_PAD_MOVE;
    if (!r->a) {
        r->a = gPadNew & DT_PAD_A;
        if (r->a) {
            LandLoad(r);
            return;
        }
        LandSelect_Frame(r);
        return;
    }
    gSelectMove = r->a;
    uint8_t land;
    do {
        r->y = (gSelectMove >> 1) & 2;
        land = (uint8_t)(gLand + Rom((uint16_t)(DT_DATA_SELECT_STEP + r->y)));
        if (land >= LAND_COUNT) {
            land = (uint8_t)(land + Rom((uint16_t)(DT_DATA_SELECT_STEP + r->y + 1)));
        }
        gLand = land;
        r->y = land;
    } while (gTreasures & Rom((uint16_t)(DT_DATA_LAND_BIT + land)));
    LandSelect_Mark(land);
    gObjects->animFrame[2] = 0;
    gObjects->animTimer[2] = 0;
    gObjects->animFrame[4] = 0;
    gObjects->animTimer[4] = 0;
    SoundRequest(r, SFX_CURSOR);
    LandSelect_Frame(r);
}

/* $D792 */
void LandSelect_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_SELECT_NEXT, r->a, r->x, r->y);
    LandSelect_Loop(r);
}
