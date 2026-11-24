# Functions

Addresses are CPU addresses; `bank N` is the 16 KiB PRG bank at `$8000` (bank 7 is fixed at
`$C000`). Every entry gives the evidence it rests on. Names are ours, not Capcom's.

## Fixed bank (bank 7)

| Address | Name | Behaviour | Evidence |
|---|---|---|---|
| `$FEE0` | `reset` | Disables NMI and rendering, waits for VBlank twice, clears `$0000-$06FF`, initialises the PPU, creates the main task at `$D4E6` and enters the scheduler. | Static reading of the code at the RESET vector. |
| `$FF35` | `scheduler` | Cooperative task scheduler. Scans the four task slots at `$80-$8F` for one whose state is `>= 4`. State `8` starts a new task through `JMP ($0093)`; otherwise it restores the task's saved stack pointer and returns into it. Reads the controller (`$C1AB`) when switching to slot 0. | Static reading; every route spends all non-NMI time here or in task code. |
| `$FF7D` | `task_create` | A = slot (0-3). Stores the entry point from `$93/$94` in the slot and sets its state to 8. | Static reading; only caller is `reset`. |
| `$FF8D` | `task_kill` | A = slot; sets that slot's state to 0. | Static reading. |
| `$FF95` | `task_exit` | Ends the running task (slot in `$91`) and re-enters the scheduler. | Static reading. |
| `$FFAC` | `task_yield` | Saves registers and the stack pointer in the current slot, sets a one-frame wait, and returns to the scheduler. The NMI moves waiting slots back to runnable. | Static reading of this routine and the NMI loop at `$C0AE-$C0C8`. |
| `$FFA5` | `task_wait` | Calls `task_yield` X times. | Static reading. |
| `$FFDE` | `bank_select` | A = bank. Stores it in `$E7` and writes it through the bus-conflict table at `$FFE5`. | Static reading; 45 call sites in bank 7. |
| `$FFE0` | `bank_select_temp` | Same write without updating `$E7`; used by the NMI, which restores `$E7` afterwards. | Static reading of the NMI. |
| `$C000` | `nmi` | See [RENDERING.md](RENDERING.md) and [AUDIO.md](AUDIO.md). | Static reading. |
| `$C1AB` | `read_controllers` | Strobes `$4016` and shifts 8 bits from each port. | Static reading; RAM correlation in [MEMORY-MAP.md](MEMORY-MAP.md). |
| `$D4E6` | `main_task` | Entry point of the first task. Static discovery cannot reach it (it is only reached through `JMP ($0093)`), so it is listed in `config/game.toml`. | Static reading of `reset`; first interpreter miss in the unseeded build. |

### Rendering (details in [RENDERING.md](RENDERING.md))

| Address | Name | Behaviour |
|---|---|---|
| `$C139`/`$C15E` | `vram_queue`/`tile_edit_queue` | Upload the `$0400`/`$0440` queues in the NMI. |
| `$C203` | `nt_fill` | Clear a nametable and its attributes while blanked. |
| `$C267` | `oam_clear` | Park OAM entries off screen. |
| `$C279`/`$C282` | `fade_in`/`fade_out` | Step the palette from `$0100`. |
| `$C301` | `chr_load` | Upload a CHR list while blanked. |
| `$C42C` | `row_chr_stream` | Stream the current row's sprite CHR and sprite palette 3. |
| `$C71D`/`$C78F` | `draw_start_screens`/`draw_screen` | Initial nametable draw. |
| `$C7C5` | `scroll_stream` | Camera movement to block steps. |
| `$C830` | `stream_block` | Queue one 32x32 block and its attribute byte. |
| `$C976` | `row_scroll` | Vertical row transition (static reading only). |
| `$CCBD` | `decode_block` | Block to tiles, attribute and collision. |
| `$CE2C` | `screen_ptr` | Screen id to `$22/$23`. |
| `$ED05` | `message_box` | Message box and its CHR tile swap. |
| `$EFE2` | `camera_update` | Follow Scrooge, clamp, stream. |
| `$F574` | `tile_edit` | Replace a metatile at an object's position. |
| `$F76D` | `scene_palette` | Load scene palettes. |
| bank 5 `$8000` | `build_oam` | Sprite builder (HUD groups, then slots, order flipped every frame). |
| bank 5 `$BAB0` | `draw_strings` | Direct text and HUD-frame drawing. |

### Game flow (details in [GAME-STATE.md](GAME-STATE.md), [LEVELS.md](LEVELS.md))

