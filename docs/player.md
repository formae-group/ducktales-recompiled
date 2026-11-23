# Scrooge: movement and actions

Scrooge is object slot 0, so every per-object variable below is the slot-0 column of a 16-byte
row (`$0600+n` ... `$07F0+n`, n = slot). All code addresses here are in the fixed bank (bank 7,
`$C000-$FFFF`) unless a bank is named. Velocities and positions are 8.8 fixed point: a whole byte
and a fraction byte (1/256 px).

Status: **verified** = read from code whose behaviour is unambiguous *and* seen in a controlled run;
**code** = read from code but not exercised by any run yet; **hypothesis** = interpretation that
still needs an experiment.

Runs used below (all reproducible, see the end): `tests/routes/re_amazon_moves.txt` and routes built
on its first 1700 frames (identical prefix, then a save state at frame 1700): a held jump (A from
1780 to 1840), a tapped jump (A on 1780 only), pogo with B+Down only, pogo released at 1830, and
an exploration route (walk right into the first bush, B, then walk right jumping every 60 frames,
which takes three hits and a death in The Amazon on Normal).

## Per-frame order

`$E354` is Scrooge's per-frame entry. It loads gravity (`$33`) into `$EF`, decrements the
invulnerability timer `$7D`, ticks `$75` (see below), then jumps through a table indexed by the
mode byte `$39` (low bytes at `$E396`, high bytes at `$E3A7`). **verified** (code; every
run passes through `$E387`).

| `$39` | Handler | Meaning | Status |
|---|---|---|---|
| 0 | `$E3B8` | On the ground (stand, walk, crouch, push) | verified |
| 1 | `$E63B` | In the air (jump or fall) | verified |
| 2 | `$E7EA` | Pogo bounce in progress | verified |
| 3 | `$E806` | Climbing (rope/vine tile class 2) | code |
| 4 | `$E8EB` | Cane (golf) swing | verified |
| 5 | `$EAB9` | Hurt variant that waits for `$7D` | code; trigger not found |
| 6 | `$EB00` | Knockback after contact with a harmful object | verified |
| 13 (`$0D`) | `$EC9B` | Death jump | verified |
| 7-12, 14-16 | `$EB82`...`$EF62` | Other scripted states | not studied |

## Vertical motion

| Address | Meaning | Status |
|---|---|---|
| `$0640` | Y (screen) | verified |
| `$0660` | Y fraction | verified |
| `$07C0`:`$07B0` | Vertical velocity, whole:fraction, signed, **positive = up** | verified |
| `$07A0`:`$0790` | Jump take-off velocity, copied into the velocity on a jump | verified |
| `$EF` | Gravity used by `$F2B5` (`$33` = 0.199 px/frame², reloaded every frame at `$E354`; `$EF` is reused as scratch elsewhere) | verified |
| `$0780` | Vertical direction for the background probe: 8 while rising, 4 while falling or standing | verified |

Routines (per slot, X = slot): `$F2A1` subtracts the velocity from Y (Y -= v, so positive v moves
up); `$F2B5` subtracts `$EF` from the velocity. Rising (`$E9BA`) moves first and applies gravity
last; falling (`$E970`) applies gravity first. Gravity is skipped while the velocity's whole byte
is `$F4`, which caps the fall speed near -12 px/frame (**code**; no run fell that long).

On the ground the falling routine still runs every frame: the velocity becomes -`$33`, Y moves
down a fraction, the feet probe hits the floor, Y is snapped back and the velocity is zeroed
(`$E9A4-$E9AB`). This is why `$0660` keeps drifting by `$33` per frame while Scrooge stands still.

### Jump take-off velocity (priority finding)

- **Where it is applied:** `$E3EC-$E3FB` in the ground handler. When A is newly pressed (`$14`
  bit 7), it copies **RAM** `$0790` into `$07B0` and `$07A0` into `$07C0`, sets `$39` = 1 and
  runs the air handler in the same frame. The stores at `$E3F5`/`$E3FB` are fed by `LDA $0790` /
  `LDA $07A0`, not by immediates. **verified**: trace of re_amazon_moves frame 1780 shows
  `$07B0`=`$BB` at `$E3F5` and `$07C0`=`$04` at `$E3FB`; `tools/player_xref.py` finds no other
  readers of `$0790`/`$07A0`.
