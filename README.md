# DuckTales PC Port (Formae)

A native Windows port of *DuckTales* (NES, Capcom, 1989) built by static recompilation with
[NESRecomp](https://github.com/mstan/nesrecomp). The 6502 program is translated to C at build
time from the ROM you supply; NESRecomp's cycle-level PPU, APU, mapper and SDL2 host run around
it. Nothing from the game is stored in this repository.

## Requirements

- Windows x64, Visual Studio 2022 with *Desktop development with C++* (MSVC, Windows SDK)
- CMake 3.20+, Python 3.11+ (code generation runs at configure time), Git
- Your own `DuckTales (USA).nes`; identity in [docs/rom.md](docs/rom.md)

## Build

```powershell
git submodule update --init external/nesrecomp external/imgui
copy "C:\path\to\DuckTales (USA).nes" roms\
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel
build\windows\Release\ducktales.exe
```

`-DDUCKTALES_ROM=<path>` uses a ROM outside `roms/`. The executable checks the ROM's SHA-256
before running. Generated C lives under `build/*/cycle-ducktales/` and is regenerated when the
ROM, `config/` or NESRecomp's compiler changes.

`-DNESRECOMP_DEV_UI=ON` adds developer keys (F2 switches between recompiled code and the
interpreter) and shows native coverage in the title bar.

## Playing

**First run.** Start `ducktales.exe` with no arguments. A window asks for your
`DuckTales (USA).nes`: choose it, or drop it on the window. The file must match the original's
SHA-256 exactly. A modified or different file is refused, and the screen shows its hash. The game
keeps a copy in `%APPDATA%\Formae\DuckTales\`, and later starts use that copy directly. To import
without a window, use `ducktales.exe --import-rom "C:\path\to\DuckTales (USA).nes"` (exit code 0
when imported, 1 when refused). A ROM path on the command line is always used as given. The
`FORMAE_DATA_DIR` environment variable moves the data folder.

**Menu.** Escape, or RB on a controller, pauses the game and opens the menu: Resume, Reset,
Settings, Controls, Enhancements and Quit. It works with the keyboard (arrow keys, Enter, Esc),
the mouse and a controller (D-pad, A, B). The game is paused while the menu is open.

- Settings: display (windowed, borderless or exclusive fullscreen), window size, integer scaling,
  VSync (applies at the next start) and volume.
- Controls: keyboard and controller bindings for each NES button and for the menu. Choose a cell,
  then press the new key or button. Esc cancels and Delete clears.

Everything is saved in `config.ini` next to the executable, in NESRecomp's format
(`--config FILE` uses another file). The defaults are Z/X = A/B, Return = Start, Backslash =
Select and the arrow keys for the D-pad. Controllers work without setup. F11 or Alt+Enter toggles
fullscreen, Tab fast-forwards, and F8/F9 save and load a state.

**Enhancements.** All are off by default, and while one is off the game runs its original code.
Set them in the menu, in `config.ini` `[Game]`, or on the command line:

| Menu | `[Game]` key | Option | Values |
|---|---|---|---|
| Jump height | `JumpScale` | `--jump-scale N` | 25-400 percent, 100 = original |
| No damage | `NoDamage` | `--no-damage on` | on/off: hits still knock Scrooge back but cost no health |
| Infinite time | `InfiniteTime` | `--infinite-time on` | on/off: the level timer stops |
| Infinite lives | `InfiniteLives` | `--infinite-lives on` | on/off: losing a life costs none |
| Extra lives at start | `StartLives` | `--start-lives N` | 0-9 or off (original: 2), from the next new game |

How each works, and the evidence for it, is in [docs/architecture.md](docs/architecture.md).
`-DDUCKTALES_DEV_DIAGNOSTICS=ON` makes a developer build with a typed state log
(`--dt-state-log -`), scheduled changes (`--dt-enh-at`, `--dt-reset-at`) and TCP commands. It is
never used for distribution.

**UI self-test.** `FORMAE_UI_SELFTEST=<folder>` (a Windows path) makes the first-run screen and the
menu run by themselves and write PNGs of each screen. `py -3 -I tests/window_smoke.py --build
build/windows` runs it, together with the start, import and refusal checks.

## Verification build

```powershell
cmake -S . -B build/headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON
cmake --build build/headless --config Release --parallel
py -3 -I tests/verify_routes.py --build build/headless
```

Each route in `tests/routes/` runs recompiled, interpreter-only and on the TriCNES reference
machine at four CPU/PPU alignments, and must match on every frame. See
[docs/validation.md](docs/validation.md).

## Layout

| Path | Contents |
|---|---|
| `external/nesrecomp` | NESRecomp, pinned submodule, unmodified |
| `external/imgui` | Dear ImGui v1.91.9b, pinned submodule (window build only) |
| `config/game.toml` | Recompiler configuration for this ROM |
| `config/ducktales.seeds` | Bank-qualified entry points found by route runs (addresses only) |
| `tests/routes/` | Deterministic input schedules (`<frame> <buttons>`) |
| `tests/verify_routes.py` | Differential regression test |
| `src/game/` | Typed game state, revision-specific symbols, behaviour hooks |
| `src/host/` | Integration with NESRecomp's host (`main`, settings, options, dev diagnostics) |
| `src/ui/` | The Formae application: first run and ROM import, menu, live settings |
| `tools/` | Investigation helpers (disassembler, frame logs, access tracer, map/OAM checks, RAM pokes in save states, Mesen2 replay) |
| `docs/` | ROM identity, reverse-engineering notes, validation, licensing |

## Refreshing seeds

New routes can reach code no route has executed yet; it still runs correctly on the interpreter.
To compile it, append the route's misses and rebuild:

```powershell
build\headless\Release\ducktales.exe "roms\DuckTales (USA).nes" --input tests\routes\NEW.txt --frames N --miss-log config\ducktales.seeds
```

## Licence

Formae's files are MIT ([LICENSE](LICENSE)). NESRecomp is PolyForm Noncommercial 1.0.0 with
additional permissions, so builds of this port must be free of charge. See
[docs/licensing.md](docs/licensing.md).
