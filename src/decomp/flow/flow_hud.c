/*
 * flow_hud.c - the HUD of the game flow: the timer's tick and the refreshes
 * of the money, TOTAL and the whole HUD (plain routines: no frame waits). It
 * owns no settings or options.
 */
#include "decomp/flow/flow_internal.h"

#define SFX_TIME_LOW  0x36
#define TIME_LOW_TENS 3

/* ---- HUD ---- */

/* $E28C: the HUD time loses a second every 60 frames; at 000 the time box
 * shows the time-out word and the next frame kills Scrooge. Stops while he
 * dies or holds up a treasure. Below 30 seconds a warning sounds every 16
 * frames. */
void Timer_Tick(Regs *r)
{
    Decomp_Enter(DT_CODE_TIMER_TICK, r->a, r->x, r->y);
    if (gPlayerMode == PLAYER_MODE_TREASURE || gPlayerMode == PLAYER_MODE_DEATH) {
        return;
    }
    if (gTimeDigit(0) == DT_TIME_TILE_UP0) {
        gPlayerMode = PLAYER_MODE_DEATH;
        gFinalArena = 0;
        return;
    }
    Decomp_RaiseEvent(EVENT_TIMER_TICK);
    if (--gTickFrames == 0) {
        gTickFrames = DT_TICK_FRAMES_RELOAD;
        uint8_t borrow = 1;
        for (int i = 2; i >= 0; i--) {
            uint8_t digit = gTimeDigit(i);
            uint8_t next = (uint8_t)(digit - borrow);
            borrow = digit < borrow;
            gTimeDigit(i) = borrow ? (uint8_t)(next + 10) : next;
        }
        gScratch0 = 0;
        r->x = 0xFC;
        if ((gTimeDigit(0) | gTimeDigit(1) | gTimeDigit(2)) == 0) {
            gTimeDigit(0) = DT_TIME_TILE_UP0;
            gTimeDigit(1) = DT_TIME_TILE_UP1;
            gTimeDigit(2) = DT_TIME_TILE_UP2;
        }
    }
    if (gTimeDigit(0) == 0 && gTimeDigit(1) < TIME_LOW_TENS && !(gSpritePhase & 0x0F)) {
        SoundRequest(r, SFX_TIME_LOW);
    }
}

/* $DE6C: queue the money for drawing (after a pickup changed it). Blank
 * digits after the first shown one become zeros. Waits while the VRAM queue
 * is still pending. */
void Hud_RefreshMoney(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_MONEY, r->a, r->x, r->y);
    if (gVramPending) {
        return;
    }
    uint8_t first = 0;
    while (first < DT_MONEY_DIGIT_COUNT && gRam[DT_ADDR_MONEY_DIGITS + first] >= DT_MONEY_TILE_BLANK) {
        first++;
    }
    for (uint8_t i = first; i < DT_MONEY_DIGIT_COUNT; i++) {
        if (gRam[DT_ADDR_MONEY_DIGITS + i] == DT_MONEY_TILE_BLANK) {
            gRam[DT_ADDR_MONEY_DIGITS + i] = 0;
        }
    }
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_MONEY_PACKET, DT_HUD_MONEY_PACKET_LEN);
    gVramPending = 0xFF;
    gHudMoneyDirty = 0;
    r->a = 0;
    r->y = 0xFF;
}

/* $DEA3: queue TOTAL for drawing. */
void Hud_RefreshTotal(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_TOTAL, r->a, r->x, r->y);
    if (gVramPending) {
        return;
    }
    gHudTotalDirty = 0;
    Copy(DT_ADDR_VRAM_QUEUE, DT_ADDR_HUD_TOTAL_PACKET, DT_HUD_TOTAL_PACKET_LEN);
    gVramPending = 0xFF;
    r->a = gRam[DT_ADDR_HUD_TOTAL_PACKET];
    r->x = 0xFF;
}

/* $D60A: the TOTAL packet and the HP dots as a new game starts them. */
void Hud_Reset(Regs *r)
{
    Decomp_Enter(DT_CODE_HUD_TEMPLATE, r->a, r->x, r->y);
    CopyRom(DT_ADDR_HUD_TOTAL_PACKET, DT_DATA_HUD_TOTAL, DT_HUD_TOTAL_PACKET_LEN);
    CopyRom(DT_ADDR_HUD_SPRITES, DT_DATA_HUD_HP, DT_HUD_HP_TEMPLATE_LEN);
    r->a = Rom(DT_DATA_HUD_HP);
    r->y = 0xFF;
}