- **Where the value comes from:** the land-entry initialisation at `$D7EA-$D824` stores two
  immediates: `LDA #imm` at `$D813` (operand byte at CPU `$D814`, ROM file offset `0x1D824`) to
  `$0790` at `$D815`, and `LDA #imm` at `$D818` (operand at `$D819`, file offset `0x1D829`) to
  `$07A0` at `$D81A`. Value: **`$04BB`** (4.73 px/frame up). These are the only stores to
  `$0790`/`$07A0` in the ROM. **verified**: trace of re_amazon_moves frame 1638 (land entry, the
  same frame Y is set to `$40` at `$D802`) shows the writes at `$D815`/`$D81A`; the same routine
  sets the walking speed (`$D81D`/`$D822`).
- **Encoding:** 16-bit 8.8, whole byte `$07A0`, fraction `$0790`; Y -= v each frame; gravity
  `$33` per frame. Measured held jump from the ground at Y `$B4`: highest Y `$79` (59 px), 47
  frames in the air.
- **Reversible experiment (done):** `tools/player_poke_state.py` writes RAM inside a save state.
  With the frame-1700 state patched to `$07A0:$0790` = `$0600`, the held jump reaches Y `$57`
  (93 px, 60 frames); with `$0300`, Y `$9C` (24 px, 30 frames). Patching `$04BB` back yields a
  state byte-identical to the original. The patched value survived the death and life loss in the
  exploration route (still `$0600` at frame 3000), so it holds until the next land entry
  re-runs `$D7EA`. A RAM write of `$0790`/`$07A0` after land entry is therefore the least
  invasive knob; the ROM operands above are the source if the change must survive land entry (the
  executable refuses a modified ROM, so that needs a code-side override).

### Jump height with button hold

`$E9BA` (every rising frame, `$39` < 5): if the velocity is below a threshold and A is not held,
the velocity and Y fraction are zeroed (`$EA07`) and Scrooge starts falling at once. Thresholds are
a two-entry 16-bit table at `$EAB5` indexed by `$39` AND 2: **`$042D`** for a jump (`$39` = 1),
**`$05D2`** for a pogo bounce (`$39` = 2). So releasing A has no effect during the first frames of a
jump (velocity still at or above `$042D`, i.e. the first three frames) and cuts the rise on the
first later frame A is up. **verified**: tapped jump (A on frame 1780 only) is cut on frame 1783,
where the velocity before gravity was `$0422`; highest Y `$A6` (14 px), 14 frames airborne,
against 59 px / 47 frames when held.

### Pogo

- **Entering:** while airborne with B and Down both held (`$16` AND `$44` = `$44`), the air
  handler sets action `$0620` = 6 (`$EA24-$EA58`). **verified** (frame 1788 of re_amazon_moves).
- **Bounce:** on landing (`$E661`: falling and the feet probe hit), if the middle feet probe (probe 7,
  whose class the background test leaves in `$43`) is solid, B is held and either `$39` = 2 or Down is held (and, when `$A7` is
  non-zero, Y < `$A0`; `$A7` meaning unknown), it plays sound `$11` and stores immediate
  velocity **`$05A8`** (fraction operand at `$E6A4`, whole operand at `$E6A9`; file offsets
  `0x1E6B4`/`0x1E6B9`), `$0620` = 6, `$39` = 2. Otherwise it lands: sound `$10`, `$0620` = 8,
  `$39` = 0. **verified**: frame 1827 velocity `$05A8`, highest Y `$60` (84 px above the floor).
- **Holding:** the pogo handler copies B into the A bit of `$16` (`$E7EE-$E7F9`), so B alone keeps
  the bounce at full height; if neither A nor B is held it falls back to `$39` = 1 and the jump
  threshold `$042D` applies. **verified**: B+Down without A gives the same highest Y `$60` as
  A+B+Down; releasing everything at frame 1830 turns `$39` to 1 and the rise is cut at 1836
  (velocity before gravity `$0410` < `$042D`).
- Landing on objects: `$D291` (below) checks pogo (`$0620` = 6) when falling onto an object;
  the rest of that branch (`$D32F`...) was not studied.

## Horizontal motion

| Address | Meaning | Status |
|---|---|---|
| `$0730`:`$0720` | World X, high:low (pixels) | verified |
| `$0670` | World X fraction | verified |
| `$0770`:`$0760` | Horizontal speed, whole:fraction (unsigned; direction comes from the D-pad) | verified |
| `$0650` | Screen X = `$0720` - `$F2` (camera X low), written in bank 5 at `$80CB` | verified |
| `$0600` bit 6 | Facing right | verified |
| `$3E` | Push counter: 16 frames walking into a wall before the push pose | verified |
| `$3F` | Direction held when the push pose started | verified |

