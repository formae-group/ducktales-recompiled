# Mods

DuckTales Recompiled loads mods from `.dtmod` archives and from loose folders. A mod can replace
tiles, palettes, music and sound effects, and it can run a Lua script. Asset mods change only what
the window shows and what the speakers play. They never write to the machine, so its state, its
own picture and every verified hash stay the console's (`tests/mods_test.py` checks the per-frame
hashes with every kind of asset loaded). Scripts read the game's state, can cancel the cancelable
game events and can set the few fields the game declares writable; there is no free RAM write.

Mods are **off by default**. With mods off, or with no mod enabled, nothing is loaded and the game
runs exactly as the console does.

## Using mods

1. Put `.dtmod` files, or folders that hold a `mod.toml`, in the mods folder:
   `%APPDATA%\DuckTales Recompiled\mods` (`DUCKTALES_DATA_DIR\mods` when that variable is set). The
   game creates the folder on its first start.
2. Open the menu (Escape) and go to **Settings, Mods**. Switch **Enable mods** on, tick the mods you
   want and put them in order. Each mod shows its version and author, and *folder* or *script*
   when it is a loose folder or runs a script. **Open mods folder** shows the folder, **Rescan**
   reads it again and **Open mods log** opens `mods.log` (see below).
3. Restart the game to apply changes to which mods load, their priority, or their source folders
   and archives. The page asks you to restart when the selection differs from what is running;
   undoing a change clears the prompt. Edited assets inside an already loaded mod still need a
   restart even if the selection hasn't changed.

