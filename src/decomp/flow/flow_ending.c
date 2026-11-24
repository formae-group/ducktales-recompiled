/*
 * flow_ending.c - the ending: its screens, the scene objects (bank 5), the
 * credits with their raster split (polling $2002) and the last wait. It owns
 * no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define STATUS_SPRITE0        0x40
#define CTRL_NAMETABLE_1      0x01
#define ENDING_WAIT           0x78
#define ENDING_LAND           9
#define MUSIC_ENDING          0x05
#define ENDING_TREASURES      0x7F
#define ENDING_SCREENS        6
#define ENDING_SCREEN_TEXT    3    /* the first screen with strings; the earlier ones are drawn as one */
#define ENDING_SCREEN_SPLIT   4    /* screens from here have the HUD split */
#define END_OBJ_STEPS         0x10 /* an object entry's first byte below this ends the list: slot 0's kind */
#define END_OBJ_FACING_RIGHT  0x19
#define END_STEP_LEAVE        3
#define END_STEP_DONE         5
#define END_LAST_SCENE        3
#define END_HOPS              0x0B
#define END_HOP_VY            4
#define END_HOP_GROUND        0xB4
#define END_WALK_STOP         0x90
#define END_LEAVE_SPEED       3
#define END_LEAVE_X           0x48
#define END_LEAVE_Y           0x78
#define END_WAIT_SECONDS      10
#define END_SECOND            60
#define END_LAST_SCREEN       5
#define END_LAST_SPLIT        0xB0
#define END_SPLIT_LIMIT       0xE8
#define END_MUSIC_LINE        0xB7
#define END_PAUSE_MASK        0x3F
#define MUSIC_CREDITS         0x07
#define ENDING_FILL           0x24
#define gOamSprite0Y     RAM_U8(DT_ADDR_OAM)
#define gCreditsPause    RAM_U8(DT_ADDR_SCRATCH10)
#define gEndingScreen    RAM_U8(DT_ADDR_STREAM_SCREEN) /* $24 during the ending */
#define gEndingVariant   RAM_U8(DT_ADDR_ENDING_VARIANT)
#define gEndingText      RAM_U8(DT_ADDR_SCRATCH10)

/* ---- bank 5: the ending's scene objects ---- */

/* bank 5 $B382: the objects of ending scene X (its first entry at
 * DT_B5_END_OBJ_LIST): from slot 0 entries of three bytes, the action, Y and
 * world X ($FF: the slot left out), up to a byte below $10, slot 0's kind (the
 * scene's step); then the sprites. Entry $19 faces right. */
void EndingObjects_Place(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_PLACE, r->a, r->x, r->y);
    uint8_t y = Rom((uint16_t)(DT_B5_END_OBJ_LIST + r->x));
    uint8_t x = 0;
    while ((r->a = Rom((uint16_t)(DT_B5_END_OBJ_ENTRIES + y))) >= END_OBJ_STEPS) {
        if (r->a != 0xFF) {
            gObjects->action[x] = r->a;
            gObjects->y[x] = Rom((uint16_t)(DT_B5_END_OBJ_ENTRIES + 1 + y));
            gObjects->worldX[x] = Rom((uint16_t)(DT_B5_END_OBJ_ENTRIES + 2 + y));
            gObjects->worldPage[x] = gObjects->animTimer[x] = gObjects->animFrame[x] = 0;
            gObjects->state[x] = 0x80;
            gObjects->flags[x] = y == END_OBJ_FACING_RIGHT ? 0xD0 : 0x90;
        }
        x++;
        y = (uint8_t)(y + 3);
    }
    gObjects->kind[0] = r->a;
    r->y = Oam_Build();
    r->x = RAM_U8(DT_ADDR_SPR_SLOT);
}

/* bank 5 $B380: the objects of ending screen $24. */
void EndingObjects(Regs *r)
{
    Decomp_Enter(DT_B5_ENDING_OBJECTS, r->a, r->x, r->y);
    r->x = gEndingScreen;
    EndingObjects_Place(r);
}

/* bank 5 $B4A2: step 3 with slot 0's timer at 0. */
static void EndingObjects_StartExit(void)
{
    gObjects->kind[0] = END_STEP_LEAVE;
    gObjects->state[0] = gObjects->animTimer[0] = 0x80;
    gObjects->timer[0] = 0;
}