- Speed is **`$0180`** (1.5 px/frame): set at land entry (`$D81D`/`$D822`) and re-set every
  frame at `$E495-$E4A0` while the momentum mode below is off. **verified**: X steps 2,1,2,1 and
  `$0670` alternates `$00`/`$80` while walking (re_amazon_moves 1680-1719); same speed in the air
  (exploration route 1870-1872).
- `$E57F`: Right adds the speed (`$F234`), Left subtracts it (`$F252`), each followed by the
  object test `$D202` and the background test (`$E615`, probes 0-2 for right, 3-5 for left);
  a background hit snaps X back to the tile edge (`$D49A`/`$D4B2`). **verified**: walking right
  into the first bush of The Amazon stops at screen X `$76` with the fraction still toggling.
- **Momentum mode** (`$E4A6-$E54A`), active while `$71` or `$72` is non-zero: speed rises by
  `$10` per frame up to `$0280` while a direction is held, falls by `$06` per frame when released
  and by `$10` when reversing, never below `$0180`; `$72` holds the direction. `$71` is incremented
  by a special-tile handler of the background probe (`$D1B1`, feet probe only). **code**; that this
  is slippery ground (ice/snow) is a **hypothesis**.
- `$73`: when non-zero, the next frame adds a fixed 1.5 px to X (`$E551-$E575`). **hypothesis**:
  a push from the environment; trigger unknown.

## Actions (`$0620`)

| `$0620` | Meaning | Status |
|---|---|---|
| 0 | Standing | verified |
| 1 | Walking | verified |
| 2 | Jumping / falling | verified |
| 3 | Crouching (Down on the ground) | code |
| 4 | Climbing | code |
| 5 | Hurt (knockback and death) | verified |
| 6 | Pogo | verified |
| 7 | Pushing against a wall or object | verified |
| 8 | Landed (kept until the next action) | verified |
| 9 | Cane swing | verified |
| `$41` | Cane hit recoil (set when the swing connects, `$06B0` = 5 frames) | code |

`$0610` is the animation frame and `$0630` the frame timer of the current action (both reset on
every action change). **hypothesis** for the exact roles; they count as expected in every run.

## Cane swing

Only from the push pose: when `$0620` = 7 and B is newly pressed (ground `$E3CB-$E3E9`, air
`$E642-$E660`), `$0620` = 9 and `$39` = 4. The push pose itself needs 16 frames of walking into
something (`$3E` counts down from `$10` at `$E458`). The swing handler (`$E8EB`) leaves Scrooge in
place (no gravity), and on animation frame 2 checks the object in front with `$D202`; if it
connects (object type in `$0710,X` not `$08`, `$15` or `$42`) it plays sound `$18` and switches to
`$41`. When the animation reaches frame 6 it returns to the ground with the push pose if the same
direction is still held. **verified**: exploration route, B at frame 1760 against the bush: `$39` = 4,
`$0620` = 9, `$0610` 0-6 at 5 frames each, back to `$39` = 0 / `$0620` = 7 on frame 1790 (30
frames). The bush is background, so no hit happened; the connect branch is **code** only.

## Damage, knockback, invulnerability

- **Trigger:** `$D202` (horizontal object test, called while walking) finds an overlapping live
  object whose hitbox entry (`$07D0,X`) has bit 7 set; if the entry's bits 4-6 are non-zero the
  object is harmful and `$D288` sets `$39` = 6, unless `$75` is non-zero, in which case the object's
  `$0690,X` is incremented instead (`$D28C`). While `$7D` is non-zero harmful objects are skipped
  (`$D259`). **verified**: exploration route frame 1858, `$D288` stores 6 with X = `$0E`.
- **Knockback** (`$EB00`, first frame): sound `$14`, vertical velocity immediate **`$0169`**
  (operands at `$EB0A`/`$EB0F`), speed fraction `$0760` = 0 (speed `$0100`), `$7D` = **`$46`**, then the
  damage routine `$EB5E`; if the first HP dot (`$0342`) is now lost, mode `$0D` (death). Each
  following frame: `$0620` = 5, Scrooge moves 1 px/frame opposite to his facing (facing bit is
  restored afterwards, `$EADC-$EAFF`), under normal gravity. When `$7D` reaches `$3C` (10 frames
  later) speed goes back to `$0180` and `$39` = 0. **verified**: frames 1859-1869, world X `$C2`
  down to `$B8`, velocity `$0136` after the first gravity step.
- **Invulnerability:** `$7D` counts down by 1 per frame at `$E35E` while it is 1-`$7F`; 70 frames
  after a hit. **verified**: `$46` at 1859, 0 at 1929.
