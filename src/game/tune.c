/*
 * tune.c - Scrooge's tunables (ducktales/tune.h).
 *
 * Three have a [[mod_function_hook]] site right after the stores of the
 * game's value (config/game.toml, evidence in ducktales_symbols.h): while
 * the tunable is set its site is on and stores the tuned value over the
 * game's, as the next instruction would find it. The take-off velocity has
 * no site: the land set-up stores it once, and Tune_FrameEnd rewrites it.
 *
 * In check mode's isolated call a site changes nothing: the original runs
 * there only to be compared with the C, which uses the game's values outside
 * replace mode. Replace mode's measuring call takes the tuned path, as the C
 * does (src/decomp/decomp.c).
 */
#include "ducktales/tune.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"

#include "decomp/decomp.h"

#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stddef.h>

static int32_t s_value[TUNE_COUNT] = { TUNE_UNSET, TUNE_UNSET, TUNE_UNSET, TUNE_UNSET };
static int32_t s_jump_written = TUNE_UNSET; /* the take-off velocity Tune_FrameEnd last stored */

static const char *const SITE[TUNE_COUNT] = {
    [TUNE_GRAVITY] = DT_SITE_TUNE_GRAVITY_ID,
    [TUNE_WALK_SPEED] = DT_SITE_TUNE_WALK_ID,
    [TUNE_POGO_VELOCITY] = DT_SITE_TUNE_POGO_ID,
};

void Tune_Set(TuneId id, int32_t value)
{
    if (id >= TUNE_COUNT) {
        return;
    }
    s_value[id] = value;
    if (SITE[id]) {
        nes_mod_set_function_hook_enabled(SITE[id], value != TUNE_UNSET);
    }
}

int32_t Tune_Get(TuneId id)
{
    return id < TUNE_COUNT ? s_value[id] : TUNE_UNSET;
}

void Tune_FrameEnd(void)
{
    int32_t v = s_value[TUNE_JUMP_VELOCITY];
    if (v == TUNE_UNSET && s_jump_written == TUNE_UNSET) {
        return;
    }
    GameRam_Begin();
    int32_t now = RAM_U8(DT_ADDR_JUMP_VY) << 8 | RAM_U8(DT_ADDR_JUMP_VY_FRAC);
    if (now == DT_JUMP_VY_STOCK || now == s_jump_written) {
        uint16_t want = v == TUNE_UNSET ? DT_JUMP_VY_STOCK : (uint16_t)v;
        RAM_U8(DT_ADDR_JUMP_VY) = (uint8_t)(want >> 8);
        RAM_U8(DT_ADDR_JUMP_VY_FRAC) = (uint8_t)want;
        s_jump_written = v;
    } else if (v == TUNE_UNSET) {
        s_jump_written = TUNE_UNSET;
    }
    GameRam_End();
}

/* ---- the sites (cyc_hooks.h): before the instruction after the stores ---- */

static bool applies(TuneId id)
{
    return s_value[id] != TUNE_UNSET && !(cyc_mod_isolated() && Decomp_Mode() == DECOMP_CHECK);
}

static int on_gravity(uint16_t addr)
{
    (void)addr;
    if (applies(TUNE_GRAVITY)) {
        cyc_mod_poke(DT_ADDR_GRAVITY, (uint8_t)s_value[TUNE_GRAVITY]);
    }
    return 0;
}

static int on_walk(uint16_t addr)
{
    (void)addr;
    if (applies(TUNE_WALK_SPEED)) {
        cyc_mod_poke(DT_ROW_SPEED_FRAC + PLAYER_SLOT, (uint8_t)s_value[TUNE_WALK_SPEED]);
        cyc_mod_poke(DT_ROW_SPEED + PLAYER_SLOT, (uint8_t)(s_value[TUNE_WALK_SPEED] >> 8));
    }
    return 0;
}

static int on_pogo(uint16_t addr)
{
    (void)addr;
    if (applies(TUNE_POGO_VELOCITY)) {
        cyc_mod_poke(DT_ROW_VY_FRAC + PLAYER_SLOT, (uint8_t)s_value[TUNE_POGO_VELOCITY]);
        cyc_mod_poke(DT_ROW_VY + PLAYER_SLOT, (uint8_t)(s_value[TUNE_POGO_VELOCITY] >> 8));
    }
    return 0;
}

NES_MOD_CONSTRUCTOR(dt_register_tune_sites)
{
    nes_mod_register_function_entry_plugin(DT_SITE_TUNE_GRAVITY_ID, DT_SITE_TUNE_GRAVITY, on_gravity);
    nes_mod_register_function_entry_plugin(DT_SITE_TUNE_WALK_ID, DT_SITE_TUNE_WALK, on_walk);
    nes_mod_register_function_entry_plugin(DT_SITE_TUNE_POGO_ID, DT_SITE_TUNE_POGO, on_pogo);
}
