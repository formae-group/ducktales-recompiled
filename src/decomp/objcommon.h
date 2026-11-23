/*
 * objcommon.h - the decompiled object routines of the fixed bank that every
 * object kind shares (objcommon.c). X = slot in the original; here the slot
 * is the argument, and routines that hand X on return it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* |a - b| as the original computes it (SBC, then negate on a borrow); for
 * 16-bit world X only the low byte is kept. */
static inline uint8_t AbsDiff(uint8_t a, uint8_t b)
{
    return a >= b ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static inline uint8_t AbsDiff16(uint16_t a, uint16_t b)
{
    return (uint8_t)(a >= b ? a - b : b - a);
}

bool Obj_TouchScrooge(uint8_t slot);
bool Obj_TouchBlockH(uint8_t slot);
bool Obj_TouchBlockV(uint8_t slot);
uint8_t Obj_WalkRight(uint8_t slot, uint8_t first, uint8_t count);
uint8_t Obj_WalkLeft(uint8_t slot, uint8_t first, uint8_t count);
uint8_t Obj_Fall(uint8_t slot, uint8_t first, uint8_t count);
uint8_t Obj_Rise(uint8_t slot, uint8_t first, uint8_t count);
void Obj_Despawn(uint8_t slot);
void Obj_FaceScrooge(uint8_t slot);
uint8_t Obj_DistanceX(uint8_t slot);
uint8_t Obj_DistanceY(uint8_t slot);
void Obj_MarkConsumed(uint8_t slot);
void Obj_DifficultAction(uint8_t slot);
void Obj_SetSpeedIndex(uint8_t slot, uint8_t index);
bool Obj_FindFreeSlot(uint8_t *slot);
