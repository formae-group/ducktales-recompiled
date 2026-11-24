/*
 * flow_land.c - the land load: the per-land tables and palettes, then the
 * land's screen built behind a fade (strings, CHR, the rows), Scrooge placed
 * and the fade in. It owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define PPU_STRINGS_LAND    0 /* $BAB0 set drawn at every land load */
#define ROW_SCROLL_HI       0x24
#define SCROOGE_START_Y     0x40
#define SCROOGE_JUMP_VY     0x04BB /* take-off velocity, whole and fraction */
#define SCROOGE_START_SPEED 0x0180
#define SPRITE0_Y           0x2C
#define gRowScrollAddrHi    RAM_U8(DT_ADDR_ROW_SCROLL_ADDR_HI)

/* ---- lands ---- */

/* $D9F5: background colours of set (an offset of 16 per set) and the sprite
 * colours, into the palette as loaded ($0100) and as shown ($0300). */
void Land_LoadPalette(Regs *r, uint8_t set)
{
    Decomp_Enter(DT_CODE_LAND_PALETTE, r->a, set, r->y);
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t bg = Rom((uint16_t)(DT_DATA_PALETTES + (uint8_t)(set + i)));
        uint8_t sprite = Rom((uint16_t)(DT_DATA_SPRITE_PALETTE + i));
        gRam[DT_ADDR_PALETTE_SHOWN + i] = gRam[DT_ADDR_PALETTE + i] = bg;
        gRam[DT_ADDR_PALETTE_SHOWN + 0x10 + i] = gRam[DT_ADDR_PALETTE + 0x10 + i] = sprite;
        r->a = sprite;
    }
    r->x = (uint8_t)(set + 16);
    r->y = 16;
}

/* $D9CA: the current land's music, tileset bank, start screen, row and X,
 * and its palette. */
void Land_ApplyTables(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_TABLES, r->a, r->x, r->y);
    uint8_t land = gLand;
    MusicRequest(r, land);
    gRam[DT_ADDR_TILESET_BANK] = Rom((uint16_t)(DT_DATA_LAND_TILESET + land));
    gCameraScreen = Rom((uint16_t)(DT_DATA_LAND_SCREEN + land));
    gCameraPage = gObjects->worldPage[P_] = gStreamPage = gCameraScreen & 7;
    gObjects->worldX[P_] = Rom((uint16_t)(DT_DATA_LAND_START_X + land));
    gMapRow = Rom((uint16_t)(DT_DATA_LAND_ROW + land));
    Land_LoadPalette(r, (uint8_t)(land << 4));
}

/* ---- land load ---- */

/* $D79B: load the land in $F6. The picture fades out and rendering stops for
 * the redraw: both nametables cleared, the land's tables applied, the first
 * screens drawn, the HUD strings and the land's CHR uploaded, and the row's
 * sprite CHR streamed. Then the objects' spawn tables, Scrooge at his start
 * and the HUD, and the frame loop. */
void LandLoad(Regs *r)
{
    Decomp_Enter(DT_CODE_LAND_LOAD, r->a, r->x, r->y);
    Jsr(DT_CODE_FADE_OUT, DT_FLOW_LOAD_FADED, 0);
}

/* $D79E */
void LandLoad_Faded(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FADED, r->a, r->x, r->y);
    gHudSplit = 0;
    r->a = 0;
    Yield(DT_FLOW_LOAD_BLACK);
}

/* $D7A5 */
void LandLoad_Black(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_BLACK, r->a, r->x, r->y);
    Jsr(DT_CODE_SCREEN_OFF, DT_FLOW_LOAD_SCREEN_OFF, 0);
}

/* $D7A8 */
void LandLoad_ScreenOff(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_SCREEN_OFF, r->a, r->x, r->y);
    Call(r, DT_CODE_OAM_CLEAR);
    gObjects->state[1] = 0;
    r->a = NT_FILL_ADDR;
    r->x = NT_FILL_TILE;
    r->y = 0;
    Jsr(DT_CODE_NT_FILL, DT_FLOW_LOAD_FILLED, OUT_A | OUT_X | OUT_Y);
}

/* $D7B9 */
void LandLoad_Filled(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FILLED, r->a, r->x, r->y);
    Land_ApplyTables(r);
    r->a = Obj_ClearSlots(&r->x);
    Jsr(DT_CODE_DRAW_START, DT_FLOW_LOAD_DRAWN, 0);
}

/* $D7C2 */
void LandLoad_Drawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_DRAWN, r->a, r->x, r->y);
    SelectBank(r, DT_BANK_SPRITES);
    r->x = PPU_STRINGS_LAND;
    Jsr(DT_B5_PPU_STRINGS, DT_FLOW_LOAD_STRINGS, OUT_X);
}

/* $D7CC */
void LandLoad_Strings(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_STRINGS, r->a, r->x, r->y);
    r->x = gLand;
    Jsr(DT_CODE_CHR_LOAD, DT_FLOW_LOAD_CHR, OUT_X);
}

