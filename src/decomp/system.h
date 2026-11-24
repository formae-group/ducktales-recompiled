/*
 * system.h - the system routines of system.c and sprites.c that other
 * decompiled code calls in C.
 */
#pragma once
#include <stdint.h>

/* $C267: park every OAM entry below the screen, but the split sprite. The
 * original leaves A = $F8 and X = 0. */
void Oam_Clear(void);
/* bank 5 $8000 (bank 5 mapped): the sprite builder. Returns the Y the
 * original leaves; its X is the slot loop's end, DT_ADDR_SPR_SLOT. */
uint8_t Oam_Build(void);
/* $FFDE: map PRG bank at $8000 and remember it in $E7. */
void Bank_Select(uint8_t bank);
void Bank_SelectTemp(uint8_t bank);