Mods higher in the list win: when two mods replace the same tile, palette or sound, the one nearer
the top is used. A mod that cannot be loaded stays in the list with the reason under its name, in
red, and its checkbox is off: it was made for another game or another PRG, the archive cannot be
read, its mod.toml is invalid, one of its files is invalid, or it has two files for the same tile,
palette or sound. **Rescan** checks the files of every mod that is not loaded yet, so a broken new
mod shows its reason before you restart. A mod whose script stopped is named under the list with
the reason (see [Scripts](#scripts)).

With mods on, the game writes what it loaded, every refused mod with its reason, and the scripts'
`dt.log` lines, toasts and errors to `mods.log` in the data folder, emptied
at each start and capped at 4 MiB. The same lines go to the console when the game runs from one.
Attach that file to a bug report about a mod.

The settings live in `config.ini`:

```ini
[Mods]
Enabled = on
Order = hd-hud:on,purple-amazon:off,title-remix:on
```

`Order` lists the mods by id, top first. New mods join the bottom of the list, switched on. A
refused copy cannot take another mod's saved position or switch.

The command line selects mods without the menu, in the window and in the headless host. Its
switch and order override `config.ini` for that run without changing the saved values. Off-screen
runs do not scan the player's mods folder, so pass `--mods-dir` explicitly.

| Option | Meaning |
|---|---|
| `--mods on\|off` | Load mods or not (the window's default is `[Mods] Enabled`; headless runs default to off) |
| `--mods-dir DIR` | Another folder to scan (repeatable, up to 4); a path that is not a folder is an error |
| `--mods-order ID:on\|off,...` | The order and switches, as `[Mods] Order` |
| `--mods-dump DIR` | Writes every tile drawn, once, as a replacement template, even with mods off or without `--present-out` in a headless run. Creates missing parent folders and reports a write failure before stopping the dump |

## Making a mod

A mod is a zip archive renamed to `.dtmod`, or a folder with the same layout (handy while you
work on it):

```text
my-mod.dtmod
  mod.toml
  tiles/        8x8 PNG replacements, named by tile key
  palettes/     global.pal, or one per land
  music/        OGG Vorbis or WAV, named by music request id
  sfx/          OGG Vorbis or WAV, named by effect request id
  scripts/
    main.lua    optional, see Scripts
```

The files may sit in one folder inside the archive, next to `mod.toml`. Subfolders under
`tiles/` are allowed (`tiles/hud/...`). Other files are ignored.

### mod.toml

```toml
id = "purple-amazon"
name = "Purple Amazon"
version = "1.0.0"
author = "You"
description = """The Amazon at dusk."""
game = "DuckTales (USA)"
prg_sha256 = "8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013"
```

`id` is 1 to 63 characters of `a-z`, `0-9`, `-`, `_` and `.`, starting with a letter or a digit,
and it is what `[Mods] Order` saves. `id`, `name`, `version`, `game` and `prg_sha256` are required;
`author` and `description` are optional. These seven keys are quoted strings and each may be set
once; other keys and tables are ignored. `prg_sha256` is the SHA-256 of the PRG ROM the mod was
made for, in 64 hex digits: tile keys and sound ids belong to that program, so a mod for another
PRG is refused.

A mod.toml that breaks these rules is listed with the reason and the line: *mod.toml is missing
version, prg_sha256*, *mod.toml line 3: version must be a quoted string (version = "...")*,
*mod.toml line 5: name is set twice*. The file may start with a UTF-8 byte order mark (Notepad
writes one), and so may `scripts/main.lua`. A NUL byte is refused, rather than silently ending
the manifest before the rest of the file. File and folder names inside a mod are matched
whatever their case (`Scripts/Main.lua`, `MOD.TOML`), as Windows matches them.

### Tiles

A tile is addressed by its content, not by where it sits in CHR: the key is a 64-bit hash of the
tile's 16 CHR bytes in 16 hex digits, optionally followed by `_` and the palette it is drawn with in
8 hex digits (backdrop and the three colours of its palette entry, one byte each).

- `tiles/<hash>_<palette>.png` replaces the tile drawn with that palette.
- `tiles/<hash>.png` replaces the tile with any palette. A palette-specific file wins over it.

Run the game with `--mods-dump DIR` and play to the place you want to change: `DIR`
fills with `<hash>_<palette>.png`, one per tile and palette drawn, each the tile as the console
shows it. Edit a copy and put it in `tiles/`. Replacements are 8x8 for now. Pixels with alpha
below 128 show the original pixel, so a replacement can change part of a tile.

### Palettes

A `.pal` file holds 64 RGB triples (192 bytes), one for each NES colour, or 512 (1536 bytes) for
every colour and emphasis combination. `palettes/global.pal` applies everywhere.
`amazon.pal`, `transylvania.pal`, `african-mines.pal`, `himalayas.pal` and `moon.pal` apply in
that land only and win over `global.pal` there. On the title and Land Select screens only
`global.pal` applies.

### Music and effects

`music/<id>.ogg` or `.wav` replaces music request `<id>` (00-0F), `sfx/<id>.ogg` or `.wav`
replaces effect request `<id>` (10-37). `<id>` is two hex digits and may be followed by a dash
and any name: `music/07-title.ogg`, `sfx/13-swing.wav`. The ids are listed in
[docs/audio.md](audio.md). WAV files are PCM, 8 or 16 bits, mono or stereo. Music loops from the
file's own loop start (the OGG `LOOPSTART` comment, a WAV `smpl` loop), or from the beginning.

While a replacement plays, the sound driver's channels for that request are muted in the mix. The
APU still runs them, so the machine is unchanged. A replacement fades in over 5 ms when it starts
and out over 5 ms when it is stopped or the game pauses, and the muted channels fade the same way,
so no file has to start or end at silence to avoid a click. The native player keeps the sum under
full scale with its limiter ([audio.md](audio.md#output-stage)); a file at full scale over the
game's effects is turned down there instead of clipping.

## Scripts

A mod may ship `scripts/main.lua`. It runs in Lua 5.4 when the mod loads, in a sandbox of its
own, and registers handlers with `dt.on`:

```lua
dt.on("timer-tick", function()
    if dt.state.land == 0 then
        return dt.CANCEL -- time stands still in the Amazon
    end
end)

dt.on("enemy-defeated", function(e)
    dt.log("beaten", e.kind, "at", e.x, e.y)
end)

dt.on("frame", function(frame)
    if frame % 600 == 0 then
        dt.log("money", dt.state.money)
    end
end)
```

### The dt table

| Name | What it is |
|---|---|
| `dt.on(event, fn)` | Calls `fn` at every `event`, in the order handlers were added; mods higher in the list run first. A game event's `fn` gets a table: `e.name` and the event's values |
| `dt.CANCEL` | Returned by a handler of a cancelable event: the event is cancelled. Returned for an event that cannot be cancelled, it is ignored and the log says so once |
| `dt.state` | The game's state, read only, inside handlers (fields below) |
| `dt.set.<field>(value)` | Sets a writable field at the frame's end, inside handlers (see [Writing the game](#writing-the-game)) |
| `dt.objects()`, `dt.camera()` | The live object slots and the camera, read only, inside handlers (see [Objects and the camera](#objects-and-the-camera)) |
| `dt.toast(text)` | Shows a short notice over the picture; returns `false` when it was refused (see [Toasts](#toasts)) |
| `dt.draw.text(x, y, text[, color])`, `dt.draw.rect(x, y, w, h, color)` | Draws over the presented picture for one frame (see [Drawing](#drawing)) |
| `dt.store.get(key)`, `dt.store.set(key, value)` | The mod's own saved values (see [Store](#store)) |
| `dt.log(...)` | Writes a line to `mods.log` and the console (`print` does the same) |
| `dt.frame()` | Frames since the game started |
| `dt.mod` | The mod's id |

The events are the game's events (`include/ducktales/events.h`) and `frame`. A game event's
handler gets a table with the event's `name` and the values below (`function(e) ... e.slot ...`):

| Event | When | Values | Cancel |
|---|---|---|---|
| `frame` | After every machine frame; the handler gets the frame number | | |
| `player-damaged` | A hit is about to cost an HP dot | | No dot is lost (the knock-back still happens) |
| `life-lost` | A death is about to cost a life | | The lives count stays |
| `timer-tick` | A frame of the HUD timer elapses | | The HUD second does not advance |
| `boss-hit` | A hit is about to count against a boss | | The hits left stay |
| `boss-defeated` | The hit that counts is the boss's last (after the `boss-hit` handlers, and not when one of them cancelled) | `land` | Not cancelable |
| `enemy-defeated` | An enemy is beaten | `slot`, `kind`, `x`, `y` | Not cancelable |
| `item-collected` | Scrooge takes a pickup (an item, a diamond, a treasure) | `slot`, `item`, `x`, `y` | Not cancelable |
| `land-start` | A land was loaded (from Land Select, or another land sent Scrooge there); not after a death | `land` | Not cancelable |
| `land-clear` | The land is left with its "clear" result (not by the Launchpad ride home) | `land` | Not cancelable |
| `checkpoint` | Scrooge reached his land's boss checkpoint | `land` | Not cancelable |
| `money-changed` | The HUD money differs from the last frame that showed it | `money`, `delta` | Not cancelable |
| `new-game` | The title's set-up reset the HUD | | Not cancelable |
| `jump-start` | A jump copied the take-off velocity into Scrooge's | | Not cancelable |

`slot` is the object's slot (1-15), `kind` its type (`$0710`), `item` the pickup's item
(`$0620`; the land treasures are `$6F` and up, see [game-state.md](game-state.md)), `x` its world X
and `y` its Y on the screen, as `dt.state.x` and `y` are Scrooge's. `land` is 0 (The Amazon) to 4
(The Moon). `money-changed` gives the new `money` and the `delta`; several additions in one frame,
and the time bonus counted at the end of a land, come as one event per frame that changed it.

`land-start`, `land-clear`, `checkpoint` and `money-changed` are seen at the end of the frame,
before the `frame` handlers; the others run at the moment the game does it, inside the frame. Every
event fires once per occurrence with the decompiled code on or off.

`dt.state` fields: `land`, `lives`, `hp`, `hp_max`, `money` (`nil` while the HUD digits are
not drawn), `time` (the HUD's 0-999), `difficulty`, `paused` (boolean), `boss_hits`,
`treasures` and `key_items` (bit masks), `pad` and `pad_new` (controller 1, held and newly
pressed), `music`, `mode` (Scrooge's mode), `invulnerable` (frames left), `x` (Scrooge's world
X) and `y` (his Y on screen). Their addresses and meaning are in
[docs/memory-map.md](memory-map.md) and [docs/game-state.md](game-state.md). Reading an unknown
field is an error.

A handler that only reads, logs, toasts or uses the store leaves the machine exactly as it was
(`tests/mods_test.py` compares the per-frame hashes). Cancelling an event or `dt.set` changes the
game, as the matching enhancement does.

### Objects and the camera

`dt.objects()` returns an array with a table for every live object slot (Scrooge is slot 0):
`slot`, `kind`, `action` (for a pickup, its item), `state`, `x` (world X), `y` (Y on the screen)
and `screen_x` (X on the screen). `dt.camera()` returns `x` (the world X of the camera's left
edge), `row` (the map row) and `screen` (the screen index, row * 8 + page). Both read the game
when they are called, only inside handlers, and change nothing. The object tables are in
[docs/memory-map.md](memory-map.md).

```lua
dt.on("frame", function()
    for _, o in ipairs(dt.objects()) do
        if o.slot ~= 0 then
            dt.draw.rect(o.screen_x, o.y - 2, 8, 1, 0xFFFF00) -- a mark over every object
        end
    end
end)
```

### Drawing

`dt.draw.rect(x, y, w, h, color)` fills a rectangle and `dt.draw.text(x, y, text[, color])` writes
text in the toast font (3x5 capitals, 4 pixels a character, the characters and `?` rule of
[Toasts](#toasts), at most 64 characters), at picture pixels (256 x 240, the top left is 0, 0).
`color` is `0xRRGGBB`; text is white without one. What the handlers draw during a frame is shown
over the next presented picture, after the mods' tiles and palettes and under the toast, and then
forgotten, so a script draws again every frame for something to stay. A mod may draw 64 things a
frame: past that the call returns `false` and the log says so once. Drawing clipped by the picture's
edge is fine. Like toasts, drawings never reach the machine's picture, screenshots taken from it,
or any hash (`tests/mods_test.py` compares the per-frame hashes with a drawing script).

### Writing the game

`dt.set` holds one function per field the game declares writable, and no others:

| Field | Values | What changes |
|---|---|---|
| `lives` | 0 to 9 | The lives count (`$0361`, also the HUD digit) |
| `hp` | 0 to `hp_max` | The HUD's HP dots: the first `hp` full, the rest lost |
| `money` | 0 to 9,999,990, a multiple of 10, while the HUD shows money | The money digits, leading zeros blank as the game draws them |
| `time` | 0 to 999 | The HUD time digits |

The value is checked when `dt.set` is called, and a value out of range is an error (catchable
with `pcall`). Calls are only allowed inside handlers, and they are queued: the writes happen at
the end of the frame, after the `frame` handlers, through the game's RAM transaction (the same
helpers the enhancements use), so `dt.state` shows the new value from the next frame on. The
value is checked again then, and a write the state no longer allows (the HUD stopped showing
money) is dropped with a line in the log. **Every
write is a gameplay change**: the machine then differs from the console's, like an enhancement
that is on. A mod whose script stopped has its queued writes dropped.

### Toasts

`dt.toast(text)` shows a notice with the mod's name and `text` near the top of the picture for 3
seconds (180 frames). At most one per mod per second is shown (a refused call returns `false`), a
newer notice replaces the one showing, and `text` is cut to 64 characters (UTF-8 code points;
control characters become spaces). The picture's font is small: it draws `A`-`Z` (lower case in
capitals), `0`-`9`, the space and `- . : / ( ) = + ! ? , '`, any other character as `?`, and the
first 61 characters of a line, ending in `...` when the text is longer. The log keeps the text as
written. The notice is drawn with
the hosts' overlay font (`cyc_overlay`) over the presented picture only, after the mods' tiles and
palettes: it never reaches the machine's picture, screenshots taken from it, or any hash. It is also
written to the log.

### Store

`dt.store.set(key, value)` keeps a string, number or boolean under `key` (1 to 64 characters of
`A-Z`, `a-z`, `0-9`, `_`, `-` and `.`); `nil` removes it. `dt.store.get(key)` returns it, or
`nil`. Each mod has its own store of at most 64 KiB, kept in the data folder as
`mods-data/<id>.ini`. It is written when it changed, at most once a second and when the game
exits, and it survives a hot reload. A write goes to `<id>.ini.tmp` first and then takes the old
file's place, so a crash or a full disk never leaves half a store; a write that fails is logged
once and tried again a second later. Keys of a store file past 64 KiB are dropped with a line in
the log. Scripts never see a path: the file is named by the mod's id,
which can only hold `a-z`, `0-9`, `-`, `_` and `.`, and a mod whose id is a Windows device name
(`con`, `nul`, `com1`, ...) has no store.

### The sandbox

- Only the base, `string`, `table`, `math`, `utf8` and `coroutine` libraries are there.
  `io`, `os`, `package`, `debug`, `require`, `dofile`, `loadfile` and `string.dump` are absent;
  the `io`, `os`, `package` and `debug` libraries are not even built into the game.
- `load` reads text only: a binary chunk is refused, and `scripts/main.lua` must be text too.
- A callback, and the script's first run, may run 1,000,000 Lua instructions. A mod's Lua state
  may hold 16 MiB.
- An error, or a limit reached, stops that mod's script: its handlers and queued writes are dropped, the reason is
  written to the log and shown in the Mod Menu, and the game and the other mods go on. `pcall`,
  `xpcall`, `coroutine.resume` and `coroutine.close` do not catch a limit.
- The reason names the line and where it ran: *scripts/main.lua:12: attempt to index a nil value
  (field 'x') (in a "frame" handler)*, *scripts/main.lua:3: ran more than 1000000 instructions in
  one callback (in a "timer-tick" handler)*, or *(when the script started)* for the script's first
  run. The memory limit has no line: *used more than 16 MiB of memory*.
- The instruction limit counts Lua instructions. A single long call into the C library, such as a
  pathological string pattern, is not interrupted.

### Hot reload

About once a second the game checks `scripts/main.lua` of a folder mod, or the `.dtmod` archive.
When its write time or size changed, the script starts again from scratch with the new text, and
the log says *script reloaded*. A script that stopped with an error also starts again once you
save a fix. Only the scripts of mods loaded at start reload: a mod that had no
`scripts/main.lua` then, and tiles, palettes and sounds, need a restart.

## How it works

`src/mods/` is split so the game-agnostic part can become a library of its own:

| Folder | What it holds |
|---|---|
| `core/` | Knows no game. `fm_mods.h` is its interface: the package format and catalogue (`fm_catalog.c`: folders, `mod.toml`, order, switches, restart check), the archive and folder reader (`fm_archive.c`), PNG, OGG, WAV and deflate on stb (`fm_codec.c`) and the Lua sandbox with the generic script API (`fm_script.c`: `dt.on`, cancel, `dt.state` and `dt.set` over fields the game describes, toast, store, log, limits, hot reload) |
| `nes/` | NES games: tile keys, the replacement tile table, `.pal` palettes and tile templates (`fm_nes.c`) |
| `ducktales/` | This game: what the assets mean and how they are painted and mixed (`dt_mods.c`, `dt_mods_audio.c`), the event list, the `dt.state` and writable fields, the store folder and the toast drawing |
| `app_mods.h` | The interface the hosts and the menu call |

Both hosts and the native player call the same entry points:

- **Picture.** After the stock compositor (`host/ducktales_sprites.c`) draws the frame, the native
  renderer runs again on the same video memory with a per-pixel source (tile, row, column,
  palette), and every pixel whose colour is still the console's is repainted from the mod's tile or
  palette. The HUD split repair (`ui/app_present.c`) uses the mod's palette for the pixels it
  redraws, but not its tiles.
- **Sound.** At the end of each frame the sound driver's request ids in RAM start and stop the
  replacements; the mixer mutes the channels the driver gave the request and adds the decoded file.
- **Scripts.** Each mod gets its own `lua_State` with a capped allocator and a count hook. A game
  event's hook site is on only while a handler for it exists, as the enhancements do; the events
  seen at the frame's end read RAM only while one has a handler. At the end of each frame
  (including the frames the open menu runs) those events are raised, then the `frame` handlers run,
  the queued `dt.set` writes are applied in one RAM transaction and the frame's drawings replace
  the shown ones.

The tests are in `tests/mods_test.py`, `tests/mods_catalogue_test.c` and `tests/window_smoke.py`.
They generate their fixtures in scratch folders, and the tile templates come from the player's own
ROM; no game content is committed.
