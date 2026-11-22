# Audio

The sound driver lives in bank 3 and drives the four tone channels of the 2A03 APU (two pulses,
triangle, noise). It never uses the DMC. Addresses below are bank 3 unless another bank is
named; RAM is CPU RAM. Status words follow [memory-map.md](memory-map.md): **verified** = seen
in a controlled run or read from unambiguous code, **hypothesis** = needs another experiment.

Tools (they read your ROM locally; their output is never committed):

- `tools/audio_driver.py ROM table|decode ID|xref|apu`: the sound table, a frame-exact
  simulation of the sequencer (lengths, loops), every request call site, every APU write.
- `tools/audio_record.py STATE IDS`: patches the request ring of a save state, runs the
  headless build with `--wav-out`, reports loudness per frame, driver bytes (`--ram`) and
  loop checks (`--loop`).
- `tools/audio_requests.py ROUTE`: every request a route makes; with `--callers`, the code
  that made it.
- `tests/audio_compare.py`: the native synth (below) against the cycle host's APU on the routes.

## Request path

| Step | Code | What happens | Status |
|---|---|---|---|
| Queue music | `$F752` (bank 7) | Stores A in `$C5` (last music id), then falls into `$F754`. | verified |
| Queue any sound | `$F754` (bank 7) | Writes A into ring slot `$C8+$C6` only if that slot holds `$88` (empty), then advances `$C6` (mod 8). A full ring silently drops the request. X is preserved through `$00`. | verified (static) |
| Drain | NMI `$C110-$C127` | With bank 3 mapped, takes every non-empty slot from `$C7` on, marks it `$88` and calls `$8003` with the id in A and Y = 0 (Y is left at 0 by the task loop at `$C0B3-$C0C7`). | verified |
| Tick | NMI `$C12A` | Calls `$8000` once per NMI, then restores the bank in `$E7`. | verified |

`$8000` jumps to the update at `$823D`; `$8003` is the request handler. 13 call sites use
`$F752` and 50 use `$F754` (`audio_driver.py xref`); no other code calls `$8003` or writes the
ring.

## Request ids

The handler first checks four control ids, then treats anything else as an index into a table
of 2-byte pointers at `$8A40` (56 entries, ids `$00-$37`; the table ends where the first header
begins, `$8AB0`). Ids `$38-$FB` would read past the table; the game never sends them.

| Id | Effect | Used by the game | Status |
|---|---|---|---|
| `$FC` | Ticks per frame `$D7` = Y + 1 (so always 1 from the NMI) | never | verified (static); poking `$D7` = 2 doubles the tempo (below) |
| `$FD` | Global fade `$D8` = Y (always 0 = off from the NMI), `$D9` = 1, `$DA` keeps only bit 0 | never | verified (static) |
| `$FE` | Stop the current effect: clears the effect priority and gives every effect channel back to the music | never | verified (static) |
| `$FF` | Stop music: clears the music priority, `$D7`, `$D8` and the four music stream pointers; idle channels are muted on the next tick | `$EF6D` (after a wait in a scripted Scrooge state), `$FDD8` (just before the `$37` fanfare that leads to the ending) | verified |

## Driver RAM

### Globals

