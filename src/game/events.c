/*
 * events.c - the event system (ducktales/events.h).
 *
 * An event with a site owns one [[mod_function_hook]] (config/game.toml,
 * address and evidence in ducktales_symbols.h, docs/DECOMPILATION.md
 * "Events"); EVENT_BOSS_DEFEATED shares the boss hit's. A site's hook is
 * enabled exactly while one of its events has a handler, so with no handler
 * the generated code runs the original instruction and nothing changes. At
 * the site the callback opens a RAM transaction, runs the handlers and, when
 * one cancelled, applies the cancel at that site:
 *
 *   PLAYER_DAMAGED  entry of the damage routine: return as its RTS would
 *   LIFE_LOST       before the lives decrement: one more life first
 *   TIMER_TICK      before the decrement of $B0: reload it with 60
 *   BOSS_HIT        before the decrement of $B1: one more hit first
 *
 * A boss hit that is not cancelled while $B1 is 1 is the boss's last: the
 * decrement that follows reaches 0, so EVENT_BOSS_DEFEATED is raised after
 * EVENT_BOSS_HIT, at the same site (or by the decompiled Boss_Hit, which
 * raises EVENT_BOSS_HIT through Events_Raise).
 *
 * The events without a site are seen at the end of each machine frame
 * (Events_FrameEnd), from what RAM holds then against the previous frame,
 * and only while one of them has a handler; the first frame after that
 * only records RAM.
 *
 * Inside an isolated call (cyc_mod.h) a site raises nothing: the original
 * routine runs there only to be measured (src/decomp/decomp.c).
 */
#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/objects.h"

#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stddef.h>

#define MAX_HANDLERS 8

typedef struct {
    const char *name;
    const char *site_id; /* NULL: seen at the frame's end */
    bool cancelable;
} EventInfo;

static const EventInfo INFO[EVENT_COUNT] = {
    [EVENT_PLAYER_DAMAGED] = { "player-damaged", DT_SITE_DAMAGE_ID, true },
    [EVENT_LIFE_LOST] = { "life-lost", DT_SITE_LIFE_LOST_ID, true },
    [EVENT_TIMER_TICK] = { "timer-tick", DT_SITE_TIMER_TICK_ID, true },
    [EVENT_NEW_GAME] = { "new-game", DT_SITE_NEW_GAME_ID, false },
    [EVENT_JUMP_START] = { "jump-start", DT_SITE_JUMP_START_ID, false },
    [EVENT_BOSS_HIT] = { "boss-hit", DT_SITE_BOSS_HIT_ID, true },
    [EVENT_BOSS_DEFEATED] = { "boss-defeated", DT_SITE_BOSS_HIT_ID, false },
    [EVENT_ENEMY_DEFEATED] = { "enemy-defeated", DT_SITE_ENEMY_DEFEATED_ID, false },
    [EVENT_ITEM_COLLECTED] = { "item-collected", DT_SITE_ITEM_COLLECTED_ID, false },
    [EVENT_LAND_START] = { "land-start", NULL, false },
    [EVENT_LAND_CLEAR] = { "land-clear", NULL, false },
    [EVENT_CHECKPOINT] = { "checkpoint", NULL, false },
    [EVENT_MONEY_CHANGED] = { "money-changed", NULL, false },
};

static struct {
    EventHandler fn;
    void *user;
} s_handlers[EVENT_COUNT][MAX_HANDLERS];

static int s_count[EVENT_COUNT];
static uint64_t s_raised[EVENT_COUNT];
static EventArg s_args[EVENT_MAX_ARGS];
static int s_nargs;

/* What the previous frame's end saw (Events_FrameEnd). */
static struct {
    bool primed, has_money;
    uint8_t music, land_left, checkpoint;
    uint32_t money;
} s_last;

const char *Events_Name(EventId id)
{
    return id < EVENT_COUNT ? INFO[id].name : "?";
}

bool Events_Cancelable(EventId id)
{
    return id < EVENT_COUNT && INFO[id].cancelable;
}

uint64_t Events_Raised(EventId id)
{
    return id < EVENT_COUNT ? s_raised[id] : 0;
}

