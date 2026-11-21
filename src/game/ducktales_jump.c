#include "ducktales_jump.h"
#include "ducktales_symbols.h"

#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

static int s_percent = DT_JUMP_SCALE_STOCK;
static uint64_t s_rescaled;

/* Runs before the instruction at DT_SITE_JUMP_START, after the jump start
 * stored the take-off velocity in slot 0. Rewrites it scaled, as two CPU
 * stores would, and lets the original instruction run (return 0). */
static int on_jump_start(uint16_t addr)
{
    (void)addr;
    const uint16_t lo = DT_ROW_VY_FRAC + DT_SLOT_SCROOGE, hi = DT_ROW_VY + DT_SLOT_SCROOGE;
    uint32_t v = (uint32_t)cyc_mod_peek(hi) << 8 | cyc_mod_peek(lo);
    if (v & 0x8000) return 0;
    v = v * (uint32_t)s_percent / 100;
    if (v > 0x7FFF) v = 0x7FFF;
    cyc_mod_poke(lo, (uint8_t)v);
    cyc_mod_poke(hi, (uint8_t)(v >> 8));
    s_rescaled++;
    return 0;
}

NES_MOD_CONSTRUCTOR(dt_register_jump_hook)
{
    nes_mod_register_function_entry_plugin(DT_SITE_JUMP_START_ID, DT_SITE_JUMP_START, on_jump_start);
}

bool dt_jump_set_scale(int percent)
{
    if (percent < DT_JUMP_SCALE_MIN || percent > DT_JUMP_SCALE_MAX) return false;
    s_percent = percent;
    nes_mod_set_function_hook_enabled(DT_SITE_JUMP_START_ID, percent != DT_JUMP_SCALE_STOCK);
    return true;
}

int dt_jump_scale(void) { return s_percent; }

uint64_t dt_jump_rescaled(void) { return s_rescaled; }
