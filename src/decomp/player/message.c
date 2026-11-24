/*
 * player/message.c - modes 15 and 16, task code (decomp.h) reached at their
 * own sites: the message box with its letters, paging and paid choices, and
 * the treasure's count-down (docs/PLAYER.md, docs/DECOMPILATION.md).
 */
#include "decomp/player/player_internal.h"

/* ---- modes 15 and 16: task code (decomp.h), reached at their own sites ---- */

#define gMessageId       RAM_U8(DT_ADDR_SCENE_STEP)
#define gMessageNext     RAM_U8(DT_ADDR_MESSAGE) /* offset of the next letter; 0 once the text is done */
#define gMessageText     DT_ADDR_MESSAGE_TEXT
#define gMessageAt       RAM_U8(DT_ADDR_MESSAGE_AT)
#define gMessageLine     RAM_U8(DT_ADDR_MESSAGE_LINE)
#define gMessageSlot     RAM_U8(DT_ADDR_MESSAGE_SLOT)
#define gLineEntry       RAM_U8(DT_ADDR_SCRATCH10) /* queue offset of the line's entry */
#define gLineCount       RAM_U8(DT_ADDR_SCRATCH11) /* the entry's byte count - 1 */
#define gVramPending     RAM_U8(DT_ADDR_VRAM_PENDING)
#define gQueue(i)        RAM_U8(DT_ADDR_VRAM_QUEUE + (i))
#define gMoneyDigit(i)   RAM_U8(DT_ADDR_MONEY_DIGITS + (i))
#define gTimeDigit(i)    RAM_U8(DT_ADDR_TIME_DIGITS + 4 * (i))
#define gCollision(i)    RAM_U8(DT_ADDR_COLLISION_MAP + (i))
#define gLaunchpadRides  RAM_U8(DT_ADDR_LAUNCHPAD_RIDES)
#define gBgOffBelowSplit RAM_U8(DT_ADDR_BG_OFF_BELOW_SPLIT)
#define gHudMoneyDirty   RAM_U8(DT_ADDR_HUD_MONEY_DIRTY)
#define gLandLeft        RAM_U8(DT_ADDR_LAND_LEFT)
#define gSpritePhase     RAM_U8(DT_ADDR_SPR_PHASE)
#define gScene           RAM_U8(DT_ADDR_SCENE)

#define BANK_TILE_OFF   1
#define BANK_TILE       2
#define BANK_MESSAGES   4
#define TILE_ENTRY_HI   0x12 /* the queue entry of CHR tile $12D: PPU $12D0, 16 bytes */
#define TILE_ENTRY_LO   0xD0
#define TILE_BYTES      0x10
#define QUEUE_END       0xFF
#define BOX_ROW_HI      0x20
#define TILE_BLANK      0x24
#define TILE_TEXT_END   0x2E /* the mark after the last letter */
#define TEXT_END        0xAE
#define TEXT_LINE_END   0xFF
#define LINE_LETTERS    0x10
#define BOX_LINES       3
#define LETTER_FRAMES   3
#define SFX_LETTER      0x2E
#define SFX_TIME_BONUS  0x15
#define MUSIC_NONE      0xFF
#define MUSIC_FRAMES    0 /* task_wait counts 256 frames */
#define LEAVE_FRAMES    0x3C
#define PAY_COST        3 /* taken from the second money digit */
#define SCENE_LAUNCHPAD 0x81

#define MESSAGE_KEY_CHECK 0x03 /* the African Mines key not taken: Transylvania next (hypothesis) */
#define MESSAGE_BLOCK     0x06
#define MESSAGE_PAY       0x0A
#define MESSAGE_SCENE     0x0B
#define MESSAGE_LIFE      0x0C
#define MESSAGE_LAUNCHPAD 0x0F

#define BLOCK_STATE  0xA0
#define BLOCK_ACTION 0x1B
#define BLOCK_Y      0xB8
#define BLOCK_X      0x30
#define BLOCK_TIMER  0x0B
#define BLOCK_CELL   0x21 /* the two collision cells the block's draw clears */
#define BLOCK_CELL_2 0x29
#define BLOCK_TILE   0
#define LIFT_STATE   0xE0 /* bits kept */
#define LIFT_Y       0x80
#define LIFT_X       0x80