int Events_Args(const EventArg **args)
{
    *args = s_args;
    return s_nargs;
}

/* The site of id on exactly while one of the events that share it has a handler. */
static void sync_site(EventId id)
{
    const char *site = INFO[id].site_id;
    if (!site) {
        s_last.primed = false;
        return;
    }
    int users = 0;
    for (int e = 0; e < EVENT_COUNT; e++) {
        if (INFO[e].site_id == site) {
            users += s_count[e];
        }
    }
    nes_mod_set_function_hook_enabled(site, users > 0);
}

bool Events_On(EventId id, EventHandler fn, void *user)
{
    if (id >= EVENT_COUNT || !fn || s_count[id] == MAX_HANDLERS) {
        return false;
    }
    for (int i = 0; i < s_count[id]; i++) {
        if (s_handlers[id][i].fn == fn && s_handlers[id][i].user == user) {
            return false;
        }
    }
    s_handlers[id][s_count[id]].fn = fn;
    s_handlers[id][s_count[id]].user = user;
    if (s_count[id]++ == 0) {
        sync_site(id);
    }
    return true;
}

bool Events_Off(EventId id, EventHandler fn, void *user)
{
    if (id >= EVENT_COUNT) {
        return false;
    }
    for (int i = 0; i < s_count[id]; i++) {
        if (s_handlers[id][i].fn != fn || s_handlers[id][i].user != user) {
            continue;
        }
        for (int j = i + 1; j < s_count[id]; j++) {
            s_handlers[id][j - 1] = s_handlers[id][j];
        }
        if (--s_count[id] == 0) {
            sync_site(id);
        }
        return true;
    }
    return false;
}

static void arg(const char *name, int32_t value)
{
    s_args[s_nargs++] = (EventArg){ name, value };
}

/* Runs id's handlers with the values in s_args; true when one cancelled. */
static bool run_handlers(EventId id)
{
    s_raised[id]++;
    bool cancel = false;
    for (int i = 0; i < s_count[id]; i++) {
        if (s_handlers[id][i].fn(id, s_handlers[id][i].user) == EVENT_CANCEL) {
            cancel = true;
        }
    }
    s_nargs = 0;
    return cancel && INFO[id].cancelable;
}

static void raise_land(EventId id)
{
    s_nargs = 0;
    arg("land", gLand);
    run_handlers(id);
}

EventResult Events_Raise(EventId id)
{
    if (id >= EVENT_COUNT) {
        return EVENT_CONTINUE;
    }
    s_nargs = 0;
    if (!run_handlers(id)) {
        if (id == EVENT_BOSS_HIT && gBossHits == 1) {
            raise_land(EVENT_BOSS_DEFEATED);
        }
        return EVENT_CONTINUE;
    }
    switch (id) {
        case EVENT_LIFE_LOST:
            gLives++;
            break;
        case EVENT_TIMER_TICK:
            gTickFrames = DT_TICK_FRAMES_RELOAD;
            break;
        case EVENT_BOSS_HIT:
            gBossHits++;
            break;
        default:
            break;
    }
    return EVENT_CANCEL;
}

EventResult Events_RaiseSlot(EventId id, uint8_t slot)
{
    if (id >= EVENT_COUNT || slot >= OBJ_SLOTS) {
        return EVENT_CONTINUE;
    }
    s_nargs = 0;
    arg("slot", slot);
    arg(id == EVENT_ITEM_COLLECTED ? "item" : "kind",
        id == EVENT_ITEM_COLLECTED ? gObjects->action[slot] : gObjects->kind[slot]);
    arg("x", Obj_GetWorldX(slot));
    arg("y", gObjects->y[slot]);
    run_handlers(id);
    return EVENT_CONTINUE;
}