| Address | Meaning | Status | Evidence |
|---|---|---|---|
| `$D0` | Priorities: high nibble = playing effect, low nibble = playing music (0 = none) | verified | `$8044-$8057`, `$80BB-$80CE`; frame logs show `$07` with music only, `$C7` while effect `$13` (priority 12) plays over it, `$07` again when it ends. |
| `$D1` | Previous effect channel mask while a new effect starts | verified (static) | `$80EA`, `$8106-$810D`. |
| `$D2/$D3` | Pointer to the header being started | verified (static) | `$8031-$8039`. |
| `$D4` | Busy flag: set while `$8003` runs; `$8000` then skips the channel update | verified (static) | `$824C`. |
| `$D5/$D6` | Scratch during requests (channel counter, register offset) | verified (static) | |
| `$D7` | Sequencer ticks per frame; 0 or 1 = normal | verified | `$86AC`; with `$D7` = 2 poked the Land Select music repeats its state after 48/96 frames instead of 96/192. |
| `$D8` | Global music fade: bits 0-6 = frames per step (0 = off); bit 7 set = fade out, clear = fade in | verified | `$82CC-$82EE`, `$830D-$833C`; `$D8` = `$84` poked: music silent after 33 frames; `$D8` = `$04`: loudness rises over about 100 frames. |
| `$D9` | Fade step 1-16 (held at 15 after a fade out; a fade in clears `$D8` when done) | verified (static) | `$82DC-$82EE`. |
| `$DA` | Driver frame counter, +1 per update; bit 0 is the parity used by triplets and the fade timer | verified (static) | `$824A`, `$86C2`. |
| `$DB` | APU register offset of the channel being updated (0, 4, 8, `$0C`) | verified (static) | `$8251-$82BA`. |
| `$DC/$DD` | Pointer to the current channel record | verified (static) | same loop. |
| `$DE` | Channel countdown 4..1 (4 pulse 1, 3 pulse 2, 2 triangle, 1 noise) | verified (static) | The triangle and noise special cases compare it with 2 and 1. |
| `$DF` | Effect channel mask: bit 0 pulse 1, 1 pulse 2, 2 triangle, 3 noise; rotated through bits 0-7 during an update and back in place after it | verified | `$80F2`, `$820D`, `$821F`; frame logs show `$2`, `$8`, `$A` or `$F` while an effect plays and 0 after. |
| `$E0/$E1` | Effect stream pointer (one stream per effect) | verified | Advances one event group per wait in a run of `$13`. |
| `$E2` | Effect wait counter (frames until the stream is read again), decremented at `$82F0` | verified | Counts 2, 1, 0 between reads in the same run. |
| `$E3` | Effect loop counter | verified (static) | `$860D-$862C`. |
| `$E4/$E5` | Scratch | verified (static) | |
| `$03EC/$03ED` | Instrument table of the current song | verified (static) | Set from the song header at `$80A6-$80B0`, read by `$88D9`. |

`$E6` (pause) is game RAM that the driver also reads: while its bit 0 is set the music does not
advance and channels not owned by an effect are muted (`$827B`). Verified: with `$E6` = 1 poked
only the effect is audible.

### Channel records

Four 31-byte records at `$0370` (pulse 1), `$038F` (pulse 2), `$03AE` (triangle) and `$03CD`
(noise), followed by `$03EC`. Offsets `$00-$0F` are the music's sequencer state, `$10-$13` the
effect's, `$14-$1E` the modulation state of whoever owns the channel. Static reading unless a
row says otherwise.

| Offset | Meaning |
|---|---|
| `$00/$01` | Music stream pointer; 0 = channel idle (verified: zero after `$FF` and after songs `$09`/`$0A` end) |
| `$02/$03` | Time left in the current note in quarter frames; 4 is subtracted per tick (verified: counts down by 4 per frame) |
| `$04` | Tempo: every note's length unit is multiplied by it |
| `$05` | Bit 7 = triplet flag of the current note; bits 0-6 = loop counter |
| `$06` | Bits 5-7 = repeat-pitch counter; bits 0-4 = instrument number |
| `$07/$08` | Pointer into the period table: `$897B` + 2 x transpose |
| `$09` | Music pitch slide, signed, added to the period every frame |
| `$0A/$0B` | Music period, 0 = rest (verified: changes at each note) |
| `$0C` | Music `$4000`-style byte: duty in bits 6-7 (bit 7 = short noise mode on the noise channel), envelope control bits, volume in bits 0-3; on the triangle the whole byte is the `$4008` value |
| `$0D` | Per-note volume envelope: bit 7 = decay from 15, clear = attack from 0; bits 0-6 = frames per step (0 = off) |
| `$0E` | Envelope frame counter |
| `$0F` | Envelope: high nibble = step, low nibble = current level |
| `$10` | Effect pitch slide, signed |
| `$11/$12` | Effect period (verified: holds the effect's pitch while pulse 2's music period keeps changing underneath) |
| `$13` | Effect `$4000`-style byte |
| `$14` | Vibrato: bits 0-6 = frames per step; bit 7 = half-cycle flag |
| `$15` | Vibrato: bits 5-7 = period change per step, bits 0-4 = steps per half-cycle |
| `$16` | Tremolo: bits 0-6 = frames per step (0 = off); bit 7 = restart the level from the note volume on each note |
| `$17` | Tremolo step, signed |
| `$18` | Vibrato frame counter |
| `$19` | Bit 7 = vibrato direction; bits 0-6 = step counter |
| `$1A/$1B` | Vibrato offset added to the period (16-bit signed) |
| `$1C` | Last value written to the period high register; `$FF` forces the next write |
| `$1D` | Tremolo frame counter |
| `$1E` | Tremolo level |

