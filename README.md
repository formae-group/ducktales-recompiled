# DuckTales Recompiled

A native PC port of *DuckTales* (NES, Capcom, 1989), built by static recompilation with
[NESRecomp](https://github.com/mstan/nesrecomp). At build time the game's 6502 program is
translated to C from a ROM you supply, and NESRecomp's cycle-accurate PPU, APU, mapper and SDL2
host run around it. The repository contains no game data.

## Features

- **Native code, verified frame by frame.** Every test route runs recompiled, interpreted and on a
  reference machine, and the three must match on every frame (see [Verification](#verification)).
- **No sprite flicker.** Every sprite is shown on every frame, in a fixed order, with the HUD always
  up; the console's 8-sprites-per-line and 64-sprite limits only apply to the machine underneath. On by
  default; switch it off for the original look.
- **Settings menu.** Display mode, window size, integer scaling, VSync, volume, sprite flicker and key
  and controller bindings, all applied live and saved to `config.ini`.
- **Optional enhancements.** Jump height, no damage, infinite time, infinite lives and starting
  lives. All are off by default, and while one is off the original code runs unchanged.
- **Keyboard, mouse and controller** support with no setup.

## Requirements

| | |
|---|---|
| OS | Windows, x64 |
| Toolchain | Visual Studio 2022 with *Desktop development with C++*, CMake 3.20+, Python 3.11+, Git |
| Game | Your own `DuckTales (USA).nes` (identity in [docs/rom.md](docs/rom.md)) |

## Building

``powershell
git clone --recurse-submodules https://gitlab.com/formae/ducktales.git
cd ducktales
copy "C:\path\to\DuckTales (USA).nes" roms\
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel
``

The executable is `build\windows\Release\ducktales.exe`. To use a ROM outside `roms/`, pass
`-DDUCKTALES_ROM=<path>` when configuring. The generated C code is written under
`build/*/cycle-ducktales/` and regenerated whenever the ROM, `config/` or NESRecomp's
compiler changes.

| CMake option | Effect |
|---|---|
| `-DNESRECOMP_DEV_UI=ON` | Developer keys (F2 switches between recompiled code and the interpreter) and native coverage in the title bar |
| `-DDUCKTALES_DEV_DIAGNOSTICS=ON` | Typed state log, scheduled changes and TCP commands; never for distribution |
| `-DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON` | Windowless verification build (see [Verification](#verification)) |

## Playing

### First run

Start `ducktales.exe` without arguments and choose your `DuckTales (USA).nes`, or drop it on
the window. The file must match the original's SHA-256 exactly; anything else is refused and its
hash is shown. The verified copy is stored in `%APPDATA%\DuckTales Recompiled\` and used on
every later start.

To import without a window, run `ducktales.exe --import-rom "C:\path\to\DuckTales (USA).nes"`
(exit code 0 on success, 1 when refused). A ROM path given on the command line is always used
as is, and the `DUCKTALES_DATA_DIR` environment variable moves the data folder.

### Controls

| NES | Keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| A / B | Z / X | A / X |
| Start / Select | Return / Backslash | Start / Back |
| Menu | Escape | RB |

| Key | Action |
|---|---|
| F11 or Alt+Enter | Toggle fullscreen |
| Tab | Fast-forward |
| F8 / F9 | Save / load state |

### Menu

Escape or RB pauses the game and opens the menu: Resume, Reset, Settings, Controls, Enhancements
and Quit. It works with the keyboard, the mouse and a controller. To rebind, choose a cell on the
Controls page and press the new key or button; Escape cancels and Delete clears the binding. VSync
changes take effect at the next start.

All settings are saved in `config.ini` next to the executable, in NESRecomp's format
(`--config FILE` selects another file).

### No sprite flicker

The NES draws at most 8 sprites on a line and 64 in all, and DuckTales alternates their order every
frame, so crowded screens flicker, objects swap front and back, and the HUD vanishes while Scrooge is at
the top of the screen. With **No sprite flicker** (Settings page, `[Game] NoSpriteFlicker = on|off`,
`--no-sprite-flicker on|off`; default on) the window draws every sprite the game wants in a fixed order
and keeps the HUD up. Only the presented picture changes: the machine, its timing and save states are
the console's. Damage blinking stays. How it works: [docs/rendering.md](docs/rendering.md#sprites-without-limits-presentation).

### Enhancements

Set them in the menu, in the `[Game]` section of `config.ini`, or on the command line.

| Enhancement | `[Game]` key | Option | Values |
|---|---|---|---|
| Jump height | `JumpScale` | `--jump-scale N` | 25 to 400 percent; 100 is the original |
| No damage | `NoDamage` | `--no-damage on` | Hits still knock Scrooge back but cost no health |
| Infinite time | `InfiniteTime` | `--infinite-time on` | The level timer stops |
| Infinite lives | `InfiniteLives` | `--infinite-lives on` | Losing a life costs none |
| Extra lives at start | `StartLives` | `--start-lives N` | 0 to 9, or off (original: 2), from the next new game |

How each one works, and the evidence behind it, is in [docs/architecture.md](docs/architecture.md).

## Verification

``powershell
cmake -S . -B build/headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON
cmake --build build/headless --config Release --parallel
py -3 -I tests/verify_routes.py --build build/headless
``

Each input route in `tests/routes/` runs recompiled, interpreter-only and on the TriCNES
reference machine at four CPU/PPU alignments, and the runs must agree on every frame. Details are in
[docs/validation.md](docs/validation.md).

`py -3 -I tests/sprites_test.py --build build/headless --dev-build build/headless-dev` checks the sprite
presentation against the machine's picture.

`py -3 -I tests/window_smoke.py --build build/windows` checks the window build: start-up, ROM
import and refusal, and every menu page. Setting `DUCKTALES_UI_SELFTEST=<folder>` makes the
first-run screen and the menu run on their own and save a PNG of each screen.

### Refreshing seeds

A new route can reach code that no route has executed yet. That code still runs correctly on the
interpreter; to compile it natively, append the route's misses and rebuild:

``powershell
build\headless\Release\ducktales.exe "roms\DuckTales (USA).nes" --input tests\routes\NEW.txt --frames N --miss-log config\ducktales.seeds
``

## Repository layout

| Path | Contents |
|---|---|
| `src/game/` | Typed game state, revision-specific symbols, enhancement hooks |
| `src/host/` | Integration with NESRecomp's host: entry point, settings, options, diagnostics |
| `src/ui/` | First run and ROM import, menu, live settings |
| `config/` | Recompiler configuration and entry-point seeds for this ROM |
| `tests/` | Input routes, differential regression, window and enhancement tests |
| `tools/` | Reverse-engineering helpers (disassembler, frame logs, access tracer, Mesen2 replay) |
| `docs/` | ROM identity, reverse-engineering notes, architecture, validation, licensing |
| `external/` | NESRecomp and Dear ImGui as pinned, unmodified submodules |

## License

The code in this repository is MIT licensed ([LICENSE](LICENSE)). NESRecomp is licensed under
PolyForm Noncommercial 1.0.0 with additional permissions, so builds of this port must be
distributed free of charge. See [docs/licensing.md](docs/licensing.md).

*DuckTales* is a trademark of Disney. This project is not affiliated with Disney or Capcom and does
not include any part of the game.