void Events_FrameEnd(void)
{
    if (!s_count[EVENT_LAND_START] && !s_count[EVENT_LAND_CLEAR] && !s_count[EVENT_CHECKPOINT] &&
        !s_count[EVENT_MONEY_CHANGED]) {
        return;
    }
    GameRam_Begin();
    uint8_t music = gMusic, land = gLand, left = RAM_U8(DT_ADDR_LAND_LEFT), checkpoint = gBossCheckpoint;
    uint32_t money = 0;
    bool shown = Hud_GetMoney(&money);
    if (s_last.primed) {
        bool from_land = s_last.music < DT_LANDS && s_last.music != land;
        if (music != s_last.music && music == land && land < DT_LANDS &&
            (s_last.music == DT_MUSIC_LAND_SELECT || from_land)) {
            raise_land(EVENT_LAND_START);
        }
        if (left && !s_last.land_left && !RAM_U8(DT_ADDR_SCENE)) {
            raise_land(EVENT_LAND_CLEAR);
        }
        if (checkpoint == DT_CHECKPOINT_SET && s_last.checkpoint != DT_CHECKPOINT_SET) {
            raise_land(EVENT_CHECKPOINT);
        }
        if (shown && s_last.has_money && money != s_last.money) {
            s_nargs = 0;
            arg("money", (int32_t)money);
            arg("delta", (int32_t)money - (int32_t)s_last.money);
            run_handlers(EVENT_MONEY_CHANGED);
        }
    }
    s_last.primed = true;
    s_last.music = music;
    s_last.land_left = left;
    s_last.checkpoint = checkpoint;
    if (shown) {
        s_last.money = money;
        s_last.has_money = true;
    }
    GameRam_End();
}

/* ---- the sites (cyc_hooks.h): before the site's instruction ---- */

static int at_site(EventId id)
{
    if (cyc_mod_isolated()) {
        return 0;
    }
    GameRam_Begin();
    EventResult result = Events_Raise(id);
    GameRam_End();
    return id == EVENT_PLAYER_DAMAGED && result == EVENT_CANCEL;
}

static int at_object_site(EventId id)
{
    if (cyc_mod_isolated()) {
        return 0;
    }
    CycModRegs regs;
    cyc_mod_regs(&regs);
    GameRam_Begin();
    Events_RaiseSlot(id, regs.x);
    GameRam_End();
    return 0;
}

static int on_damage(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_PLAYER_DAMAGED);
}

static int on_life_lost(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_LIFE_LOST);
}

static int on_timer_tick(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_TIMER_TICK);
}

static int on_new_game(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_NEW_GAME);
}

static int on_jump_start(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_JUMP_START);
}

static int on_boss_hit(uint16_t addr)
{
    (void)addr;
    return at_site(EVENT_BOSS_HIT);
}

static int on_enemy_defeated(uint16_t addr)
{
    (void)addr;
    return at_object_site(EVENT_ENEMY_DEFEATED);
}

static int on_item_collected(uint16_t addr)
{
    (void)addr;
    return at_object_site(EVENT_ITEM_COLLECTED);
}

NES_MOD_CONSTRUCTOR(dt_register_event_sites)
{
    nes_mod_register_function_entry_plugin(DT_SITE_DAMAGE_ID, DT_SITE_DAMAGE, on_damage);
    nes_mod_register_function_entry_plugin(DT_SITE_LIFE_LOST_ID, DT_SITE_LIFE_LOST, on_life_lost);
    nes_mod_register_function_entry_plugin(DT_SITE_TIMER_TICK_ID, DT_SITE_TIMER_TICK, on_timer_tick);
    nes_mod_register_function_entry_plugin(DT_SITE_NEW_GAME_ID, DT_SITE_NEW_GAME, on_new_game);
    nes_mod_register_function_entry_plugin(DT_SITE_JUMP_START_ID, DT_SITE_JUMP_START, on_jump_start);
    nes_mod_register_function_entry_plugin(DT_SITE_BOSS_HIT_ID, DT_SITE_BOSS_HIT, on_boss_hit);
    nes_mod_register_function_entry_plugin(DT_SITE_ENEMY_DEFEATED_ID, DT_SITE_ENEMY_DEFEATED, on_enemy_defeated);
    nes_mod_register_function_entry_plugin(DT_SITE_ITEM_COLLECTED_ID, DT_SITE_ITEM_COLLECTED, on_item_collected);
}
