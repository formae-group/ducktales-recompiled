/*
 * flow_title.c - the main task's start and the title screen with the
 * difficulty select. It owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define MAIN_TASK_STACK    0xBF
#define TITLE_SEED         0x88
#define TITLE_OBJECTS      2
#define TITLE_STRINGS      2 /* $BAB0 set of the title */
#define TITLE_CHR          6 /* $C301 set of the title */
#define TITLE_PALETTE      0x60
#define TITLE_COLOUR_TEXT  0x11
#define TITLE_COLOUR_A     0x2C
#define TITLE_COLOUR_B     0x20
#define TITLE_PAD_MOVE     (DT_PAD_SELECT | DT_PAD_LEFT | DT_PAD_RIGHT)
#define TITLE_BLINK_FRAMES 0xB4
#define DIFFICULTY_COUNT   3

/* ---- title and difficulty ---- */

/* $D4E6: the main task starts (power-on and every reset): the title. */
void MainTask(Regs *r)
{
    Decomp_Enter(DT_CODE_MAIN_TASK, r->a, r->x, r->y);
    r->x = MAIN_TASK_STACK;
    Decomp_SetStack(MAIN_TASK_STACK);
    r->a = RAM_U8(DT_ADDR_PPU_CTRL);
    Decomp_Store(DT_PPU_CTRL, r->a);
    Yield(DT_FLOW_TITLE_START);
}

/* $D4F1: the generator's seed, the HUD packets, the title music; fade out. */
void Title_Start(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_START, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_RANDOM) = TITLE_SEED;
    Hud_Reset(r);
    MusicRequest(r, DT_MUSIC_TITLE);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_TITLE_FADED, 0);
}

/* $D500 */
void Title_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    gPlayerInvulnerable = 0;
    r->a = 0;
    Yield(DT_FLOW_TITLE_BLACK);
}

/* $D509 */
void Title_Black(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_BLACK, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_TITLE_SCREEN_OFF, 0);
}

/* $D50C: no scroll, both nametables cleared. */
void Title_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_SCREEN_OFF, r->a, r->x, r->y);
    RAM_U8(DT_ADDR_SCROLL_X) = 0;
    RAM_U8(DT_ADDR_SCROLL_Y) = 0;
    gCameraPage = 0;
    gCameraX = 0;
    r->a = NT_FILL_ADDR;
    r->x = NT_FILL_TILE;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_FLOW_TITLE_FILLED, OUT_A | OUT_X | OUT_Y);
}

/* $D51F: the title's strings. */
void Title_Filled(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FILLED, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    r->a = Obj_ClearSlots(&r->x);
    SelectBank(r, DT_BANK_SPRITES);
    r->x = TITLE_STRINGS;
    Jsr(DT_B5_PPU_STRINGS, DT_FLOW_TITLE_STRINGS, OUT_X);
}

/* $D52F: the title's CHR. */
void Title_Strings(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_STRINGS, r->a, r->x, r->y);
    r->x = TITLE_CHR;
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_TITLE_CHR, OUT_X);
}

/* $D534: the title's palette and its two objects (Scrooge and the
 * difficulty cursor). */
void Title_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_CHR, r->a, r->x, r->y);
    r->x = TITLE_PALETTE;
    Land_LoadPalette(r, TITLE_PALETTE);
    gRam[DT_ADDR_PALETTE_SHOWN + 0x10] = gRam[DT_ADDR_PALETTE + 0x10] = TITLE_COLOUR_TEXT;
    for (int slot = TITLE_OBJECTS - 1; slot >= 0; slot--) {
        uint16_t e = (uint16_t)(DT_DATA_TITLE_OBJECTS + 3 * slot);
        gObjects->state[slot] = 0x80;
        gObjects->flags[slot] = 0x80;
        gObjects->action[slot] = Rom(e);
        gObjects->y[slot] = Rom((uint16_t)(e + 1));
        gObjects->worldX[slot] = Rom((uint16_t)(e + 2));
        gObjects->animTimer[slot] = 0;
        gObjects->animFrame[slot] = 0;
        gObjects->worldPage[slot] = 0;
    }
    r->x = 0xFF;
    r->y = 0xFD;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1A] = gRam[DT_ADDR_PALETTE + 0x1A] = TITLE_COLOUR_A;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1E] = gRam[DT_ADDR_PALETTE + 0x1E] = TITLE_COLOUR_B;
    gRam[DT_ADDR_PALETTE_SHOWN + 0x1F] = gRam[DT_ADDR_PALETTE + 0x1F] = TITLE_COLOUR_A;
    r->a = TITLE_COLOUR_A;
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_TITLE_SHOWN, 0);
}

