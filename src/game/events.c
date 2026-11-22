/*
 * events.c - the event system (ducktales/events.h).
 *
 * Every event owns one [[mod_function_hook]] site (config/game.toml, address
 * and evidence in ducktales_symbols.h, docs/decompilation.md "Events"). Its
 * hook is enabled exactly while the event has a handler, so with no handler
 * the generated code runs the original instruction and nothing changes. At
 * the site the callback opens a RAM transaction, runs the handlers and, when
 * one cancelled, applies the cancel at that site:
 *
 *   PLAYER_DAMAGED  entry of the damage routine: return as its RTS would
 *   LIFE_LOST       before the lives decrement: one more life first
 *   TIMER_TICK      before the decrement of $B0: reload it with 60
 *   BOSS_HIT        before the decrement of $B1: one more hit first
 */
#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"

#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stddef.h>

#define MAX_HANDLERS 8

typedef struct {
    const char *name;
    const char *site_id;
    uint16_t site;
    bool cancelable;
} EventInfo;

static const EventInfo INFO[EVENT_COUNT] = {
    [EVENT_PLAYER_DAMAGED] = { "player-damaged", DT_SITE_DAMAGE_ID, DT_SITE_DAMAGE, true },
    [EVENT_LIFE_LOST] = { "life-lost", DT_SITE_LIFE_LOST_ID, DT_SITE_LIFE_LOST, true },
    [EVENT_TIMER_TICK] = { "timer-tick", DT_SITE_TIMER_TICK_ID, DT_SITE_TIMER_TICK, true },
    [EVENT_NEW_GAME] = { "new-game", DT_SITE_NEW_GAME_ID, DT_SITE_NEW_GAME, false },
    [EVENT_JUMP_START] = { "jump-start", DT_SITE_JUMP_START_ID, DT_SITE_JUMP_START, false },
    [EVENT_BOSS_HIT] = { "boss-hit", DT_SITE_BOSS_HIT_ID, DT_SITE_BOSS_HIT, true },
};

static struct { EventHandler fn; void *user; } s_handlers[EVENT_COUNT][MAX_HANDLERS];
static int s_count[EVENT_COUNT];
static uint64_t s_raised[EVENT_COUNT];

const char *Events_Name(EventId id) { return id < EVENT_COUNT ? INFO[id].name : "?"; }

bool Events_Cancelable(EventId id) { return id < EVENT_COUNT && INFO[id].cancelable; }

uint64_t Events_Raised(EventId id) { return id < EVENT_COUNT ? s_raised[id] : 0; }

bool Events_On(EventId id, EventHandler fn, void *user)
{
    if (id >= EVENT_COUNT || !fn || s_count[id] == MAX_HANDLERS) return false;
    for (int i = 0; i < s_count[id]; i++)
        if (s_handlers[id][i].fn == fn && s_handlers[id][i].user == user) return false;
    s_handlers[id][s_count[id]].fn = fn;
    s_handlers[id][s_count[id]].user = user;
    if (s_count[id]++ == 0) nes_mod_set_function_hook_enabled(INFO[id].site_id, 1);
    return true;
}

bool Events_Off(EventId id, EventHandler fn, void *user)
{
    if (id >= EVENT_COUNT) return false;
    for (int i = 0; i < s_count[id]; i++) {
        if (s_handlers[id][i].fn != fn || s_handlers[id][i].user != user) continue;
        for (int j = i + 1; j < s_count[id]; j++) s_handlers[id][j - 1] = s_handlers[id][j];
        if (--s_count[id] == 0) nes_mod_set_function_hook_enabled(INFO[id].site_id, 0);
        return true;
    }
    return false;
}

EventResult Events_Raise(EventId id)
{
    if (id >= EVENT_COUNT) return EVENT_CONTINUE;
    s_raised[id]++;
    EventResult result = EVENT_CONTINUE;
    for (int i = 0; i < s_count[id]; i++)
        if (s_handlers[id][i].fn(id, s_handlers[id][i].user) == EVENT_CANCEL) result = EVENT_CANCEL;
    if (!INFO[id].cancelable || result != EVENT_CANCEL) return EVENT_CONTINUE;
    switch (id) {
    case EVENT_LIFE_LOST: gLives++; break;
    case EVENT_TIMER_TICK: gTickFrames = DT_TICK_FRAMES_RELOAD; break;
    case EVENT_BOSS_HIT: gBossHits++; break;
    default: break;
    }
    return EVENT_CANCEL;
}

/* ---- the sites (cyc_hooks.h): before the site's instruction ---- */

static int at_site(EventId id)
{
    GameRam_Begin();
    EventResult result = Events_Raise(id);
    GameRam_End();
    return id == EVENT_PLAYER_DAMAGED && result == EVENT_CANCEL;
}

static int on_damage(uint16_t addr) { (void)addr; return at_site(EVENT_PLAYER_DAMAGED); }
static int on_life_lost(uint16_t addr) { (void)addr; return at_site(EVENT_LIFE_LOST); }
static int on_timer_tick(uint16_t addr) { (void)addr; return at_site(EVENT_TIMER_TICK); }
static int on_new_game(uint16_t addr) { (void)addr; return at_site(EVENT_NEW_GAME); }
static int on_jump_start(uint16_t addr) { (void)addr; return at_site(EVENT_JUMP_START); }
static int on_boss_hit(uint16_t addr) { (void)addr; return at_site(EVENT_BOSS_HIT); }

NES_MOD_CONSTRUCTOR(dt_register_event_sites)
{
    nes_mod_register_function_entry_plugin(DT_SITE_DAMAGE_ID, DT_SITE_DAMAGE, on_damage);
    nes_mod_register_function_entry_plugin(DT_SITE_LIFE_LOST_ID, DT_SITE_LIFE_LOST, on_life_lost);
    nes_mod_register_function_entry_plugin(DT_SITE_TIMER_TICK_ID, DT_SITE_TIMER_TICK, on_timer_tick);
    nes_mod_register_function_entry_plugin(DT_SITE_NEW_GAME_ID, DT_SITE_NEW_GAME, on_new_game);
    nes_mod_register_function_entry_plugin(DT_SITE_JUMP_START_ID, DT_SITE_JUMP_START, on_jump_start);
    nes_mod_register_function_entry_plugin(DT_SITE_BOSS_HIT_ID, DT_SITE_BOSS_HIT, on_boss_hit);
}
