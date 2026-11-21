# Architecture: the port's systems layer

The port's own code sits on top of NESRecomp without changing it. The 6502 program is recompiled
from the user's ROM, NESRecomp's cycle host runs it, and the game-specific layer in `src/` reads the
machine's state and, when a feature asks for it, changes behaviour at specific instructions.

```
src/game/ducktales_symbols.h         the only file that names revision-specific addresses
src/game/ducktales_state.[ch]        typed state decoded from a CPU RAM image (pure functions)
src/game/ducktales_jump.[ch]         jump-height experiment: one hook site, off by default
src/host/ducktales_host.c            cyc_host_extras(): settings, options, per-frame callbacks
src/host/ducktales_diagnostics.[ch]  developer diagnostics (DUCKTALES_DEV_DIAGNOSTICS only)
tests/state_decode_test.c            unit test of the decoder (built with the headless build)
tests/jump_experiment.py             measures the experiment and checks that it switches off cleanly
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

## Developer diagnostics

`-DDUCKTALES_DEV_DIAGNOSTICS=ON` (default OFF) compiles `src/host/ducktales_diagnostics.c`. Without
it the host links empty inline stubs and lists none of these options.

- `--dt-state-log FILE|-` and `--dt-state-every N` write one line of typed state per frame, for
  example `frame=1780 diff=Normal money=0 hp=3/3 lives=2 x=68 y=174 vy=+5.711 held=80 new=80 jump=125`.
- `--dt-jump-at FRAME:PERCENT` (repeatable) changes the jump scale before that frame.
- At exit the log reports the jump scale, how many jumps were rescaled and how many times the hook
  fired.
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
- The window build compiles and links the layer, and `JumpScale` goes through the host's
  `load_setting`/`save_settings`. Nobody has played with it in the window yet.
