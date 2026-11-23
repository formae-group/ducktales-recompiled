/*
 * ducktales/events.h - game events for mods and enhancements.
 *
 * Each event is backed by one verified code site (docs/decompilation.md
 * "Events"). A handler runs at that site, inside a RAM transaction: it reads
 * and changes the game through the typed API (gPlayerMode, gObjects, gLives,
 * ...) and its changes reach the machine as CPU stores would. While no
 * handler is registered for an event its site's hook is disabled, so the
 * game runs exactly as the original.
 *
 * A cancelable event is cancelled when any handler returns EVENT_CANCEL:
 *
 *   EVENT_PLAYER_DAMAGED  a hit is about to cost an HP dot      cancel: no dot is lost (the knock-back still happens)
 *   EVENT_LIFE_LOST       a death is about to cost a life        cancel: the lives count stays
 *   EVENT_TIMER_TICK      a frame of the HUD timer elapses       cancel: the HUD second does not advance
 *   EVENT_BOSS_HIT        a hit is about to count against a boss cancel: the hits left stay (the boss still flashes)
 *   EVENT_NEW_GAME        the title set-up reset the HUD (lives = 2); not cancelable
 *   EVENT_JUMP_START      a jump copied the take-off velocity into Scrooge's; not cancelable
 *
 * Handlers run in registration order; all of them run even after one
 * cancels.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum EventId {
    EVENT_PLAYER_DAMAGED,
    EVENT_LIFE_LOST,
    EVENT_TIMER_TICK,
    EVENT_NEW_GAME,
    EVENT_JUMP_START,
    EVENT_BOSS_HIT,
    EVENT_COUNT
} EventId;

typedef enum EventResult {
    EVENT_CONTINUE = 0,
    EVENT_CANCEL = 1
} EventResult;

typedef EventResult (*EventHandler)(EventId id, void *user);

/* False when the table is full or the same handler/user is registered. */
bool Events_On(EventId id, EventHandler fn, void *user);
bool Events_Off(EventId id, EventHandler fn, void *user);
const char *Events_Name(EventId id);
bool Events_Cancelable(EventId id);
/* Times the event was raised since start. */
uint64_t Events_Raised(EventId id);

/* Raise the event inside the open RAM transaction (ram.h) and apply a
 * cancel's effect to it. Called by the event sites and by decompiled
 * routines that replace the code an event site is in. */
EventResult Events_Raise(EventId id);

#ifdef __cplusplus
}
#endif
