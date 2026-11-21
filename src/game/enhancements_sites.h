/*
 * enhancements_sites.h - the code sites the enhancements hook, for DuckTales
 * (USA), PRG SHA-256 8ba8baed...e2013 (docs/rom.md). All are in the fixed
 * bank 7 ($C000-$FFFF), so the bank mapped at $8000 never matters.
 *
 * These belong in game/ducktales_symbols.h (the one file meant to name
 * revision-specific addresses); they are kept here until that file's owner
 * moves them. Evidence for each is in docs/architecture.md "Enhancements".
 * config/game.toml declares every site with a length/crc32 content key.
 */
#pragma once

/* Entry of the damage routine (functions.md `damage`): marks the last full
 * HP dot lost (every other hit on Easy). Reached only by JSR, from $EAD0 and
 * $EB22; the instruction before it is a JMP, so nothing falls into it. */
#define DT_SITE_DAMAGE            0xEB5E
#define DT_SITE_DAMAGE_ID         "ducktales.no-damage"

/* Inside the HUD timer (`timer_tick`, $E28C): the decrement of the
 * frames-per-tick counter $B0, after the time-out check. */
#define DT_SITE_TIMER_TICK        0xE2A6
#define DT_SITE_TIMER_TICK_ID     "ducktales.infinite-time"
#define DT_ADDR_TICK_FRAMES       0x00B0   /* frames until the next time tick (verified) */
#define DT_TICK_FRAMES_RELOAD     0x3C     /* what the timer reloads it with: 60 frames */

/* In `death_or_warp` ($DC3E): the only instruction in the ROM that decrements
 * the lives counter $0361, reached after a death when lives are not 0. */
#define DT_SITE_LIFE_LOST         0xDCB0
#define DT_SITE_LIFE_LOST_ID      "ducktales.infinite-lives"

/* In the title set-up of the main task ($D4E6): the first instruction after
 * the loop that copies the 13-byte HUD template into $0358-$0364, which sets
 * the lives counter $0361 to 2. Runs after every power-on, reset and game
 * over (game over jumps to the reset vector). */
#define DT_SITE_NEW_GAME_LIVES    0xD59C
#define DT_SITE_NEW_GAME_LIVES_ID "ducktales.start-lives"
