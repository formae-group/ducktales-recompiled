# Changelog

All notable changes to DuckTales Recompiled are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). No version has been released yet.

## [Unreleased]

### Added

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
- The player build on the native runtime: `ducktales.exe` runs the recompiled game on the native
  bus, draws it with the native renderer and keeps the menu, settings, bindings, presentation, No
  sprite flicker, enhancements, fast-forward and full screen of the cycle build; F8/F9 save and load
  the native machine state. Sound goes through a synth seam that plays silence until the native
  synth lands. `-DDUCKTALES_PLAYER=native|cycle` chooses the runtime; the cycle host build is
  `ducktales_cycle.exe`. `tests/native_player_test.py` compares its picture with the cycle build's.
- The native player keeps its pace with an unfocused window on Windows: no power throttling, the
  frame loop in the MMCSS Games class, a 1 ms timer, and an audio queue that stretches a late frame's
  samples instead of running dry.
- Native synth for the native runtime (`src/native/native_audio.c`): plays each frame's APU
  register writes without emulating the APU cycle by cycle; `ducktales_native --wav-out` records
  it, and `tests/audio_compare.py` compares it with the cycle host's APU per route and per track.
- Differential verification against the TriCNES reference machine on 12 routes, including all five
  lands played to their LAND CLEAR screens, at four CPU/PPU alignments, with the oracle's results
  cached between runs.
- Mesen2 differential harness, sprite, enhancement and window smoke tests.
- Reverse-engineering references: memory map, functions, Scrooge's movement, game state, levels,
  rendering pipeline and sound driver.

### Changed

- One code layout for every C, C++ and Python source: `.clang-format` (4 spaces, 120 columns,
  braces always, no one-line statements) and Black, applied by `tools/format.py` (`--check` to
  verify). The machine and every hash are unchanged.
- The menu and the first-run screen are drawn by [formae-ui](https://gitlab.com/formae/formae-ui)
  0.1.0 (`external/formae-ui`), Formae's menu library extracted from them for the next ports. Their
  look and behaviour are unchanged: the 16 self-test PNGs are pixel-identical.
- The application icon is now original artwork, a treasure chest.
- The window build links the static MSVC runtime, so players need no Visual C++ redistributable.
- The window build is a Windows program: no console window opens beside the game. Started from a
  terminal, it prints there; pipes and redirections work as before.
- The README was rewritten as a technical overview with the supported ROM's hashes.
- The pause menu no longer shows help and hint text.

### Removed

- The Formae name from the application, its files and its data folder.
