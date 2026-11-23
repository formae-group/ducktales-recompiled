/*
 * collision.h - the background probe and its snaps (collision.c). X = slot
 * in the original; here the slot is the argument, which they leave as it is.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Bg_Probe(int slot);
void Bg_SnapRight(int slot);
void Bg_SnapLeft(int slot);
void Bg_SnapHead(int slot);
void Bg_SnapFeet(int slot);
uint8_t Bg_ProbePoint(int slot, uint8_t point, bool *carry);