/* bank_select: the C reads the bank's ROM too, so its view follows. */
static void Task_SelectBank(Regs *r, uint8_t bank)
{
    Decomp_SelectBank(bank);
    Game_MapperWrite((uint16_t)(DT_CODE_BANK_TABLE + bank), bank);
    r->a = r->x = bank;
}

static void Task_Wait(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_WAIT, resume, OUT_X | OUT_Y);
}

static void Task_Yield(uint16_t resume)
{
    Decomp_Handoff(DT_CODE_TASK_YIELD, resume, OUT_X | OUT_Y);
}

/* JSR from task code to task code in C: true when the callee returned. */
static bool Task_Gosub(void (*routine)(Regs *), Regs *r, uint16_t resume)
{
    Decomp_PushReturn(resume);
    routine(r);
    if (Decomp_HandedOff()) {
        return false;
    }
    Decomp_PullReturn();
    return true;
}

/* Queue CHR tile $12D from the bank's copy at tile; the NMI uploads it. */
static void Message_QueueTile(Regs *r, uint8_t bank, uint16_t tile)
{
    Task_SelectBank(r, bank);
    gQueue(0) = TILE_ENTRY_HI;
    gQueue(1) = TILE_ENTRY_LO;
    gQueue(2) = TILE_BYTES - 1;
    for (int i = TILE_BYTES - 1; i >= 0; i--) {
        gQueue(3 + i) = r->a = Game_ReadRom((uint16_t)(tile + i));
    }
    r->x = 0xFF;
    gQueue(3 + TILE_BYTES) = r->x;
    gVramPending = r->x;
}

/* $EF20: end the line's queue entry at x and count it. */
static void Message_EndEntry(Regs *r, uint8_t extra)
{
    gQueue(r->x) = QUEUE_END;
    r->x = gLineEntry;
    r->a = (uint8_t)(gLineCount + extra);
    gQueue(2 + r->x) = r->a;
    gVramPending = r->a = 0xFF;
}

/* $EEAE: write the box's lines from gMessageLine on, a letter every 3 frames
 * with a blip (spaces are free). A line ends at 16 letters or at $FF, and
 * its entry is padded with spaces; the text ends at $AE with the end mark.
 * The box holds 3 lines: the page ends with text left (gMessageNext) and
 * the wait scrolls it. letter: resume at the next letter, x the queue. */
static void Message_Write(Regs *r, bool letter)
{
    for (;;) {
        if (!letter) {
            r->y = gMessageLine;
            gMessageAt = gLineEntry = r->x = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_AT + r->y));
            gLineCount = 0xFF;
            gQueue(r->x) = BOX_ROW_HI;
            gQueue(r->x + 1) = r->a = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_LO + r->y));
        }
        letter = false;
        for (;;) {
            r->y = gMessageNext;
            r->a = Game_ReadRom((uint16_t)(Ram_GetU16(gMessageText, gMessageText + 1) + r->y));
            if (r->a == TEXT_END) {
                gQueue(r->x + 3) = TILE_TEXT_END;
                gQueue(r->x + 4) = QUEUE_END;
                r->x = gLineEntry;
                gQueue(2 + r->x) = (uint8_t)(gLineCount + 1);
                gVramPending = 0xFF;
                gMessageNext = r->a = 0;
                return;
            }
            if (r->a == TEXT_LINE_END) {
                gMessageNext++;
                break;
            }
            gQueue(r->x + 3) = r->a;
            r->x++;
            gLineCount++;
            gMessageNext++;
            if (r->a == TILE_BLANK) {
                continue;
            }
            gMessageAt = r->x;
            r->a = gLineCount;
            if (r->a == LINE_LETTERS) {
                break;
            }
            r->a = SFX_LETTER;
            Sound_Request(SFX_LETTER, r->x);
            r->x = (uint8_t)(r->x + 3);
            Message_EndEntry(r, 0);
            r->x = LETTER_FRAMES;
            Task_Wait(DT_MESSAGE_LETTER_NEXT);
            return;
        }
        r->x = (uint8_t)(r->x + 3);
        for (;;) {
            gMessageAt = r->x;
            r->y = gMessageLine;
            r->a = Game_ReadRom((uint16_t)(DT_CODE_MESSAGE_LINE_END + r->y));
            if (r->a == gMessageAt) {
                break;
            }
            gQueue(r->x) = r->a = TILE_BLANK;
            gLineCount++;
            if (++r->x == 0) {
                break;
            }
        }
        Message_EndEntry(r, 0);
        r->a = gMessageLine;
        if (r->a == BOX_LINES - 1) {
            return;
        }
        gMessageLine++;
    }
}

