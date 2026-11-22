<div align="center">

<img src="assets/icon/ducktales-128.png" alt="DuckTales Recompiled" width="128" height="128">

# DuckTales Recompiled

A native Windows port of *DuckTales* (NES, Capcom, 1989), built by static recompilation.

</div>

DuckTales Recompiled translates the game's 6502 program into C at build time and compiles it into a
native executable. The game logic runs as native code; the cartridge hardware it talks to (PPU, APU,
UxROM mapper, controllers) is supplied by the [NESRecomp](https://github.com/mstan/nesrecomp) runtime,
and a port layer adds the menu, settings, presentation and enhancements on top. Every build is checked
frame by frame against an independent reference machine.

The repository contains no game data. You supply your own ROM, and the build generates all
ROM-derived code locally.

## Features

- **Native execution.** 100% of the CPU cycles executed across the test routes run in recompiled
  code; the interpreter remains only as a fallback for code no route has reached.
- **Verified against a reference.** Recompiled, interpreted and TriCNES runs must produce identical
  per-frame state on every route at all four CPU/PPU alignments.
- **No sprite flicker.** Every sprite is drawn on every frame in a fixed order, with no 8-per-line or
  64-sprite limit, and the HUD stays visible. Presentation only; the machine is untouched.
- **No slowdown.** Extra CPU time per frame removes the console's lag frames without changing game
  logic.
- **Modern presentation.** Direct3D 11, per-monitor DPI awareness, refresh-aware frame pacing,
  sharp scaling, 8:7 pixel aspect and overscan crop.
- **Enhancements.** Optional gameplay changes, all off by default; when off, the original code runs.
- **Input.** Keyboard, mouse and SDL game controllers, fully rebindable.

## Supported ROM

Only one revision is supported. The file is verified when imported and again at every start; any
other dump, revision or modified image is refused.

| Field | Value |
|---|---|
| File | `DuckTales (USA).nes`, 131,088 bytes, iNES 1.0, mapper 2 (UxROM), 128 KiB PRG, 8 KiB CHR-RAM |
| SHA-256 | `97d472f7b2c1088448d1644155b2650a546bad887b3cba571e16d7986824c169` |
| SHA-1 | `f83191749d019d75eb5ff95d247ef25882c2571a` |
| MD5 | `ee0812f62f6595e45812a8b26bbb637e` |
| CRC32 | `06B994B2` |
| CRC32 (without header) | `EFB09075` |

Further detail on the header and board behaviour is in [docs/rom.md](docs/rom.md).

## Building

Requirements: Windows x64, Visual Studio 2022 (*Desktop development with C++*), CMake 3.20+,
Python 3.11+ and Git.

```powershell
git clone --recurse-submodules https://gitlab.com/formae/ducktales-recompiled.git
cd ducktales-recompiled
copy "C:\path\to\DuckTales (USA).nes" roms\
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake --build build/windows --config Release --parallel
```

The executable is written to `build\windows\Release\ducktales.exe`. Code generation runs at configure
time and again whenever the ROM, `config/` or the recompiler changes; the generated sources live under
`build/*/cycle-ducktales/` and are never committed. The window build links the static MSVC runtime,
and `cmake --build build/windows --config Release --target PACKAGE` writes the player package
`DuckTalesRecompiled-<version>-win64.zip` (see [CONTRIBUTING.md](CONTRIBUTING.md#releasing)).

| CMake option | Effect |
|---|---|
| `-DDUCKTALES_ROM=<path>` | Use a ROM outside `roms/` |
| `-DDUCKTALES_HEADLESS=ON` | Windowless build for verification |
| `-DDUCKTALES_ORACLE=ON` | Also build the TriCNES reference machine (`ducktales_oracle.exe`) |
| `-DDUCKTALES_DEV_DIAGNOSTICS=ON` | Typed state log, scheduled changes, TCP commands; development only |
| `-DNESRECOMP_DEV_UI=ON` | Developer keys and native-coverage display |

## Running

On first launch, select your `DuckTales (USA).nes` or drop it on the window. The file's SHA-256 is
checked against the table above, and the verified copy is stored in `%APPDATA%\DuckTales Recompiled\`
for later launches. Headless import: `ducktales.exe --import-rom <path>` (exit code 0 on success,
1 when refused). `DUCKTALES_DATA_DIR` overrides the data folder.

| Action | Keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| A / B | Z / X | A / X |
| Start / Select | Return / Backslash | Start / Back |
| Menu | Escape | RB |
| Fullscreen | F11, Alt+Enter | |
| Fast-forward (hold) | Tab | RT |
| Save / load state | F8 / F9 | |

Settings are saved to `config.ini` next to the executable (`--config FILE` selects another file).

### Menu

Escape or RB pauses the game and opens the menu; Escape, B, RB or the Resume button closes it. The bar
at the top holds the **Settings**, **Enhancements** and **About** tabs, and Reset, Quit (each asks
first) and Resume on the right. Each tab lists its categories on the left and the chosen one's page
on the right. Everything works with the mouse, the keyboard (arrows, Tab, Enter, Space) and a
controller (D-pad, A, B); changes apply at once and are saved to `config.ini`. On the Controls page,
choose a cell and press the new key or button: Escape cancels and Delete clears.

| Tab, category | Setting | Values | Key |
|---|---|---|---|
| Settings, General | Menu background opacity | 0% to 100% (80%) | `[Game] MenuOpacity` |
| | Controller navigation | On (default), off | `[Game] MenuControllerNav` |
| Settings, Graphics | Display mode | Windowed, borderless, exclusive fullscreen | `[Display] Fullscreen` |
| | Window size | 1x to 8x | `[Display] WindowScale` |
| | Integer scaling | On, off | `[Display] IntegerScale` |
| | Pixels | Nearest, sharp, smooth | `[Game] Pixels` |
| | Pixel aspect | Square, 8:7 | `[Game] PixelAspect` |
| | Crop overscan | Off, top and bottom, all edges | `[Game] Overscan` |
| | HUD split line | Clean, original | `[Game] HudSplit` |
| | Frame pacing | Auto, VSync, steady | `[Game] FramePacing` |
| | No sprite flicker | On (default), off | `[Game] NoSpriteFlicker` |
| Settings, Audio | Master volume | 0% to 100% | `[Audio] Volume` |
| Settings, Controls | Player 1 keyboard and controller bindings, Menu shortcut | | `[Keyboard.*]`, `[Gamepad.*]` |
| Settings, Gameplay | No slowdown | On (default), off | `[Game] NoSlowdown` |
| Enhancements, Gameplay | The enhancements below | | `[Game]` |
| Enhancements, Restore defaults | Restore the original game | | |

Frame pacing applies at the next launch: *Auto* uses VSync on displays up to 70 Hz and a steady
present hold above that. Measurements are in [docs/presentation.md](docs/presentation.md). The About
tab shows the version, the build date and the credits.

### Enhancements

| Enhancement | Key | Option | Values |
|---|---|---|---|
| Jump height | `JumpScale` | `--jump-scale N` | 25 to 400 percent (100 = original) |
| No damage | `NoDamage` | `--no-damage on` | Hits knock Scrooge back but cost no health |
| Infinite time | `InfiniteTime` | `--infinite-time on` | The level timer stops |
| Infinite lives | `InfiniteLives` | `--infinite-lives on` | Losing a life costs none |
| Extra lives at start | `StartLives` | `--start-lives N` | 0 to 9, or off (original: 2) |

Each enhancement hooks a single, documented site in the original code, guarded by a content key
(address, length and CRC-32) so it can never fire on unexpected code.

## How it works

1. **Recompilation.** NESRecomp's compiler reads the ROM, starts from the vectors, the functions
   declared in `config/game.toml` and the entry points recorded in `config/ducktales.seeds`, and
   emits one C function per 1 KiB chunk of each 4 KiB bank view. Each instruction performs its CPU cycles through the runtime, so timing stays
   cycle-exact with the PPU and APU.
2. **Runtime.** The generated code links against NESRecomp's cycle host, which supplies the PPU, APU,
   mapper, input, audio output and the SDL2 window. Code that no seed covers runs on the interpreter
   and is reported, so it can be added to the seeds.
3. **Port layer.** `src/game` (typed game state, revision-specific symbols, enhancement hooks),
   `src/host` (settings, options, the sprite compositor, diagnostics) and `src/ui` (Dear ImGui menu,
   ROM import, presentation) attach to the host through its public extension points. The NESRecomp
   submodule is never modified.
4. **Presentation.** The window shows a picture composed from the machine's output: all sprites
   without hardware limits, the cleaned HUD split, the chosen scaling and pacing. The machine's own
   picture, used for verification and save states, is unchanged.

## Verification

```powershell
cmake -S . -B build/headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON
cmake --build build/headless --config Release --parallel
py -3 -I tests/verify_routes.py --build build/headless
```

Each route in `tests/routes/` is a deterministic input schedule from power-on. It covers the five
lands, played to their LAND CLEAR screens, plus intro and mapping routes. Every route runs three
ways at four CPU/PPU clock alignments:

| Run | Purpose |
|---|---|
| Recompiled | The build under test |
| Interpreter only | Same host, no native code |
| TriCNES oracle | Independent reference machine |

Per-frame hashes cover the bus trace, CPU RAM, nametables, OAM, palette, CHR-RAM, the picture and
the CPU registers, and must be identical on every frame. The oracle's hashes are cached in
`build/verify-cache/`, keyed on the oracle binary, ROM, route, frame count and alignment
(`--no-cache` reruns it).

| Test | Checks |
|---|---|
| `tests/verify_routes.py` | Recompiled, interpreter and oracle equality on every route |
| `tests/sprites_test.py` | Sprite presentation versus the machine picture |
| `tests/enhancements_test.py` | Each enhancement's effect, and identity with the original when off |
| `tests/window_smoke.py` | Start-up, ROM import and refusal, settings persistence, every menu tab and category |
| `tests/package_test.py` | The release ZIP's contents, and the game starting from it unpacked |

New routes can reach code no route has executed; append its interpreter misses to the seeds and
rebuild:

```powershell
build\headless\Release\ducktales.exe "roms\DuckTales (USA).nes" --input tests\routes\NEW.txt --frames N --miss-log config\ducktales.seeds
```

## Documentation

| Document | Contents |
|---|---|
| [architecture.md](docs/architecture.md) | Port layer, hooks, enhancements, application |
| [validation.md](docs/validation.md) | Routes, coverage and verification results |
| [rendering.md](docs/rendering.md) | Screen data, sprite builder, flicker analysis |
| [presentation.md](docs/presentation.md) | Pacing, DPI, scaling and picture measurements |
| [native-runtime.md](docs/native-runtime.md) | Native frame loop and lag analysis |
| [memory-map.md](docs/memory-map.md), [functions.md](docs/functions.md) | RAM and routine reference |
| [player.md](docs/player.md), [game-state.md](docs/game-state.md), [levels.md](docs/levels.md), [audio.md](docs/audio.md) | Reverse-engineering notes |

## License

The code in this repository is MIT licensed ([LICENSE](LICENSE)). NESRecomp is licensed under
PolyForm Noncommercial 1.0.0 with additional permissions, so builds of this port must be distributed
free of charge. Dear ImGui and SDL2 are used under their own licenses. See
[docs/licensing.md](docs/licensing.md).

*DuckTales* is a trademark of Disney. This project is not affiliated with Disney or Capcom and
distributes no part of the game.
