# Changelog

All notable changes to DuckTales Recompiled are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Script mods get random numbers of their own (docs/MODS.md "Random numbers"): `dt.random(n)`,
  `dt.random(m, n)` and `dt.random.seed(n)`, a generator per mod seeded from its id, the same every
  run and never the game's. The object tables also give `facing`, `speed` and `vy`.
- Script mods can give the enemies code of their own (docs/MODS.md "Object code"):
  `dt.ai(kind, {before = fn, after = fn})` runs at the turn of every object of one of the 23 enemy
  kinds `dt.spawn` makes, before and after the kind's code, with the object's handle, and a `before`
  that returns `dt.SKIP` skips the kind's code for that turn. Two hook sites in the object dispatch
  (`$8020`, `$8023`), on only while a mod uses it; a mod's `dt.ai` callbacks run at most 250,000
  Lua instructions a frame. The handles of those enemies can also set `facing`, `speed` and `vy`,
  and `o:set` writes and logs its fields in the same order in every run.
- Script mods can make pickups (docs/MODS.md "Spawning items"): `dt.spawn_item(item, x, y)` makes a
  diamond (2,000, 10,000 or 50,000), an HP dot, full HP or a life, through the game's own code
  for a hidden item's pickup, with no land record; the power-up, key items, extra dot and
  treasures cannot be made. No hook site.

## [1.1.0] - 2026-11-25

### Added

- The build with no ROM (docs/ROMLESS.md): `-DDUCKTALES_ROMLESS=ON` builds the native player
  without generating code from the ROM or reading one at configure time; every instruction the
  decompiled code leaves to the original runs in NESRecomp's instruction templates over the ROM
  the player imports, equal to the ROM build on every frame of the 13 routes. Releases are built
  this way by GitHub Actions on every `v*` tag (`.github/workflows/release.yml`), scanned for any
  run of the ROM (`tools/rom_scan.py`), and hold the native player only.
- Script mods can change gameplay (docs/MODS.md "Writing the game"): the `update` event runs once
  per step of play, where `Player_Update` returns in the frame loop (one hook site, `$DB6F`, and the
  decompiled frame loop raises it itself), and `dt.set` in a game event's handler writes at once.
  Every write is logged, identical repeats counted in one line.
- Script mods can tune Scrooge's physics (docs/MODS.md "Tunables"): `dt.tune.gravity`,
  `walk_speed`, `jump_velocity` and `pogo_velocity`, `nil` for the game's value, the highest mod
  in the order in force. Three hook sites right after the game's stores (`$E358`, `$E4A3`,
  `$E6AD`), on only while a mod sets the value; the take-off velocity is replaced at the frame's
  end. The decompiled player code reads them in replace mode (`Tune_Value`), so `--decomp` check
  and replace stay equal (docs/DECOMPILATION.md "Tunables"). `dt.set.invulnerable` is writable,
  refused where the knock-back would never end.
- Script mods can move and remove objects (docs/MODS.md "Objects and the camera"): the tables of
  `dt.objects()` are handles with `o:set{x, y, action}` and `o:despawn()` (the decompiled
  `Obj_Despawn`, through the new `Decomp_Outside`) in a game event's handler, checked against the
  slot's kind and land record; actions only those the land's records spawn, no items, no player
  slot. `dt.set.boss_hits` during a fight. Four events seen at the frame's end, with no hook site:
  `object-spawned`, `object-despawned`, `screen-changed` and `player-mode-changed`.
- Script mods can make enemies (docs/MODS.md "Spawning enemies"): `dt.spawn(kind, x, y[, action])`
  in a game event's handler puts an enemy this land already uses on the screen, through the
  decompiled `Obj_FindFreeSlot` and `Obj_SpawnTemplate` (the template step of `Obj_SpawnRecord`,
  now shared), and returns its handle; no bosses, chests or pickups, none in a boss fight, and two
  slots always left to the game. No hook site.
- Script mods can edit the level (docs/MODS.md "Editing the level"): `dt.level.set(x, y, metatile)`
  in an `update` handler replaces a metatile on the screen through the game's tile edit queue, with
  its collision class and palette, through the decompiled `Level_EditCell` (the queue and map step
  of `Level_TileEdit`, now shared). One edit a step, below the HUD, no special metatiles; it lasts
  until the game draws that part of the level again. No hook site, so the use stays at 112 of 128.