void Message_Text(Regs *r)
{
    Decomp_Enter(DT_CODE_MESSAGE_TEXT, r->a, r->x, r->y);
    Message_Write(r, false);
}

void Message_LetterNext(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_LETTER_NEXT, r->a, r->x, r->y);
    r->x = gMessageAt;
    Message_Write(r, true);
}

/* $EE8B: lines 2 and 3 move up, the last one is cleared. */
void Message_Scroll(Regs *r)
{
    Decomp_Enter(DT_CODE_MESSAGE_SCROLL, r->a, r->x, r->y);
    for (int i = LINE_LETTERS; i >= 0; i--) {
        gQueue(3 + i) = gQueue(0x17 + i);
        gQueue(0x17 + i) = gQueue(0x2B + i);
        gQueue(0x2B + i) = r->a = TILE_BLANK;
    }
    r->x = 0xFF;
    gVramPending = r->x;
    Task_Yield(DT_MESSAGE_SCROLLED);
}

void Message_Scrolled(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_SCROLLED, r->a, r->x, r->y);
    gMessageLine = BOX_LINES - 1;
    gMessageAt = r->a = 0x28;
    Message_Text(r);
}

/* $EE22: closing message $0A with A pays 30,000 (2 digits, blanks as 0)
 * when money has it, and makes the speaker's slot a lift. */
static void Message_Pay(Regs *r)
{
    r->a = gPadNew & DT_PAD_B;
    if (r->a) {
        return;
    }
    gScratch(0) = PAY_COST;
    uint8_t p = P_C;
    for (int x = 1; x >= 0; x--) {
        uint8_t d = gMoneyDigit(x) == TILE_BLANK ? 0 : gMoneyDigit(x);
        d = Alu_Sbc(d, gScratch(0), &p);
        if (!(p & P_C)) {
            d = Alu_Adc(d, 10, &p);
            p &= (uint8_t)~P_C;
        }
        gScratch(1 + x) = d;
        gScratch(0) = r->a = 0;
    }
    r->x = 0xFF;
    if (!(p & P_C)) {
        return;
    }
    for (int x = 1; x >= 0; x--) {
        gMoneyDigit(x) = r->a = gScratch(1 + x);
    }
    for (r->x = 0; (r->a = gMoneyDigit(r->x)) == 0;) {
        gMoneyDigit(r->x) = r->a = TILE_BLANK;
        if (++r->x == DT_MONEY_DIGIT_COUNT) {
            break;
        }
    }
    gHudMoneyDirty++;
    r->x = gMessageSlot;
    gObjects->kind[r->x] = OBJ_KIND_LIFT;
    gObjects->state[r->x] &= LIFT_STATE;
    gObjects->y[r->x] = LIFT_Y;
    gObjects->worldX[r->x] = LIFT_X;
    gObjects->hitbox[r->x] = r->a = (uint8_t)(HITBOX_SOLID | gObjects->hitbox[r->x]);
}

/* $EDCF: closing message $06 puts a block in the speaker's slot, clears two
 * collision cells and draws the block. */
static void Message_PlaceBlock(Regs *r)
{
    r->x = gMessageSlot;
    gObjects->state[r->x] = BLOCK_STATE;
    gObjects->kind[r->x] = OBJ_KIND_BLOCK;
    gObjects->action[r->x] = BLOCK_ACTION;
    gObjects->y[r->x] = BLOCK_Y;
    gObjects->worldX[r->x] = BLOCK_X;
    gObjects->animFrame[r->x] = 0;
    gObjects->animTimer[r->x] = 0;
    gObjects->unk_0690[r->x] = 0;
    gCollision(BLOCK_CELL) = 0;
    gCollision(BLOCK_CELL_2) = 0;
    gObjects->timer[r->x] = r->a = BLOCK_TIMER;
    r->y = BLOCK_TILE;
    Decomp_Handoff(DT_CODE_BLOCK_DRAW, 0, OUT_Y);
}

/* $ED60: A or B ends the page: the next one scrolls in, or the box closes
 * (the mode back to 0, the pause text cleared, the tile restored). */
