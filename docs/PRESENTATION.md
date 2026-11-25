# Presentation

How the window shows the machine's picture: frame pacing, the HUD split line, overscan, scaling,
pixel aspect and DPI. All of it is presentation. The machine, its 256x240 picture, `--hash-out`,
`--screenshot` and `--frame-log` never see it, and the headless build does not link it. Measured
on 2026-11-21 on the development laptop (Windows 11, Intel UHD + RTX 3050, 1920x1080 panel at
144 Hz, 125% display scaling).

| What | Where |
|---|---|
| Settings, `[Game]` keys, the picture | `src/ui/app_present.[ch]` |
| DPI awareness, renderer | `src/host/ducktales_main.c` |
| Window size, menu background, VSync at launch | `src/ui/app_settings.c`, `src/ui/app_ui.cpp` |
| Measuring | `tools/pacing_report.py`, `tools/scroll_edges.py` |

## Settings

| Settings row | `[Game]` key | Values | Default |
|---|---|---|---|
| Pixels | `Pixels` | `nearest`, `sharp`, `smooth` | `nearest` |
| Pixel aspect | `PixelAspect` | `square`, `8:7` | `square` |
| Crop overscan | `Overscan` | `off`, `top-bottom` (8 lines each), `all` (also 8 columns each side) | `off` |
| HUD split line | `HudSplit` | `clean`, `original` | `clean` |
| Frame pacing | `FramePacing` | `auto`, `vsync`, `steady` | `auto` |

The keys live in `[Game]` because the host rewrites `[Display]` from its own list of keys
whenever it saves. Everything but Frame pacing applies at once.

## How it attaches to NESRecomp

Only public interfaces, no change to the submodule:

- `CycHostExtras.present` (window build only) returns the picture the window shows. It starts from
  `cyc_render_present`, so a compositor the game installs with `cyc_render.h` (such as a
  stock-width sprite compositor) is already in it, and edits a copy.
- `CycHostExtras.present_rate = 60` with `present_from_snapshot` and no `present_phase`: the
  window then holds each picture until one frame period after the previous one while VSync is off
  (`cyc_sdl.c`, "Steady pictures"). Without `present_phase` it draws no in-between pictures and
  never runs the machine on a second thread, so `present_from_snapshot` promises nothing that is
  ever used. With VSync on the host never holds.
- `[Display] VSync` is read by the host only at start. `app_ui_configure` runs before the host
  reads `config.ini` and writes the VSync that `FramePacing` asks for when it differs.
- SDL hints set in `main` before any window: `SDL_WINDOWS_DPI_AWARENESS=permonitorv2` and
  `SDL_RENDER_DRIVER=direct3d11` (the environment variable still overrides it). They are set
  again after the first-run screen, because `SDL_Quit` clears hints.

## Frame pacing

The machine runs at 60.0988 Hz on its own deadline (`cyc_frame_wait.h`), whatever the display
does. The window emulates a frame and presents its picture right away, then waits for the next
deadline. A frame takes 5 to 17 ms to emulate here, so without a hold the moment a picture
reaches the display moves by that much from frame to frame.

Method: `NESRECOMP_PACING_LOG` and `CYC_PACING_LOG` on the window build, `full_amazon` with
`--window-input` for 1,500 frames at 3x, the first 120 left out, summarised with
`tools/pacing_report.py`. "Irregular" is that tool's model of a display without variable refresh:
the share of pictures shown for a number of refreshes other than 2 or 3 at 144 Hz (1 at 60 Hz).
These are application-side times (when the picture was handed to the swap chain). PresentMon,
which would read the display side, needs administrator rights and was not used.

144 Hz:

| Renderer, mode | Presents skipped | Interval sd | Min / max | Irregular at 144 Hz |
|---|---|---|---|---|
| Direct3D 9, VSync on (the previous default) | 9 and 4 (two runs) | 2.00 ms | 7.3 / 54.4 ms | 1.4-2.0% |
| Direct3D 9, VSync off | 0 | 1.42 ms | 9.7 / 28.9 ms | 0.8% |
| Direct3D 9, steady hold | 0 | 0.86 ms | 8.1 / 24.2 ms | 0.2% |
| Direct3D 11, VSync on | 0 | 1.69 ms | 10.6 / 23.0 ms | 0.8% |
| Direct3D 11, steady hold | 0 | 0.57 ms | 14.3 / 21.4 ms | 0.0% |
| This build, Auto (Direct3D 11, steady) | 0 | 0.44 ms | 15.1 / 21.1 ms | 0.0% |

The same panel switched to 60 Hz for the measurement (and back):