Bytes `$14-$17` form an *instrument*: a song selects one with command `$08`, which copies four
bytes from the song's instrument table; an effect writes them directly with its command `$05`.
The vibrato is a triangle wave: the offset moves one way for N steps, back for 2N, and so on, so
it swings symmetrically around the note.

## Sound table and headers

Each table entry points to a header. The low nibble of the header's first byte decides the kind:

- **Music** (low nibble non-zero): first byte = priority (every song has 7), then four 2-byte
  stream pointers (pulse 1, pulse 2, triangle, noise; 0 = channel unused), then the 2-byte
  instrument table pointer (4 bytes per instrument).
- **Effect** (low nibble zero): the high nibble is the priority; the next byte's low nibble is
  the channel mask that goes into `$DF`; the event stream starts right after.

Streams may share data: some songs start one channel with a rest and then jump into another
channel's stream, which produces a delayed echo of that part.

## Music stream format

A channel reads one-byte events until it reaches a note (`$86FE`); the high nibble decides the
kind. Ten commands exist (jump table at `$87BB`):

| Byte | Operands | Meaning |
|---|---|---|
| `$00` | 1 | Tempo (length multiplier, `+$04`) |
| `$01` | 1 | Pitch slide per frame, signed (`+$09`) |
| `$02` | 1 | Duty: replaces bits 6-7 of `+$0C` |
| `$03` | 1 | Volume and envelope bits: replaces bits 0-5 of `+$0C` (the whole byte on the triangle) |
| `$04` | 3: count, address | Loop. Count 0 = jump unconditionally (the song loop, and jumps into shared data). Otherwise the section back to the address plays count + 1 times; the counter is `+$05`, one per channel, so counted loops do not nest. |
| `$05` | 1 | Transpose: pitch p then plays period-table entry transpose + p |
| `$06` | 0 | The next note is dotted (length table at `$8973`, 1.5 x) |
| `$07` | 2 | Per-note volume envelope (`+$0D`, `+$0F`), restarted on every note |
| `$08` | 1 | Instrument (loads `+$14-$17` unless an effect owns the channel) |
| `$09` | 0 | End: the channel stops and the music priority is cleared |

- **`$2n`**: plays the next note normally, then the following n notes keep its pitch: only their
  length bits count, and each restarts the modulation.
- **`$3x`**: triplet: the next note is ticked twice on frames with odd `$DA`, so it lasts about
  two thirds of its written length.
- **`$40-$FF`**: a note. Bits 5-7 select a length from the table at `$896B` (index 2-7 = 2, 4, 8,
  16, 32 or 64 units; dotted 3 to 96); the time in frames is units x tempo / 4. Bits 0-4 are
  the pitch: 0 = rest, 1-31 = semitones above the transpose. On the noise channel the pitch is
  the noise period index itself (only bits 0-3 reach `$400E`; the mode bit comes from `+$0C`).
- **`$10-$1F`** would be notes of length 0, and `$0A-$0F` have no handler; the simulator finds
  neither in any song.