void Message_Wait(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_WAIT, r->a, r->x, r->y);
    r->a = gPadNew & (DT_PAD_A | DT_PAD_B);
    if (!r->a) {
        Task_Yield(DT_MESSAGE_WAIT_NEXT);
        return;
    }
    r->a = gMessageNext;
    if (r->a) {
        if (Task_Gosub(Message_Scroll, r, DT_MESSAGE_PAGED)) {
            Task_Yield(DT_MESSAGE_WAIT_NEXT);
        }
        return;
    }
    Decomp_Push(r->a = gPadNew);
    gPlayerMode = r->a = PLAYER_MODE_GROUND;
    Task_SelectBank(r, DT_BANK_SPRITES);
    Decomp_Handoff(DT_B5_PAUSE_TEXT, DT_MESSAGE_CLEARED, OUT_X | OUT_Y);
}

void Message_Cleared(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_CLEARED, r->a, r->x, r->y);
    Message_QueueTile(r, BANK_TILE_OFF, DT_B1_MESSAGE_TILE_OFF);
    Task_Yield(DT_MESSAGE_CLOSED);
}

void Message_WaitNext(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_WAIT_NEXT, r->a, r->x, r->y);
    Message_Wait(r);
}

void Message_Paged(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_PAGED, r->a, r->x, r->y);
    Task_Yield(DT_MESSAGE_WAIT_NEXT);
}

/* $EDAA: the box is gone; some messages do something as it closes. */
void Message_Closed(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_CLOSED, r->a, r->x, r->y);
    gPadNew = r->a = Decomp_Pull();
    r->a = gMessageId;
    switch (r->a) {
        case MESSAGE_PAY:
            Message_Pay(r);
            return;
        case MESSAGE_LIFE:
            Player_PitDeath();
            return;
        case MESSAGE_BLOCK:
            Message_PlaceBlock(r);
            return;
        case MESSAGE_LAUNCHPAD:
            r->a = gPadNew & DT_PAD_B;
            if (!r->a) {
                r->y = gLand;
                gLaunchpadRides = r->a = gLaunchpadRides | Game_ReadRom((uint16_t)(DT_CODE_LAND_BIT + r->y));
                gScene = r->a = SCENE_LAUNCHPAD;
            }
            return;
        case MESSAGE_KEY_CHECK:
            r->a = gKeyItems & 2;
            if (!r->a) {
                gLandRedirect++;
            }
            return;
        case MESSAGE_SCENE:
            gBgOffBelowSplit = r->a = 0;
            return;
        default:
            return;
    }
}

/* $ED33: the sprites are rebuilt and the box's frame drawn (the pause text
 * of mode 15, which waits for frames). */
void Message_Shown(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_SHOWN, r->a, r->x, r->y);
    r->a = 0;
    gMessageNext = gMessageAt = gMessageLine = 0;
    Task_SelectBank(r, DT_BANK_SPRITES);
    Oam_Clear();
    r->y = Oam_Build();
    r->x = RAM_U8(DT_ADDR_SPR_SLOT);
    Decomp_Handoff(DT_B5_PAUSE_TEXT, DT_MESSAGE_DRAWN, OUT_X | OUT_Y);
}

/* bank 5 $B76A (bank 5 mapped): the land's records whose progress flag is
 * set get their consumed bit. Per land a list (its first index at
 * DT_B5_PROGRESS_FIRST): the count - 1, then per record the flag's address in
 * page 0, its mask and the record's bit number in $0120. $B760 clears the bits
 * first and runs into it. */
void Records_MarkTaken(Regs *r)
{
    Decomp_Enter(DT_B5_PROGRESS_BITS, r->a, r->x, r->y);
    r->x = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FIRST + gLand));
    gScratch(0) = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FLAG + r->x));
    r->x++;
    do {
        r->y = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_FLAG + r->x));
        r->a = RAM_U8(r->y) & Game_ReadRom((uint16_t)(DT_B5_PROGRESS_MASK + r->x));
        if (r->a) {
            uint8_t bit = Game_ReadRom((uint16_t)(DT_B5_PROGRESS_RECORD + r->x));
            gScratch(1) = (uint8_t)(1 << (bit & 7));
            r->y = bit >> 3;
            r->a = RAM_U8(DT_ADDR_CONSUMED + r->y) |= gScratch(1);
        }
        r->x++;
        gScratch(0)--;
    } while (!(gScratch(0) & 0x80));
}

