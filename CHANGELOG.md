# Changelog

All notable changes to DuckTales Recompiled are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- The game flow is decompiled as task code (`src/decomp/flow.c`, docs/decompilation.md "Task
  code"): the title and difficulty select, Land Select with the bank transfer of money, the land load, the frame loop with pause, death, warps,
  respawn, game over and land exit, the special scenes, the HUD timer and refreshes, and the boss trigger with the
  arena scroll. Its resume points start from the RTS observer, with no hook site.
- `tests/decomp_test.py --changed <ranges> --from-state --span N` checks only the routes that run
  a decompiled routine, from the golden save state before it: 32 s instead of 689 s for a routine
  the routes reach late.
- `tools/test_slot.py` queues heavy test runs across worktrees, at most two at once.
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
  dispatch of bank 6, 21 of the 67 object kinds (the Amazon's enemies, the Himalayas' dangler,
  Transylvania's swooper, blocks, pickups and what taking each item does, hidden items, chests,
  doors, the lift and talking characters) and the shared helpers: contact with Scrooge, the movers,
  block tests, the climbing steps, money and HP. 62 routines, each checked on its own at every call
  (1,989,532 calls compared on the 12 routes) and RAM-identical in replace mode; poke cases cover
  the lift, the extra HP dot and the money cap, which no route reaches. Hook sites only where
  original code calls them.
- Decompiled: the row change (`$E8C0`), as task code with its own site. A task routine may now pop
  its own return address before it hands off.
- `src/decomp/player.c` calls the C of the background probe (`$D120`) and its four snaps instead
  of running their originals through `Decomp_Call`.

### Fixed

- Replace mode made a decompiled routine's PPU writes back to back. The PPU acts on `$2006`/`$2007`
  writes a few cycles late, so every byte of a VRAM queue landed one address off; the writes are
  now spread over the original routine's cycles.
- `tests/decomp_test.py --from-state` starts only from a golden state where the game's scheduler is
  idle. `full_moon` saved at frame 500 inside a task, and replace mode failed on its first frame
  from there, because the run resumed the original's task while replace ran the C.

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
- Mods, off by default (docs/mods.md): `.dtmod` archives and loose folders from the data folder's
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