- Decompiled the scheduler `$FF35-$FF7C` as timed task code (`Task_Schedule`, `Task_Pass`,
  `Task_Resume`), whose time the native runtime spends, giving the call to the original when an NMI
  is pending or VBlank would come during it. The native step dumps and lag logs are unchanged on all
  13 routes.
- Timing for decompiled code (docs/DECOMPILATION.md "Timing"): the C of a timed routine marks the
  original's branches, indirect jumps and page-crossing reads (`DC_BR`, `DC_JUMP`, `DC_EA`), and
  `src/decomp/decomp_timing.c` walks the ROM between the marks for the original's path, cycles and
  bus-operation cycles. Check mode compares them with the original run on the machine, instruction by
  instruction, and stops at the first difference with the C line of the mark. The scheduler is the
  first timed code; `tools/decomp_timing.py` lists the marks a routine needs.
- 20 decompilation poke cases cover launched blocks, pouncers, the snapper grab, pickup respawn,
  the final stand-in spawn, and smaller object, collision, row-boundary and controller branches no
  existing route or poke case reached.
- `tools/decomp_gap.py`: the PRG code a static walk from the executed code reaches (branches, calls and
  dispatch tables) but no route or poke case executes, by block and routine, and the unreached runs that
  decode as code (docs/DECOMPILATION.md "Gap").
- Decompiled the ending's scene objects (bank 5 `$B380-$B50C`) and later frames
  (`$E1F9-$E28B`). Only the sprite-0 raster wait and timed PPU write stay in the original.
  The three long ending pokes exercise each variant through the end of the credits.
- Decompiled the reset `$FEE0-$FF34` as task code. Task code in replace goes to the original when
  the NMI line has risen but the CPU has not latched the edge yet, as on a warm reset at the frame
  boundary.
- Polling task code (`DecompRoutine.polls`): task routines that poll a device with `Decomp_BusWait`
  are measured on a clocked snapshot in check and replace, and replace makes the C's bus operations at
  the original's cycles (the NMI's measure and replay, generalized). The reset's four-VBlank wait
  `$FEEE-$FEFA`, the credits' raster split `$E229-$E23B` and the frame loop's `$DC29` are now C; the
  flow has 14 sites and no stop sites (`gFlowStops` is gone). `tests/decomp_test.py` has a `warm-reset` events case.
- Bank 6's objects no longer call original code: the tile edits `$F4D8`/`$F506` are called as C,
  and the charger's bounce off a hidden block runs in C. `Bg_Probe` and the walks and fall now return
  the Y the original leaves (checked), which the charger, the block kicker and the boulder pass on.
  New poke cases `charger-hidden-block` and `kicker-hidden-block`.
- Mods: seven script events. `enemy-defeated` and `item-collected` have hook sites of their own in
  bank 6 (`$8932` Obj_Defeat, `$8B21` Pickup_Take) and carry the object's slot, kind or item and
  position. `boss-defeated` comes from the boss hit's site. `land-start`, `land-clear`,
  `checkpoint` and `money-changed` (with the new money and the delta) are seen in RAM at the end of
  the frame and cost no site. A game event's handler now gets a table with the event's `name`
  and values, instead of the event's name. Each fires once per occurrence with the decompiled code
  off, checked or replacing (`tests/decomp_test.py --modes events`).
- Mods: `dt.objects()` (the live object slots: kind, action, state, world X, screen position) and
  `dt.camera()` (world X, map row, screen), read only.
- Mods: `dt.draw.rect` and `dt.draw.text` draw over the presented picture for one frame, at most 64
  a mod each frame; like toasts they never reach the machine's picture or any hash, and the
  window's HUD split repair no longer draws over them.

- The chest's opening, sliding block and health block handlers (kinds `$16`, `$17` and `$42`)
  are decompiled, with poke cases based on three Transylvania chest records.