- **Death** (`$EC9B`): `$0620` = 5, velocity `$0400`, speed `$0080`, then a rise-and-fall off the
  bottom of the screen. **verified**: third hit at frame 2221 (`$0342` = 1), velocity `$03CD`
  after the first gravity step.
- `$75` (16-bit counter `$76/$77`, decremented every frame at `$E36F`, restarts music `$F6` when it
  ends; blocks damage from objects and spikes). **hypothesis**: invincibility power-up.
- `$70`: hazard request set by special background tiles: 6 from a spike-type tile when not
  invulnerable (pogo blocks it except on probe `$0A`), 7 from another tile type while pogoing, 8
  when a pit tile is reached with Y >= `$D0`. **code**; outcomes not traced.

## Background collision

`$D120` (X = slot, `$40` = first probe, `$41` = probe count - 1) tests probe points against the
room's collision map and ORs the results into `$10` (bit 0 = solid hit). Each probe (`$D350`) adds
a signed offset from `$D433` (dx) and `$D3CE` (dy) to world X and Y, turns the point into a 16x16
metatile cell and reads a 2-bit class from the map at `$0580` (four cells per byte). Points left of
X = 0 read as empty; points outside the current vertical screen (`$F1`) return a fixed value.
Class 1 is solid, class 2 climbable (`$E768`), class 3 is special and dispatched on the tile's high
nibble (`$D14D`, handlers `$D162-$D1DE`). **verified** for the floor and wall cases below; class
3 handlers are **code**.

Scrooge's probes (offsets from his position): 0-2 right side (dx +10; dy +11, 0, -11), 3-5 left
side (dx -10), 6-8 feet (dy +12; dx -9, 0, +9), 9-11 head (dy -12). `$EA5E` picks feet or head
from the Y movement this frame (`$0780` when unchanged) through the two-byte table at `$EAB3`; on a
hit, feet snap Y so the probe sits on the tile's top edge (`$D4D6`), head pushes Y down below the
tile (`$D4C8`). **verified**: Scrooge stands at Y `$B4` on the Amazon floor (feet at `$C0`) and at
`$94` on the ledge at re_amazon_moves 1683.

Object collision is separate: `$D202` (horizontal) and `$D291` (vertical) compare Scrooge with slots
1-15 using half-height `$F498` and half-width `$F4B8` tables indexed by `$07D0,X` AND `$7F`; against
harmful objects (`$07D0,X` bits 4-6 set) a crouching Scrooge (`$0620` = 3) gets 7 px more vertical
clearance. **code**, plus the knockback trace.

## Proposed memory-map rows

See the final report of this task; rows are not added to `docs/memory-map.md` here.

## Still unknown

- Modes 5, 7-12 and 14-16; what sets mode 5.
- Which tiles are class 3 in each land; whether `$71` is ice; what `$73`, `$A7`, `$75` are in play.
- The pogo-on-object branch of `$D291` and the cane's effect on objects.
- Respawn after a life loss: it does not re-run `$D7EA` (the jump velocity patch survived it), but
  the respawn routine itself was not traced.

## Reproducing

```sh
E="build/headless/Release/ducktales.exe roms/baserom.nes"   # quote the ROM path
# frame log of the moves route and a trace of the jump frame
$E --frames 2130 --input tests/routes/re_amazon_moves.txt --frame-log captures/moves.bin --frame-log-frames 1600:2130
py -3 -I tools/player_trace.py captures/moves.bin --first 1778 --last 1830 --summary
py -3 -I tools/whoaccesses.py --input tests/routes/re_amazon_moves.txt --frame 1780 07B0,07C0
py -3 -I tools/whoaccesses.py --input tests/routes/re_amazon_moves.txt --frame 1638 0790,07A0
py -3 -I tools/player_xref.py roms/baserom.nes 0790,07A0
# jump-height experiment
$E --frames 1701 --input tests/routes/re_amazon_moves.txt --save-state 1700:captures/f1700.state --frame-log captures/f1700.bin --frame-log-frames 1700:1700
py -3 -I tools/player_poke_state.py captures/f1700.state captures/f1700.bin captures/hi.state 07A0=06 0790=00
$E --load-state captures/hi.state --frames 1860 --input ROUTE_WITH_A_HELD_FROM_1780 --frame-log captures/hi.bin --frame-log-frames 1775:1860
py -3 -I tools/player_trace.py captures/hi.bin --summary
```

Routes for the other runs are described at the top; any route whose first 1700 frames equal
re_amazon_moves can be continued from the frame-1700 state.