/* bank 5 $B412: bounce eleven times before the exit. */
void EndingObjects_Hop(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_HOP, r->a, r->x, r->y);
    if (!(gObjects->state[0] & 0x0F)) {
        gObjects->timer[0] = END_HOPS;
        gObjects->vyFrac[0] = 0;
        gObjects->vy[0] = END_HOP_VY;
        gObjects->state[0]++;
    }
    if (gObjects->state[0] & 2) {
        Obj_ApplyGravity(0);
        Obj_ApplyVelocityY(0);
        if (gObjects->y[0] >= END_HOP_GROUND) {
            gObjects->vyFrac[0] = 0;
            gObjects->vy[0] = END_HOP_VY;
            gObjects->state[0]--;
        }
        return;
    }
    Obj_ApplyVelocityY(0);
    Obj_ApplyGravity(0);
    if (!(gObjects->vy[0] & 0x80)) {
        return;
    }
    gObjects->vyFrac[0] = gObjects->vy[0] = 0;
    gObjects->state[0]++;
    if (!--gObjects->timer[0]) {
        EndingObjects_StartExit();
    }
}

/* bank 5 $B46A: walk left to X $90, then wait before the exit. */
void EndingObjects_Walk(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_WALK, r->a, r->x, r->y);
    if (!(gObjects->state[0] & 0x0F)) {
        gObjects->timer[0] = gObjects->speedFrac[0] = 0;
        gObjects->speed[0] = 1;
        gObjects->state[0]++;
    }
    if (gObjects->worldX[0] >= END_WALK_STOP) {
        Obj_MoveLeft(0);
        return;
    }
    if (!--gObjects->timer[0]) {
        EndingObjects_StartExit();
    }
}

/* bank 5 $B48F: wait 256 frames before the exit. */
void EndingObjects_Wait(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_WAIT, r->a, r->x, r->y);
    if (!(gObjects->state[0] & 0x0F)) {
        gObjects->timer[0] = 0;
        gObjects->state[0]++;
        return;
    }
    if (!--gObjects->timer[0]) {
        EndingObjects_StartExit();
    }
}

/* bank 5 $B4B5: wait, walk left to X $48, rise to Y $78, then wait again
 * and place scene 3's objects with its palette. */
void EndingObjects_Exit(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_EXIT, r->a, r->x, r->y);
    if (!(gObjects->state[0] & 0x0F)) {
        if (--gObjects->timer[0]) {
            return;
        }
        gObjects->speedFrac[0] = gObjects->vyFrac[0] = 0;
        gObjects->speed[0] = gObjects->vy[0] = END_LEAVE_SPEED;
        gObjects->state[0]++;
    }
    if (gObjects->worldX[0] >= END_LEAVE_X) {
        Obj_MoveLeft(0);
        return;
    }
    if (gObjects->y[0] >= END_LEAVE_Y) {
        Obj_SubVelocityY(0);
        return;
    }
    if (--gObjects->timer[0]) {
        return;
    }
    r->x = END_LAST_SCENE;
    EndingObjects_Place(r);
    r->y = END_LAST_SCENE;
    Palette_LoadScene(END_LAST_SCENE);
    RAM_U8(DT_ADDR_PALETTE_REQUEST) = 0xFF;
    RAM_U8(DT_ADDR_PPU_CTRL) |= 1;
    gObjects->kind[0] = END_STEP_DONE;
}

/* bank 5 $B507: mark the scene done. */
void EndingObjects_Done(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_DONE, r->a, r->x, r->y);
    gObjects->kind[0] = END_STEP_DONE;
}

/* bank 5 $B400: a frame of the ending scene's slot 0 by its step (its kind,
 * handlers at DT_B5_END_OBJ_LO/HI, jumped to through $00). */
void EndingObjects_Update(Regs *r)
{
    Decomp_Enter(DT_B5_END_OBJ_UPDATE, r->a, r->x, r->y);
    r->x = 0;
    r->y = gObjects->kind[0];
    gScratch(0) = Rom((uint16_t)(DT_B5_END_OBJ_LO + r->y));
    gScratch(1) = r->a = Rom((uint16_t)(DT_B5_END_OBJ_HI + r->y));
    switch (r->y) {
        case 0:
            EndingObjects_Hop(r);
            return;
        case 1:
            EndingObjects_Walk(r);
            return;
        case 2:
            EndingObjects_Wait(r);
            return;
        case END_STEP_LEAVE:
            EndingObjects_Exit(r);
            return;
        default:
            EndingObjects_Done(r);
            return;
    }
}

