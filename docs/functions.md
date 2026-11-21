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
| `$C000` | `nmi` | See [rendering.md](rendering.md) and [audio.md](audio.md). | Static reading. |
| `$C1AB` | `read_controllers` | Strobes `$4016` and shifts 8 bits from each port. | Static reading; RAM correlation in [memory-map.md](memory-map.md). |
| `$D4E6` | `main_task` | Entry point of the first task. Static discovery cannot reach it (it is only reached through `JMP ($0093)`), so it is listed in `config/game.toml`. | Static reading of `reset`; first interpreter miss in the unseeded build. |

### Scrooge (details in [player.md](player.md))

| Address | Name | Behaviour | Evidence |
|---|---|---|---|
| `$D7EA` | `player_land_init` | Land-entry setup of Scrooge: start Y, jump take-off velocity (`$0790/$07A0`), walking speed. | Trace of land entry. |
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

## Why static discovery needs help

Two patterns hide code from NESRecomp's static discovery:

1. Tasks resume by returning into a stack saved by `task_yield`, and start through `JMP ($0093)`.
2. Code in a switchable bank is only called after `bank_select`, and the compiler does not know
   which bank the `$8000-$BFFF` window holds at a call site.

Both are handled by seeds: `config/game.toml` names `main_task`, and `config/ducktales.seeds`
lists the bank-qualified instruction addresses that the route runs executed (see
[validation.md](validation.md)). Code no route has reached still runs correctly on the
interpreter.
