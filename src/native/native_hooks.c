/*
 * native_hooks.c - game.toml [[mod_function_hook]] sites on the native
 * runtime: the same dispatch as the cycle host's scheduler (NESRecomp
 * runner/cyc/cyc_hooks.c), so the enhancements in src/game register their
 * callbacks once (mod_function_hooks.h) and fire identically on both.
 *
 *   binding    each registered plugin binds to the site that names its id
 *              (or, without one, sits at its address); a plugin no site
 *              declares is refused at start
 *   armed      cyc_hooks_armed is set while any bound hook is enabled; the
 *              generated code then stops at its sites (cyc_hook_stop)
 *   firing     at the instruction boundary before the site, unless an
 *              interrupt is about to be taken there, when the site's content
 *              key (length and CRC-32 of the bytes at its address) holds; a
 *              callback that returns nonzero handled the routine, which then
 *              returns as its RTS would, without bus cycles
 *   isolated   the native runtime makes no isolated calls, so
 *              cyc_mod_isolated() is always false here
 */
#include "native/native_runtime.h"

#include "cpu6502.h"
#include "cyc_hooks.h"
#include "cyc_mod.h"
#include "cyc_recomp.h"
#include "mod_function_hooks.h"
#include "common/nes_cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BINDINGS 8

typedef struct {
    int plugin[MAX_BINDINGS];
    int plugins;
} Site;

static uint8_t s_bits[0x10000 / 8];
static Site *s_sites;
static bool s_valid;

static void update_armed(void)
{
    cyc_hooks_armed = (uint8_t)(s_valid && cyc_native_hook_site_count && nes_mod_function_hooks_active() > 0);
    if (!cyc_hooks_armed) cyc_hook_passed = -1;
}

bool native_hooks_init(void)
{
    uint32_t n = cyc_native_hook_site_count;
    free(s_sites);
    s_sites = n ? (Site *)calloc(n, sizeof(Site)) : NULL;
    memset(s_bits, 0, sizeof s_bits);
    bool ok = true;
    for (uint32_t i = 0; i < n; i++) s_bits[cyc_native_hook_sites[i].addr >> 3] |= (uint8_t)(1u << (cyc_native_hook_sites[i].addr & 7));
    for (int r = 0; r < nes_mod_function_hook_count(); r++) {
        const char *id = NULL;
        uint16_t addr = 0;
        nes_mod_function_hook_at(r, &id, &addr, NULL, NULL);
        int bound = 0;
        for (uint32_t i = 0; i < n; i++) {
            const CycHookSite *site = &cyc_native_hook_sites[i];
            bool named = site->id && id && !strcmp(site->id, id);
            if (!(named || (!site->id && site->addr == addr)) || site->addr != addr || s_sites[i].plugins == MAX_BINDINGS)
                continue;
            s_sites[i].plugin[s_sites[i].plugins++] = r;
            bound++;
        }
        if (!bound) {
            fprintf(stderr, "[native hooks] plugin %s registered at $%04X: no such [[mod_function_hook]] site\n",
                    id ? id : "?", addr);
            ok = false;
        }
    }
    s_valid = ok;
    nes_mod_function_hooks_set_listener(update_armed);
    update_armed();
    return ok;
}

bool native_hooks_due(uint16_t pc)
{
    return (s_bits[pc >> 3] >> (pc & 7)) & 1 && cyc_hook_passed != (int32_t)pc && !(cpu.do_nmi | cpu.do_irq | cpu.do_reset);
}

static bool holds(const CycHookSite *site)
{
    uint8_t b[16];
    if (site->len > sizeof b) return false;
    for (unsigned k = 0; k < site->len; k++)
        if (!native_peek((uint16_t)(site->addr + k), &b[k])) return false;
    return !site->len || nes_crc32(0, b, site->len) == site->crc32;
}

static void return_from_routine(void)
{
    uint8_t lo, hi;
    native_peek((uint16_t)(0x100 | (uint8_t)(cpu.s + 1)), &lo);
    native_peek((uint16_t)(0x100 | (uint8_t)(cpu.s + 2)), &hi);
    cpu.s = (uint8_t)(cpu.s + 2);
    cpu.pc = (uint16_t)((lo | hi << 8) + 1);
}

void native_hooks_fire(uint16_t pc)
{
    for (uint32_t i = 0; s_sites && i < cyc_native_hook_site_count; i++) {
        const CycHookSite *site = &cyc_native_hook_sites[i];
        if (site->addr != pc || !s_sites[i].plugins || !holds(site)) continue;
        for (int b = 0; b < s_sites[i].plugins; b++) {
            int enabled = 0;
            NESModFunctionEntryCallback cb = NULL;
            nes_mod_function_hook_at(s_sites[i].plugin[b], NULL, NULL, &enabled, &cb);
            if (!enabled || !cb) continue;
            if (cb(pc)) {
                return_from_routine();
                return;
            }
            if (cpu.pc != pc) return;
        }
    }
}

bool cyc_mod_isolated(void)
{
    return false;
}
