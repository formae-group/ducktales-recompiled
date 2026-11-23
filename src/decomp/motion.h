/*
 * motion.h - the decompiled object motion primitives (motion.c). X = slot in
 * the original; here the slot is the argument.
 */
#pragma once

void Obj_ApplyVelocityY(int slot);
void Obj_ApplyGravity(int slot);
void Obj_MoveRight(int slot);
void Obj_MoveLeft(int slot);
void Obj_AddVelocityY(int slot);
void Obj_SubVelocityY(int slot);
