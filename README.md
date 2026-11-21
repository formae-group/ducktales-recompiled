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
git submodule update --init external/nesrecomp
copy "C:\path\to\DuckTales (USA).nes" roms\
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel
build\windows\Release\ducktales.exe
```

`-DDUCKTALES_ROM=<path>` uses a ROM outside `roms/`. The executable checks the ROM's SHA-256
before running. Generated C lives under `build/*/cycle-ducktales/` and is regenerated when the
ROM, `config/` or NESRecomp's compiler changes.

Window controls come from NESRecomp's host and are stored in `config.ini` next to the
executable: keyboard Z/X = A/B, Return = Start, Backslash = Select, arrows = D-pad; game
controllers work without setup. F11 or Alt+Enter toggles borderless fullscreen; Escape opens the
host menu; Tab fast-forwards; F8/F9 save and load a state. `-DNESRECOMP_DEV_UI=ON` adds
developer keys (F2 switches between recompiled code and the interpreter) and shows native
coverage in the title bar.

## Experimental options

All off by default; the original game is unchanged unless you set them.

- `[Game] JumpScale` in `config.ini` (or `--jump-scale N`): Scrooge's jump take-off velocity in
  percent (25-400, 100 = original). See [docs/architecture.md](docs/architecture.md).
- `-DDUCKTALES_DEV_DIAGNOSTICS=ON`: developer build with a typed state log
  (`--dt-state-log -`) and TCP commands; never used for distribution.

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
| `config/game.toml` | Recompiler configuration for this ROM |
| `config/ducktales.seeds` | Bank-qualified entry points found by route runs (addresses only) |
| `tests/routes/` | Deterministic input schedules (`<frame> <buttons>`) |
| `tests/verify_routes.py` | Differential regression test |
| `src/game/` | Typed game state, revision-specific symbols, behaviour hooks |
| `src/host/` | Integration with NESRecomp's host (settings, options, dev diagnostics) |
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
