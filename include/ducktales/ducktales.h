/*
 * ducktales/ducktales.h - the typed game API in one include.
 *
 *   ram.h      RAM transactions and gRam
 *   objects.h  the object tables, by slot
 *   player.h   Scrooge: modes, actions, his zero-page state
 *   hud.h      HP dots, lives, money, time
 *   game.h     difficulty, land, map, timer, bosses, progress flags
 *   events.h   events for mods: Events_On(EVENT_..., handler, user)
 *
 * docs/DECOMPILATION.md describes the API, the events and the decompiled
 * routines built on it.
 */
#pragma once
#include "ducktales/events.h"
#include "ducktales/game.h"
#include "ducktales/hud.h"
#include "ducktales/level.h"
#include "ducktales/objects.h"
#include "ducktales/player.h"
#include "ducktales/ram.h"
