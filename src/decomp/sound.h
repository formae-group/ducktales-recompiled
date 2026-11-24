/*
 * sound.h - the decompiled sound request (sound.c). x is the caller's X
 * register, which the original keeps in scratch $00; the result is the
 * N, Z and C flags the routine returns with.
 */
#pragma once
#include <stdint.h>

#define SFX_LAND        0x10
#define SFX_POGO_BOUNCE 0x11
#define SFX_CLIMB       0x12
#define SFX_CANE_SWING  0x13
#define SFX_HURT        0x14
#define SFX_CANE_HIT    0x18
#define MUSIC_POWER     0x08

uint8_t Sound_Request(uint8_t id, uint8_t x);
uint8_t Music_Request(uint8_t id, uint8_t x);
void Sound_Play(uint8_t id, uint8_t y);
void Sound_Update(void);
