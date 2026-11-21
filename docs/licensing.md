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
| MSVC runtime DLLs copied next to the executable | build output | Microsoft Visual C++ redistributable terms | Redistributable as part of an application. |
| Other submodules of NESRecomp (`lib/recomp-net`, `lib/retcomm-rbengine`) | not checked out | not audited | Not needed by this port; audit before enabling netplay. |
| denisfox3554/Nesrecomp (MinGW fork) | not used | MIT as stated in the repository | Not a dependency. Its README credits NESRecomp, but its code is an independent, much smaller reimplementation, so the MIT grant does not relicense upstream code. |

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
