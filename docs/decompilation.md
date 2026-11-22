# Decompilation layer

The port's first step from "recompiled 6502" towards readable C: typed names for the game's RAM and
verified code sites where mod code can react to, or cancel, what the game does. Both are in the
port's own files and change nothing while unused.

Addresses are named only in `src/game/ducktales_symbols.h`. Status words (verified, code,
hypothesis) are used as in [memory-map.md](memory-map.md). Code addresses are in bank 7 (the fixed
bank).

## The game API

`#include "ducktales/ducktales.h"` gives all of it:

| Header | Contents |
|---|---|
| `ram.h` | `gRam`, `RAM_U8`, `Ram_GetU16`/`Ram_SetU16`, the transactions, and `Game_ReadRom` for tables the game reads at run time |
| `objects.h` | `gObjects`: the 16-slot object tables at `$0600-$07FF` as a structure of arrays (`gObjects->y[slot]` is `$0640,X`), with 16-bit helpers for speed, velocity and world X |
| `player.h` | `PLAYER_SLOT`, `PlayerMode` (`$39`), `PlayerAction` (`$0620`) and Scrooge's zero-page variables |
| `hud.h` | HP dots (sprite records at `$0340`), lives, money and time digits |
| `game.h` | difficulty, land, map and camera, timer, bosses, progress flags, controller bytes |
| `events.h` | the events below |

Every accessor is an lvalue in `gRam`, the RAM image of the open transaction:

- `GameRam_Begin()`/`GameRam_End()` open a transaction on the running machine. The image starts as
  a copy of CPU RAM, and at the end every byte that changed is stored with `cyc_mod_poke`, through
  the compiled-code write watch, as a CPU store would. A nested `Begin` joins the open transaction.
- `GameRam_Enter(image)`/`GameRam_Leave()` point `gRam` at a private image (a decompiled routine
  in progress) and store nothing.

Outside a transaction `gRam` is `NULL`, so a stray access fails at once instead of reading stale
memory. Rows and variables whose meaning is a hypothesis say so in their comment, and unknown object
rows keep their address as their name (`unk_0690`).

## Events

Each event owns one `[[mod_function_hook]]` site (`config/game.toml`, with a `length`/`crc32`
content key). A handler registered with `Events_On(EVENT_..., fn, user)` runs inside a RAM
transaction at that site, before the site's instruction. **While an event has no handler its site's
hook is disabled**, so the generated code runs the original instruction with nothing changed. A
cancelable event is cancelled when any handler returns `EVENT_CANCEL`. All handlers run, in
registration order, even after one cancels.

| Event | Site | When | Cancel | Used by |
|---|---|---|---|---|
| `EVENT_PLAYER_DAMAGED` | `$EB5E`, entry of `damage` | a hit is about to cost an HP dot | the routine returns as its RTS would: no dot is lost and the Easy hit counter `$5F` stays; the knock-back still happens | NoDamage |
| `EVENT_LIFE_LOST` | `$DCB0`, in `death_or_warp` (`$DC3E`) | a death is about to cost a life | one life is added before the decrement | InfiniteLives |
| `EVENT_TIMER_TICK` | `$E2A6`, in `timer_tick` (`$E28C`) | a frame of the HUD timer passes | `$B0` is reloaded with 60 before the decrement, so the HUD second never passes | InfiniteTime |
| `EVENT_BOSS_HIT` | `$F6DD`, in the boss hit routine `$F6D7` | a hit is about to count against a boss | one hit is added to `$B1` before the decrement (the boss still flashes) | none yet |
| `EVENT_NEW_GAME` | `$D59C`, title set-up | the HUD template was copied (lives = 2) | not cancelable | StartLives |
| `EVENT_JUMP_START` | `$E3FE`, ground handler | the take-off velocity was copied into Scrooge's | not cancelable | JumpScale |

Evidence for each site:

- `$EB5E`, `$E2A6`, `$DCB0` and `$D59C` are the enhancement sites. Their evidence is under
  "Enhancements" in [architecture.md](architecture.md) (verified).
- `$E3FE` is reached only by falling through from the two velocity stores at `$E3F5`/`$E3FB`.
  See "Jump experiment" in [architecture.md](architecture.md) (verified).
- `$F6DD` is the `DEC $B1` of the boss hit routine `$F6D7`. That routine is called by the boss
  objects of bank 6 and by `$FA44`, and it reaches the decrement only when the boss's cooldown
  `$06F0,X` is 0, that is, for a hit that counts. `$B1` holds the hits left (verified for Amazon and
  Himalayas in [game-state.md](game-state.md)). With `--dt-events-log`, `full_amazon` raises
  `boss-hit` at frames 7093, 7285, 7645, 7933 and 8173, exactly the frames where `$B1` drops from 5
  to 0 (verified). The cancel's effect comes from the code alone: no enhancement cancels it yet.

`--dt-events-log FILE` (developer builds) registers a handler on every event that changes nothing
and writes one line per raised event.
