# The build with no ROM

`-DDUCKTALES_ROMLESS=ON` builds the native player with nothing taken from the ROM: no code is
generated, no ROM is read when CMake configures, and the program carries none of the ROM's bytes.
The player imports their own ROM at first run (or with `--import-rom`), as with every build, and the
game runs on it. The releases are this build (`.github/workflows/release.yml`,
[CONTRIBUTING.md](../CONTRIBUTING.md#releasing)). The line drawn is the one Ship of Harkinian draws:
the decompiled code ships, the game's data comes from the player's ROM.

The ROM build (the default) is unchanged and stays the maintainer's build: the cycle host
(`ducktales_cycle.exe`), the headless verification host, the oracle and the recompiled code all need
it.

## What came from the ROM, and where it comes from now

| In the ROM build | In the build with no ROM |
|---|---|
| The recompiled 6502 program (`cycle-ducktales/generated/game_cyc*.c`, about 25 MB of C that folds the ROM's operands into constants; it can also embed the whole PRG for its guards, compiled out on the native runtime) | Not built. `src/native/native_romless.c` answers `cyc_native_has()` with false for every address, so each instruction the decompiled routines leave to the original runs in NESRecomp's instruction templates (`cpu6502_interp.c`, already linked by the native runtime for code the recompiler missed) over the PRG loaded at run time |
| `cyc_native_hook_sites` (id, address, length and CRC-32 of each `[[mod_function_hook]]`) | Written from `config/game.toml` by `tools/hook_sites.py` when CMake configures; the same 111 rows. The CRCs are checked against the loaded ROM before a site fires (`src/native/native_hooks.c`) |
| `cyc_native_prg_hash` in the save-state header | `native_prg_hash()`: FNV-1a of the loaded PRG, the same value, in both builds, so states move between them |
| `DUCKTALES_ROM_SHA256`, the identity the import checks | `DUCKTALES_ROM_SHA256_EXPECTED` from `CMakeLists.txt` |

What was already the player's ROM at run time stays so: the decompiled code reads level data,
sound streams, messages and tables through `Game_ReadRom()`; the tiles are CHR RAM, written by the
program from PRG; the mods' `prg_sha256` is hashed from the running PRG and their tile hashes from
CHR RAM. The 6502 timing table, the APU's tables and the power-on palette are the hardware's.

## Building

```powershell
cmake -S . -B build/romless-windows -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_ROMLESS=ON
cmake --build build/romless-windows --config Release --parallel
cmake -S . -B build/romless-headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_ROMLESS=ON -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON
cmake --build build/romless-headless --config Release --parallel
```

The window build gives `ducktales.exe` (the native player) and its package; the headless one
`ducktales_native` and the unit tests. The option refuses `DUCKTALES_PLAYER=cycle` and
`DUCKTALES_ORACLE`.

## Verification

- `tests/romless_test.py --build build/headless --romless build/romless-headless --golden <golden>`:
  on every route, `ducktales_native` from both builds in Original mode (`--lag faithful`), No
  slowdown (`--lag none`) and Decompiled code (`--decomp replace`, fed the ROM build's latches):
  RAM, cycles and registers every frame, the logic steps, the latches and each routine's `decomp:`
  line must be byte-equal; then `tests/quick_verify.py --runtime native` on the build with no ROM
  against the golden frames. It is the proof that the instruction templates play as the recompiled
  code does on the native runtime.
- `tools/rom_scan.py`: the leak scan. At every byte offset of each file, and of each file in a zip,
  it looks for a 32-byte run of the PRG with at least 8 distinct byte values. With `--rom` every run
  of the PRG is looked for (a copy of 32 bytes or more is found); with `--windows
  tests/rom_windows.txt` the BLAKE2b-64 digests of the runs at every 16th offset are (47 bytes or
  more), which needs no ROM and is what the release workflow runs. `tests/package_test.py` scans
  the package with `--rom`. A planted copy is found: scanning the ROM file itself reports runs.
- `tests/window_smoke.py --build build/romless-windows` and `tests/package_test.py --build
  build/romless-windows`: the first-run import, the menu and the package, as for the ROM build.

The instruction templates cost the native player about a fifth more time per frame than the
recompiled code: 0.507 ms against 0.439 ms in Original mode over the 13 routes (88,826 frames,
`romless_test`, two runs at once), so about 1,970 frames a second headless. A frame lasts 16.7 ms.
