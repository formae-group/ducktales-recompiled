/*
 * native_romless.c - the build with no ROM in it (DUCKTALES_ROMLESS,
 * docs/ROMLESS.md): the part of the generated code the native runtime calls,
 * without the generated code. No address has compiled code, so every
 * instruction the decompiled routines leave to the original runs in
 * NESRecomp's instruction templates (cpu6502_interp.c) over the PRG the player
 * imported. The hook sites come from config/game.toml (tools/hook_sites.py).
 */
#include "cyc_recomp.h"

const char *cyc_native_display_name = NULL;

bool cyc_native_has(uint16_t addr)
{
    (void)addr;
    return false;
}

void cyc_native_run(void)
{
}
