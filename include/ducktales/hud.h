/*
 * ducktales/hud.h - what the HUD shows (docs/memory-map.md "Other RAM").
 *
 * The HUD keeps its values as the sprite records and tile ids it draws: HP
 * dots are sprite records at $0340 (attribute bit 0 = lost), lives the tile
 * of the "P.n" digit, money and time decimal digit tiles.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "ducktales/ram.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_RECORD(i)   (&gRam[DT_ADDR_HUD_SPRITES + 4 * (i)])
#define gLives          RAM_U8(DT_ADDR_LIVES)

/* Full HP dots and dots shown (verified). */
static inline uint8_t Hud_GetHp(uint8_t *max)
{
    uint8_t full = 0;
    for (uint8_t i = 0; i < DT_HUD_SPRITE_RECORDS; i++) {
        if (HUD_RECORD(i)[0] == DT_HUD_RECORD_END_Y) {
            if (max) *max = i;
            return full;
        }
        if (!(HUD_RECORD(i)[2] & DT_HP_DOT_LOST)) full++;
    }
    if (max) *max = 0;
    return 0;
}

/* Money as the HUD shows it, trailing 0 included; false while a digit tile is
 * not 0-9 or blank (verified). */
static inline bool Hud_GetMoney(uint32_t *money)
{
    uint32_t value = 0;
    for (int i = 0; i < DT_MONEY_DIGIT_COUNT; i++) {
        uint8_t tile = gRam[DT_ADDR_MONEY_DIGITS + i];
        if (tile == DT_MONEY_TILE_BLANK) tile = 0;
        else if (tile > 9) return false;
        value = value * 10 + tile;
    }
    *money = value * 10;
    return true;
}

/* HUD time, 0-999, from its three digit tiles (verified). */
static inline unsigned Hud_GetTime(void)
{
    return gRam[DT_ADDR_TIME_DIGITS] * 100u + gRam[DT_ADDR_TIME_DIGITS + 4] * 10u + gRam[DT_ADDR_TIME_DIGITS + 8];
}

#ifdef __cplusplus
}
#endif