The period table at `$897B` holds 96 two-byte NTSC periods. Entries 0-8 are zero; entry 9 is A1
(55 Hz) and each later entry is one semitone higher, up to entry 95 (A#8). Entry 10 is the one
exception: it holds a period about a quarter of a semitone above A1 instead of A#1.

## Effect stream format

An effect has one stream and one wait counter for all its channels (`$856B-$8698`). On each
frame where `$E2` is 0, each owned channel in channel order reads events until it reaches a
note, so a two-channel effect alternates note groups between its channels. Commands (jump table
at `$85C3`):

| Byte | Operands | Meaning |
|---|---|---|
| `$00` | 1 | Wait n frames before the stream is read again |
| `$01` | 1 | Pitch slide per frame, signed (`+$10`) |
| `$02` | 1 | Duty bits of `+$13` |
| `$03` | 1 | Volume and envelope bits of `+$13` (whole byte on the triangle) |
| `$04` | 3 | Loop, as in music, with the single counter `$E3` |
| `$05` | 4 | Vibrato/tremolo bytes `+$14-$17` |
| `$06` | 0 | End for this channel; the stream stays on this byte so the other channels end on their next read |
| `$80-$FF` | 1 | Note: a raw 11-bit period (bits 0-2 of this byte are the high bits, the operand the low byte). A byte whose low nibble is `$F` instead skips its operand and only restarts the modulation. |

When an effect ends on a channel (`$8654`), the effect priority is cleared, the channel returns
to the music, the music's instrument is reloaded, and the channel is muted if the music is
resting or idle there.

## Per-frame update

`$823D` writes `$08` to `$4001` and `$4005` (sweep off) and `$0F` to `$4015` every frame,
then for each channel: advances the vibrato and tremolo counters; runs the effect if it owns the
channel; advances the music `$D7` times unless paused; and computes the output:

1. volume = the music's `+$0C` volume (or the effect's `+$13`);
2. music only: the global fade (`$D8/$D9`) lowers or caps it, then the per-note envelope
   (`+$0D/$0F`) caps it;
3. the tremolo level caps it when `+$16` is set (the level moves by the step every N frames and
   reflects at 1 and 15);
4. `$4000+x` = duty bits | volume; period = music or effect period + vibrato offset (the slide
   changes the stored period); the low byte is written every frame, the high byte (with the
   length-counter load bit set) only when it changed, which avoids restarting the pulse phase.

The music sequencer keeps running on channels an effect owns, without writing the APU, so the
music is in the right place when it gets the channel back (verified: pulse 2's music pointer,
time and period advance while effect `$13` holds it). Effects are not affected by the global
fade or the per-note envelope.

## Priority and channel stealing

Verified with `audio_record.py` on a Land Select state (music 5 playing) by queuing two ids in
the same frame:

- **Music vs music**: a song replaces the current one when its priority is at least the current
  music priority; all songs have 7 and the priority is cleared when a song ends, so every music
  request is honoured. Starting a song resets speed and fade but leaves effect channels to the
  effect (`$13+$00`: pulse 2 and noise stay with `$13` until it ends).
- **Effect vs effect**: one effect at a time. A new effect replaces the current one when its
  priority is at least the current one (`$1A+$1B`: `$1B` plays; `$1B+$1A`: `$1A` plays); a
  lower one is dropped (`$13+$15`: `$15`, priority 6, is ignored while `$13`, priority 12,
  plays). Channels the old effect held and the new one does not go back to the music at once
  (`$37+$14`: `$14` keeps only the noise channel).
- **Effect vs music**: an effect takes exactly the channels in its mask. 39 of the 40 effects use
  only pulse 2 and/or noise, so pulse 1 and the triangle keep playing music; `$37` takes all four.

## DMC

Not used. The only `$4015` write in the ROM stores `$0F` (DMC disabled) and no instruction
writes `$4010-$4013` (`audio_driver.py apu`; the one other hit, a `$4003` store pattern at
`$96F6`, is inside song data). NESRecomp's APU statistics report `dmc_playing=0`.

## Native synth

`src/native/native_audio.c` is the sound of the native runtime ([native-runtime.md](native-runtime.md)),
which has no APU. The recompiled driver keeps running as game logic and writes the APU registers
as it always did; the native bus records those writes per frame with their CPU cycle
(`NativeFrame.apu_writes`), and the synth turns one frame's writes into that frame's samples:

