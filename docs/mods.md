# Mods

DuckTales Recompiled loads mods from `.dtmod` archives and from loose folders. A mod can replace
tiles, palettes, music and sound effects, and it can run a Lua script. Asset mods change only what
the window shows and what the speakers play. They never write to the machine, so its state, its
own picture and every verified hash stay the console's (`tests/mods_test.py` checks the per-frame
hashes with every kind of asset loaded). Scripts read the game's state and can cancel the
cancelable game events. They cannot write RAM.

Mods are **off by default**. With mods off, or with no mod enabled, nothing is loaded and the game
runs exactly as the console does.

## Using mods

1. Put `.dtmod` files, or folders that hold a `mod.toml`, in the mods folder:
   `%APPDATA%\DuckTales Recompiled\mods` (`DUCKTALES_DATA_DIR\mods` when that variable is set). The
   game creates the folder on its first start.
2. Open the menu (Escape) and go to **Settings, Mods**. Switch **Enable mods** on, tick the mods you
   want and put them in order. **Open mods folder** shows the folder and **Rescan** reads it again.
3. Restart the game. Mods load once, at start, so the page shows *Restart DuckTales to apply these
   changes* until you restart.

Mods higher in the list win: when two mods replace the same tile, palette or sound, the one nearer
the top is used. A mod that cannot be loaded stays in the list with the reason under its name, in
red, and its checkbox is off: it was made for another game or another PRG, the archive cannot be
read, or one of its files is invalid. A mod whose script stopped is named under the list with the
reason (see [Scripts](#scripts)).

The settings live in `config.ini`:

```ini
[Mods]
Enabled = on
Order = hd-hud:on,purple-amazon:off,title-remix:on
```

`Order` lists the mods by id, top first. New mods join the bottom of the list, switched on.

The command line selects mods without the menu, in the window and in the headless host:

| Option | Meaning |
|---|---|
| `--mods on\|off` | Load mods or not (the window's default is `[Mods] Enabled`; headless runs default to off) |
| `--mods-dir DIR` | Another folder to scan (repeatable) |
| `--mods-order ID:on\|off,...` | The order and switches, as `[Mods] Order` |
| `--mods-dump DIR` | Writes every tile drawn, once, as the template a tile replacement starts from |

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

`id` is 1 to 63 characters of `a-z`, `0-9`, `-`, `_` and `.`, and it is what `[Mods] Order`
saves. `name`, `version`, `game` and `prg_sha256` are required. `prg_sha256` is the SHA-256 of the
PRG ROM the mod was made for: tile keys and sound ids belong to that program, so a mod for another
PRG is refused.

### Tiles

A tile is addressed by its content, not by where it sits in CHR: the key is a 64-bit hash of the
tile's 16 CHR bytes in 16 hex digits, optionally followed by `_` and the palette it is drawn with in
8 hex digits (backdrop and the three colours of its palette entry, one byte each).

- `tiles/<hash>_<palette>.png` replaces the tile drawn with that palette.
- `tiles/<hash>.png` replaces the tile with any palette. A palette-specific file wins over it.

Run the game with `--mods on --mods-dump DIR` and play to the place you want to change: `DIR`
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
APU still runs them, so the machine is unchanged.

## Scripts

A mod may ship `scripts/main.lua`. It runs in Lua 5.4 when the mod loads, in a sandbox of its
own, and registers handlers with `dt.on`:

```lua
dt.on("timer-tick", function()
    if dt.state.land == 0 then
        return dt.CANCEL -- time stands still in the Amazon
    end
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
| `dt.on(event, fn)` | Calls `fn` at every `event`, in the order handlers were added; mods higher in the list run first |
| `dt.CANCEL` | Returned by a handler of a cancelable event: the event is cancelled |
| `dt.state` | The game's state, read only, inside handlers (fields below) |
| `dt.log(...)` | Writes a line to the game's log (`print` does the same) |
| `dt.frame()` | Frames since the game started |
| `dt.mod` | The mod's id |

The events are the game's events (`include/ducktales/events.h`) and `frame`:

| Event | When | Cancel |
|---|---|---|
| `frame` | After every machine frame; the handler gets the frame number | |
| `player-damaged` | A hit is about to cost an HP dot | No dot is lost (the knock-back still happens) |
| `life-lost` | A death is about to cost a life | The lives count stays |
| `timer-tick` | A frame of the HUD timer elapses | The HUD second does not advance |
| `boss-hit` | A hit is about to count against a boss | The hits left stay |
| `new-game` | The title's set-up reset the HUD | Not cancelable |
| `jump-start` | A jump copied the take-off velocity into Scrooge's | Not cancelable |

`dt.state` fields: `land`, `lives`, `hp`, `hp_max`, `money` (`nil` while the HUD digits are
not drawn), `time` (the HUD's 0-999), `difficulty`, `paused` (boolean), `boss_hits`,
`treasures` and `key_items` (bit masks), `pad` and `pad_new` (controller 1, held and newly
pressed), `music`, `mode` (Scrooge's mode), `invulnerable` (frames left), `x` (Scrooge's world
X) and `y` (his Y on screen). Their addresses and meaning are in
[docs/memory-map.md](memory-map.md) and [docs/game-state.md](game-state.md). Reading an unknown
field is an error.

A handler that only reads leaves the machine exactly as it was (`tests/mods_test.py` compares the
per-frame hashes). Cancelling an event changes the game, as the matching enhancement does.

### The sandbox

- Only the base, `string`, `table`, `math`, `utf8` and `coroutine` libraries are there.
  `io`, `os`, `package`, `debug`, `require`, `dofile`, `loadfile` and `string.dump` are absent;
  the `io`, `os`, `package` and `debug` libraries are not even built into the game.
- `load` reads text only: a binary chunk is refused, and `scripts/main.lua` must be text too.
- A callback, and the script's first run, may run 1,000,000 Lua instructions. A mod's Lua state
  may hold 16 MiB.
- An error, or a limit reached, stops that mod's script: its handlers are dropped, the reason is
  written to the log and shown in the Mod Menu, and the game and the other mods go on. `pcall`,
  `xpcall`, `coroutine.resume` and `coroutine.close` do not catch a limit.
- The instruction limit counts Lua instructions. A single long call into the C library, such as a
  pathological string pattern, is not interrupted.

### Hot reload

About once a second the game checks `scripts/main.lua` of a folder mod, or the `.dtmod` archive.
When it changed, the script starts again from scratch with the new text. A script that stopped
with an error also starts again once you save a fix. Only scripts reload; tiles, palettes and
sounds load at start.

## How it works

`src/mods/` holds the mod loader (`app_mods.c`), the archive and folder reader
(`app_mods_archive.c`), the PNG, OGG and deflate decoders on stb (`app_mods_codec.c`), the audio
mixer (`app_mods_audio.c`) and the Lua sandbox (`app_mods_lua.c`). Both hosts and the native
player call the same entry points:

- **Picture.** After the stock compositor (`host/ducktales_sprites.c`) draws the frame, the native
  renderer runs again on the same video memory with a per-pixel source (tile, row, column,
  palette), and every pixel whose colour is still the console's is repainted from the mod's tile or
  palette. The HUD split repair (`ui/app_present.c`) uses the mod's palette for the pixels it
  redraws, but not its tiles.
- **Sound.** At the end of each frame the sound driver's request ids in RAM start and stop the
  replacements; the mixer mutes the channels the driver gave the request and adds the decoded file.
- **Scripts.** Each mod gets its own `lua_State` with a capped allocator and a count hook. A game
  event's hook site is on only while a handler for it exists, as the enhancements do.

The tests are in `tests/mods_test.py`. They generate their mods on the fly from the player's own
ROM; no game content is committed.
