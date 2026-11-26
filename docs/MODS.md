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
| `--mods-dump DIR` | Writes every tile drawn, once, as a replacement template, and every object frame drawn as `DIR/sprites/<kind>/<frame>_<x>_<y>.png`, even with mods off or without `--present-out` in a headless run. Creates missing parent folders and reports a write failure before stopping the dump |

## Making a mod

A mod is a zip archive renamed to `.dtmod`, or a folder with the same layout (handy while you
work on it):

```text
my-mod.dtmod
  mod.toml
  tiles/        8x8 PNG replacements, named by tile key
  sprites/      whole object frames, any size, by kind and frame
  palettes/     global.pal, or one per land
  music/        OGG Vorbis or WAV, named by music request id
  sfx/          OGG Vorbis or WAV, named by effect request id
  scripts/
    main.lua    optional, see Scripts
```

The files may sit in one folder inside the archive, next to `mod.toml`. Subfolders under
`tiles/` are allowed (`tiles/hud/...`). Other files are ignored.

### Example mods

`examples/mods/` holds three script mods to copy into the mods folder or to start from:

- **Money or Life** (`money-or-life`): one beaten enemy in three drops a pickup made with
  `dt.spawn_item`, mostly 2,000 or 10,000 money, sometimes an HP dot (money when HP is full), now and
  then a life (money at 9 lives) with a toast, rarely 50,000. The chance and the weights are at the
  top of its script.
- **Money Rain** (`money-rain`): every 1,200 steps (about 20 seconds) diamonds fall from the top of
  the screen for 480 steps, one every 10 steps at a random X, mostly 2,000, sometimes 10,000, rarely
  50,000, with a toast and a countdown drawn under the HUD. A boss fight or full slots skip a diamond.
- **Restless Enemies** (`restless-enemies`): every enemy `dt.ai` can name moves half again as fast,
  up to 8 px a frame. Its `after` callback scales `speed` each time the kind's code sets a new one.
  Turning an enemy around through `facing` does not work for the walkers: their code sets `facing`
  again from the way they walk.

Both are folders with a `mod.toml` and `scripts/main.lua` and change gameplay, so the machine
differs from the console's while one is on.

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

### Sprites

A sprite replacement is one picture for a whole object frame, drawn in place of the 8x8 sprites the
game builds that frame from. It can be larger than the original and use any colours: pixel art at
the console's scale, not a high-resolution layer.

`sprites/<kind>/<frame>_<x>_<y>.png`

- `<kind>` is the object's kind (`$0710`, the `kind` the scripts see; Scrooge's slot is `00`) and
  `<frame>` the metasprite id the animation shows (the frame the sprite builder at bank 5 `$8000`
  draws), each in 2 hex digits.
- `<x>_<y>` is the anchor in decimal: the pixel of the picture that sits on the object's position,
  the point the game places the frame's sprites from (its X, and one line below its Y). The
  picture may extend past the original on any side.
- `..._flip.png` is the art for the frame flipped (the object facing the other way). Without it
  the plain picture is mirrored about the axis the game mirrors that frame about.

Run with `--mods-dump DIR` and play to the place you want to change: `DIR/sprites/` fills with
every object frame drawn, once, unflipped, in the colours it was first drawn in, already named with
its anchor. Edit a copy, keeping the anchor in its name pointing at the same spot, and put it in
`sprites/`. Pixels with alpha below 128 are transparent. The kinds and frames seen on the five land
intros never changed shape from one land to another, but a frame is not checked against the
graphics loaded, so a kind that uses other graphics elsewhere shows the same replacement there.

Replacements change only the presented picture. The object's sprites are left out and the picture
is drawn where they were, in their place in the drawing order:

- behind the background when the object is (`$0600` bit 2), and not at all while it blinks out
  after a hit or the game hides it;
- cut at the edges of the picture pixel by pixel (the console drops a whole sprite that would wrap),
  in the left 8 columns when the game hides sprites there, and above the HUD band as the object's
  own sprites are;
- with No sprite flicker off, in the console's order and with its 8 sprites a line: the picture
  counts as the sprites it replaces, and the columns only its dropped sprites covered are left out;