```c
native_audio_reset(48000);                                   /* at power-on */
size_t n = native_audio_frame(native_last_frame(), pcm, NATIVE_AUDIO_MAX_FRAME_SAMPLES);
```

It is a port's synth, not an APU: nothing is stepped per CPU cycle. Between two events (a register
write at its cycle offset, or a frame-sequencer clock) every channel's parameters are fixed, so its
contribution to a sample is the integral of a periodic step sequence over the sample's span of CPU
time, taken in closed form:

| Channel | What the synth keeps | Rendered as |
|---|---|---|
| Pulse 1/2 | duty, volume or envelope, 11-bit period, step, time to the timer's next expiry, sweep | 8-step duty sequence, one step per 2 x (period + 1) cycles; silent below period 8 or on a sweep overflow |
| Triangle | period, step, time to the next expiry, linear and length counters | 32-step sequence, one step per period + 1 cycles while both counters are non-zero; below period 2 it keeps stepping while the output holds (the driver's rests write period 0) |
| Noise | mode, period, 15-bit LFSR, envelope | one LFSR shift per table period (the only loop, never per cycle) |
| DMC | the `$4011` level | constant; the driver never starts a sample ([DMC](#dmc)) |

The frame sequencer (4- or 5-step, NTSC cycle positions) clocks envelopes, the linear counter,
length counters and sweeps and carries over from frame to frame. Register side effects follow the
2A03: `$4003`/`$4007` restart the duty sequence and the envelope without resetting the timer,
`$400B` sets the linear counter's reload flag, `$400F` restarts the noise envelope, `$4015` clears
disabled length counters, `$4017` restarts the sequencer. A period change takes effect at the
timer's next expiry, as on the console, so pulse waveforms stay sample-locked to the APU's. The
mix is the console's nonlinear mixer and the NES output stage (90 Hz and 440 Hz high-pass, 14 kHz
low-pass), as in NESRecomp's APU.

A frame the no-slowdown mode stretched (`held_cycles`) is played in `cycles - held_cycles`, its
write offsets scaled to match, since it is still shown for one 60 Hz period; every other frame is
played at its own length, so the sample count follows the machine exactly (the native and APU
WAVs of a route have the same length).

What cannot match sample for sample, and why the comparison uses phase-free measures: the triangle
steps once per CPU cycle during a rest (period 0), so where it stops when the next quarter-frame
clock clears its linear counter depends on single cycles of write timing, and the noise LFSR's
phase likewise. Two recordings of the APU itself at different CPU/PPU alignments differ the same
way. Results: [validation.md](validation.md#native-synth).

`ducktales_native --wav-out FILE` writes the synth's output (48 kHz mono) for a route.

## Catalogue

"Length" comes from the simulator and was confirmed with `audio_record.py`: for effects, the
frame at which `$DF` returns to 0 and the audible span of the WAV agree with it for every id;
for music, `--loop` finds the four channel records (pointer, time left, loop counter, period)
identical at the loop start and one period later. "Seen in" is the route and first frame where
`audio_requests.py --callers` traced the request. Ids that share a header are the same sound.

### Music (`$00-$0F`)

| Id | Piece | Length (frames) | Requested by | Seen in |
|---|---|---|---|---|
| `$00` | The Amazon | loops every 2304 from the start | land table `$D9CA` (`$F6` = 0), end of invincibility `$E384`, respawn `$DD09` | `amazon_intro` |
| `$01` | Transylvania | 288 intro, loops 2304 | same | `transylvania_intro` 877 |
| `$02` | African Mines | short intro, loops 1792 (pulse 1: 1791) | same | `african_mines_intro` 897 |
| `$03` | The Himalayas | 144 intro, loops 2048 | same | `himalayas_intro` 917 |
| `$04` | The Moon | 910 intro, loops 2080 (the noise part repeats every 160) | same | `moon_intro` 937 |
| `$05` | Land Select; also the ending | loops 96 (triangle 192), noise unused | `$D9CA` with `$F6` = 5; ending `$E1C1` | every route, frame 220; ending poke run |
| `$06` | Boss fight | 224 intro, loops 896 (pulse 2: 895) | boss trigger `$D043`; respawn during a boss fight | poke runs ([game-state.md](game-state.md#boss)) |
| `$07` | Title | loops every 3168 | `$D4FA`; ending `$E256` (static reading: when sprite 0's Y is `$B7` or more and `$C5` is not 7) | every route, frame 7 |
| `$08` | Invincibility | loops 96, noise unused | `$E36C` while `$75` is positive | `african_mines_intro` 1821 |
| `$09` | Game over | ends after 160, noise unused | `$DEB9` | `amazon_intro` 4020 |
| `$0A` | Death | ends after 90, noise unused | `$DC51` (`$7E` of `$87` or more) | `amazon_intro` 1409 |
| `$0B` | Launchpad ride scene | 112 intro, loops 896 (noise every 112) | scene loader `$DFBE` | poke run `$78` = `$81` |
| `$0C-$0F` | Same header as `$00` | | never requested | |

Every route also requests music 0 at frame 5, two frames before the title music (hypothesis:
from the title set-up `$D60A`, which runs before `$D4FA`; the trace of that frame did not show
the caller).

Songs `$02` and `$06` each have one triplet-heavy channel whose loop is one frame shorter than
the other three, so that channel drifts one frame earlier on every pass. Verified: in `$06`
pulse 2's record repeats after 895 frames (1791 for two passes) while the other three repeat
after 896; in `$02` pulse 1 repeats after 1791 frames. A native player has to keep this.

### Effects (`$10-$37`)

Priority is the header's high nibble; channels: P2 = pulse 2, N = noise. "Where" is the traced
or static call site; object types are `$0620+X` of the calling slot. Names of enemies and objects
behind bank 6 call sites are not known.

| Id | Prio | Channels | Frames | Where it plays | Status |
|---|---|---|---|---|---|
| `$10` | 8 | P2+N | 7 | Scrooge lands (`$E6C9`) | verified (routes, [player.md](player.md)) |
| `$11` | 5 | P2 | 40 | Pogo bounce (`$E6A0`) | verified (routes) |
| `$12` | 10 | P2 | 9 | Climbing while Up or Down is held (`$E82C`) | static |
| `$13` | 12 | P2+N | 16 | Cane swing (`$E928`) | verified (routes) |
| `$14` | 15 | N | 14 | Scrooge is hit, knockback (`$EB06`) | verified (routes) |
| `$15` | 6 | P2 | 17 | Money: pickups of types `$23-$26` and `$28` (table bank 6 `$8C0C`), bank 6 `$8B79`, each step of the TOTAL count (`$D9B0`), money added at `$EFD4`; also pause (`$DB4E`) | verified (routes: type `$23`) |
| `$16` | 13 | P2+N | 25 | Bank 6 `$88F8` (type `$2A`) and bank 4 `$BDC3`; often in the same frame as `$1D` (hypothesis: an enemy defeated) | verified (routes); meaning hypothesis |
| `$17` | 11 | P2 | 33 | Extra-life pickup (type `$27`, table bank 6 `$8C0C`) | static |
| `$18` | 12 | P2+N | 30 | The cane hits an object (`$E953`, same frame as `$13`) | verified (routes) |
| `$19` | 5 | P2+N | 17 | Bank 6 `$85BD` (type `$12`) | verified (route) |
| `$1A` | 5 | P2 | 10 | Bank 6 `$8642` | static |
| `$1B` | 5 | P2 | 14 | Bank 6 `$839A` | static |
| `$1C`, `$1D` | 5 | N | 8 | Bank 6 `$A579` (`$1C`); bank 6 `$941F` (`$1D`, type `$2E`) | verified (routes, `$1D`) |
| `$1E`, `$1F`, `$20` | 5 | P2+N | 38 | Bank 6 `$A652`, `$A987` (`$1E`), `$98C4` (`$1F`), `$9910` (`$20`) | static |
| `$21`, `$22`, `$23` | 5 | N | 29 | Bank 6 `$9A36` (`$21`), `$A74E` (`$22`), `$A9E4` (`$23`) | static |
| `$24` | 5 | P2+N | 21 | Bank 6 `$A0D8` (type `$4D`), `$A10E` | verified (route, The Moon) |
| `$25` | 5 | P2 | 22 | Bank 6 `$A234` | static |
| `$26` | 5 | P2+N | 9 | Bank 6 `$9242` | static |
| `$27`, `$28`, `$29` | 5 | P2+N | 21 | Bank 6 `$9168`, `$9FB5`, `$A020` (`$27`), `$AE03` (`$28`); `$29` is never requested | static |
| `$2A` | 5 | P2 | 17 | Bank 6 `$84B9` | static |
| `$2B` | 5 | P2 | 3 | Bank 6 `$920B` | static |
| `$2C` | 12 | N | 7 | Bank 6 `$9599` | static |
| `$2D` | 9 | P2+N | 160 | Warp that costs no life (`$DC5E`, `$7E` below `$87`) | static |
| `$2E` | 5 | P2 | 4 | One per character of a text message (`$EEE9`, skipped for blanks) | verified (routes, `$39` = `$0F`) |
| `$2F` | 5 | P2 | 19 | Menu cursor: title difficulty (`$D5CB`), Land Select (`$D781`) | verified (routes) |
| `$30` | 5 | P2+N | 97 | Bank 6 `$8E1F`, `$9469` (type `$34`) | verified (routes) |
| `$31` | 5 | N | 9 | Bank 6 `$93DF` (type `$2D`) | verified (routes, Transylvania) |
| `$32` | 5 | N | 9 | Bank 6 `$9882` | static |
| `$33` | 5 | P2+N | 11 | Bank 6 `$9F39` | static |
| `$34` | 15 | P2 | 28 | Bank 6 `$9BBF` (type `$56`) | verified (route, The Himalayas) |
| `$35` | 15 | P2 | 56 | Bank 6 `$8942` (types `$4E`, `$38`) | verified (routes) |
| `$36` | 15 | P2 | 25 | Time below 30, every 16 frames (`$E2FA`) | static ([game-state.md](game-state.md#timer)) |
| `$37` | 15 | all four | 255 | Fanfare: key item (type `$29`, table `$8C0C`), treasure (bank 6 `$8C15`), and after `$FF` at `$FDDD` before the ending | static |

Every id `$10-$37` was recorded from the Land Select state with the music stopped first
(`--stop-music --ram 300`); each sounded for the listed time and held exactly the listed
channels in `$DF`.

## Reproducing

```sh
E=build/headless/Release/ducktales.exe; ROM="roms/DuckTales (USA).nes"
mkdir -p captures/audio
$E "$ROM" --frames 351 --input tests/routes/amazon_intro.txt --save-state 350:captures/audio/ls350.state
py -3 -I tools/audio_driver.py "$ROM" table
py -3 -I tools/audio_record.py captures/audio/ls350.state 10-37 --stop-music --ram 300 --frames 300
py -3 -I tools/audio_record.py captures/audio/ls350.state 01 --frames 2632 --loop 298:2304
py -3 -I tools/audio_record.py captures/audio/ls350.state 13+15,15+13,1A+1B,37+14 --frames 120 --ram 100
py -3 -I tools/audio_record.py captures/audio/ls350.state FE --frames 120 --poke D8=84,D9=01,DA=00
py -3 -I tools/audio_record.py captures/audio/ls350.state FE --frames 300 --poke D7=02 --loop 19:96
py -3 -I tools/audio_requests.py tests/routes/transylvania_intro.txt --frames 5112 --callers
```

`audio_record.py` deletes its WAVs unless `--keep` is given; the frame logs of both recorders
are deleted after reading.

## Not yet known

- Launchpad ride music (`$0B`) through the native synth: no route reaches it.

- Which enemies and objects stand behind most bank 6 call sites; the table gives their addresses
  and, where a route reached them, their object types.
- The caller of the music 0 request at frame 5.
- Whether any note reaches period-table entry 10.