| Address | Name | Behaviour |
|---|---|---|
| `$C7B0` | `map_header` | Loads map rows, stride and grid pointer. |
| `$CF93` | `boss_trigger` | Boss-room entry and arena scroll. |
| `$D630` | `land_select` | Land Select screen, banking, cursor skipping taken lands. |
| `$D79B` | `land_load` | Loads a land. |
| `$D8D9` | `bank_money` | Moves HUD money into TOTAL. |
| `$D9CA` | `apply_land_tables` | Per-land tables indexed by `$F6`. |
| `$DB36` | `frame_loop` | Per-frame gameplay loop. |
| `$DC04` | `boss_checkpoint` | Sets `$7C`. |
| `$DC3E` | `death_or_warp` | Handles `$7E`. |
| `$DD4E` | `place_from_entry` | Places Scrooge from a `$DDC1` entry. |
| `$DEB9` | `game_over` | Game-over music, 240-frame wait, reset. |
| `$DF08` | `land_exit` | Back to Land Select after `$5D`. |
| `$DF27` | `scene` | Loads a `$78` scene. |
| `$E1AF` | `ending` | Selects and plays the ending variant. |
| `$E28C` | `timer_tick` | HUD time countdown and time-out death. |
| `$E88C` | `row_change` | Moves to the row above or below (static reading only). |
| `$F752` | `music_request` | Sets `$C5` and queues a sound. |
| bank 5 `$B760` | `reset_consumed` | Clears and pre-sets `$0120-$013F`. |
| bank 5 `$B800` | `land_select_message` | Chooses the Land Select message. |
| bank 6 `$8B21` | `pickup` | Dispatches on the touched object's type. |
| bank 6 `$AE20/$AE60/$AEA0` | `spawner` | Object spawn set-up and per-frame spawning. |

Evidence for each row is in the linked documents (traces, poke runs, static reading).

### Scrooge (details in [PLAYER.md](PLAYER.md))

| Address | Name | Behaviour | Evidence |
|---|---|---|---|
| `$D7EA` | `player_land_init` | Land-entry setup of Scrooge: start Y, jump take-off velocity (`$0790/$07A0`), walking speed. | Trace of land entry. |
| `$E3EC` | `jump_start` | On a new A press copies `$0790/$07A0` into slot 0's vertical velocity and sets `$39` = 1. The instruction after it (`$E3FE`) is the jump-scale hook site. | Trace of frame 1780; reference search over the ROM. |
| `$E354` | `player_update` | Per-frame entry: gravity into `$EF`, timers, then dispatch on mode `$39` through the table at `$E387`. | Code; every route passes through it. |
| `$F2A1` | `apply_y_velocity` | Y -= vertical velocity (X = slot). | Code and trace. |
| `$F2B5` | `apply_gravity` | Vertical velocity -= `$EF` (X = slot). | Code and trace. |
| `$F234`/`$F252` | `move_right`/`move_left` | Add/subtract horizontal speed to world X. | Code and trace. |
| `$D120` | `bg_probe` | Tests probe points against the collision class map at `$0580`. | Code and trace. |
| `$D202`/`$D291` | `object_contact_h`/`object_contact_v` | Scrooge-object contact tests; `$D202` starts knockback. | Code and trace. |
| `$EB5E` | `damage` | Marks the last full HP dot lost; on Easy only every other hit. | Code and dumps. |

## Bank 3

| Address | Name | Behaviour | Evidence |
|---|---|---|---|
| `$8000` | `sound_update` | Per-frame sound driver tick, called by the NMI with bank 3 mapped. | NMI code; executed 593 times in a 600-frame run (one per NMI). |
| `$8003` | `sound_play` | A = sound/music id, called by the NMI for each queued request. | NMI code at `$C110-$C127`. |
| `$823D` | `sound_tick` | Per-frame update: effect step, music step, APU output. | Static; runs. |
| `$86AC` | `music_step` | Advances one channel's music stream `$D7` times. | Static; simulator matches runs. |
| `$856B` | `sfx_step` | Effect stream step for one owned channel. | Static; runs. |
| `$8654` | `sfx_release` | Gives a channel back to the music. | Static. |
| `$8139`/`$8192` | `sfx_stop_all`/`music_stop` | Handlers for ids `$FE`/`$FF`. | Static; `$FF` runs. |
| `$8228` | `channel_mute` | Silences one APU channel. | Static. |
| `$88D9` | `load_instrument` | Copies instrument bytes into the channel record. | Static. |

Fixed bank: `$F754` `sound_request` queues A in the ring when the slot is free (dropped otherwise); `$F752` also records the music id in `$C5`. See [AUDIO.md](AUDIO.md) for the full catalogue of 56 ids.

## Why static discovery needs help

Two patterns hide code from NESRecomp's static discovery:

1. Tasks resume by returning into a stack saved by `task_yield`, and start through `JMP ($0093)`.
2. Code in a switchable bank is only called after `bank_select`, and the compiler does not know
   which bank the `$8000-$BFFF` window holds at a call site.

Both are handled by seeds: `config/game.toml` names `main_task`, and `config/ducktales.seeds`
lists the bank-qualified instruction addresses that the route runs executed (see
[VALIDATION.md](VALIDATION.md)). Code no route has reached still runs correctly on the
interpreter.
