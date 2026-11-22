# Licensing audit

This repository's own files are MIT licensed (`LICENSE`). This page records why and what still constrains
distributed builds.

## Components

| Component | Where | Licence | Consequence |
|---|---|---|---|
| NESRecomp (mstan/nesrecomp @ `77ad5b8`) | `external/nesrecomp` (submodule, unmodified) | PolyForm Noncommercial 1.0.0 + `PERMISSIONS.md` | Noncommercial use, modification and distribution allowed. `PERMISSIONS.md` also allows donations, sponsorship and advertising, **only if** the software and the game built with it are free to obtain and play. Selling copies, paid access or paid content is not covered. Redistribution must include the licence text, any `Required Notice:` lines, and `PERMISSIONS.md`. |
| TriCNES-derived reference core | `external/nesrecomp/runner/cyc/tric_core.cpp` | MIT (`runner/cyc/LICENSE.TriCNES`) | Test-only oracle; keep the notice if it is ever distributed. |
| SDL2 (Windows binary bundled by NESRecomp) | `external/nesrecomp/runner/external/SDL2` | zlib | Ship its licence with any package. |
| Dear ImGui | `external/imgui` (submodule, pinned) | MIT | Used by the in-game menu; keep its notice in packages. |
| emu2413 (Mitsutaka Okazaki) | `external/nesrecomp/runner/cyc/vendor/emu2413` | MIT | Compiled into every cycle-host executable (NESRecomp's VRC7 sound), so its notice ships in packages. |
| stb_image | `external/nesrecomp/runner/src/stb_image.h` | Public domain or MIT, at the user's choice | Decodes the About page logo; taken under the public-domain option, so no notice is required. |
| MSVC runtime | static in the window build (`/MT`); NESRecomp still copies the DLLs next to the executable in the build tree | Microsoft Visual C++ terms | The release package ships no runtime DLL and needs no redistributable. |
| Other submodules of NESRecomp (`lib/recomp-net`, `lib/retcomm-rbengine`) | not checked out | not audited | Not needed by this port; audit before enabling netplay. |
| denisfox3554/Nesrecomp (MinGW fork) | not used | MIT as stated in the repository | Not a dependency. Its README credits NESRecomp, but its code is an independent, much smaller reimplementation, so the MIT grant does not relicense upstream code. |

## Release package

The `PACKAGE` target of `build/windows` (CPack; see CONTRIBUTING.md "Releasing") writes
`DuckTalesRecompiled-<version>-win64.zip`. Besides `ducktales.exe`, `SDL2.dll` and the player
`README.txt` it carries:

| File | Source | Why |
|---|---|---|
| `LICENSE.txt` | `LICENSE` | The port's own MIT licence |
| `licenses/NESRecomp-LICENSE.txt` | `external/nesrecomp/LICENSE` | PolyForm Noncommercial 1.0.0 requires the licence text with every copy |
| `licenses/NESRecomp-PERMISSIONS.md` | `external/nesrecomp/PERMISSIONS.md` | Its additional permissions travel with it. NESRecomp provides no `Required Notice:` lines (only the licence's own example) |
| `licenses/imgui-LICENSE.txt` | `external/imgui/LICENSE.txt` | MIT notice |
| `licenses/SDL2-COPYING.txt`, `licenses/SDL2-README-SDL.txt` | `external/nesrecomp/runner/external/SDL2` | zlib licence; SDL asks for its README to accompany the DLL |
| `licenses/emu2413-LICENSE.txt` | `external/nesrecomp/runner/cyc/vendor/emu2413/LICENSE` | MIT notice |

The player README repeats that the package must be free of charge. The TriCNES notice is not
included because the oracle is never packaged.

## Game material

The ROM, anything extracted from it, generated C (which embeds translated game code and data) and
captures (screenshots, WAV, frame logs) are Capcom/Disney material and stay out of Git
(`.gitignore`). Files that are tracked contain only addresses, input schedules and our own code
and notes.

## Decision

The project's own code (CMake, tools, tests, docs, `src/`) is MIT. It is compatible with every
dependency above and keeps those files reusable in future ports. It does not lift the constraint
on binaries: every build links NESRecomp, so a distributed build is bound by PolyForm
Noncommercial plus `PERMISSIONS.md` (free of charge, no paid access). Commercial distribution
would need separate permission from NESRecomp's author.
