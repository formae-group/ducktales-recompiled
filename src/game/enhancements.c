#include "enhancements.h"
#include "enhancements_sites.h"
#include "ducktales_jump.h"
#include "ducktales_symbols.h"

#include "cpu6502.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DtEnhInfo INFO[DT_ENH_COUNT] = {
    [DT_ENH_JUMP_SCALE] = { "JumpScale", "--jump-scale", "Jump height",
        "Scrooge's jump take-off speed in percent (100 = original)",
        DT_ENH_KIND_PERCENT, DT_JUMP_SCALE_MIN, DT_JUMP_SCALE_MAX, DT_JUMP_SCALE_STOCK, 5 },
    [DT_ENH_NO_DAMAGE] = { "NoDamage", "--no-damage", "No damage",
        "Hits still knock Scrooge back but never cost health",
        DT_ENH_KIND_TOGGLE, 0, 1, 0, 1 },
    [DT_ENH_INFINITE_TIME] = { "InfiniteTime", "--infinite-time", "Infinite time",
        "The level timer stops counting down",
        DT_ENH_KIND_TOGGLE, 0, 1, 0, 1 },
    [DT_ENH_INFINITE_LIVES] = { "InfiniteLives", "--infinite-lives", "Infinite lives",
        "Losing a life never lowers the lives counter",
        DT_ENH_KIND_TOGGLE, 0, 1, 0, 1 },
    [DT_ENH_START_LIVES] = { "StartLives", "--start-lives", "Extra lives at start",
        "Extra lives for a new game, 0-9 (off = original, 2); applies from the next new game",
        DT_ENH_KIND_COUNT_OR_OFF, -1, 9, -1, 1 },
};

static int s_value[DT_ENH_COUNT] = { DT_JUMP_SCALE_STOCK, 0, 0, 0, -1 };
static uint64_t s_applied[DT_ENH_COUNT];

const DtEnhInfo *dt_enh_info(DtEnh e) { return e < DT_ENH_COUNT ? &INFO[e] : NULL; }

bool dt_enh_parse(DtEnh e, const char *text, int *out)
{
    if (e >= DT_ENH_COUNT || !text) return false;
    while (*text == ' ' || *text == '\t') text++;
    char word[16];
    size_t n = 0;
    while (text[n] && text[n] != ' ' && text[n] != '\t' && text[n] != '\r' && text[n] != '\n' && n + 1 < sizeof word) {
        word[n] = (char)(text[n] >= 'A' && text[n] <= 'Z' ? text[n] + 32 : text[n]);
        n++;
    }
    word[n] = 0;
    for (const char *rest = text + n; *rest; rest++)
        if (*rest != ' ' && *rest != '\t' && *rest != '\r' && *rest != '\n') return false;
    const DtEnhInfo *in = &INFO[e];
    if (in->kind == DT_ENH_KIND_TOGGLE) {
        static const char *on[] = { "1", "on", "true", "yes" }, *off[] = { "0", "off", "false", "no" };
        for (int i = 0; i < 4; i++) {
            if (!strcmp(word, on[i])) { *out = 1; return true; }
            if (!strcmp(word, off[i])) { *out = 0; return true; }
        }
        return false;
    }
    if (in->kind == DT_ENH_KIND_COUNT_OR_OFF && !strcmp(word, "off")) { *out = in->stock; return true; }
    char *end;
    long v = strtol(word, &end, 10);
    if (end == word || *end || v < in->min || v > in->max) return false;
    *out = (int)v;
    return true;
}

const char *dt_enh_format(DtEnh e, int value, char *buf, unsigned cap)
{
    if (e >= DT_ENH_COUNT) return "";
    const DtEnhInfo *in = &INFO[e];
    if (in->kind == DT_ENH_KIND_TOGGLE) snprintf(buf, cap, "%s", value ? "on" : "off");
    else if (in->kind == DT_ENH_KIND_COUNT_OR_OFF && value == in->stock) snprintf(buf, cap, "off");
    else snprintf(buf, cap, "%d", value);
    return buf;
}

/* ---- the hook callbacks (cyc_hooks.h): before the site's instruction ---- */

/* The damage routine's entry: return as its RTS would, so no dot is marked
 * and the Easy hit counter $5F is left alone. */
static int on_damage(uint16_t addr)
{
    (void)addr;
    s_applied[DT_ENH_NO_DAMAGE]++;
    return 1;
}