/* ---- ending ---- */
static void EndingScreen_Stream(Regs *r);
static void EndingScreen_Strings(Regs *r);
static void EndingScreen_Drawn(Regs *r);
static void Ending_Frame(Regs *r);
static void Ending_Scroll(Regs *r);
static void Ending_WaitSecond(Regs *r);
static void Ending_Over(Regs *r);

/* $E1AF: the ending, in place of a frame of play. The first frame banks the
 * money into TOTAL in the HUD, waits two seconds with the ending music, picks
 * the variant ($AD) and shows the first ending screen; the later ones are
 * Ending_Frame. */
void Ending(Regs *r)
{
    Decomp_Enter(DT_CODE_ENDING, r->a, r->x, r->y);
    r->a = gEnding;
    if (gEnding & 0x80) {
        Ending_Frame(r);
        return;
    }
    gEnding |= 0x80;
    r->a = gEnding;
    if (Gosub(Ending_BankTransfer, r, DT_FLOW_ENDING_BANKED)) {
        Ending_Banked(r);
    }
}

/* $E1BA */
void Ending_Banked(Regs *r)
{
    Decomp_Enter(DT_FLOW_ENDING_BANKED, r->a, r->x, r->y);
    r->x = ENDING_WAIT;
    Jsr(DT_CODE_TASK_WAIT, DT_FLOW_ENDING_WAITED, OUT_X | OUT_Y);
}

/* $E1BF: the variant: 2 with all seven treasures and TOTAL in the millions,
 * 1 with TOTAL blank, 0 otherwise. */
void Ending_Waited(Regs *r)
{
    Decomp_Enter(DT_FLOW_ENDING_WAITED, r->a, r->x, r->y);
    MusicRequest(r, MUSIC_ENDING);
    r->x = ENDING_LAND;
    gLand = ENDING_LAND;
    Call(r, DT_CODE_MAP_HEADER);
    uint8_t variant = 2;
    bool best = false;
    r->a = gTreasures & ENDING_TREASURES;
    if (r->a == ENDING_TREASURES) {
        r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET + 4];
        best = r->a != DT_MONEY_TILE_BLANK;
    }
    if (!best) {
        variant = 1;
        r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET + 10];
        if (r->a != DT_MONEY_TILE_BLANK) {
            variant = 0;
        }
    }
    r->y = variant;
    gEndingScreen = variant;
    gEndingVariant = variant;
    if (Gosub(EndingScreen, r, DT_FLOW_ENDING_SHOWN)) {
        Ending_Shown(r);
    }
}

/* $E1EC: the next screen, after the last the first. */
void Ending_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_ENDING_SHOWN, r->a, r->x, r->y);
    gEndingScreen++;
    r->a = gEndingScreen;
    if (gEndingScreen == ENDING_SCREENS) {
        gEndingScreen = 0;
        r->a = 0;
    }
}

/* $E1F9: a frame of the ending after the first: the scene's objects until
 * slot 0's step is 5; then on the screens with the HUD split the credits
 * scroll, on the others ten seconds and the next screen. */
static void Ending_Frame(Regs *r)
{
    r->a = gObjects->kind[0];
    if (r->a != END_STEP_DONE) {
        SelectBank(r, DT_BANK_SPRITES);
        EndingObjects_Update(r);
        return;
    }
    r->a = gHudSplit;
    if (!gHudSplit) {
        r->a = END_WAIT_SECONDS;
        Ending_WaitSecond(r);
        return;
    }
    gHudSplit = 0;
    gCreditsPause = 0;
    Ending_Scroll(r);
}

/* $E212: the split (sprite 0's Y) a line down, the credits with it, until
 * line $E8 or on screen 5 $B0; then a frame. */
static void Ending_Scroll(Regs *r)
{
    r->a = ++gOamSprite0Y;
    if (r->a == END_SPLIT_LIMIT) {
        return;
    }
    r->x = gEndingScreen;
    if (gEndingScreen == END_LAST_SCREEN && r->a == END_LAST_SPLIT) {
        return;
    }
    Yield(DT_FLOW_END_SPLIT_WAIT);
}

/* $E229, in that frame: the sprite-0 hit of the last frame cleared (the
 * pre-render line), then this frame's, below which the credits' nametable
 * shows. Then ($E23C) every eighth line the scroll stops for 64 frames, but
 * on screen 0 from line $B7 on, where the credits music starts. */
