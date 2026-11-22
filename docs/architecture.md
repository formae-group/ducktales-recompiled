# Architecture: the port's systems layer

The port's own code sits on top of NESRecomp without changing it. The 6502 program is recompiled
from the user's ROM, NESRecomp's cycle host runs it, and the game-specific layer in `src/` reads the
machine's state and, when a feature asks for it, changes behaviour at specific instructions.

```
src/game/ducktales_symbols.h         the only file that names revision-specific addresses
src/game/ducktales_state.[ch]        typed state decoded from a CPU RAM image (pure functions)
src/game/ducktales_jump.[ch]         jump-height experiment: one hook site, off by default
src/game/enhancements.[ch]           the optional enhancements: one table, one hook site each
src/game/enhancements_sites.h        their code sites (to move into ducktales_symbols.h)
src/host/ducktales_host.c            cyc_host_extras(): settings, options, per-frame callbacks
src/host/ducktales_sprites.[ch]      presentation: every sprite, fixed order, HUD kept (on by default)
src/host/ducktales_main.c            main() of the window build: ROM choice, then the host
src/ui/                              the application: first run, menu, live settings
src/host/ducktales_diagnostics.[ch]  developer diagnostics (DUCKTALES_DEV_DIAGNOSTICS only)
tests/state_decode_test.c            unit test of the decoder (built with the headless build)
tests/jump_experiment.py             measures the experiment and checks that it switches off cleanly
tests/sprites_test.py                the sprite presentation against the machine's picture
```

`CMakeLists.txt` passes the `.c` files to `nesrecomp_add_cycle_game(... HOST_EXTRAS ...)`, so the
windowed and headless builds both link the same layer. The TriCNES oracle does not link it.

## Mechanism

NESRecomp offers several ways for a game to work with the running machine
(`external/nesrecomp/runner/cyc/cyc_host_extras.h`, `cyc_hooks.h`, `cyc_mod.h`). The port uses the
two smallest ones that cover what it needs:

| Need | Mechanism | When disabled |
|---|---|---|
| Read game state every frame | `CycHostExtras.frame_end` plus `cyc_cpu_ram()` / `cyc_mod_peek` | Reads only. Nothing is written, and no bus access or clock is involved. |
| Change behaviour at one instruction | A `config/game.toml` `[[mod_function_hook]]` site with a content key, plus a plugin registered with `nes_mod_register_function_entry_plugin` | The generated code tests one global flag (`cyc_hooks_armed`) at that instruction. While no hook is enabled, the instruction runs as compiled, so registers, flags, memory and cycle count are unchanged. |

When a hook is enabled, compiled code returns to the scheduler at the site. The scheduler runs the
callback at the instruction boundary, before the opcode fetch, and then runs the original
instruction. The callback uses no guest time. It can read and write RAM through `cyc_mod_poke`,
which goes through the compiled-code write watch like a CPU store, and it can read and write
registers. Returning 0 keeps the original instruction. Recompiled code, RAM views and the
interpreter all fire the site in the same way (`cyc_hooks.h`). The content key (`length` + `crc32`
of the bytes at the site) means a different revision's code at that address never fires the hook.
The repository stores only the checksum, never ROM bytes.

Alternatives that were considered:

- `cyc_mod_set_ram_read_hook` changes the value one instruction reads from RAM. It is a single
  global function pointer keyed only by PC, so a different bank at the same address would also
  match. While it is set, the CPU takes its slow path for every RAM operand (`cpu6502.c`). It also
  cannot change stores or registers. For the jump experiment it would give the same result as the
  hook site, because the game stores the read value straight into the velocity. Keep it in reserve
  for cases where a read is the only thing to change.
- `cyc_mod_set_prg_read_override` (Game Genie style) changes an immediate operand or a data byte in
  ROM. The jump's take-off value is not an immediate where the jump starts. Changing the land-entry
  immediate instead would only take effect at the next land entry.
- `[[replace_func]]` works at compile time, so there is no way to switch the original code back on.

## Typed state (`src/game/ducktales_state.h`)

`dt_state_decode(ram, &state)` turns a 2 KiB CPU RAM image into a `DtState`:

| Field | Source | Status |
|---|---|---|
| `difficulty` | `$F9`: 0 Easy, 1 Normal, 2 Difficult, anything else `DT_DIFFICULTY_UNKNOWN` | verified (memory-map) |
| `money`, `money_valid` | `$0324-$0329`: digit tiles, most significant first, with `$24` (blank) read as 0. The result is multiplied by 10 for the HUD's fixed trailing 0. `money_valid` is false if any other tile appears. | verified (memory-map) |
| `hp`, `hp_max` | HUD records at `$0340+4i` up to the first record with Y = `$FF`. `hp` counts the records whose attribute bit 0 is clear. | verified (memory-map); `hp_max` is 3 after a Normal start in `re_amazon_moves` (frames 1650-2128) |
| `lives` | `$0361` | verified (memory-map) |
| `scrooge_x`, `scrooge_y` | `$0650`, `$0640` (slot 0) | verified (memory-map) |
| `scrooge_vy` | `$07C0` (whole, signed) and `$07B0` (fraction) of slot 0, 8.8 fixed point, up positive | verified (below) |
| `pad_held`, `pad_new` | `$16`, `$14` | verified (memory-map) |

The functions take a RAM pointer and nothing else, so the same code works in `frame_end`, in a hook
callback, in a TCP command and in a unit test. The values only mean something during gameplay. On
the title screen and Land Select the HUD areas still hold whatever was last written there.

## Jump experiment (`src/game/ducktales_jump.c`)

The evidence comes from controlled runs of `tests/routes/re_amazon_moves.txt`, where A is newly
pressed at frame 1780 while Scrooge stands at Y `$B4` in The Amazon. It was gathered with
`tools/whoaccesses.py`, `tools/framelog.py` and a search of the ROM for every reference to the two
addresses.

- The take-off velocity is kept in RAM: `$07A0` (whole) and `$0790` (fraction), `$04BB` in 8.8.
  The land-entry code in the fixed bank stores it from constants at `$D815`/`$D81A`. No other code
  stores to these addresses, and the jump start is the only code that reads them.
