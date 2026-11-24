/*
 * player.h - routines of src/decomp/player.c that other decompiled code calls.
 */
#pragma once
#include "decomp/decomp.h"

/* bank 5 $B76A (bank 5 mapped): the consumed bits of the records whose
 * progress flag is set. */
void Records_MarkTaken(Regs *r);