/* Before the timer decrements $B0: give it a full second again, so the
 * decrement never reaches 0 and the HUD time never drops. */
static int on_timer_tick(uint16_t addr)
{
    (void)addr;
    if (cyc_mod_peek(DT_ADDR_TICK_FRAMES) == 1) s_applied[DT_ENH_INFINITE_TIME]++;
    cyc_mod_poke(DT_ADDR_TICK_FRAMES, DT_TICK_FRAMES_RELOAD);
    return 0;
}

/* Before the lives decrement: one more, so the decrement leaves the count as
 * it was. The counter is 1-9 here (0 went to game over before this point). */
static int on_life_lost(uint16_t addr)
{
    (void)addr;
    uint8_t lives = cyc_mod_peek(DT_ADDR_LIVES);
    if (lives == 0xFF) return 0;
    cyc_mod_poke(DT_ADDR_LIVES, (uint8_t)(lives + 1));
    s_applied[DT_ENH_INFINITE_LIVES]++;
    return 0;
}

/* After the title set-up copied the HUD template (lives = 2): the player's
 * count instead. */
static int on_new_game_lives(uint16_t addr)
{
    (void)addr;
    cyc_mod_poke(DT_ADDR_LIVES, (uint8_t)s_value[DT_ENH_START_LIVES]);
    s_applied[DT_ENH_START_LIVES]++;
    return 0;
}

static const char *const HOOK_ID[DT_ENH_COUNT] = {
    [DT_ENH_NO_DAMAGE] = DT_SITE_DAMAGE_ID,
    [DT_ENH_INFINITE_TIME] = DT_SITE_TIMER_TICK_ID,
    [DT_ENH_INFINITE_LIVES] = DT_SITE_LIFE_LOST_ID,
    [DT_ENH_START_LIVES] = DT_SITE_NEW_GAME_LIVES_ID,
};

NES_MOD_CONSTRUCTOR(dt_register_enhancement_hooks)
{
    nes_mod_register_function_entry_plugin(DT_SITE_DAMAGE_ID, DT_SITE_DAMAGE, on_damage);
    nes_mod_register_function_entry_plugin(DT_SITE_TIMER_TICK_ID, DT_SITE_TIMER_TICK, on_timer_tick);
    nes_mod_register_function_entry_plugin(DT_SITE_LIFE_LOST_ID, DT_SITE_LIFE_LOST, on_life_lost);
    nes_mod_register_function_entry_plugin(DT_SITE_NEW_GAME_LIVES_ID, DT_SITE_NEW_GAME_LIVES, on_new_game_lives);
}

bool dt_enh_set(DtEnh e, int value)
{
    if (e >= DT_ENH_COUNT || value < INFO[e].min || value > INFO[e].max) return false;
    if (e == DT_ENH_JUMP_SCALE && !dt_jump_set_scale(value)) return false;
    s_value[e] = value;
    if (HOOK_ID[e]) nes_mod_set_function_hook_enabled(HOOK_ID[e], value != INFO[e].stock);
    return true;
}

int dt_enh_get(DtEnh e) { return e < DT_ENH_COUNT ? s_value[e] : 0; }

uint64_t dt_enh_applied(DtEnh e)
{
    if (e == DT_ENH_JUMP_SCALE) return dt_jump_rescaled();
    return e < DT_ENH_COUNT ? s_applied[e] : 0;
}

/* Between frames the CPU is at an instruction boundary. Raising the reset
 * line there (cpu.do_reset) is not enough: a frame starts at VBlank, and the
 * NMI that arrives during the 7-cycle reset sequence takes over its vector
 * (cpu6502.c cpu_interrupt models that hijack; seen in a trace of
 * amazon_intro frame 1200, which read $FFFA instead of $FFFC). On the NES the
 * reset button also resets the PPU, so no NMI follows. So the CPU is put
 * where the sequence leaves it: S lowered by 3, I set, PC from the reset
 * vector, pending interrupts dropped. The game's reset handler then
 * re-initialises everything else itself (functions.md `reset`). */
void dt_soft_reset(void)
{
    uint16_t vector = (uint16_t)(cyc_mod_peek(0xFFFC) | cyc_mod_peek(0xFFFD) << 8);
    cpu.do_nmi = cpu.do_irq = cpu.do_reset = 0;
    cpu.nmi_edge = 0;
    cpu.s = (uint8_t)(cpu.s - 3);
    cpu.i = 1;
    cpu.pc = vector;
}
