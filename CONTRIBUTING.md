# Contributing to DuckTales Recompiled

DuckTales Recompiled is a static recompilation of one specific ROM. Every change has to keep the port
identical to the console when its options are at their original settings, and that is checked frame
by frame. These notes describe how to set up, what a change must satisfy, and how to submit it.

## Setup

Follow the build steps in the [README](README.md). For development you need three build trees:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64
cmake -S . -B build/headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON
cmake -S . -B build/headless-dev -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_HEADLESS=ON -DDUCKTALES_DEV_DIAGNOSTICS=ON
cmake --build build/<tree> --config Release --parallel
```

A clone made before formae-ui became a submodule gets it with
`git submodule update --init external/formae-ui`.

`build/windows` is the game as players run it, `build/headless` adds the TriCNES reference machine for
differential tests, and `build/headless-dev` adds the typed state log and scheduled state changes the
enhancement and sprite tests use.

## Ground rules

- **No game data.** Never commit the ROM, generated C, captured frames, frame logs, recordings or
  anything else derived from the ROM. `roms/`, `build*/` and `captures/` are ignored for that reason.
- **NESRecomp is not modified.** `external/nesrecomp` is a pinned submodule. The port attaches through
  its public extension points (hooks, host extras, the render compositor, settings). If something
  can only be done inside the runtime, raise it upstream instead of patching the submodule.
- **Original behaviour by default.** Every gameplay change is an enhancement that is off by default.
  When it is off, the original code runs unchanged. Presentation options may change what the window
  shows, but never the machine state, the machine picture or any verified hash.
- **The menu library is shared.** `external/formae-ui` is a pinned submodule, used by Formae's other
  ports too. `app_ui.cpp` keeps only DuckTales' pages, settings and texts; a change to the look,
  a widget or the navigation goes to [formae-ui](https://github.com/formae-group/formae-ui) first, then the
  submodule is moved to its new tag.
- **Windows only.** The supported target is Windows x64 with Visual Studio 2022.

## Where code goes

| Path | Contents |
|---|---|
| `src/game/` | Revision-specific symbols, typed game state, enhancement sites and hooks (C) |
| `src/host/` | Host integration: options, sprite compositor, diagnostics, entry point (C) |
| `src/ui/` | Menu, ROM import, settings and presentation; `app_ui.cpp` is the only C++ file |
| `external/formae-ui` | The menu library (look, shell, widgets, About and first-run layouts); its own repository |
| `src/native/` | Native runtime prototype (`DUCKTALES_NATIVE`) |
| `config/` | Recompiler configuration (`game.toml`) and the recorded entry points (`ducktales.seeds`) |
| `tests/` | Differential, presentation, enhancement and window tests; `tests/routes/` holds input schedules |
| `tools/` | Reverse-engineering and analysis scripts |
| `docs/` | Design notes, reverse-engineering references and validation results |

## Changing the game

Game behaviour changes go through [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#adding-a-behaviour-change):

1. Identify the routine and document it in [docs/FUNCTIONS.md](docs/FUNCTIONS.md) and the RAM it uses
   in [docs/MEMORY-MAP.md](docs/MEMORY-MAP.md).
2. Name the address in `src/game/enhancements_sites.h` and declare a `[[mod_function_hook]]` for it
   in `config/game.toml` with a content key (length and CRC-32 of the original bytes), so the hook
   refuses to run on unexpected code. Compute the CRC from your own ROM and never commit the bytes.
3. Implement the enhancement in `src/game/enhancements.c` with a `config.ini` key and a command-line
   option, off by default, and expose it in the menu.
4. Add a case to `tests/enhancements_test.py` that shows the effect when on and identity with the
   original when off.

## Verification

Run the checks that cover what you touched before submitting:

| Change | Required checks |
|---|---|
| Game code, seeds, native runtime (every commit) | `py -3 -I tests/quick_verify.py --build build/headless` |
| Hooks, host, runtime integration (every commit) | `py -3 -I tests/quick_verify.py --build build/headless --runtime cycle` |
| Decompiled routines | `py -3 -I tests/decomp_test.py --build build/headless --dev-build build/headless-dev` |
| Nightly, a release | `py -3 -I tests/verify_routes.py --build build/headless` |
| Enhancements | `py -3 -I tests/enhancements_test.py --build build/headless --dev-build build/headless-dev` |
| Sprite presentation | `py -3 -I tests/sprites_test.py --build build/headless --dev-build build/headless-dev` |
| Menu, settings, ROM import, window | `py -3 -I tests/window_smoke.py --build build/windows` |
| ROM identity and import | `build\headless\Release\app_rom_test.exe` |
| Native player sound output | `py -3 -I tests/audio_output_test.py --build build/windows` |
| Native runtime, build with no ROM, a release | `py -3 -I tests/romless_test.py --build build/headless --romless build/romless-headless` |

Checks come in two tiers ([docs/VALIDATION.md](docs/VALIDATION.md#fast-verification)):

- **Every commit**, `tests/quick_verify.py` runs the routes at alignment 0 and compares every
  frame with golden hashes recorded from a verified build, stopping at the first difference and
  printing the fields that differ. `--changed 6:8B00-8BFF` or `--routes-for C10B` runs only the
  routes that execute that code (16 KiB bank and address; `$C000-$FFFF` needs no bank), and
  `--from-state auto --span 600` starts them from a save state just before it. The native runtime
  (the default, about 11 s for all 12 routes with `--jobs 4`) runs the generated code on its own bus; it does not run
  the host's hooks and enhancements, so check those with `--runtime cycle` (about 3.5 min).
  `tests/decomp_test.py` takes the same `--changed` ranges, with `--from-state` and `--span`, to
  check one decompiled routine from the golden states; the full run is the gate before a merge.
  `tools/test_slot.py -- <command>` queues heavy runs so that at most three run at once.
- **Nightly and before a release**, not per branch per change,
  `verify_routes.py` must report native, interpreter and oracle equality on every frame of every
  route at all four alignments, with 100% of CPU cycles in recompiled code (3 h 18 min with `--jobs 3` and no oracle cache). The oracle's
  results are cached in `build/verify-cache/`, and `--no-cache` discards them.

The golden files are recorded with `py -3 -I tests/quick_verify.py --record --build build/headless`
right after a full `verify_routes.py` pass on that build, into `build/golden/<ROM SHA-256>/`. They are
derived from the ROM: never commit them. They are keyed on the ROM, each route file and its frame
count; quick_verify refuses a stale one and asks for a new recording. Record again after a NESRecomp
update too: its bus trace and hardware hashes are part of the golden line.

A new route that reaches code no route has executed adds interpreter misses. Append them to
`config/ducktales.seeds` with `--miss-log`, rebuild and rerun the route; see the README.

CI ([GitHub Actions](.github/workflows/ci.yml)) runs on every push to `main` and every pull request,
without the ROM: `tools/format.py --check` with the pinned clang-format and Black, and a byte-compile
of `tests/` and `tools/`. Everything that needs the game (the table above) runs on your machine.

Record new verified results in [docs/VALIDATION.md](docs/VALIDATION.md), and list anything you did
not verify in its *Not verified* section.

## Style

- C for `src/game`, `src/host`, `src/native` and `src/ui`; C++ only in `src/ui/app_ui.cpp`.
- Four-space indentation, CRLF line endings, 120 columns, braces on every `if` and loop, no
  one-line functions or statements. `.clang-format` describes the C and C++ layout and
  `[tool.black]` in `pyproject.toml` the Python one; `py -3 tools/format.py` applies both and
  `py -3 tools/format.py --check` must list nothing before a commit. The pinned tools are
  `py -3 -m pip install --user clang-format==23.1.5 black==26.10.1`. `external/` is never formatted.
- Each file opens with a header comment that says what it is for and which settings or options it
  owns. Do not add comments inside function bodies.
- Use the existing prefixes: `dt_` / `Dt` for game code, `app_` for the application layer.
- Python tools and tests run with `py -3 -I` and use only the standard library unless the script
  says otherwise.

## Commits and pull requests

- One logical change per commit. Write the subject as a short imperative sentence in sentence case,
  without a prefix or trailing period: *Cache the oracle's route hashes in verify_routes*.
- Put the reason and the evidence (routes run, frames, results) in the body or the pull request.
- Work on a branch and open a pull request against `main`. Update the README, the relevant `docs/`
  page and [CHANGELOG.md](CHANGELOG.md) in the same pull request.
- A pull request is accepted when CI and the checks above pass and the change keeps the original behaviour at
  default settings.

## Releasing

A release is a ZIP for players, built by GitHub Actions from a tag `v*` on `main`
(`.github/workflows/release.yml`) with no ROM: the build with no ROM
([docs/ROMLESS.md](docs/ROMLESS.md)), the native player and the decompiled code, each player
importing their own ROM at first run. Its version is `git describe --tags --always --dirty` when
CMake configures (the same string the About page shows), so the tag.

1. Run the checks in [Verification](#verification) on the release commit, with the build with no ROM
   against the ROM build:

   ```powershell
   cmake -S . -B build/romless-headless -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_ROMLESS=ON -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON
   cmake --build build/romless-headless --config Release --parallel
   py -3 -I tests/romless_test.py --build build/headless --romless build/romless-headless --golden build/golden
   cmake -S . -B build/romless-windows -G "Visual Studio 17 2022" -A x64 -DDUCKTALES_ROMLESS=ON
   cmake --build build/romless-windows --config Release --parallel
   py -3 -I tests/window_smoke.py --build build/romless-windows
   py -3 -I tests/package_test.py --build build/romless-windows
   ```

2. Tag it and push the tag: `git tag -a v1.0.0 -m "DuckTales Recompiled 1.0.0"`,
   `git push origin v1.0.0`. The workflow checks the formatting, builds both builds with no ROM,
   runs the unit tests, checks that the player refuses a file that is not the ROM, packs the ZIP,
   scans the player, `SDL2.dll` and the ZIP for any run of the ROM (`tools/rom_scan.py --windows
   tests/rom_windows.txt`) and publishes `DuckTalesRecompiled-v1.0.0-win64.zip` as the release,
   free of charge (NESRecomp's licence, see below). Put the version's `CHANGELOG.md` section in the
   release's notes.

The package holds `ducktales.exe`, the native player (static MSVC runtime, so players need no Visual
C++ redistributable), `SDL2.dll`, the player `README.txt` (from `packaging/README.txt.in`),
`LICENSE.txt` and the third-party licences under `licenses/`; the install rules at the end of
`CMakeLists.txt` list them and [docs/LICENSING.md](docs/LICENSING.md) says why each is there. The cycle
host, `ducktales_cycle.exe`, is a maintainer's build and is not in it. It never contains the ROM,
generated sources or test tools; `tests/package_test.py` checks the file list and scans the archive.

## Licensing

Contributions are accepted under the MIT license in [LICENSE](LICENSE). Builds link NESRecomp, which
is licensed for noncommercial use only; see [docs/LICENSING.md](docs/LICENSING.md).