- The jump start (`$E3EC`, fixed bank) runs when the A bit of `$14` is set. It copies `$0790` to
  `$07B0` and `$07A0` to `$07C0` (slot 0's velocity), with the stores at `$E3F5` and `$E3FB`, and
  then continues at `$E3FE`. Nothing jumps or branches to `$E3FE`, so it is reached only by falling
  through from those stores.
- Every frame, `$F2B5` subtracts the gravity in `$EF` from the velocity, and `$F2A1` subtracts the
  velocity from Y (`$0660` fraction, `$0640` whole). For Scrooge the gravity is `$33`, loaded at
  `$E354`. The traces of frames 1780 and 1781 show these reads and writes in that order.
- The pogo bounce sets its own velocity elsewhere (`$07C0` = 5 at frame 1827). The experiment does
  not touch it.

The hook site is `$E3FE` (`config/game.toml`, id `ducktales.jump-scale`). The callback reads slot 0's
velocity, multiplies it by `JumpScale`/100 (clamped to `$7FFF`; a negative value is left alone) and
writes it back. After that the game's own physics runs the jump unchanged.

The scale is set with `config.ini` `[Game] JumpScale = <percent>` in the window, or
`--jump-scale <percent>` in both hosts. The range is 25-400 and the default is 100. At 100 the hook is
disabled, so the machine runs exactly as the stock game.

Measured with `py -3 -I tests/jump_experiment.py --build build/headless --dev-build build/headless-dev`:

| Run | Velocity after the take-off frame | Apex Y (frame) | Rise |
|---|---|---|---|
| stock / `--jump-scale 100` | `$0488` | 121 (1803) | 59 px |
| `--jump-scale 125` | `$05B6` | 89 (1809) | 91 px |

The same test also showed:

- `--jump-scale 100` produces per-frame hashes identical to stock (trace, memory, picture,
  registers, cycles and hardware).
- With `--jump-scale 125`, frame 1780 (the jump frame) is the first frame that differs.
- Turning the experiment on at frame 0 and off at frame 1779, before the jump, gives hashes
  identical to stock on every frame.
- Turning it on only at frame 1779 gives exactly the `--jump-scale 125` run.

So switching the experiment on and off changes nothing except the velocity of a jump that starts
while it is on.

With the layer linked and the experiment off (the default), `tests/verify_routes.py --build
build/headless` passes all seven routes at all four alignments: native, interpreter and oracle
match on every frame, with 100.0% native cycles (2026-11-21).

## Enhancements (`src/game/enhancements.[ch]`)

Five optional changes, all off by default. `enhancements.c` holds one table that gives each its
`config.ini` `[Game]` key, its command-line option, its menu label, its range and its stock value.
The host (load, save, usage), the menu and the tests iterate that table. Every enhancement except
the jump owns one `[[mod_function_hook]]` site (`config/game.toml`, addresses in
`src/game/enhancements_sites.h`). Its hook is enabled only while its value differs from stock.
So with an enhancement off, the generated code runs the original instruction, as described under
Mechanism. All sites are in the fixed bank 7, so the bank mapped at `$8000` never matters.

| Key / option | Values | Site | What the callback does |
|---|---|---|---|
| `JumpScale` / `--jump-scale` | 25-400, 100 = stock | `$E3FE` | See "Jump experiment" above. |
| `NoDamage` / `--no-damage` | on/off | `$EB5E`, entry of `damage` | Returns as the routine's RTS would, so no HP dot is marked and the Easy hit counter `$5F` is left alone. The knock-back is done by the callers and still happens. |
| `InfiniteTime` / `--infinite-time` | on/off | `$E2A6`, in `timer_tick` (`$E28C`) | Before the decrement of `$B0` (frames to the next time tick), sets `$B0` back to its reload value `$3C`, so it never reaches 0 and the HUD time never drops. |
| `InfiniteLives` / `--infinite-lives` | on/off | `$DCB0`, in `death_or_warp` (`$DC3E`) | Before the decrement of `$0361`, adds one, so the count stays the same. |
| `StartLives` / `--start-lives` | 0-9 or off (stock 2) | `$D59C`, title set-up | After the copy of the HUD template sets `$0361` to 2, writes the chosen count. It takes effect from the next new game (power-on, reset, or the restart after a game over). |

Evidence for the sites (controlled runs with `tools/whoaccesses.py`, `tools/framelog.py` and a
search of the ROM for every reference to the address; status: verified unless marked):

- `$EB5E` is reached only by JSR, from `$EAD0` and `$EB22`, and the instruction before it is a
  JMP, so no code falls into it. Stock `moon_intro` loses HP dots at frames 1004, 1075 and 1336,
  and with the hook on, frame 1004 is the first frame that differs.
- `$E2A6` is the decrement of `$B0` in `timer_tick`, after the time-out check. The timer reloads
  `$B0` with `$3C` (60 frames) when the HUD digits (`$0151/$0155/$0159`) count down. In
  `amazon_intro` the timer starts at about frame 896, which is also the first frame that differs
  with the hook on.
- `$DCB0` is the only instruction in the ROM that decrements `$0361`. In `moon_intro` stock
  lives go 2 -> 1 at frame 1592 and 1 -> 0 at frame 2731. A death with 0 lives goes to the game
  over instead (frame 3609).
- `$D59C` is the first instruction after the loop at `$D591-$D59A` that copies the 13-byte HUD
  template into `$0358-$0364`. `whoaccesses` shows that loop writing `$0361 = 2` at frame 44. The
  game over restarts through the same set-up: `moon_intro` has lives 2 again at frame 3648.

Measured with `py -3 -I tests/enhancements_test.py --build build/headless --dev-build
build/headless-dev` (2026-11-21, all checks pass):

| Enhancement | Off | On | On, then off before it first acts |
|---|---|---|---|
| No damage | identical to stock, frames 0-1300 of `moon_intro` | HP at frames 1010/1300: stock 2/1, on 3/3; first differing frame 1004 | identical to stock (off at 1000) |
| Infinite lives | identical, frames 0-1600 | lives at 1600: stock 1, on 2; first differing frame 1592 | identical (off at 1500) |
| Infinite time | identical, frames 0-3000 of `amazon_intro` | HUD time at 3000: stock 482, on 500; first differing frame 896 | identical (off at 600) |
| Extra lives at start | identical, frames 0-1600 | `--start-lives 7`: lives 7 at frame 100 (stock 2), first differing frame 44; `--start-lives 0`: 0 | identical (off at 30) |

"Identical" means the per-frame hashes of trace, memory, picture, registers, cycles and hardware
all match. A route only follows its recorded inputs while the game behaves as recorded, so a run
with an enhancement on drifts from the route after the first difference. For example
`--start-lives 7` has 4 lives at frame 3700 of `moon_intro` while stock has 2: the three
deaths of the route cost 3 of the 7 lives, and stock had a game over and a new game with 2.

With every enhancement off, `tests/verify_routes.py --build build/headless` passes all seven
routes, as before.

### Sprite presentation (`src/host/ducktales_sprites.c`)

Not an enhancement: nothing in the machine changes, so it has no hook site and is not in the table.
It is a `cyc_render.h` stock-width compositor that paints the picture the window presents (and
headless `--present-out`), from the exact background capture and a sprite list rebuilt from the RAM the
frame's OAM was built from, without the per-line or OAM limits and in a fixed order; the HUD stays up
while Scrooge is in the top band. The design and its evidence are in
[rendering.md](rendering.md#sprites-without-limits-presentation). The host saves it as `[Game]
NoSpriteFlicker` (`ducktales_host_saved_sprites`), `--no-sprite-flicker on|off` overrides it for one run,
and the menu's Settings page switches it live; on by default. `frame_end` saves the RAM of the last two
frames, `power_on` and `state_loaded` forget them. Off turns the background capture and stock
compositing off, so the window presents the machine's own picture. With the feature on and off the
per-frame hashes of all five `full_*` routes are identical, and `tests/sprites_test.py` checks the
pictures. In a `DUCKTALES_DEV_DIAGNOSTICS` build, `--dt-sprite-log FILE` composes every frame and logs
how (rebuilt, kept, fallback) with the sprites drawn, lines over 8 and HUD restored; the exit report
gives the totals. The menu's dimmed background is the presented picture.

### Reset

The menu's Reset calls `dt_soft_reset()` between frames. Raising the CPU's reset line
(`cpu.do_reset`) there is not enough: a frame starts at VBlank, and the NMI that arrives during
the 7-cycle reset sequence takes over its vector. `cpu6502.c` models that hijack, and a trace of a
reset at `amazon_intro` frame 1200 read `$FFFA` instead of `$FFFC`. The console's reset button
also resets the PPU, so no NMI follows it on hardware. So the function puts the CPU where the reset
sequence leaves it: S lowered by 3, I set, PC from `$FFFC/$FFFD`, pending NMI, IRQ and reset
dropped. The game's reset handler re-initialises the PPU, the APU and RAM itself. Tested: after a
reset at frame 1200 the title music is requested (`$C5 = 7`) from frame 1217 and lives are 2 by
frame 1257. `--start-lives 7` gives 7 lives after the reset. The PPU and APU registers are not
reset by the host. That is a difference from hardware, and it is harmless only because the handler
writes them itself (hypothesis for any state the handler does not write).

## Application (window build)

### Why C++ Dear ImGui

NESRecomp's cycle window (`cyc_sdl.c`) runs the game. Its launcher, in-game menu, pause and
settings screens (`cyc_ui.h`, `cyc_ui_menu.c`, `cyc_ui_imgui.cpp`, `cyc_overlay.c`) are compiled
only into builds that link NESRecomp's recomp-ui, and this port does not link it. So the window has
no menu and no pause, and its `CycSettings` are private to `cyc_sdl.c`. These options were
considered:

- Link recomp-ui. It brings NESRecomp's own look and screens, and the application should have its own
  look, not NESRecomp's.
- Draw with `cyc_overlay`. It draws simple text and boxes over the picture. It has no layout,
  widgets, focus handling or gamepad navigation, so every one of those would have to be written
  here.
- Rust with dear-imgui-rs. It adds a second toolchain to the CMake/MSVC build and an FFI layer
  around SDL handles that the C host owns, and it gives nothing a C++ build cannot.
- C++ Dear ImGui (chosen). It is pinned as the submodule `external/imgui` (v1.91.9b) and uses
  the official `imgui_impl_sdl2` and `imgui_impl_sdlrenderer2` backends. It draws with the
  host's own `SDL_Renderer`, so there is no second graphics context. Only the window target
  compiles it. The headless and oracle builds do not.

### How it attaches to the host

```
src/host/ducktales_main.c   the real main(): chooses the ROM, then calls the host
src/ui/app_rom.[ch]      ROM check, import and the data folder (UTF-8 paths, _w* calls)
src/ui/app_settings.[ch] config.ini copy, live settings, bindings, menu shortcut, menu settings
src/ui/app_present.[ch]  the picture the window shows: HUD split, overscan, scaling, aspect, pacing
src/ui/app_ui.cpp        Dear ImGui: the first-run screen and the menu
assets/brand/formae.png  the About page's logo, embedded (app_about.c, generated by CMake)
```

- For the `ducktales` target only, CMake compiles NESRecomp's `cyc_host.c` with
  `main=ducktales_host_main`. `ducktales_main.c` provides `main`, chooses the ROM (next
  section) and then calls `ducktales_host_main` with the same arguments. Headless options skip
  all of this.
- `ducktales_host.c` connects the UI through the host's `CycHostExtras` callbacks:
  `frame_begin`, `event`, `input`, `audio_mix`, and the `[Game]` section writer.
- The menu opens from the `event` callback, on Escape or on the controller's Menu binding (RB by
  default). It runs its own modal loop inside that callback. It pumps SDL events and draws over the
  last game picture, so no machine frame runs while it is open, and the pause is exact.
- The menu's layout follows Ship of Harkinian's: a bar of tabs (Settings, Enhancements, About) with
  Reset, Quit and Resume icon buttons on its right, and under it a sidebar of the tab's categories
  beside the chosen category's page. Settings has General (menu background opacity, controller
  navigation), Graphics, Audio, Controls and Gameplay (No slowdown); Enhancements has Gameplay (the
  `DtEnhInfo` table, iterated) and Restore defaults. Pages are titled separators and ImGui's own
  checkboxes, sliders with -/+ buttons, combos and buttons; there are no tooltips or help lines.
  Reset and Quit ask in a modal first.
- Navigation is ImGui's: `NavEnableKeyboard` always, `NavEnableGamepad` while
  `[Game] MenuControllerNav` is on. The sidebar and the page are `NavFlattened` children, so
  the arrows and the D-pad cross between the bar, the sidebar and the page; moving onto a tab or a
  category chooses it. Escape and B close the menu unless a popup is open or an item is active
  (then ImGui gets them: a combo closes, a confirmation cancels). The menu binding (RB) always
  closes it.
- During a capture on the Controls page the raw SDL events go to the binding, not to ImGui: Escape
  (or B for a keyboard cell) cancels, Delete or Backspace clears, a click elsewhere cancels. The menu
  stays disabled until Enter, Space and A are released, so the button that was bound does not start
  another capture.
- Sizes follow the drawable: the base size is a 42nd of its height, at least 15 px at 96 DPI times
  the display's scale and at most a 20th of the height. Fonts (Segoe UI and Segoe UI Semibold) and
  the style are built again when it changes. The menu's background is the game picture under a
  dark layer of `MenuOpacity` percent.
- The About tab shows the version (`git describe --tags --always --dirty` when CMake configures),
  the configure date, "Developed by Formae" with the logo, the credits and licences, and that the
  project is not affiliated with Disney or Capcom. The logo is `assets/brand/formae.png` (the mark
  and wordmark in the menu's text colour on a transparent background). CMake turns it into a byte
  array in `app_about.c`, so the program needs no loose file; `stb_image` from NESRecomp's
  `runner/src` (header only, `STB_IMAGE_STATIC`) decodes it, and it is area-averaged to the
  size drawn.
- Settings are applied live through what the window exposes. `app_settings.c` keeps its own copy of
  `config.ini`, read and written with the host's `cyc_settings_load`/`cyc_settings_save`, so the
  file format stays the host's:
  - `[Display] Fullscreen` and `WindowScale` go through SDL.
  - `IntegerScale` is applied again every frame, because the host resets it on a resize or F11.
  - `[Audio] Volume` is applied in `audio_mix` as the ratio to the starting volume.
  - The bindings go through the `input` callback with `cyc_input_eval`.
  - `[Game] FramePacing` applies at the next launch: before the host reads `config.ini`,
    `app_ui_configure` writes the `[Display] VSync` it asks for, because the host reads VSync only
    at start.
  - Pixels, pixel aspect, overscan crop and the HUD split line act on the picture the window
    shows (`CycHostExtras.present`), never on the machine: [presentation.md](presentation.md).
  - `[Game] MenuOpacity` and `MenuControllerNav` are the menu's own; `ducktales_host.c` hands
    them to `app_ui_load_game_setting`/`app_ui_write_game_settings` with the other `[Game]` keys.
  - `[Game] NoSlowdown` (the No slowdown row, on by default) sets the cycle host's extra
    scanlines between frames (`ducktales_host_set_no_slowdown`); see
    [native-runtime.md](native-runtime.md#the-same-thing-on-the-cycle-host-without-changing-nesrecomp).
- The host also writes `config.ini`, on F11 and at exit, from its own stale copy. Its save calls
  the `[Game]` writer (`app_ui_host_saved`). On the next frame, or after the host exits, the
  file is written again from the menu's copy, keeping only the host's fullscreen value. This runs
  only after the player changed something in the menu. The FDS Disk shortcut stays off for a
  cartridge.
- The Controls category remaps the player 1 keyboard and controller bindings
  (`[Keyboard.Player1]`/`[Gamepad.Player1]`) and the Menu shortcut
  (`[Keyboard.Shortcuts]`/`[Gamepad.Shortcuts]`).

### First run and ROM import

The ROM is chosen in this order:

1. A ROM path on the command line, used as given. The host checks it as before.
2. The copy imported earlier, at `%APPDATA%\DuckTales Recompiled\DuckTales (USA).nes` (or in
   `DUCKTALES_DATA_DIR`). It is checked against the SHA-256 again at every start.
3. Otherwise the first-run screen. The player chooses the file, or drops it on the window. The file
   is imported only if its whole SHA-256 matches `DUCKTALES_ROM_SHA256`, which CMake takes from
   the ROM the build was made with. Otherwise the screen shows why it was refused, with the file's
   hash. The copy is written to a temporary file and then renamed.
4. `--import-rom FILE` does the same with no window (exit 0 or 1). A `--hidden` start with no ROM
   exits 2 with a message, instead of opening a window nobody can see.

The data folder path is UTF-8 internally and uses the wide Windows calls. The host receives its
8.3/ANSI form, because NESRecomp opens the ROM with `fopen`.

### Self-test and checks

`DUCKTALES_UI_SELFTEST=<folder>` (a Windows path) runs the UI without input. The menu opens by itself
at frame 120, visits every tab and category (a capture, the Reset and Quit confirmations, and
Graphics and About drawn off screen at 1280x720 and 3840x2160 included) and writes a PNG of each. The
first-run screen, in the menu's colours and fonts, writes its two screens, and `DUCKTALES_UI_SELFTEST_PICK=<file>` sets the file it picks.
`py -3 -I tests/window_smoke.py --build build/windows` uses this to check:

- a hidden start and exit;
- the refusal of a modified ROM and of a file that is not a ROM;
- the import, and a start from the imported copy;
- `[Game]` persistence, the menu's own keys included;
- both selftests.

`app_rom_test` (headless build) checks the ROM import code.

## Developer diagnostics

`-DDUCKTALES_DEV_DIAGNOSTICS=ON` (default OFF) compiles `src/host/ducktales_diagnostics.c`. Without
it the host links empty inline stubs and lists none of these options.

- `--dt-state-log FILE|-` and `--dt-state-every N` write one line of typed state per frame, for
  example `frame=1780 diff=Normal money=0 hp=3/3 lives=2 x=68 y=174 vy=+5.711 held=80 new=80 jump=125`.
- `--dt-jump-at FRAME:PERCENT` (repeatable) changes the jump scale before that frame.
- At exit the log reports the jump scale, how many jumps were rescaled and how many times the hook
  fired.
- `--dt-enh-at FRAME:KEY=VALUE` (repeatable) changes an enhancement before that frame, for example
  `--dt-enh-at 1000:NoDamage=off`. `--dt-reset-at FRAME` presses Reset before that frame.
- At exit the log reports, for each enhancement, its value and how many times it changed the game.
- `--dt-sprite-log FILE|-` composes the presented picture every frame and writes one line per frame from
  the sprite compositor; at exit the log reports its totals.
- In the window, with `--tcp PORT`: `dt_state` returns the typed state as JSON, and `dt_jump_scale`
  returns the scale or sets it with `{"value":N}`.

## Adding a behaviour change

1. Find the instruction with a controlled run and record the evidence in `docs/`.
2. Add its address to `ducktales_symbols.h`.
3. Add a `[[mod_function_hook]]` with an `id` and a `length`/`crc32` key of the bytes at the site.
   Compute the CRC locally from your ROM and never commit the bytes.
4. Register a callback for that id with `NES_MOD_CONSTRUCTOR` and
   `nes_mod_register_function_entry_plugin`. Enable it only while its setting differs from stock.
5. With the feature off, run `tests/verify_routes.py`. Every route must still match the
   interpreter and the oracle.

## Open questions

- Nobody knows yet whether `$0790`/`$07A0` ever hold anything other than `$04BB`, for example in
  another land or on the Moon. Only the land-entry constants have been seen.
- How a scaled jump behaves with ceilings, things Scrooge can hang from and screen transitions has
  not been examined.
- Nobody has used the menu with real keyboard, mouse or controller input yet, and nobody has tried
  the file dialog, switching between windowed and fullscreen, VSync, a volume change by ear, or
  remapping. Only the self-test drove it.
- Infinite time and infinite lives have been measured in one land each. Whether every land, the
  bosses and the Moon's timer use the same code is a hypothesis.
- The window build compiles and links the layer, and `JumpScale` goes through the host's
  `load_setting`/`save_settings`. Nobody has played with it in the window yet.
