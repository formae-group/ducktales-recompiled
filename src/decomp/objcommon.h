/*
 * objcommon.h - the decompiled object routines of the fixed bank that every
 * object kind shares (objcommon.c). X = slot in the original; here the slot
 * is the argument, and routines that hand X on return it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

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