- dimmed with the game's fades, a quarter for each step the fade takes off the sprite colours.

A sprite replacement wins over tile replacements and palettes for the pixels it draws. Earlier
mods in the order win per kind, frame and flip, as for tiles. A file named in another way, or a
PNG larger than 256x240, refuses the mod with the reason.

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
[docs/AUDIO.md](AUDIO.md). WAV files are PCM, 8 or 16 bits, mono or stereo. Music loops from the
file's own loop start (the OGG `LOOPSTART` comment, a WAV `smpl` loop), or from the beginning.

While a replacement plays, the sound driver's channels for that request are muted in the mix. The
APU still runs them, so the machine is unchanged. A replacement fades in over 5 ms when it starts
and out over 5 ms when it is stopped or the game pauses, and the muted channels fade the same way,
so no file has to start or end at silence to avoid a click. The native player keeps the sum under
full scale with its limiter ([AUDIO.md](AUDIO.md#output-stage)); a file at full scale over the
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
| `dt.set.<field>(value)` | Sets a writable field, inside handlers: at once in a game event's handler, at the frame's end in a `frame` handler (see [Writing the game](#writing-the-game)) |
| `dt.tune.<name>` | Scrooge's physics: assign a number to use in place of the game's, `nil` for the game's again; reads give the value in force or `nil` (see [Tunables](#tunables)) |
| `dt.objects()`, `dt.camera()` | The live object slots, as handles that can move or remove an object in a game event's handler, and the camera, inside handlers (see [Objects and the camera](#objects-and-the-camera)) |
| `dt.spawn(kind, x, y[, action])` | Makes an enemy the land uses, in a game event's handler; returns its handle, or `nil` and the reason (see [Spawning enemies](#spawning-enemies)) |
| `dt.spawn_item(item, x, y)` | Makes a pickup of money, HP or a life, in a game event's handler; returns `true`, or `false` and the reason (see [Spawning items](#spawning-items)) |
| `dt.ai(kind, {before = fn, after = fn})` | The mod's own code for every enemy of a kind, run at each one's turn before and after the kind's own; `dt.ai(kind, nil)` takes it back (see [Object code](#object-code)) |
| `dt.SKIP` | Returned by a `before` callback of `dt.ai`: the kind's own code does not run for that object this turn |
| `dt.level.set(x, y, metatile)` | Replaces a metatile on the screen, with its collision, in an `update` handler; returns `true`, or `false` and the reason (see [Editing the level](#editing-the-level)) |
| `dt.toast(text)` | Shows a short notice over the picture; returns `false` when it was refused (see [Toasts](#toasts)) |
| `dt.draw.text(x, y, text[, color])`, `dt.draw.rect(x, y, w, h, color)` | Draws over the presented picture for one frame (see [Drawing](#drawing)) |
| `dt.store.get(key)`, `dt.store.set(key, value)` | The mod's own saved values (see [Store](#store)) |
| `dt.log(...)` | Writes a line to `mods.log` and the console (`print` does the same) |
| `dt.frame()` | Frames since the game started |
| `dt.random(n)`, `dt.random(m, n)`, `dt.random.seed(n)` | The mod's own random numbers, anywhere (see [Random numbers](#random-numbers)) |
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
| `update` | A step of play: Scrooge has moved and the objects are about to (once per step, not on paused frames, also while a message is up) | | Not cancelable |
| `object-spawned` | A slot 1-15 holds an object it did not hold at the last frame's end: it became live, or its `kind` or `record` changed (a hidden item that shows becomes a pickup, for example) | `slot`, `kind`, `action`, `x`, `y`, `record` | Not cancelable |
| `object-despawned` | The object a slot 1-15 held at the last frame's end is gone (raised before the `object-spawned` of what replaced it) | the same, as last seen | Not cancelable |
| `screen-changed` | The camera's screen index changed | `screen`, `row` | Not cancelable |
| `player-mode-changed` | Scrooge's `mode` changed | `player` (1), `mode`, `previous` | Not cancelable |

`slot` is the object's slot (1-15), `kind` its type (`$0710`), `item` the pickup's item
(`$0620`; the land treasures are `$6F` and up, see [GAME-STATE.md](GAME-STATE.md)), `x` its world X
and `y` its Y on the screen, as `dt.state.x` and `y` are Scrooge's. `land` is 0 (The Amazon) to 4
(The Moon). `money-changed` gives the new `money` and the `delta`; several additions in one frame,
and the time bonus counted at the end of a land, come as one event per frame that changed it.

`land-start`, `land-clear`, `checkpoint`, `money-changed`, `object-spawned`, `object-despawned`,
`screen-changed` and `player-mode-changed` are seen at the end of the frame, by comparing the
game's state with the last frame's, before the `frame` handlers: a change that comes and goes
within one frame is not seen. The others run at the moment the game does it, inside the frame. Every
event fires once per occurrence with the decompiled code on or off. `update` is the place for a
script that changes gameplay: it runs once per step of the game's logic, at the same point of
the step, so a frame the console lags (where the game's step takes two frames) has none, where
`frame` runs after every machine frame.

`dt.state` fields: `land`, `lives`, `hp`, `hp_max`, `money` (`nil` while the HUD digits are
not drawn), `time` (the HUD's 0-999), `difficulty`, `paused` (boolean), `boss_hits`,
`treasures` and `key_items` (bit masks), `pad` and `pad_new` (controller 1, held and newly
pressed), `music`, `mode` (Scrooge's mode), `invulnerable` (frames left), `x` (Scrooge's world
X) and `y` (his Y on screen). Their addresses and meaning are in
[docs/MEMORY-MAP.md](MEMORY-MAP.md) and [docs/GAME-STATE.md](GAME-STATE.md). Reading an unknown
field is an error.

A handler that only reads, logs, toasts or uses the store leaves the machine exactly as it was
(`tests/mods_test.py` compares the per-frame hashes). Cancelling an event or `dt.set` changes the
game, as the matching enhancement does.

### Objects and the camera

`dt.objects()` returns an array with a table for every live object slot (Scrooge is slot 0):
`slot`, `kind`, `action` (for a pickup, its item), `state`, `x` (world X), `y` (Y on the screen),
`screen_x` (X on the screen), `record` (the land record it was spawned from, 255 for none),
`player` (1 for Scrooge's slot, 0 for an object), `facing` (1 facing right, 0 left), `speed` (the
horizontal speed, 8.8 fixed point: 256 is a pixel a frame; the object's code moves it left or right
by it) and `vy` (the vertical velocity, 8.8 and signed, as the kind's code uses it). `dt.camera()` returns `x` (the world X of the
camera's left edge), `row` (the map row) and `screen` (the screen index, row * 8 + page). Both
read the game when they are called, only inside handlers. The object tables are in
[docs/MEMORY-MAP.md](MEMORY-MAP.md).

```lua
dt.on("frame", function()
    for _, o in ipairs(dt.objects()) do
        if o.slot ~= 0 then
            dt.draw.rect(o.screen_x, o.y - 2, 8, 1, 0xFFFF00) -- a mark over every object
        end
    end
end)
```

Each object table is also a handle on its object, with two methods that work only in a game
event's handler (`update` is the one meant for them) and change the game at once:

- `o:set{x = ..., y = ..., action = ..., facing = ..., speed = ..., vy = ...}` writes any of them. `x` is a world X on the
  screen now (from the camera's `x` to 255 more) and needs an object with a world position, `y` is
  0 to 239, and `action` must be one this land spawns that kind with (in a record or a timed
  object of any of its map rows); a new action
  starts its animation from the first frame. A pickup's, hidden item's or chest's action is its
  item and cannot be set, and no action that holds an item can be given. `facing` (0 or 1),
  `speed` (0 to 2048) and `vy` (-2048 to 2048), at most 8 px a frame, can be set on the enemies
  `dt.spawn` makes (the kinds in its table) only: their code uses them to move the object and to
  test which way it goes, never to choose a table entry or an address. They stay the kind's own,
  so the kind's code may change them again: most kinds set `facing` again from the way they walk,
  and a kind that is falling takes a `vy` that is not downward as having left the bottom of the
  screen and despawns the object, as it does when the object falls off. The table's fields follow
  the values written. `kind`, `state` and the rest cannot be set: `state` is the step of the
  object's own code, and another value sends the game into code that does not belong to it.
- `o:despawn()` frees the slot at once, through the game's own despawn routine. The land record is
  not used up, so the object comes back when the spawner reaches its record again. The bosses and
  the objects that drive a scene or the final climb cannot be despawned.

Both return `true`. Every field is checked before any is written (they are written, and logged,
in the order the object table lists them), and a value that cannot be written is an error
(catchable with `pcall`), as is a call on the player's slot or outside a game event's handler. A
handle kept from an earlier call names a slot, a kind and a record: when the slot holds something
else now, the call changes nothing, returns `false, "gone"` and the log says so. Every write is
logged as for `dt.set`: `object 9 (kind 1):set{y = 120}, was 144`, `object 9 (kind 1):despawn()`.

```lua
dt.on("update", function()
    for _, o in ipairs(dt.objects()) do
        if o.player == 0 and o.kind == 1 and o.y > 140 then
            o:despawn() -- no hoppers on the floor
        end
    end
end)
```

### Spawning enemies

`dt.spawn(kind, x, y[, action])` makes an enemy at world X `x` and Y `y` on the screen now (the
same ranges as `o:set`) through the game's own spawn code: the highest free slot of 15 to 1, the
4-byte template of the first record or timed object of this land that spawns that kind (with that
`action`, when one is given), and land record 255, as the game's timed enemies have. It works only
in a game event's handler, writes at once and returns the new object's handle, which the game then
runs as one of its own. Only enemies this land already uses can be made, since only their tiles and
code are loaded:

| Kinds | Enemy |
|---|---|
| 1, 2, 3, 5, 6 | hopper, walker, thrower, watcher, dangler |
| 12, 13, 16, 17 | crawler, flyer, charger, swooper |
| 24 to 31, 32, 34, 37 | patroller, pouncer, pit jumper, bouncer, leaper, block kicker, ledge walker, pacer, bobber, hop walker, ceiling hopper |
| 48, 49, 50 | the thieves that take money |

Bosses, scenes, doors, blocks, chests, pickups and the other objects cannot be made: a chest or a
pickup made twice would give its treasure twice. A kind outside the list or not used by this land,
an action the land does not spawn it with, a position off the screen, a call during a boss fight
and a call outside a game event's handler are errors (catchable with `pcall`). Two slots are
always left free for the game's own spawns and shots: when fewer than three are free,
`dt.spawn` makes nothing and returns `nil, "2 free slots, 2 kept for the game"` (with the count
free now). Each spawn is logged: `dt.spawn(1, 258, 100): object 8 (kind 1)`.

A spawned enemy is not tied to a record, so it does not come back once it leaves the screen or is
beaten. Its handle names slot, kind and record 255 like any other: a later enemy of the same kind
made in the same slot is taken for the same one.

```lua
local made = 0
dt.on("update", function()
    if made < 3 and dt.state.pad_new & 0x40 ~= 0 then -- B
        local o, why = dt.spawn(1, dt.camera().x + 200, 100)
        made = made + (o and 1 or 0)
    end
end)
```

### Spawning items

`dt.spawn_item(item, x, y)` makes a pickup at world X `x` and Y `y` on the screen now (the same
ranges as `o:set`), as the game shows the item of a hidden item it found: a pickup (kind 19) in the
highest free slot, with land record 255, that falls to the floor, blinks after a while and vanishes.
Scrooge takes it as any other, through the game's own code, and `item-collected` reports it. It
works only in a game event's handler and returns `true`. Only the items whose taking adds money,
HP or a life, and nothing else, can be made:

| Item | Taken |
|---|---|
| 35 (`$23`) | 2,000 money |
| 40 (`$28`) | 10,000 money |
| 97 (`$61`) | 50,000 money |
| 36 (`$24`) | the first lost HP dot back |
| 37 (`$25`) | every HP dot back; on Difficult the game makes it a 10,000 one, as it does its own |
| 39 (`$27`) | one more life, up to 9 as the game caps it |

The power-up, the key items, the extra HP dot and the treasures cannot be made, nor any other
item; such an item, a position off the screen, a call during a boss fight and a call outside a game
event's handler are errors (catchable with `pcall`). The same two slots as for `dt.spawn` are kept
for the game: with fewer than three free, it makes nothing and returns `false` and the reason. Each
call is logged as a write: `dt.spawn_item(39, 214, 60)`.

Nothing of the land's progress can change through it. Taking these items adds to the HUD's money,
HP dots or lives only: the code that sets the treasure bits, the key items, the extra dot's flag or
starts a scene runs for the items left out. A record of 255 is none of the land's, and a pickup's own
code never marks a record used up (blocks, chests, hidden items and props do) and gives one back
only for item 41, so no record bit is touched; and no chest, hidden item or block is made. Their sprites are in every
land.

```lua
dt.on("enemy-defeated", function(e)
    if dt.random(4) == 1 then
        dt.spawn_item(35, e.x, e.y) -- a diamond, now and then
    end
end)
```

### Object code

`dt.ai(kind, {before = fn, after = fn})` gives every object of `kind` the mod's own code. The game
runs the code of its live objects, slot 1 to 15, once a step; at the turn of each object of that
kind it calls `before(o)` first and `after(o)` once the kind's own code returned, with the object's
handle (the fields and methods of [Objects and the camera](#objects-and-the-camera)). Either may be
left out. A `before` that returns `dt.SKIP` keeps the kind's code from running for that object this
turn. That code is what moves the object, tests its contacts with Scrooge and beats it once it is
hit, so a skipped object does none of that on that turn. `after` runs only
while the slot still holds a live object of that kind (not after the kind's code beat or removed it),
whether its turn was skipped or not. `dt.ai(kind, nil)` takes the code back; calling `dt.ai` again
for a kind replaces it.

Only the enemies `dt.spawn` makes (see [Spawning enemies](#spawning-enemies)) can be named, a
land's own or spawned; any other kind is an error. In the callbacks the handles, `dt.set`,
`dt.spawn` and the reads work as in a game event's handler, and every write is logged; so is
each skip, with its repeats counted in one line: `dt.ai(1): object 8 (kind 1) skipped`. Of the
object's fields a callback can change only what `o:set` can: `kind`, `state`, the timer and the
rest stay read only. An object whose `before` despawned it is skipped. When several mods name the
same kind, each one's `before` runs, in the mods' order, and any `dt.SKIP` skips.

A mod's `dt.ai` callbacks may run 250,000 Lua instructions in all in one frame (the game runs at
most 15 objects a step); past that the mod stops, as for the other limits (see
[The sandbox](#the-sandbox)). The game's own code for a kind costs nothing while no mod names one:
the two hook sites are on only while a `dt.ai` is in force.

```lua
local held = {}
dt.ai(1, {
    before = function(o)
        if dt.state.pad & 0x40 ~= 0 then -- B held: every hopper waits
            return dt.SKIP
        end
    end,
    after = function(o)
        if o.vy == 1024 then -- it just jumped: twice as fast
            o:set{vy = 2048}
        end
    end,
})
```

### Editing the level

`dt.level.set(x, y, metatile)` replaces the 16x16 metatile at world X `x` and Y `y` with
`metatile` (0 to 255) of the land's tileset, through the game's own tile edit, the one a block
uses when it opens: the new tiles go in the queue the game uploads to the screen at the next
VBlank, and the metatile's collision class (empty, solid or climbable) and palette replace the
old ones at once, so Scrooge and the objects collide with the new metatile from this step on.
A land's metatiles are the 256 of its tileset bank, with the format in
[docs/RENDERING.md](RENDERING.md) ("Map, screen, block and metatile format"); in the Amazon, for
example, metatile 0 is empty and 102 is solid.

It works only in an `update` handler, and only on a cell on the screen now: `x` from the camera's
`x` to 255 more, `y` from 48 (below the HUD) to 239. The game's queue takes one edit a step: a
second call in the same step, or one while the game's own edit or a row scroll is under way,
changes nothing and returns `false, "the tile edit queue is in use"`. A position off the screen
or in the HUD, a metatile above 255, a special metatile (collision class 3, whose effect the game
finds from the map's own metatile) and a call from any other handler are errors (catchable with
`pcall`). Each edit is logged, and the same call repeated is counted in one line:
`dt.level.set(214, 180, 102)`, `dt.level.set(214, 180, 102) repeated 9 more times`.

An edit changes the screen, not the map: it lasts until the game draws that part of the level
again from the map, when it scrolls off the screen and back. A mod that wants the change to stay
sets it again whenever the cell is back on the screen:

```lua
local wall = { x = 600, y = 176 }
dt.on("update", function()
    local left = dt.camera().x
    if wall.x >= left and wall.x <= left + 255 then
        dt.level.set(wall.x, wall.y, 102) -- false while the queue is busy; tried again next step
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
| `boss_hits` | 1 to 5, only during a boss fight | The hits the boss still takes (`$B1`); the fight's spawn sets 5 |
| `invulnerable` | 0 to 255; during the knock-back (`mode` 6) 61 to 127, while hurt (`mode` 5) below 128 | Scrooge's invulnerability frames (`$7D`): while not 0 hits do not hurt him and his sprite flickers. It counts down once a step; 128 and up do not count down. The knock-back ends when it reaches 60, and a hit at 0 starts another, so values that would never end it are refused |

The value is checked when `dt.set` is called, and a value out of range is an error (catchable
with `pcall`). Calls are only allowed inside handlers:

- In a game event's handler (`update`, `player-damaged`, ...) the write happens at once, inside
  the event's RAM transaction at that point of the game's code, so `dt.state` shows the new value
  right away and the rest of the game's step sees it.
- In a `frame` handler it is queued: the writes happen at the end of the frame, after the `frame`
  handlers, through the game's RAM transaction (the same helpers the enhancements use), so
  `dt.state` shows the new value from the next frame on. The value is checked again then, and a
  write the state no longer allows (the HUD stopped showing money) is dropped with a line in the
  log. A mod whose script stopped has its queued writes dropped.

**Every write is a gameplay change**: the machine then differs from the console's, like an
enhancement that is on. Every write is logged as `dt.set.lives(7), was 2`. A write that repeats the
value the mod last wrote there gets no line of its own: the repeats are counted and logged as one
line, `dt.set.lives(7) repeated 60 more times`, at the next change, every 600 frames and when the
game closes.

```lua
dt.on("update", function()
    if dt.state.hp < dt.state.hp_max and dt.state.pad_new & 0x20 ~= 0 then
        dt.set.hp(dt.state.hp_max) -- Select refills the HP dots
    end
end)
```

A hit's cost needs no field of its own: a `player-damaged` handler can return `dt.CANCEL` (no dot
is lost) or set `hp` itself.

### Tunables

`dt.tune` holds numbers of Scrooge's physics that the game reloads every time it uses them. All
are 8.8 fixed point: 256 is one pixel per frame (per step of play), so `0x180` is 1.5.

| Name | The game's | Values | What it is |
|---|---|---|---|
| `gravity` | `0x33` | `0x10` to `0xFF` | What his vertical velocity loses each step in the air (jumps, falls, bounces); the enemies keep their own |
| `walk_speed` | `0x180` | `0x40` to `0x280` | His speed walking on normal ground (slippery floor keeps its own speeds; `0x280` is its top) |
| `jump_velocity` | `0x4BB` | `0x100` to `0x1000` | His take-off velocity when he jumps |
| `pogo_velocity` | `0x5A8` | `0x100` to `0x1000` | His velocity when he bounces on the pogo cane |

```lua
dt.tune.gravity = 0x20             -- floatier jumps from now on
dt.on("land-start", function(e)
    dt.tune.walk_speed = e.land == 4 and 0x200 or nil -- faster on the Moon only
end)
```

Assigning works anywhere, also outside handlers; a value out of range or an unknown name is an
error (catchable with `pcall`). The value holds until the script assigns another or `nil`, or
stops (an error, a reload, the game closing): then the game uses its own again. When several mods
set the same tunable, the one highest in the list is in force, and reading `dt.tune.<name>` in
any mod gives that value. Like `dt.set`, **a tunable is a gameplay change**: every assignment is
logged as `dt.tune.gravity = 32` (`dt.tune.gravity = nil`), identical repeats are counted in one
line as for writes, and a script that stops logs `dt.tune.gravity = nil (the script ended)`.

`gravity`, `walk_speed` and `pogo_velocity` take effect where the game stores them, the next
time it does (each step in the air, each step of walking, each bounce). `jump_velocity` is the
value the land's set-up stores for every jump of the land: it is replaced at the end of the frame,
so it applies from the next jump on.

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

### Random numbers

`dt.random(n)` returns a whole number from 1 to `n`, and `dt.random(m, n)` one from `m` to `n`.
Each mod has a generator of its own (xorshift64*), seeded from its id when the script starts: a run
gives the same numbers every time, whatever the other mods draw, and the game's own generator
(`$C0`, which its enemies use) is never read or changed, so drawing numbers is not a write.
`dt.random.seed(n)` starts the sequence again from `n`. Lua's `math.random` is still there; it is
not tied to the mod and its numbers differ from one run to the next.

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
- A callback, and the script's first run, may run 1,000,000 Lua instructions, and a mod's `dt.ai`
  callbacks 250,000 in all in one frame. A mod's Lua state may hold 16 MiB.
- An error, or a limit reached, stops that mod's script: its handlers and queued writes are dropped, the reason is
  written to the log and shown in the Mod Menu, and the game and the other mods go on. `pcall`,
  `xpcall`, `coroutine.resume` and `coroutine.close` do not catch a limit.
- The reason names the line and where it ran: *scripts/main.lua:12: attempt to index a nil value
  (field 'x') (in a "frame" handler)*, *scripts/main.lua:3: ran more than 1000000 instructions in
  one callback (in a "timer-tick" handler)*, *scripts/main.lua:6: ran more than 250000 instructions
  in dt.ai callbacks in one frame (in dt.ai(1)'s before callback)*, or *(when the script started)* for the script's first
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
| `core/` | Knows no game. `fm_mods.h` is its interface: the package format and catalogue (`fm_catalog.c`: folders, `mod.toml`, order, switches, restart check), the archive and folder reader (`fm_archive.c`), PNG, OGG, WAV and deflate on stb (`fm_codec.c`) and the Lua sandbox with the generic script API (`fm_script.c`: `dt.on`, cancel, `dt.ai`, log, limits, hot reload; `fm_script_state.c`: `dt.state`, `dt.set`, `dt.tune` and the queries over the fields, tunables and records the game describes, with the records' handle methods; `fm_script_show.c`: toast and `dt.draw`; `fm_script_store.c`: store; `fm_script_internal.h`: what they share) |
| `nes/` | NES games: tile keys, the replacement tile table, `.pal` palettes and tile templates (`fm_nes.c`) |
| `ducktales/` | This game: what the assets mean and how they are loaded (`dt_mods.c`), painted (`dt_mods_picture.c`, with the toast and `dt.draw` overlay), the sprite replacements and their dump (`dt_mods_sprites.c`) and mixed (`dt_mods_audio.c`); the event list, the `dt.state` and writable fields, the tunables, the queries and their handles, the `dt.ai` kinds (`o:despawn()` runs the decompiled `Obj_Despawn` and `dt.spawn` `Obj_FindFreeSlot` and `Obj_SpawnTemplate`, through `Decomp_Outside`), `dt.level.set` (`Level_EditCell`) and the store folder (`dt_mods_script.c`) |
| `app_mods.h` | The interface the hosts and the menu call |

Both hosts and the native player call the same entry points:

- **Picture.** After the stock compositor (`host/ducktales_sprites.c`) draws the frame, the native
  renderer runs again on the same video memory with a per-pixel source (tile, row, column,
  palette), and every pixel whose colour is still the console's is repainted from the mod's tile or
  palette. The HUD split repair (`ui/app_present.c`) uses the mod's palette for the pixels it
  redraws, but not its tiles. Sprite replacements are drawn by the compositor itself: it replays
  the sprite builder on the RAM the frame's OAM was built from, so it knows each slot's kind,
  metasprite id, flip, position, priority and blink, and puts the picture in the list instead of
  the slot's sprites; their pixels are marked so the tile and palette painting leaves them alone.
  With No sprite flicker off it composes the OAM in its own order only while a replaced object is
  on screen, and presents the machine's picture otherwise.
- **Sound.** At the end of each frame the sound driver's request ids in RAM start and stop the
  replacements; the mixer mutes the channels the driver gave the request and adds the decoded file.
- **Scripts.** Each mod gets its own `lua_State` with a capped allocator and a count hook. A game
  event's hook site is on only while a handler for it exists, as the enhancements do; the events
  seen at the frame's end read RAM only while one has a handler. `update` has a site where
  `Player_Update` returns in the frame loop (`$DB6F`), and the decompiled frame loop raises it
  itself. At the end of each frame
  (including the frames the open menu runs) those events are raised, then the `frame` handlers run,
  the queued `dt.set` writes are applied in one RAM transaction and the frame's drawings replace
  the shown ones.
- **Tunables.** `src/game/tune.c` (`include/ducktales/tune.h`) owns them. `gravity`,
  `walk_speed` and `pogo_velocity` each have a hook site right after the game's stores of its
  value (`$E358`, `$E4A3`, `$E6AD`), on only while a mod sets it, which stores the tuned value
  over the game's; the decompiled player code reads the same value there in replace mode
  (`Tune_Value`). `jump_velocity` has no site: at the end of each frame, after the scripts, the
  take-off velocity the land set-up stored (`$07A0:$0790`) is replaced while it holds the game's
  value, and put back once the tunable is `nil`. See "Tunables" in
  [docs/DECOMPILATION.md](DECOMPILATION.md).
- **Objects.** Handles and `dt.spawn` have no hook site: they write the object tables at once from
  a game event's handler, and the game's routines they use run as decompiled C
  (`Obj_Despawn`, `Obj_FindFreeSlot` and `Obj_SpawnTemplate`, the template step `Obj_SpawnRecord`
  shares) through `Decomp_Outside`. `dt.spawn_item` takes a free slot the same way and runs
  `Obj_ShowItem` (`$8CAC`), the step that turns a hidden item into its pickup.
- **Object code.** `src/game/ai.c` (`include/ducktales/ai.h`) owns `dt.ai`'s calls. The object
  dispatch (bank 6 `$8000`) loads a live slot's handler address into `$00`/`$01` and runs it with
  `JMP ($0000)` at `$8020`; the handler's `RTS` returns to the `INC $4A` at `$8023`. Each has a
  hook site, on only while a kind has `dt.ai` code: 2 of the 128 (114 in use). The before site runs
  the `before` callbacks; a skip stores the address of kind 0's handler, an `RTS`, in `$00`/`$01`,
  so the jump returns at once. The decompiled dispatch makes the same calls itself, and replace mode's
  measuring call makes the same changes at the sites, in the same order (see "Object code" in
  [docs/DECOMPILATION.md](DECOMPILATION.md)). With no `dt.ai` code a step costs nothing; with it,
  each live object of a named kind costs its callbacks, at most 250,000 Lua instructions a frame
  for each mod.
- **Level.** `dt.level.set` has no hook site either: in the `update` handler it runs
  `Level_EditCell`, the part of the tile edit `$F574` it shares with the game's, which writes
  the `$0440` queue entry, the collision map `$0580` and the attribute shadow `$0500`, and
  moves the queue's end `$5C` past the entry; the frame loop hands the queue to the NMI at its
  end as for the game's edits. The metatile's tiles are read from the tileset bank (`$F7`) in
  the ROM, with no bank switch.

The tests are in `tests/mods_test.py` (its steps in `tests/mods/`), `tests/mods_catalogue_test.c` and `tests/window_smoke.py`.
They generate their fixtures in scratch folders, and the tile templates come from the player's own
ROM; no game content is committed.
