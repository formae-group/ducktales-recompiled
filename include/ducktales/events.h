/*
 * ducktales/events.h - game events for mods and enhancements.
 *
 * Most events are backed by one verified code site (docs/DECOMPILATION.md
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
 * The others cannot be cancelled. Two have sites of their own in bank 6, one
 * in the frame loop, one shares the boss hit's site, and four are seen at the end of a machine
 * frame (Events_FrameEnd) by comparing RAM with the previous frame's, so
 * they cost no site:
 *
 *   EVENT_BOSS_DEFEATED   the hit that counts is a boss's last (after the boss-hit handlers; not when one cancelled)
 *   EVENT_ENEMY_DEFEATED  an enemy is beaten ($8932)            slot, kind, x (world), y
 *   EVENT_ITEM_COLLECTED  Scrooge takes a pickup ($8B21)        slot, item, x (world), y
 *   EVENT_LAND_START      the land's music starts after Land Select or another land: a land was loaded
 *   EVENT_LAND_CLEAR      the land is left with its "clear" result ($5D set with no scene in $78)
 *   EVENT_CHECKPOINT      Scrooge reached his land's boss checkpoint ($7C becomes 5)
 *   EVENT_MONEY_CHANGED   the HUD money differs from the last frame that showed it   money, delta
 *
 *   EVENT_UPDATE          a step of play: Player_Update returned and the objects are about to move ($DB6F)
 *
 * EVENT_BOSS_DEFEATED, EVENT_LAND_START, EVENT_LAND_CLEAR and EVENT_CHECKPOINT
 * carry the land. Events_Args gives a handler its event's values.
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
    EVENT_BOSS_DEFEATED,
    EVENT_ENEMY_DEFEATED,
    EVENT_ITEM_COLLECTED,
    EVENT_LAND_START,
    EVENT_LAND_CLEAR,
    EVENT_CHECKPOINT,
    EVENT_MONEY_CHANGED,
    EVENT_UPDATE,
    EVENT_COUNT
} EventId;

typedef enum EventResult {
    EVENT_CONTINUE = 0,
    EVENT_CANCEL = 1
} EventResult;

typedef EventResult (*EventHandler)(EventId id, void *user);

/* A value an event carries, by name. */
typedef struct EventArg {
    const char *name;
    int32_t value;
} EventArg;

#define EVENT_MAX_ARGS 4

/* False when the table is full or the same handler/user is registered. */
bool Events_On(EventId id, EventHandler fn, void *user);
bool Events_Off(EventId id, EventHandler fn, void *user);
const char *Events_Name(EventId id);
bool Events_Cancelable(EventId id);
/* Times the event was raised since start. */
uint64_t Events_Raised(EventId id);
/* The values of the event being raised, for its handlers; 0 outside them. */
int Events_Args(const EventArg **args);

/* Raise the event inside the open RAM transaction (ram.h) and apply a
 * cancel's effect to it. Called by the event sites and by decompiled
 * routines that replace the code an event site is in. */
EventResult Events_Raise(EventId id);
/* The same for an event about the object in slot (enemy-defeated,
 * item-collected). */
EventResult Events_RaiseSlot(EventId id, uint8_t slot);

/* After each machine frame: raises the events seen in RAM (land-start,
 * land-clear, checkpoint, money-changed) while one has a handler. */
void Events_FrameEnd(void);

#ifdef __cplusplus
}
#endif
