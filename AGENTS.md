# AGENTS.md

Instructions for coding agents working in this repository. Read [README.md](README.md),
[CONTRIBUTING.md](CONTRIBUTING.md) and the `docs/` page for the area you are changing before editing.

## Project

DuckTales Recompiled is a native Windows port of *DuckTales* (NES, USA) built by static recompilation
on NESRecomp. The ROM is translated to C at configure time; the port layer in `src/` adds the menu,
settings, presentation and enhancements. The goal is a polished PC port, not an emulator.

## Hard rules

- Never modify `external/nesrecomp`, `external/imgui` or `external/formae-ui`. Use NESRecomp's public
  extension points only; menu look and widget changes go to the formae-ui repository, then the
  submodule moves to its new tag.
- Never commit the ROM, generated C, `build*/`, `captures/`, frame logs, screenshots, recordings,
  WAVs or `config.ini`.
- With every option at its original setting the machine must behave exactly as the console:
  `tests/verify_routes.py` must pass with native, interpreter and oracle equal on every frame, at all
  four alignments, with 100% native cycles.
- Presentation changes may alter only what the window shows, never the machine picture or any hash.
- Gameplay changes are enhancements, off by default, hooked at a site declared in `config/game.toml`
  with a length/CRC-32 content key (see docs/architecture.md, "Adding a behaviour change").
- Never open a visible window on the developer's desktop. Use the headless builds, `--hidden` or the
  self-test modes.
- Windows x64 only. Do not add Linux or macOS build paths.
- Do not push to `main` or merge without being asked.

## Environment

- Windows, Visual Studio 2022, CMake, Python through `py -3 -I`, Git for Windows.
- Pass `C:/...` paths to git and to the executables, not `/c/...`.
- The ROM is `roms/DuckTales (USA).nes` (ignored by git), or `-DDUCKTALES_ROM=<path>`. Its SHA-256 is
  `97d472f7b2c1088448d1644155b2650a546bad887b3cba571e16d7986824c169`.
- Disk space is limited. Keep at least 8 GB free, bound frame logs with `--frame-log-frames` and
  delete scratch under `captures/` when done.

## Builds

| Tree | Configure flags | Use |
|---|---|---|
| `build/windows` | (none) | The game as players run it; `tests/window_smoke.py` |
| `build/headless` | `-DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON` | Differential tests with the TriCNES oracle |
| `build/headless-dev` | `-DDUCKTALES_HEADLESS=ON -DDUCKTALES_DEV_DIAGNOSTICS=ON` | Typed state log and scheduled changes for tests |

Configure with `-G "Visual Studio 17 2022" -A x64` and build with
`cmake --build <tree> --config Release --parallel`.

## Checks

| Change | Command |
|---|---|
| Game code, hooks, seeds, runtime | `py -3 -I tests/verify_routes.py --build build/headless --jobs 3` |
| Enhancements | `py -3 -I tests/enhancements_test.py --build build/headless --dev-build build/headless-dev` |
| Sprite presentation | `py -3 -I tests/sprites_test.py --build build/headless --dev-build build/headless-dev` |
| Menu, settings, window | `py -3 -I tests/window_smoke.py --build build/windows` |
| ROM identity and import | `build/headless/Release/app_rom_test.exe` |
| Release package, install rules | `py -3 -I tests/package_test.py --build build/windows` |

The full `verify_routes.py` run takes most of an hour; iterate with `--routes <name> ...` and run the
full set once at the end. Oracle results are cached in `build/verify-cache/`.

## Conventions

- C everywhere in `src/`; C++ only in `src/ui/app_ui.cpp`. Four-space indent,
  CRLF line endings.
- Each source file opens with a header comment naming its purpose and the settings or options it
  owns. No comments inside function bodies.
- Prefixes: `dt_` / `Dt` for game code, `app_` for the application layer.
- Commit subjects are short imperative sentences in sentence case, no prefix, no trailing period.
- Keep the README, the matching `docs/` page, `docs/validation.md` (results) and `CHANGELOG.md`
  current in the same change.
- Report what you verified with exact numbers (routes, frames, results), and say what you did not
  verify.