| Renderer, mode | Presents skipped | Interval sd | SDL_RenderPresent mean / max | Rate |
|---|---|---|---|---|
| Direct3D 9, VSync on | 39 | 2.96 ms | 4.9 / 29.6 ms | 58.39 fps |
| Direct3D 9, steady hold | 0 | 0.75 ms | 0.8 / 6.1 ms | 60.11 fps |
| Direct3D 11, VSync on | 1 | 1.76 ms | 0.5 / 22.3 ms | 60.10 fps |
| Direct3D 11, steady hold | 0 | 0.57 ms | 0.4 / 1.2 ms | 60.09 fps |

What this shows:

- SDL's first choice on Windows is Direct3D 9 (`render_drivers[]` in SDL 2.30). Its VSync
  presents block for up to 30 ms, the host then skips pictures to keep the machine on time
  (`cyc_frame_pacing.h`): 39 dropped pictures in 23 s at 60 Hz, 4 to 9 at 144 Hz. These are
  visible hitches. Direct3D 11 in SDL 2.30 uses a flip-model swap chain with a frame latency of
  1, and presents without VSync with `DXGI_PRESENT_DO_NOT_WAIT` and without
  `DXGI_PRESENT_ALLOW_TEARING`, so turning VSync off cannot tear in a window, in borderless full
  screen or in exclusive full screen (SDL does not switch DXGI to exclusive mode).
- At 144 Hz no VSync mode can show 60.1 pictures a second evenly: the best possible is the 2-3
  refresh cadence. The steady hold keeps the hand-over within 0.5 ms of a fixed phase, so that
  cadence holds; with the emulation jitter in the hand-over (VSync on) about 1 picture in 100
  lands a refresh early or late. A variable-refresh display (G-Sync, FreeSync) shows the steady
  pictures exactly at 60.1 Hz.
- At 60 Hz a VSync present waits in the swap chain's queue, so each picture gets exactly one
  refresh and only the 0.1 Hz difference between the two clocks costs a picture every 10 s or
  so (1 in 23 s measured). Without VSync the hand-over phase drifts through the refresh and the
  remaining jitter makes it alternate around the crossing (the model gives 2%).
- So Auto picks VSync on a display of up to 70 Hz and the steady hold above (the refresh rate of
  display 0, where the host opens the window, from `SDL_GetDesktopDisplayMode`; unknown counts as
  60 Hz). VSync and Steady force one or the other.

### Unfocused window (native player)

When its window is not focused, Windows lowers a process: no foreground boost, power throttling
(EcoQoS, which may move it to the E-cores of a hybrid CPU such as the i5-13450HX on the Balanced
plan) and coarser timers. Frames then run late, the audio queue runs dry and the sound crackles, as
reported on the cycle build. The native player (`src/native/native_window.c`) needs far less CPU per
frame and also guards against it:

- At start it turns power throttling off for the process (`SetProcessInformation`,
  `ProcessPowerThrottling` with `EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION` in the control mask and a
  zero state mask), registers the frame-loop thread with MMCSS in the `Games` class
  (`AvSetMmThreadCharacteristicsW`) and holds a 1 ms timer resolution (`timeBeginPeriod(1)`); all
  three are released at exit.
