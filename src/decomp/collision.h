/*
 * collision.h - the background probe and its snaps (collision.c). X = slot
 * in the original; here the slot is the argument, which they leave as it is.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The Y $D120 leaves: the count of the last point it tested, or that point's
 * metatile id >> 4 when it was special. The block searches $F332/$F3B7 then
 * leave the hitbox index of the last block they look at (objcommon.c). */
extern uint8_t gProbeExitY;

void Bg_Probe(int slot);
void Bg_SnapRight(int slot);
void Bg_SnapLeft(int slot);
void Bg_SnapHead(int slot);
void Bg_SnapFeet(int slot);
uint8_t Bg_ProbePoint(int slot, uint8_t point, bool *carry);