/* $D7D1 */
void LandLoad_Chr(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_CHR, r->a, r->x, r->y);
    Jsr(DT_CODE_DRAW_START, DT_FLOW_LOAD_REDRAWN, 0);
}

/* $D7D4 */
void LandLoad_Redrawn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_REDRAWN, r->a, r->x, r->y);
    gRowChrState = 0;
    r->a = 0;
    Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_LOAD_ROW_CHR, 0);
}

/* $D7DB: until the row's sprite CHR is in; then the spawn tables, Scrooge
 * at the land's start (facing right, standing, the jump velocity and walking
 * speed of the game) and TOTAL queued for drawing. */
void LandLoad_RowChr(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_ROW_CHR, r->a, r->x, r->y);
    r->a = gRowChrState;
    if (!(gRowChrState & 0x80)) {
        Jsr(DT_CODE_ROW_CHR_STREAM, DT_FLOW_LOAD_ROW_CHR, 0);
        return;
    }
    SelectBank(r, DT_BANK_OBJECTS);
    Call(r, DT_B6_SPAWN_TABLES);
    Call(r, DT_B6_SPAWN_RESET);
    gRowScroll = 0;
    gCameraX = 0;
    gRowScrollAddr = ROW_SCROLL_ADDR;
    gRowScrollAddrHi = ROW_SCROLL_HI;
    gObjects->state[P_] = SCROOGE_RESPAWN;
    gObjects->flags[P_] = SCROOGE_RESPAWN;
    gObjects->y[P_] = SCROOGE_START_Y;
    gObjects->animTimer[P_] = 0;
    gObjects->animFrame[P_] = 0;
    gObjects->action[P_] = 0;
    gObjects->xFrac[P_] = 0;
    gObjects->unk_0790[P_] = (uint8_t)SCROOGE_JUMP_VY;
    gObjects->unk_07A0[P_] = SCROOGE_JUMP_VY >> 8;
    gObjects->speedFrac[P_] = (uint8_t)SCROOGE_START_SPEED;
    gObjects->speed[P_] = SCROOGE_START_SPEED >> 8;
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_TOTAL_PACKET, DT_HUD_TOTAL_PACKET_LEN);
    r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET];
    r->y = 0xFF;
    gVramPending = 0xFF;
    Jsr(DT_CODE_SCREEN_ON, DT_FLOW_LOAD_SHOWN, 0);
}

/* $D837 */
void LandLoad_Shown(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_SHOWN, r->a, r->x, r->y);
    Yield(DT_FLOW_LOAD_FADE_IN);
}

/* $D83A: the HUD split on, and the picture fades in. */
void LandLoad_FadeIn(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_FADE_IN, r->a, r->x, r->y);
    gSprite0Y = SPRITE0_Y;
    gHudSplit = SPLIT_ON;
    r->a = SPLIT_ON;
    Jsr(DT_CODE_FADE_IN, DT_FLOW_LOAD_DONE, 0);
}

/* $D845: the land's state starts clear, the money packet, the HP dots, the
 * HUD time group and the land's start time; slot 1 is set dead, the HUD
 * may be drawn, the consumed records are marked, and the frame loop runs. */
void LandLoad_Done(Regs *r)
{
    Decomp_Enter(DT_FLOW_LOAD_DONE, r->a, r->x, r->y);
    gBossFight = 0;
    gHeldSlot = 0;
    gPlayerMode = 0;
    gLandLeft = 0;
    gPrevCameraX = 0;
    for (int i = 0; i < 16; i++) {
        gRam[DT_ADDR_HAZARD + i] = 0;
    }
    CopyRom(DT_ADDR_HUD_MONEY_PACKET, DT_DATA_HUD_MONEY, DT_HUD_MONEY_PACKET_LEN);
    r->a = Rom(DT_DATA_HUD_MONEY);
    r->x = 0xFF;
    r->y = 0xFF;
    Call(r, DT_CODE_HP_REFILL);
    CopyRom(DT_ADDR_HUD_GROUP_TIME, DT_DATA_HUD_TIME_GROUP, DT_HUD_TIME_GROUP_LEN);
    r->x = (uint8_t)(gLand << 1);
    gTimeDigit(0) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->x));
    gTimeDigit(1) = Rom((uint16_t)(DT_DATA_LAND_TIME + r->x + 1));
    gTickFrames = DT_TICK_FRAMES_RELOAD;
    gObjects->state[1] = 0xFF;
    gObjects->flags[1] = 0xFF;
    gObjects->record[1] = 0xFF;
    gHudGate = 0xFF;
    gObjects->y[1] = 0;
    gObjects->worldX[1] = 0;
    gObjects->hitbox[1] = 0;
    r->y = 0xFF;
    SelectBank(r, DT_BANK_SPRITES);
    Records_Consumed(r);
    FrameLoop(r);
}