- The audio queue is kept near two device buffers and a frame by resampling frames by at most 0.5%
  (2% when under half a buffer is left), and the sound fades out instead of running dry when the
  frame loop stops on purpose (the menu pausing the game, fast-forward, a window move or resize); see
  [AUDIO.md](AUDIO.md#output-stage).

`ducktales_cycle.exe` has neither. Not verified by ear or with an unfocused window on that machine.

## DPI scaling

Without a DPI awareness declaration Windows draws the window at 96 dpi and stretches it by the
display scaling with a bilinear filter. On this laptop (125%), queried with
`GetWindowDpiAwarenessContext` and `GetClientRect` on a 3x window:

| Build | Awareness | DPI | Client area for a 768x720 picture |
|---|---|---|---|
| main (34337a5) | unaware | 96 | 960x900: every NES pixel 3.75 screen pixels wide, blurred |
| this one | per monitor | 120 | 768x720: exactly 3x3 |

The window sizes are now physical pixels: 3x is 768x720 on this screen, as on a 100% display.

## Edges and overscan

`tools/scroll_edges.py` on frame logs of the five full routes (frames 400 to the end; Amazon,
Himalayas, Transylvania, Moon, African Mines, about 47,000 frames, 16,000 of them with the camera
moving) found no tile or attribute garbage at any edge. The rate of pixels that changed although
they were already on screen in the previous frame is lower in the 8 columns at either edge
(0.001-0.006) and in the bottom 8 rows (0.000-0.001) than in the middle of the picture
(0.008-0.017, sprites and animations). The game writes new columns outside the visible 256 pixels
(vertical mirroring gives it 512), and the vertical transitions (`$E9`, up to 29 frames in a row
in African Mines) showed nothing either. It shows the left 8 columns (`$2001 = $1E`) and they are
clean. Lines 0-7 are the HUD's black top border; lines 232-239 are playfield.

So the overscan crop is off by default. `top-bottom` gives the 256x224 a television showed and
`all` also takes 8 columns off each side (240x224).

## HUD split line

The one edge defect is at the HUD split (docs/RENDERING.md, "Sprite-0 split"). Below the HUD
the playfield's first line is line 47, and the PPU starts it with the playfield's scroll but in
tile row 5 of the nametables, which in nametable 0 is the HUD's bottom row. So wherever the camera
covers nametable 0 that line is black, and where it covers nametable 1 it shows sky or terrain:
a one-line step whose position moves with the camera every frame (x = 59, 60, 62, 63 ... in
frames 2990-3009 of `full_amazon`). The game's scroll writes also land near the end of line 46
(the HUD box's bottom edge), shifting its last pixels by the new fine X and ending it with up to
7 pixels of playfield. And the split sprite (OAM 0, tile `$21` at X 8, Y `$2C`, behind the
background) shows as two white pixels at x 8-9 on line 46, where the HUD is backdrop.

`HudSplit = clean` (default): the first line whose scroll, control or mask at its first dot
differs from the line above, within a few lines under a behind-background sprite 0, is drawn in
the backdrop colour; the line above it is drawn again from the nametable with its own starting
scroll (`cyc_render_line_scroll_x/y`, `cyc_render_nametable`, `cyc_render_chr`), and sprite 0's
opaque pixels over the backdrop become backdrop. Other screens (title, Land Select, the map)
have no such sprite 0 and are untouched. Checked in Amazon, Himalayas, Transylvania, African
Mines, the Moon, the title screen and Land Select. The two lines it redraws show no sprite, and
no sprite replacement of a mod either (docs/MODS.md "Sprites"): the compositor draws those as
sprites, before this step.

## Scaling and pixel aspect

The host draws the picture with SDL's logical size: nearest (or bilinear with `[Display]
LinearFilter`, read only at start), whole multiples with integer scaling. At a size that is not a
whole multiple nearest gives some pixels one more screen column than others, and the pattern moves
as the picture scrolls. NESRecomp's window has no sharp filter or shader for a plain picture
(`cyc_hd_gpu` is for HD packs), but `present` may return a picture of any size up to 8192, and the
host shows it at its logical size. So:

- `nearest` with `square` pixels: the (cropped) 256-wide picture goes to the host as before.
- `sharp`, `smooth`, or `8:7`: the picture is resampled here to the size it will cover in the
  drawable (aspect kept, a whole vertical factor with integer scaling), and the host draws it 1:1.
  `sharp` gives each output pixel the area average of the source pixels it covers, so whole
  pixels stay solid and only the one output pixel on a boundary blends (the sharp-bilinear look);
  with integer scaling and square pixels it equals nearest. `smooth` is bilinear. `8:7` widens
  every pixel by 8/7, the shape an NTSC television gives the NES picture.
- Cost at 1920x1080 borderless full screen, `sharp` + `8:7` + top-bottom crop: 3.5 ms of drawing
  and hold per picture against 1.3 ms for nearest, picture interval sd 0.85 ms against 0.53 ms.
- Because the host sizes its window from the picture when it leaves full screen (F11), the window
  is set back to the 1x picture times the window scale on that transition (`app_ui_frame_begin`).
  Checked over the TCP `key` command with `sharp` and `8:7`: 879x720 -> 1920x1080 -> 879x720.
- The menu draws the same picture behind itself, so opening it no longer changes the picture's
  size or shape.

Captures: `--present-out` in the window build saves what the window shows, for example

```sh
build/windows/Release/ducktales.exe roms/baserom.nes --hidden --uncapped --config captures/p.ini \
    --window-input tests/routes/full_amazon.txt --exit-after 3001 --present-out captures/p.png --present-every 3000
```

## Not verified

- How the picture looks on the 144 Hz panel to a person: smoothness, that VSync off never tears
  (by the API it cannot), exclusive full screen, a variable-refresh display, a real 60 Hz monitor.
  The times above are application-side; only a person at the screen (or PresentMon with
  administrator rights) can confirm the display side.
- Black frames at start, a flash when switching full screen, menu open and close: nothing showed
  in the captures, but captures are not the screen.
- Sprites other than sprite 0 on the redrawn HUD line are not drawn (Scrooge in the HUD band at
  that line's height): not seen in any route.
