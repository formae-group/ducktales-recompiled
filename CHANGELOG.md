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
- Differential verification against the TriCNES reference machine on 12 routes, including all five
  lands played to their LAND CLEAR screens, at four CPU/PPU alignments, with the oracle's results
  cached between runs.
- Mesen2 differential harness, sprite, enhancement and window smoke tests.
- Reverse-engineering references: memory map, functions, Scrooge's movement, game state, levels,
  rendering pipeline and sound driver.

### Changed

- The window build links the static MSVC runtime, so players need no Visual C++ redistributable.
- The README was rewritten as a technical overview with the supported ROM's hashes.
- The pause menu no longer shows help and hint text.

### Removed

- The Formae name from the application, its files and its data folder.