void Ending_Split(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_SPLIT_WAIT, r->a, r->x, r->y);
    Decomp_BusWait(PPU_STATUS, STATUS_SPRITE0, 0);
    Decomp_BusWait(PPU_STATUS, STATUS_SPRITE0, STATUS_SPRITE0);
    Ppu_SetCtrl((uint8_t)(RAM_U8(DT_ADDR_PPU_CTRL) | CTRL_NAMETABLE_1));
    r->a = gOamSprite0Y & 7;
    if (r->a != 7) {
        Ending_Scroll(r);
        return;
    }
    r->a = gEndingScreen;
    if (!gEndingScreen && (r->a = gOamSprite0Y) >= END_MUSIC_LINE) {
        r->a = MUSIC_CREDITS;
        if (gMusic != MUSIC_CREDITS) {
            MusicRequest(r, MUSIC_CREDITS);
        }
        Ending_Scroll(r);
        return;
    }
    r->a = ++gCreditsPause & END_PAUSE_MASK;
    if (r->a) {
        Yield(DT_FLOW_END_SPLIT_WAIT);
        return;
    }
    gCreditsPause = 0;
    Ending_Scroll(r);
}

/* $E26B: a second (the seconds left on the stack). */
static void Ending_WaitSecond(Regs *r)
{
    Decomp_Push(r->a);
    r->x = END_SECOND;
    Jsr(DT_CODE_TASK_WAIT, DT_FLOW_END_SECOND, OUT_X | OUT_Y);
}

/* $E271: after the last second the next screen, from 4 on; after screen 5
 * the game ends here. */
void Ending_Second(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_SECOND, r->a, r->x, r->y);
    r->a = (uint8_t)(Decomp_Pull() - 1);
    if (r->a) {
        Ending_WaitSecond(r);
        return;
    }
    r->a = gEndingScreen;
    if (!gEndingScreen) {
        Ending_Over(r);
        return;
    }
    if (gEndingScreen < ENDING_SCREEN_SPLIT) {
        gEndingScreen = r->a = ENDING_SCREEN_SPLIT;
    }
    if (Gosub(EndingScreen, r, DT_FLOW_ENDING_SHOWN)) {
        Ending_Shown(r);
    }
}

/* $E286: frames, for ever. */
static void Ending_Over(Regs *r)
{
    Yield(DT_FLOW_END_OVER);
}

/* $E289 */
void Ending_OverNext(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_OVER, r->a, r->x, r->y);
    Ending_Over(r);
}

/* $E054: ending screen $24. The picture fades out (the objects stay for
 * screen 3), the screen's map is streamed in with rendering off (screens 0-2
 * are drawn on top of each other from screen 3 on), its strings, objects and
 * CHR, and it fades in with its sprites. */
void EndingScreen(Regs *r)
{
    Decomp_Enter(DT_CODE_ENDING_SCREEN, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_END_FADED, 0);
}

/* $E057 */
void EndingScreen_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_FADED, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_PPU_CTRL) &= 0xFE;
    gHudSplit = 0;
    gStreamPos = 0;
    gStreamPage = 0;
    r->a = gEndingScreen;
    if (gEndingScreen != ENDING_SCREEN_TEXT) {
        r->a = Obj_ClearSlots(&r->x);
        gObjects->state[0] = 0;
        gObjects->state[1] = 0;
        r->a = 0;
        Call(r, DT_CODE_OAM_CLEAR);
    }
    Yield(DT_FLOW_END_BLACK);
}

/* $E07C */
void EndingScreen_Black(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_BLACK, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_END_SCREEN_OFF, 0);
}

/* $E07F */
void EndingScreen_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_SCREEN_OFF, r->a, r->x, r->y);
    r->a = ENDING_FILL;
    r->x = ENDING_FILL;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_FLOW_END_FILLED, OUT_A | OUT_X | OUT_Y);
}

/* $E088: the screen's tileset and palettes. */
void EndingScreen_Filled(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_FILLED, r->a, r->x, r->y);
    r->y = gEndingScreen;
    r->a = Rom((uint16_t)(DT_DATA_ENDING_TILESET + r->y));
    gRam[DT_ADDR_TILESET_BANK] = r->a;
    Jsr(DT_CODE_PALETTE_SCENE, DT_FLOW_END_PALETTE, OUT_A | OUT_X | OUT_Y);
}