- `tests/decomp_test.py` poke cases `extra-life` (the extra life `$8BB0`), `secret-treasure` (item `$74`,
  message `$0D` instead of the treasure pose, `$8C37`) and `row-end-right` (Scrooge's X clamped to `$F0` at a
  row's right end, `$F08B`): code no route or poke case ran before.
- Mods: with mods on, `mods.log` in the data folder holds the catalogue (each mod, loaded or why
  not), the scripts' `dt.log` lines, toasts and errors, emptied at each start and capped at 4 MiB.
  The window build has no console when started from Explorer, so `dt.log` went nowhere before.
  The Mod Menu opens it (**Open mods log**) and marks the mods that run a script.
- Route `difficult_himalayas` (6,200 frames): `full_himalayas` on Difficult to the LAND CLEAR tally,
  the first route on Difficult; it passes `verify_routes` at the four alignments with 100% native cycles.
- The native player writes a session summary when its Decompiled code ran: `decomp-session-<time>.txt`
  in `diagnostics/` beside the replays, with the frames played, the decomp error that stopped the game
  if any, and every routine's calls and calls given to the original (`decomp:` lines). None is written
  with the option off.
- The object spawner of bank 6 is decompiled (`src/decomp/level.c`): the table set-up `$AE20`/`$AE22`,
  the reset `$AE60`, the per-frame window update `$AEA0` and the timed spawns at the camera's edge
  `$B0AB`. `$AEA0`'s site replaces the flow's stop there.
- `tests/audio_output_test.py` and `ducktales.exe --audio-out FILE --audio-sim SPEC`: the native
  player's presented sound rendered off screen through its output stage, with the frame loop and the
  audio device on simulated clocks; the test fails on any sample at full scale, any click and any
  underrun over the 12 routes, the worst case (extra voices, full-scale mod, VSync 60 Hz) and the
  window events (docs/VALIDATION.md "Presented sound").
- The native player can run the decompiled C: Settings → Gameplay → Decompiled code (`[Game] DecompiledCode`,
  off by default) runs the routines decompiled to C alone in place of the recompiled ones, with No
  slowdown on only; with No slowdown off the toggle is disabled and its (?) says why. `decomp.c` is now
  the core of two backends, `decomp_cycle.c` (the cycle host's check and replace, unchanged) and
  `decomp_native.c`; `ducktales_native --decomp replace` runs it headless with `--lag none`.
  `tests/native_decomp_test.py` compares RAM after every logic step with the recompiled program on the
  12 routes: equal except the stack and NMI-run state (docs/DECOMPILATION.md "On the native runtime").
- The block draw `$D068` with the debris it throws (bank 4 `$BD80`) and the object tile edits
  `$F4D8`/`$F506` are decompiled (`src/decomp/level.c`); `$D068`'s site replaces the flow's stop
  there. The probe's special-tile switch no longer has an original fallback (every entry of
  `$D1E2` is handled).
- The row scroll `$C976`, the 60-frame vertical transition between map rows, is decompiled as task
  code with resume points `$C99D`, `$C9FB`, `$CA09` and `$CA22`, and its eight step routines
  `$CA4A-$CC84` (block row draw, queue headers, the kept half, the half rows of the screens beside
  the camera's) as routines with sites (`src/decomp/level.c`). Its site replaces the flow's stop
  at `$C976`. The sprite builder's slot routine (bank 5 `$80BE`) is checked on its own.
- The game flow is decompiled as task code (`src/decomp/flow.c`, docs/DECOMPILATION.md "Task
  code"): the title and difficulty select, Land Select with the bank transfer of money, the land load, the frame loop with pause, death, warps,
  respawn, game over and land exit, the special scenes, the scene set-up of message $0B, the final trip's take-off, the ending's screens, the HUD timer and refreshes, and the boss trigger with the
  arena scroll. Its resume points start from the RTS observer, with no hook site.
- `tests/decomp_test.py --changed <ranges> --from-state --span N` checks only the routes that run
  a decompiled routine, from the golden save state before it: 32 s instead of 689 s for a routine
  the routes reach late.
- `tests/decomp_test.py` runs each case twice instead of three times: replace mode compares with
  the check run's RAM, which equals the stock run's byte for byte, and the poke cases start from the
  golden save state at frame 1,500 instead of power-on. All cases share one queue, longest first.
  The full run on main 7bf545d took 1,904 s against 5,145 s for the three-run version, with the
  same results.
- `tools/test_slot.py` queues heavy test runs across worktrees, at most three at once (two left the 16-thread PC at 47% load).
- The system layer is decompiled (`src/decomp/system.c`, `sprites.c`, `audio.c`): bank switching,
  the controllers, PPU blanking, nametable fill, CHR uploads, the OAM clear, the random generator,
  the sprite builder (bank 5), the string drawer and the bank-3 sound driver. They use the bus
  operations of `decomp/level`, with `Decomp_BusLoad` added for reads whose value the C needs; check
  mode now also compares OAM and the device registers, and routines of the switchable window match
  their site only with their bank mapped.
- The palette fades `$C279`/`$C282` are decompiled as task code: each step yields through
  `task_yield` with its frame count on the stack, and the resume point `$C2B2` carries on.
- `tests/decomp_test.py` has an `ending` poke case (`$AC` set): the ending starts and its first scene
  loads its palettes, the only call of `Palette_LoadScene` (`$F76D`) a test reaches.
- `tests/decomp_test.py` names each differing RAM address with its `ducktales_symbols.h` name.
- The camera, the column streaming, the map and block decoding, the VRAM queue uploads and the
  background probe with its four snaps are decompiled (`src/decomp/level.c`, `src/decomp/collision.c`):
  18 routines, checked on every call of every route and RAM-identical in replace mode.
- Decompiled: Scrooge's object contact tests (`$D202`/`$D291`), the Select refill (`$E2FE`) and
  the vertical velocity moves (`$F270`/`$F284`). `tests/decomp_test.py` poke cases can add pad
  states to their route; three of them press Select.
- The object system is decompiled (`src/decomp/objects.c`, `src/decomp/objcommon.c`): the per-frame
  dispatch of bank 6, 43 of the 67 object kinds (most enemies, none of the bosses yet, blocks,
  pickups and what taking each item does, hidden items, chests, doors, the lift, the moving
  platform, the key spot, the treat spout and its treats, and talking characters) and the shared
  helpers: contact with Scrooge, the movers, block tests, the climbing steps, money and HP. 90
  routines, each checked on its own at every call (3,185,085 calls compared on the 12 routes) and
  RAM-identical in replace mode; poke cases cover what no route reaches: the lift, the extra HP
  dot, the money cap, beaten enemies, thefts of money, the pit splashes, a boulder stopping at a
  wall and a treat taken. Hook sites only where original code calls them.
- Decompiled: the row change (`$E8C0`), as task code with its own site. A task routine may now pop
  its own return address before it hands off.
- `src/decomp/player.c` calls the C of the background probe (`$D120`) and its four snaps instead
  of running their originals through `Decomp_Call`.
- Decompiled: Scrooge's modes 15 (`$ED05`, the message box and what each message does) and 16
  (`$EF62`, the treasure's time bonus), as task code at their own sites with 14 resume points.
  `tests/decomp_test.py` has 8 poke cases for what no route reaches (paying, the lost life, the
  block, the Launchpad ride, the scene draw of message `$0B`, the money's blanks and cap) and a
  `--pokes NAME...` option.
- `src/decomp/player.c` calls the C of the probe point `$D350` (with the carry it returns, new in
  `collision.c`) and of the HP refill `$F812` (`objects.c`). A poke case reaches the fall into
  `$D350` under the power-up.
- `Player_Update`'s contract drops `OUT_HW`: the probe's bank switches are undone before it
  returns, and check and replace pass on the 12 routes without it.
- `Player_Update` is task code: it calls the C of modes 15 and 16 and of the row change instead of
  giving those calls to the original (`Decomp_RunOriginal`), so no call of `player.c` goes to the
  original. The seven handlers on the way to the row change are task code checked with it. In task
  code the `Decomp_BusWrite` writes of the routines it calls are its device stores (up to 64), and a
  task routine reached from another one's C counts as compared with it.
- The NMI handler gets its own kind of decompiled routine, **interrupt routines**: task code whose C
  ends in RTI (`src/decomp/nmi.c`, docs/DECOMPILATION.md "Interrupt routines"). Its RTI pays the cycles
  a replaced routine owed when the NMI came, and tells the native runtime the handler ended. The
  handler's exit `$C132` (`Nmi_Return`) is the first.
- The whole NMI handler `$C000` is decompiled (`Nmi_Handler`, `src/decomp/nmi.c`), with the sprite-0
  wait as a new bus operation, `Decomp_BusWait`. Check mode makes the C's bus operations on a clocked
  copy of the machine and compares at the original's RTI: RAM, registers, the PPU's memories and OAM,
  the PRG bank, the PPU and APU settings the NMI writes, and the writes to `$2006`/`$2007` and the APU
  in order. Both runtimes run the C in replace mode. The cycle host measures the original on a
  clocked snapshot, then runs the C's device operations at the original's cycles.
- Decompiled: the task primitives (`src/decomp/system.c`): `task_create` `$FF7D` with the slot offsets
  `$FF9F`/`$FFA1`, and `task_yield` `$FFAC` and `task_wait` `$FFA5` (resume point `$FFA8`) as task code. Their
  sites replace the flow's stops there; `task_yield`'s C hands off to the scheduler `$FF35`, which has a
  stop. A site or stop that cannot be registered (NESRecomp's 128-hook cap) now fails at start-up.
  `tests/native_decomp_test.py` excepts and counts the NMI-counted yield (docs/DECOMPILATION.md "On the
  native runtime").
- Decompiled: object kinds `$36` (`$F869`, `Obj_LaunchpadRide`) and `$37` (`$F8F2`, `Obj_SkyFlyer`, with
  its drop `$F99C`) of the fixed bank, reached in the Launchpad scenes, and kind `$38` (`$F9F8`,
  `Obj_SkyRope`, which lifts Scrooge out of the land), reached by the `sky-rope` and `sky-rope-dying`
  poke cases, and the final arena's stand-in, kinds `$3A` (`$FA3F`, `Obj_FinalStandIn`) and `$3B`
  (`$FBC9`, `Obj_StandInBeaten`), reached by the `stand-in` and `stand-in-beaten` poke cases, and
  kinds `$3C` (`$FB68`, `Obj_Climber`, the final climb's climbers), `$3D` (`$FBFF`, `Obj_HitTalker`, a
  character a hit wakes in the Himalayas) and `$40` (`$FD9E`, `Obj_FinalTreasure`, the prize atop the
  final climb), reached by the `climbers`, `hit-talker`, `final-treasure` and `final-treasure-lost`
  poke cases. `tests/decomp_test.py` has a `clear-keep` poke case for the object
  clear's Transylvania branch (slots 14 and 15 kept).
- Decompiled: the boss hit `$F6D7` (`Boss_Hit`, objects.c; it raises the boss-hit event from C), the
  object clear `$F2C7` (`Obj_ClearSlots`, objcommon.c, called as C by flow.c and level.c) and the TOTAL
  cap `$F84A` (`Total_Max`, flow.c).
- The message box calls the C of the OAM clear `$C267` and the sprite builder (bank 5 `$8000`), and
  bank 5 `$B76A` (the consumed-record bits from the progress flags) is decompiled as
  `Records_MarkTaken`.

### Changed

- The game flow's decompiled C is split by phase into `src/decomp/flow/` (the reset and frame loop, the
  title, Land Select, the land load, the HUD, death, the scenes, the ending, the routine table and a
  shared internal header) instead of one 3,200-line `flow.c`; no behaviour changed.
- The object system's `src/decomp/objects.c` is split by object family into `src/decomp/objects/`
  (dispatch, blocks, items, enemies, walkers, thieves, flyers, hazards, props, bosses, scenes), each
  with its adapters and routine table; the code is unchanged.
- Source layout only: the cycle host's decompilation backend is split into `decomp_cycle.c`,
  `decomp_cycle_task.c`, `decomp_cycle_nmi.c` and `decomp_cycle_clock.c`, and the native player's
  `native_window.c` into `native_player.c` (entry, options, frame), `native_window.c` and
  `native_offscreen.c`. No behaviour changes.
- `src/decomp/player.c` and `src/decomp/level.c` are split into the modules of `src/decomp/player/`
  (contact tests, moves, modes, damage, message box) and `src/decomp/level/` (map, scroll, screen, row
  scroll, block draw, spawner), and `tests/decomp_test.py`'s case tables and builders move to
  `tests/decomp/`. Code moves only: no routine, behaviour or command line changes.
- The mods sources are split by responsibility, with no change in behaviour: the script sandbox
  into `fm_script.c`, `fm_script_state.c`, `fm_script_show.c` and `fm_script_store.c`, DuckTales'
  side into `dt_mods.c`, `dt_mods_script.c` and `dt_mods_picture.c`, and `tests/mods_test.py` into
  its steps under `tests/mods/`.
- The native player (`ducktales.exe`) starts with Decompiled code and No slowdown on. Settings →
  Gameplay → Original mode, at the top of the page with a (?), turns both off for the recompiled
  original with the console's slowdown, and turning it off turns both back on. `config.ini` now
  carries `[Game] SettingsVersion = 2`; a file without it gets both on once, and a later choice,
  Original mode included, is kept. Off-screen runs, `ducktales_cycle.exe` and the headless builds keep
  the C off, so no hash, `quick_verify` or `verify_routes` result changes.

### Fixed

- `--decomp replace` on the cycle host no longer drifts a cycle from the console on lag frames with
  many enemies: a plain routine during which VBlank would come goes to the original, as task code
  already did, since the cycles spent for the C took the NMI a cycle or more off the original's
  instruction boundary. The step 11 mod scripts (write, tune, objects, spawn and level) now keep
  the RAM of `--decomp off` together.

- Script mods: `o:set{action}` accepted only the actions of the records of the land's first map
  row; it now takes those of every row's records and timed objects.
- Extra lives at start had no effect with the decompiled code on: the decompiled title set-up
  (`Title_Lit`) did not raise the new-game event the option uses.

- The native player's sound no longer clips, clicks or drops out (docs/AUDIO.md "Output stage"): a
  look-ahead limiter at -1 dBFS replaces the 16-bit clamp (a full-scale mod over the game's effects
  reached +6 dBFS), the volume ramps instead of stepping and follows the menu live, the queue is
  paced by resampling frames by at most 0.5% instead of stretching one up to 2x or dropping one at
  100 ms, the sound fades in at the start and out when the menu pauses the game, for fast-forward,
  sound off, a window move or resize, F9 and closing, and an underrun decays instead of stepping.
  Mod replacements and the channels they mute fade over 5 ms. The machine and the synth's own
  samples are unchanged.
- `tools/decomp_progress.py` took bank 5 `$8003`, one instruction into `Oam_Build`, for a target
  of the NMI's `JSR $8003` (bank 3's `Sound_Play`), so 187 bytes of the sprite builder counted as
  original. A fixed-bank JSR into the window now skips a bank where an executed instruction falls
  through into the target, when another bank qualifies.
- `Chr_StreamRow` (`$C42C`) now returns the X the original leaves. The scene draw before message
  `$0B` pushes it in `task_yield`, so replace mode left a different byte on the task's stack.
- Replace mode made a decompiled routine's PPU writes back to back. The PPU acts on `$2006`/`$2007`
  writes a few cycles late, so every byte of a VRAM queue landed one address off; the writes are
  now spread over the original routine's cycles.
- `tests/decomp_test.py --from-state` starts only from a golden state where the game's scheduler is
  idle. `full_moon` saved at frame 500 inside a task, and replace mode failed on its first frame
  from there, because the run resumed the original's task while replace ran the C.
- Mods: a mod.toml saved by Notepad (with a UTF-8 byte order mark) failed on line 1, and so did a
  `scripts/main.lua`; both load now. Names inside a mod match whatever their case, so
  `Scripts/Main.lua` no longer showed as a script that then could not be found. mod.toml errors name
  the missing keys, a key set twice and a key that is not a quoted string, and a `prg_sha256` that is
  not 64 hex digits is reported as such instead of as a mod for another version of the game.
- Mods: a mod with two files for the same tile key, land palette or sound id is refused with a
  reason instead of silently using one. **Rescan** in the Mod Menu checks the files
  of the mods that are not loaded, so a broken new mod shows why before a restart, and a mod refused
  at start no longer lost its reason after a Rescan.
- Mods: a stopped script's reason says where it ran (*in a "frame" handler*, *when the script
  started*), and the instruction limit names the line that was running. A `dt.CANCEL` returned for
  an event that cannot be cancelled is reported once instead of ignored silently, and a `dt.set`
  write the state no longer allows at the frame's end is logged instead of dropped silently.
- Mods: a toast fits the picture. The 64-character cut counted bytes and could split a UTF-8
  character; it counts characters now. The drawn notice shows a character its font lacks as `?`
  instead of a gap, and a line wider than the picture ends in `...` instead of running past the
  panel.
- Mods: `dt.store` files are written to a temporary file and moved into place, so a crash or a
  full disk cannot leave half a store; a failed write is logged and tried again instead of lost.
  `--mods-dir` with a path that is not a folder, or a fifth folder, is refused instead of ignored,
  and `--mods-dump` creates the folder's missing parents.
- Mods: hot reload compares the script's write time to the 100 ns instead of the second, so a
  save of the same size within the second of the last one is no longer missed.
- Mods: the window keeps `[Mods]` when saving display defaults on startup; command-line overrides
  take effect without changing the saved choices. A refused package no longer takes the saved
  position and switch of a valid mod with the same ID. Rescan and the restart prompt distinguish
  packages by source, so replacing a file with the same ID doesn't make it appear already loaded.
- Mods: `--mods-dump` writes to paths with accented characters and reports a failed PNG write
  instead of silently dropping tiles. An off-screen run dumps tiles without needing a
  second option to request a presented picture.

## [1.0.0] - 2026-11-23

The first release.

### Added

- The ROM is found by its SHA-256 under any file name: in `roms/` for the build and the test
  scripts, and in the first-run screen and `--import-rom` for players. The configure step refuses a
  ROM with another hash and names both.
- Every setting in the menu, the enhancements included, has a "(?)" after its label with a tooltip
  saying what it does, shown to the mouse and under keyboard or controller focus (formae-ui 0.3.0).
- The project lives on GitHub (https://github.com/formae-group/ducktales-recompiled), with releases
  there and a GitHub Actions check of the layout and the Python syntax on every push and pull
  request. The `external/formae-ui` submodule is fetched from github.com/formae-group/formae-ui.
- Static recompilation of *DuckTales* (USA) on the NESRecomp cycle backend: the 6502 program is
  translated to C at build time and runs natively, with the interpreter kept as a fallback for code
  no route has reached.
- ROM identity check by SHA-256, first-run import by file dialog or drag and drop, and a data folder
  in `%APPDATA%\DuckTales Recompiled` (`DUCKTALES_DATA_DIR` overrides it).
- Dear ImGui menu in the style of Ship of Harkinian, opened with Escape: Settings (General, Graphics,
  Audio, Controls, Gameplay), Enhancements and About tabs with a category sidebar, rebindable
  keyboard and controller input, menu background opacity and controller navigation.
- Enhancements, all off by default: jump height, no damage, infinite time, infinite lives and extra
  lives at start. Each hooks a single site guarded by a content key.
- No sprite flicker: every sprite is shown on every frame without the 8-per-line and 64-sprite
  limits, and the HUD is never cut. On by default and presentation only.
- No slowdown: extra CPU time per frame removes the console's lag frames. On by default.
- Presentation: Direct3D 11, per-monitor DPI awareness, refresh-aware frame pacing (Auto, VSync,
  steady), nearest, sharp and smooth scaling, 8:7 pixel aspect, overscan crop and a clean HUD split
  line.
- Application icon in the executable and the window title bar.
- Release package: CPack writes `DuckTalesRecompiled-<version>-win64.zip` with the executable,
  SDL2, a player README (ROM import, SHA-256, controls) and every third-party licence, checked by
  `tests/package_test.py`; releasing steps in CONTRIBUTING.md.
- Native runtime prototype without the cycle-level host (`DUCKTALES_NATIVE`).
- Mods, off by default (docs/MODS.md): `.dtmod` archives and loose folders from the data folder's
  `mods` folder replace tiles (by CHR content hash and palette), palettes (global or per land),
  music and sound effects (OGG Vorbis or WAV, by request id); earlier mods win per asset, and none of
  it changes the machine. `--mods`, `--mods-dir`, `--mods-order` and `--mods-dump` on both hosts.
- Script mods: `scripts/main.lua` runs in a Lua 5.4 sandbox per mod with `dt.on` over the game
  events and `frame`, read-only `dt.state`, an instruction limit per callback, a 16 MiB memory cap
  and hot reload; an error or a limit stops only that mod's script. `dt.toast` notices over the
  presented picture, a per-mod `dt.store` in `mods-data/<id>.ini`, and `dt.set` for the declared
  writable fields (lives, hp, money, time), applied at the frame's end.
- Mod Menu: Settings, Mods with Enable mods, a reorderable mod list (formae-ui 0.2.0), Open mods
  folder, Rescan, a restart note and each mod's error.
- The player build on the native runtime: `ducktales.exe` runs the recompiled game on the native
  bus, draws it with the native renderer and keeps the menu, settings, bindings, presentation, No
  sprite flicker, enhancements, fast-forward and full screen of the cycle build; F8/F9 save and load
  the native machine state. Sound comes from the native synth. `-DDUCKTALES_PLAYER=native|cycle`
  chooses the runtime; the cycle host build is `ducktales_cycle.exe`. `tests/native_player_test.py`
  compares its picture with the cycle build's.
- The native player keeps its pace with an unfocused window on Windows: no power throttling, the
  frame loop in the MMCSS Games class, a 1 ms timer, and an audio queue that stretches a late frame's
  samples instead of running dry.
- Native synth for the native runtime (`src/native/native_audio.c`): plays each frame's APU
  register writes without emulating the APU cycle by cycle; `ducktales_native --wav-out` records
  it, and `tests/audio_compare.py` compares it with the cycle host's APU per route and per track.
- Two sound options for the native player, both off by default (Settings, Audio; `[Audio]
  MusicUnderEffects` and `OverlapEffects`): *Keep music under sound effects* plays the music part
  an effect takes over on an extra channel, and *Overlapping sound effects* plays an effect the
  driver drops or cuts off on a second effect voice. Both are computed from the sound driver's RAM
  and the ROM's sound data and only mixed in; with both off the sound is unchanged sample for sample.
- Differential verification against the TriCNES reference machine on 12 routes, including all five
  lands played to their LAND CLEAR screens, at four CPU/PPU alignments, with the oracle's results
  cached between runs.
- Mesen2 differential harness, sprite, enhancement and window smoke tests.
- Handwritten C for parts of the game loop and HUD, including death, respawn and the timer.
  The cycle host can check or run these routines; the default still runs the recompiled code.
- Reverse-engineering references: memory map, functions, Scrooge's movement, game state, levels,
  rendering pipeline and sound driver.

- Fast verification tier, `tests/quick_verify.py`: golden per-frame hashes recorded once from a
  verified build, the native runtime or the cycle build at alignment 0, a stop at the first
  differing frame with the fields that differ, route selection by the PRG addresses a change
  touches and starts from recorded save states. The full `tests/verify_routes.py` moves to nightly
  runs and releases.
- `ducktales_native --save-state F:FILE` and `--load-state FILE`.

### Changed

- One code layout for every C, C++ and Python source: `.clang-format` (4 spaces, 120 columns,
  braces always, no one-line statements) and Black, applied by `tools/format.py` (`--check` to
  verify). The machine and every hash are unchanged.
- The cycle host's `--ram-hash-out` numbers frames on from a loaded state (`--load-state`), as
  `--hash-out` does.
- The menu and the first-run screen are drawn by [formae-ui](https://github.com/formae-group/formae-ui)
  0.1.0 (`external/formae-ui`), Formae's menu library extracted from them for the next ports. Their
  look and behaviour are unchanged: the 16 self-test PNGs are pixel-identical.
- The application icon is now original artwork, a treasure chest.
- The window build links the static MSVC runtime, so players need no Visual C++ redistributable.
- The game keeps running behind the open menu on the native player, as in other PC ports; Settings,
  General, *Pause game while menu is open* (`[Game] MenuPausesGame`, off by default) stops it, and
  the Reset and Quit questions always pause it.
- The window build is a Windows program: no console window opens beside the game. Started from a
  terminal, it prints there; pipes and redirections work as before.
- The README was rewritten as a technical overview with the supported ROM's hashes.
- The pause menu no longer shows help and hint text.

### Removed

- The Formae name from the application, its files and its data folder.

[Unreleased]: https://github.com/formae-group/ducktales-recompiled/compare/v1.0.0...main
[1.0.0]: https://github.com/formae-group/ducktales-recompiled/releases/tag/v1.0.0