/* $ED49: the text of the box's frame is queued; the text begins. */
void Message_Drawn(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_DRAWN, r->a, r->x, r->y);
    Records_MarkTaken(r);
    Task_SelectBank(r, BANK_MESSAGES);
    r->x = gMessageId;
    RAM_U8(gMessageText) = Game_ReadRom((uint16_t)(DT_B4_MESSAGE_LO + r->x));
    RAM_U8(gMessageText + 1) = r->a = Game_ReadRom((uint16_t)(DT_B4_MESSAGE_HI + r->x));
    if (Task_Gosub(Message_Text, r, DT_MESSAGE_WAIT)) {
        Message_Wait(r);
    }
}

void Message_Opened(Regs *r)
{
    Decomp_Enter(DT_MESSAGE_OPENED, r->a, r->x, r->y);
    Message_QueueTile(r, BANK_TILE, DT_B2_MESSAGE_TILE);
    Task_Yield(DT_MESSAGE_SHOWN);
}

/* Mode 15, $ED05: a message box. The frame loop waits while it shows:
 * the box's tile is swapped in, the text is written page by page, each
 * page waits for A or B, and then the tile goes back and the message's
 * effect runs. */
void Player_Message(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_MESSAGE, r->a, r->x, r->y);
    r->a = gMessageId;
    if (r->a == MESSAGE_SCENE) {
        Decomp_Handoff(DT_CODE_MESSAGE_SCENE, DT_MESSAGE_OPENED, OUT_X | OUT_Y);
        return;
    }
    Message_Opened(r);
}

/* $EF70: every other frame a time unit turns into money (digit 3 + 1,
 * capped at 9,999,990); with no time left the land is left 60 frames on. */
static void Treasure_Tick(Regs *r)
{
    r->a = gSpritePhase & 1;
    if (!r->a) {
        return;
    }
    gObjects->animTimer[P_] = r->a = 0;
    gTimeDigit(2) = 0;
    r->a = gTimeDigit(0) | gTimeDigit(1);
    if (!r->a) {
        r->x = LEAVE_FRAMES;
        Task_Wait(DT_TREASURE_DONE);
        return;
    }
    gScratch(0) = 1;
    uint8_t p = P_C;
    for (int i = 1; i >= 0; i--) {
        uint8_t d = Alu_Sbc(gTimeDigit(i), gScratch(0), &p);
        if (!(p & P_C)) {
            d = (uint8_t)(d + 10);
        }
        gTimeDigit(i) = d;
        gScratch(0) = 0;
    }
    gScratch(0) = 1;
    p = 0;
    for (int x = 3; x >= 0; x--) {
        uint8_t d = Alu_Adc(gMoneyDigit(x), gScratch(0), &p);
        if (d == TILE_BLANK) {
            p = 0;
        } else {
            if (d > TILE_BLANK) {
                d = (uint8_t)(d - TILE_BLANK);
            }
            p = d >= 10 ? P_C : 0;
            if (p) {
                d = (uint8_t)(d - 10);
            }
        }
        gMoneyDigit(x) = d;
        gScratch(0) = 0;
    }
    r->x = 0xFF;
    if (p) {
        Money_Max();
    }
    r->a = SFX_TIME_BONUS;
    Sound_Request(SFX_TIME_BONUS, r->x);
    gHudMoneyDirty++;
}

/* Mode 16, $EF62: the treasure is held up. The music plays for 256 frames
 * first (the frame loop waits), then stops, and the time bonus runs. */
void Player_Treasure(Regs *r)
{
    Decomp_Enter(DT_CODE_PLAYER_TREASURE, r->a, r->x, r->y);
    r->a = gMusic;
    if (!(r->a & 0x80)) {
        r->x = MUSIC_FRAMES;
        Task_Wait(DT_TREASURE_WAITED);
        return;
    }
    Treasure_Tick(r);
}

void Treasure_Waited(Regs *r)
{
    Decomp_Enter(DT_TREASURE_WAITED, r->a, r->x, r->y);
    r->a = MUSIC_NONE;
    Music_Request(MUSIC_NONE, r->x);
    Treasure_Tick(r);
}

void Treasure_Done(Regs *r)
{
    Decomp_Enter(DT_TREASURE_DONE, r->a, r->x, r->y);
    gLandLeft++;
}
