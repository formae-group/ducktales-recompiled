/*
 * ai.c - a mod's own code for the objects of a kind (ducktales/ai.h).
 *
 * The dispatch at bank 6 $8000 loads a live slot's handler address from the
 * table at $802C/$806F into $00/$01, pushes the return $8022 and jumps there
 * with JMP ($0000) at $8020; the handler's RTS comes back to the INC $4A at
 * $8023, which the loop also reaches for a slot that is not live. $4A holds
 * the slot at both. Each has a [[mod_function_hook]] (config/game.toml,
 * evidence in ducktales_symbols.h), on exactly while a kind has a handler:
 *
 *   before  $8020  the handler's AI_BEFORE call; a skip stores kind 0's
 *                  handler (an RTS, $80B2) in $00/$01, so the JMP returns at
 *                  once to $8023
 *   after   $8023  the AI_AFTER call of the slot whose turn the before call
 *                  began, when it still holds a live object of that kind
 *
 * Inside an isolated call the sites run no handler: they make there the
 * changes the decompiled dispatch's calls made, for replace mode's
 * measuring call (Decomp_AiReplay, src/decomp/decomp.c).
 */
#include "ducktales/ai.h"
#include "ducktales/objects.h"

#include "decomp/decomp.h"

#include "cyc_core.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stddef.h>

#define KINDS 256

static struct {
    AiHandler fn;
    void *user;
} s_kind[KINDS];

static int s_kinds;
static int s_turn = -1; /* the slot whose turn the last AI_BEFORE call began, -1 none */
static uint8_t s_turn_kind;

void Ai_Set(uint8_t kind, AiHandler fn, void *user)
{
    bool had = s_kind[kind].fn != NULL;
    s_kind[kind].fn = fn;
    s_kind[kind].user = user;
    s_kinds += (fn != NULL) - had;
    nes_mod_set_function_hook_enabled(DT_SITE_AI_BEFORE_ID, s_kinds > 0);
    nes_mod_set_function_hook_enabled(DT_SITE_AI_AFTER_ID, s_kinds > 0);
}

bool Ai_Active(void)
{
    return s_kinds > 0;
}

bool Ai_Run(uint8_t slot, AiPhase phase)
{
    if (phase == AI_AFTER) {
        bool turn = s_turn == slot;
        s_turn = -1;
        uint8_t kind = gObjects->kind[slot];
        if (turn && (gObjects->state[slot] & OBJ_STATUS_LIVE) && kind == s_turn_kind && s_kind[kind].fn) {
            s_kind[kind].fn(slot, AI_AFTER, s_kind[kind].user);
        }
        return false;
    }
    s_turn = -1;
    uint8_t kind = gObjects->kind[slot];
    if (!s_kind[kind].fn) {
        return false;
    }
    s_turn = slot;
    s_turn_kind = kind;
    bool skip = s_kind[kind].fn(slot, AI_BEFORE, s_kind[kind].user);
    if (!(gObjects->state[slot] & OBJ_STATUS_LIVE)) {
        skip = true;
    }
    if (skip) {
        RAM_U8(DT_ADDR_SCRATCH0) = Game_ReadPrg(DT_BANK_OBJECTS, DT_CODE_OBJ_HANDLER_LO);
        RAM_U8(DT_ADDR_SCRATCH1) = Game_ReadPrg(DT_BANK_OBJECTS, DT_CODE_OBJ_HANDLER_HI);
    }
    return skip;
}

static int at_site(AiPhase phase)
{
    if (cyc_mod_isolated()) {
        Decomp_AiReplay(cyc_cpu_ram()[DT_ADDR_OBJ_CURSOR], phase == AI_AFTER);
        return 0;
    }
    GameRam_Begin();
    Ai_Run(RAM_U8(DT_ADDR_OBJ_CURSOR), phase);
    GameRam_End();
    return 0;
}

static int on_before(uint16_t addr)
{
    (void)addr;
    return at_site(AI_BEFORE);
}

static int on_after(uint16_t addr)
{
    (void)addr;
    return at_site(AI_AFTER);
}

NES_MOD_CONSTRUCTOR(dt_register_ai_sites)
{
    nes_mod_register_function_entry_plugin(DT_SITE_AI_BEFORE_ID, DT_SITE_AI_BEFORE, on_before);
    nes_mod_register_function_entry_plugin(DT_SITE_AI_AFTER_ID, DT_SITE_AI_AFTER, on_after);
}