/* $D58B */
void Title_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_TITLE_FADE_IN);
}

/* $D58E */
void Title_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FADE_IN, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_IN, DT_FLOW_TITLE_LIT, 0);
}

/* $D591: the lives group of the HUD as a game starts it (lives = 2), then
 * EVENT_NEW_GAME, raised by the original at $D59C. */
void Title_Lit(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_LIT, r->a, r->x, r->y);
    CopyRom(DT_ADDR_HUD_GROUP_LIVES, DT_DATA_HUD_LIVES, DT_HUD_LIVES_LEN);
    Decomp_RaiseEvent(EVENT_NEW_GAME);
    r->a = Rom(DT_DATA_HUD_LIVES);
    r->y = 0xFF;
    Yield(DT_FLOW_TITLE_READY);
}

/* $D59F: Normal is preselected. */
void Title_Ready(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_READY, r->a, r->x, r->y);
    gDifficulty = 1;
    Title_Loop(r);
}

/* $D5A3 (and $D5D1 after each frame): Start goes to Land Select; Select,
 * Left or Right moves the cursor through the three difficulties (Left
 * upward, wrapping). */
void Title_Loop(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_LOOP, r->a, r->x, r->y);
    r->a = gPadNew & DT_PAD_START;
    if (r->a) {
        LandSelect(r);
        return;
    }
    r->a = gPadNew & TITLE_PAD_MOVE;
    if (r->a) {
        r->y = r->a & DT_PAD_LEFT;
        uint8_t difficulty = (uint8_t)(gDifficulty + Rom((uint16_t)(DT_DATA_DIFFICULTY_STEP + r->y)));
        if (difficulty >= DIFFICULTY_COUNT) {
            difficulty = (uint8_t)(difficulty + Rom((uint16_t)(DT_DATA_DIFFICULTY_STEP + r->y + 1)));
        }
        gDifficulty = difficulty;
        r->y = difficulty;
        gObjects->worldX[1] = Rom((uint16_t)(DT_DATA_DIFFICULTY_X + difficulty));
        SoundRequest(r, SFX_CURSOR);
    }
    if (Gosub(Title_Frame, r, DT_FLOW_TITLE_LOOP)) {
        Title_Loop(r);
    }
}

/* $D5D7: Scrooge's idle animation picks a new variant at random every 180
 * builder frames; the sprites, and the next frame. */
void Title_Frame(Regs *r)
{
    Decomp_Enter(DT_CODE_TITLE_FRAME, r->a, r->x, r->y);
    if (gObjects->animFrame[P_] == 0 && gObjects->animTimer[P_] == 1) {
        gObjects->animTimer[P_] = 0;
        r->a = 0;
        r->x = gSpritePhase;
        if (gSpritePhase == TITLE_BLINK_FRAMES) {
            gSpritePhase = 0;
            uint8_t *random = &gRam[DT_ADDR_RANDOM];
            random[0] = (uint8_t)(random[0] + random[1] + 1);
            gObjects->animFrame[P_] = random[0] & 1;
            r->a = random[0] & 1;
        }
    }
    Call(r, DT_CODE_OAM_CLEAR);
    SelectBank(r, DT_BANK_SPRITES);
    Call(r, DT_B5_BUILD_OAM);
    Yield(DT_FLOW_TITLE_FRAME_DONE);
}

/* $D609 */
void Title_FrameDone(Regs *r)
{
    Decomp_Enter(DT_FLOW_TITLE_FRAME_DONE, r->a, r->x, r->y);
}