/* $E092: the screen streamed a block at a time (the screen kept on the
 * stack). */
void EndingScreen_Palette(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_PALETTE, r->a, r->x, r->y);
    r->a = gEndingScreen;
    Decomp_Push(r->a);
    EndingScreen_Stream(r);
}

static void EndingScreen_Stream(Regs *r)
{
    gStreamSteps = 1;
    gStreamQueueEnd = 0;
    r->a = 0;
    Jsr(DT_CODE_STREAM_BLOCK, DT_FLOW_END_STREAMED, OUT_A | OUT_X | OUT_Y);
}

/* $E0A0 (the screen on the stack): before screen 3 the map is streamed again
 * as screen 3's, from tileset 1; then the screen's strings. */
void EndingScreen_Streamed(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_STREAMED, r->a, r->x, r->y);
    gStreamPos++;
    r->a = gStreamPos & (ENDING_STREAM_STEPS - 1);
    if (r->a) {
        EndingScreen_Stream(r);
        return;
    }
    r->a = gEndingScreen;
    if (gEndingScreen < ENDING_SCREEN_TEXT) {
        gEndingScreen = ENDING_SCREEN_TEXT;
        gStreamPos = 0;
        gRam[DT_ADDR_TILESET_BANK] = 1;
        gStreamPage = 1;
        EndingScreen_Stream(r);
        return;
    }
    SelectBank(r, DT_BANK_SPRITES);
    r->x = gEndingScreen;
    r->y = Rom((uint16_t)(DT_DATA_ENDING_TEXT_AT + r->x));
    gEndingText = r->y;
    EndingScreen_Strings(r);
}

/* $E0CA: the screen's string sets; a set with bit 7 is picked by the
 * variant, or with 8 by the difficulty. */
static void EndingScreen_Strings(Regs *r)
{
    r->a = Rom((uint16_t)(DT_DATA_ENDING_TEXT + r->y));
    if (r->a == 0) {
        EndingScreen_Drawn(r);
        return;
    }
    if (r->a & 0x80) {
        r->a &= 0x0F;
        bool by_difficulty = r->a == 8;
        if (!by_difficulty) {
            r->a = (uint8_t)(r->a + gEndingVariant);
        }
        if (by_difficulty || r->a == 0) {
            r->a = (uint8_t)(r->a + gDifficulty);
        }
    }
    r->x = r->a;
    Jsr(DT_B5_PPU_STRINGS, DT_FLOW_END_STRINGS, OUT_X);
}

/* $E0E3 (the screen on the stack) */
void EndingScreen_NextString(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_STRINGS, r->a, r->x, r->y);
    gEndingText++;
    r->y = gEndingText;
    if (r->y) {
        EndingScreen_Strings(r);
        return;
    }
    EndingScreen_Drawn(r);
}

/* $E0E9: the screen back from the stack, its objects and CHR. */
static void EndingScreen_Drawn(Regs *r)
{
    gEndingScreen = Decomp_Pull();
    r->a = gEndingScreen;
    EndingObjects(r);
    r->y = gEndingScreen;
    r->x = Rom((uint16_t)(DT_DATA_ENDING_CHR + r->y));
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_END_CHR, OUT_X);
}

/* $E0F7 */
void EndingScreen_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_CHR, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_END_SHOWN, 0);
}

/* $E0FA */
void EndingScreen_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_END_NEXT);
}

/* $E0FD: screens 4 and 5 have the HUD split (sprite 0 at their Y); the
 * picture fades in with the sprites kept. */
void EndingScreen_Next(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_NEXT, r->a, r->x, r->y);
    r->a = gEndingScreen;
    if (gEndingScreen >= ENDING_SCREEN_SPLIT) {
        r->y = gEndingScreen & 1;
        gSprite0Y = Rom((uint16_t)(DT_DATA_ENDING_SPRITE0_Y + r->y));
        gHudSplit = SPLIT_ON;
        gCameraPage = 1;
        r->a = 1;
    }
    Jsr(DT_CODE_FADE_IN_KEEP, DT_FLOW_END_LIT, 0);
}

/* $E116: bank 5 mapped (JMP bank_select). */
void EndingScreen_Lit(Regs *r)
{
    Decomp_Enter(DT_FLOW_END_LIT, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_SPRITES);
}
