"""Mods (src/mods, docs/MODS.md) on the headless build, with mods made on the fly.

Nothing derived from the game is kept: every mod is generated in a temporary folder, the tile
templates come from the player's own ROM through --mods-dump and are deleted with it.

  1. the machine is untouched: with a palette, a tile and a music replacement loaded the
     per-frame hashes (--hash-out: trace, RAM, picture, registers, cycles, hardware) equal a run
     without mods, frames 0-1500 of amazon_intro;
  2. palette swap (palettes/global.pal, every colour inverted): every pixel of the presented
     picture is the inverse of the picture without mods, at frames 300 (Land Select) and 1500
     (The Amazon); a per-land palette (palettes/amazon.pal) changes the Amazon frame and leaves
     Land Select alone;
  3. one tile replaced (tiles/<key>.png from --mods-dump): with the template itself as the art
     the picture is identical (placement, flips and palette key are right); with solid magenta
     every changed pixel is magenta and there is at least one;
  4. priority: two mods replacing the same tile (magenta, cyan) and two global palettes show the
     top mod's, and the other one after --mods-order swaps them; switching the top one off shows
     the other;
  5. a mod whose prg_sha256 is another game's is refused with "Made for a different version of
     the game" and changes nothing; a broken archive (random bytes) and a truncated one are
     listed as "Cannot be read: ..." and the other mods still load;
  6. music: a generated 440 Hz tone as music/07.wav (the title song): the sound is identical
     until the frame after the title requests it (frame 7), then the 440 Hz tone carries almost
     all of the energy (the driver's channels are muted), while without the mod it does not;
  7. with the mod loaded the machine's own audio hash is unaffected: the run's --hash-out equals
     the run without mods (step 1 covers it);
  8. scripts (the native player, else headless): the sandbox, the limits and errors with where they
     ran, a dt.CANCEL an event cannot take, dt.state, dt.set, toasts (cut at 64 UTF-8 characters),
     dt.store (no temporary file left), mods.log in the data folder and hot reload;
  9. the catalogue: mod.toml and main.lua with a byte order mark, names in any case, mod.toml
     errors (missing keys, unquoted value, key set twice, malformed prg_sha256), two files for one
     tile, palette or sound, and --mods-dir with a path that is not a folder;
 10. events, state and drawing on full_amazon (headless, and the native player when it is built):
     land-start, checkpoint, boss-hit and boss-defeated, land-clear once each with the land,
     enemy-defeated and item-collected with their slot, kind or item and position, money-changed
     deltas that add up to the money; dt.objects() and dt.camera() agree with dt.state; dt.draw
     shows its rectangle and text over the presented picture and stops at 64 a frame; with all of
     it running the per-frame machine hashes equal a run without mods, and the native player logs
     the same events in the same frames.

    py -3 -I tests/mods_test.py --build build/headless
"""

import argparse
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402

ROM = Path(default_rom())
PRG_SHA256 = '8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013'
failures = []


def check(ok, what, detail=''):
    print(('PASS ' if ok else 'FAIL ') + what + (('\n    ' + detail[-1500:]) if detail and not ok else ''))
    if not ok:
        failures.append(what)


def png_write(path, w, h, pixels):
    raw = b''.join(
        b'\0' + b''.join(struct.pack('>I', p)[1:] + bytes([p >> 24]) for p in pixels[y * w : (y + 1) * w])
        for y in range(h)
    )

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    Path(path).write_bytes(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw))
        + chunk(b'IEND', b'')
    )


def png_read(path):
    """(w, h, [0xAARRGGBB]) of an 8-bit RGB or RGBA, non-interlaced PNG."""
    data = Path(path).read_bytes()
    at, idat, w = 8, b'', 0
    while at < len(data):
        n, kind = struct.unpack('>I4s', data[at : at + 8])
        body = data[at + 8 : at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            bpp = {2: 3, 6: 4}[color]
            assert depth == 8 and body[12] == 0
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    raw, stride, prev, out = zlib.decompress(idat), w * bpp, bytearray(w * bpp), []
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            px = line[x * bpp : x * bpp + bpp]
            out.append((px[3] if bpp == 4 else 255) << 24 | px[0] << 16 | px[1] << 8 | px[2])
    return w, h, out


def nes_palette():
    """The 64 colours of nv_palette_argb (src/native/native_render.c), emphasis 0."""
    low, high = [0.350, 0.518, 0.962, 1.550], [1.094, 1.506, 1.962, 1.962]
    black, white, out = 0.518, 1.962, []
    for i in range(64):
        hue, row = i & 15, (i >> 4) & 3
        if hue > 13:
            row = 1
        lo, hi = low[row], high[row]
        if hue == 0:
            lo = hi
        if hue > 12:
            hi = lo
        luma = u = v = 0.0
        for p in range(12):
            level = ((hi if (hue + p) % 12 < 6 else lo) - black) / (white - black)
            angle = math.pi * (p - 0.5) / 6.0
            luma += level
            u += level * math.cos(angle)
            v -= level * math.sin(angle)
        luma, u, v = luma / 12, u * 2 / 12, v * 2 / 12

        def ch(c):
            return 0 if c <= 0 else 255 if c >= 1 else int(c * 255.0 + 0.5)

        out.append((ch(luma + 1.140 * v), ch(luma - 0.395 * u - 0.581 * v), ch(luma + 2.032 * u)))
    return out


def mod_toml(mod_id, name, prg=PRG_SHA256, game='DuckTales (USA)'):
    return (
        f'id = "{mod_id}"\nname = "{name}"\nversion = "1.0.0"\nauthor = "mods_test"\n'
        f'description = """Generated by tests/mods_test.py."""\ngame = "{game}"\nprg_sha256 = "{prg}"\n'
    )


def make_mod(folder, mod_id, files, prg=PRG_SHA256, loose=False):
    """A .dtmod (or a loose folder) with mod.toml and files {name: bytes}."""
    files = dict(files, **{'mod.toml': mod_toml(mod_id, mod_id.replace('-', ' ').title(), prg).encode()})
    if loose:
        for name, data in files.items():
            p = folder / mod_id / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        return
    with zipfile.ZipFile(folder / f'{mod_id}.dtmod', 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)


def png_bytes(w, h, pixels, tmp):
    p = tmp / '_png.png'
    png_write(p, w, h, pixels)
    return p.read_bytes()


def pick_drawn_tile(keys, pixels):
    frame = struct.pack(f'<{len(pixels)}I', *pixels)
    stride = 256 * 4
    for key in keys:
        _, _, px = png_read(key)
        rows = [px[y * 8 : (y + 1) * 8] for y in range(8)]
        if (
            len(set(px)) < 3
            or any(p >> 24 != 255 for p in px)
            or rows == rows[::-1]
            or rows == [row[::-1] for row in rows]
        ):
            continue
        raw = [struct.pack('<8I', *row) for row in rows]
        at = frame.find(raw[0])
        while at >= 0:
            if (
                at % 4 == 0
                and at % stride <= stride - 32
                and all(frame.startswith(row, at + dy * stride) for dy, row in enumerate(raw))
            ):
                return key
            at = frame.find(raw[0], at + 1)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--native-build', default='build/windows', help='the native player\'s build ("" to skip it)')
    ap.add_argument('--native-only', action='store_true', help='only the native player\'s checks')
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    route = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
    native = ROOT / args.native_build / args.config / 'ducktales.exe' if args.native_build else None
    if native and not native.exists():
        print(f'SKIP  the native player: {native} is not built')
        native = None
    if not args.native_only:
        headless_checks(exe, route)
    if native:
        native_checks(native, route)
    script_checks(native or exe, route, native is not None)
    catalogue_checks(native or exe, route, native is not None)
    event_checks(None if args.native_only else exe, native)
    print(f'\n{"FAILED: " + ", ".join(failures) if failures else "all mods checks passed"}')
    return 1 if failures else 0


def samples(p):
    with wave.open(str(p)) as wv:
        return wv.getframerate(), struct.unpack(f'<{wv.getnframes()}h', wv.readframes(wv.getnframes()))


def share_440(s, rate):
    s = s[rate // 60 * 30 : rate // 60 * 230]
    c = sum(x * math.cos(2 * math.pi * 440 * i / rate) for i, x in enumerate(s))
    d = sum(x * math.sin(2 * math.pi * 440 * i / rate) for i, x in enumerate(s))
    power = 2 * (c * c + d * d) / len(s)
    total = sum(x * x for x in s) or 1
    return power / total


def tone_wav(path):
    with wave.open(str(path), 'wb') as wv:
        wv.setnchannels(1)
        wv.setsampwidth(2)
        wv.setframerate(48000)
        wv.writeframes(
            b''.join(struct.pack('<h', int(12000 * math.sin(2 * math.pi * 440 * i / 48000))) for i in range(48000 * 2))
        )
    return path.read_bytes()


def native_checks(exe, route):
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(name, mods=None, frames=1501, every=300, wav=False, extra=()):
            out = tmp / name
            out.mkdir()
            cmd = [
                exe,
                ROM,
                '--frames',
                str(frames),
                '--input',
                route,
                '--lag',
                'faithful',
                '--config',
                tmp / 'x.ini',
                '--present-out',
                out / 'p.png',
                '--present-every',
                str(every),
            ]
            if mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            if wav:
                cmd += ['--wav-out', out / 'a.wav']
            p = subprocess.run([str(c) for c in [*cmd, *extra]], capture_output=True, text=True, timeout=3600, env=env)
            if p.returncode:
                print(p.stdout + p.stderr)
                raise SystemExit(f'native {name}: exit {p.returncode}')
            pics = {}
            for f in out.glob('p*.png'):
                digits = ''.join(c for c in f.stem if c.isdigit())
                pics[int(digits) if digits else frames] = png_read(f)[2]
            return out, pics, p.stdout + p.stderr

        _, base, _ = run('base')
        dump = tmp / 'dump'
        run('dump', every=1500, extra=('--mods', 'on', '--mods-dump', dump))
        keys = sorted(dump.glob('*.png'))
        key = pick_drawn_tile(keys, base.get(1500, []))
        check(key is not None and 1500 in base, f'native player: --mods-dump wrote {len(keys)} tile templates')
        if key is None or 1500 not in base:
            return
        inverted = bytes(255 - c for rgb in nes_palette() for c in rgb)
        magenta = png_bytes(8, 8, [0xFFFF00FF] * 64, tmp)
        (tmp / 'mods_inv').mkdir()
        make_mod(tmp / 'mods_inv', 'inverse', {'palettes/global.pal': inverted})
        _, inv, log = run('inv', tmp / 'mods_inv')
        check(
            all(p == (0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(inv[1500], base[1500])),
            'native player: global palette inverts every pixel of frame 1500',
            log,
        )
        (tmp / 'mods_mag').mkdir()
        make_mod(tmp / 'mods_mag', 'magenta-tile', {f'tiles/{key.name}': magenta})
        _, mag, log = run('mag', tmp / 'mods_mag')
        changed = [i for i, (p, q) in enumerate(zip(mag[1500], base[1500])) if p != q]
        check(
            changed and all(mag[1500][i] == 0xFFFF00FF for i in changed),
            f'native player: tile {key.stem} in magenta: {len(changed)} pixels changed, every one of them magenta',
            log,
        )
        (tmp / 'mods_tone').mkdir()
        make_mod(tmp / 'mods_tone', 'title-tone', {'music/07.wav': tone_wav(tmp / 'tone.wav')})
        a_dir, _, _ = run('wav_base', frames=240, every=240, wav=True)
        b_dir, _, log = run('wav_tone', tmp / 'mods_tone', frames=240, every=240, wav=True)
        (ra, a), (rb, b) = samples(a_dir / 'a.wav'), samples(b_dir / 'a.wav')
        sa, sb = share_440(a, ra), share_440(b, rb)
        check(
            len(a) > ra * 3 and sb > 0.9 and sa < 0.2,
            f'native player: frames 30-230: the 440 Hz tone carries {sb:.0%} of the energy with the mod, {sa:.0%} without',
            log,
        )


SANDBOX_LUA = r'''
local open = {}
for _, name in ipairs({"io", "os", "package", "debug", "require", "dofile", "loadfile"}) do
    if _G[name] ~= nil then
        open[#open + 1] = name
    end
end
if string.dump ~= nil then
    open[#open + 1] = "string.dump"
end
local binary = load("\27Lua")
dt.log("sandbox", #open == 0 and "closed" or table.concat(open, ","), binary == nil and "binary-refused" or "binary-loaded",
    load("return 6 * 7")())
dt.on("new-game", function(e)
    dt.log("event", e.name)
end)
dt.on("frame", function(frame)
    if frame == 1000 or frame == 1400 then
        dt.log("state", frame, dt.state.land, dt.state.lives, dt.state.time, dt.state.hp)
    end
    if frame == 1200 then
        dt.toast("sandbox")
    end
    if frame == 1300 then
        dt.toast(string.rep("\u{E9}", 70))
    end
end)
'''

API_LUA = r'''
local ok, err = pcall(dt.set.lives, 3)
dt.log("outside", ok, err)
local runs = (dt.store.get("runs") or 0) + 1
dt.store.set("runs", runs)
dt.store.set("text", "a\nb\\c = d")
dt.store.set("half", 3.5)
dt.store.set("flag", true)
dt.log("store", runs, dt.store.get("text") == "a\nb\\c = d", dt.store.get("half"), dt.store.get("flag"))
local big_ok, big_err = pcall(dt.store.set, "big", string.rep("x", 70000))
dt.log("store-cap", big_ok, big_err)
local type_ok = pcall(dt.store.set, "table", {})
local key_ok = pcall(dt.store.set, "../escape", 1)
dt.log("store-refused", type_ok, key_ok)
dt.on("frame", function(frame)
    if frame == 900 then
        dt.log("toasts", dt.toast("Hello from api"), dt.toast("too soon"))
    end
    if frame == 1000 then
        dt.log("before", dt.state.lives, dt.state.time, dt.state.hp, dt.state.money)
        dt.set.lives(5)
        dt.set.time(123)
        dt.set.hp(1)
        dt.set.money(12340)
        dt.log("deferred", dt.state.lives)
        local range_ok, range_err = pcall(dt.set.lives, 10)
        dt.log("range", range_ok, range_err)
    end
    if frame == 1001 then
        dt.log("after", dt.state.lives, dt.state.time, dt.state.hp, dt.state.money)
    end
end)
'''

LIMIT_LUA = {
    'loop': 'dt.on("frame", function(f) if f == 100 then while true do end end end)',
    'pcall-loop': 'dt.on("frame", function(f) if f == 110 then while true do pcall(function() while true do end end) end end end)',
    'memory': (
        'dt.on("frame", function(f) if f == 120 then pcall(function() local t = {} for i = 1, 1000 do '
        't[i] = string.rep("x", 1000000) .. i end end) end end)'
    ),
    'error': 'dt.on("frame", function(f) if f == 130 then error("boom") end end)',
    'start-error': 'error("early")',
    'cancel-ignored': 'dt.on("new-game", function() return dt.CANCEL end)',
}

CANCEL_LUA = r'''
dt.on("timer-tick", function()
    return dt.CANCEL
end)
dt.on("frame", function(frame)
    if frame == 1000 or frame == 1400 then
        dt.log("state", frame, dt.state.land, dt.state.lives, dt.state.time, dt.state.hp)
    end
end)
'''


EVENTS_LUA = r'''
local function values(e)
    local t = {}
    for k, v in pairs(e) do
        if k ~= "name" then
            t[#t + 1] = k .. "=" .. v
        end
    end
    table.sort(t)
    return table.concat(t, " ")
end
for _, name in ipairs({"boss-hit", "boss-defeated", "enemy-defeated", "item-collected", "land-start", "land-clear",
                       "checkpoint", "money-changed"}) do
    dt.on(name, function(e)
        dt.log("event", dt.frame(), e.name, values(e))
    end)
end
dt.on("frame", function(frame)
    dt.draw.rect(8, 40, 24, 16, 0xFF00FF)
    dt.draw.text(8, 60, "draw", 0x00FF00)
    if frame == 2000 then
        local objects, me, cam = dt.objects(), nil, dt.camera()
        for _, o in ipairs(objects) do
            if o.slot == 0 then
                me = o
            end
        end
        dt.log("query", #objects, me.x, me.y, dt.state.x, dt.state.y, cam.x, cam.row)
    end
    if frame == 2001 then
        local shown = 0
        for _ = 1, 70 do
            if dt.draw.rect(0, 0, 1, 1, 0) then
                shown = shown + 1
            end
        end
        dt.log("cap", shown)
    end
end)
'''


def event_checks(exe, native):
    """Step 10: the events, dt.objects(), dt.camera() and dt.draw on full_amazon."""
    route = ROOT / 'tests' / 'routes' / 'full_amazon.txt'
    frames = 9600
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        mods = tmp / 'mods'
        mods.mkdir()
        script_mod(mods, 'events', EVENTS_LUA)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(host_exe, name, with_mods, is_native):
            out = tmp / name
            out.mkdir()
            cmd = [host_exe, ROM, '--frames', frames, '--input', route]
            cmd += ['--present-out', out / 'p.png', '--present-every', '900']
            cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini'] if is_native else ['--hash-out', out / 'hash.txt']
            if with_mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            log = p.stdout + p.stderr
            if p.returncode:
                print(log[-3000:])
                raise SystemExit(f'events {name}: exit {p.returncode}')
            return out, log

        def lines(log, what):
            return [
                line.split('\t')[1:]
                for line in log.splitlines()
                if line.startswith(f'[ducktales] Mod events: {what}\t')
            ]

        logs = {}
        for host, host_exe, is_native in (('headless', exe, False), ('native player', native, True)):
            if not host_exe:
                continue
            out, log = run(host_exe, host.replace(' ', '_'), True, is_native)
            logs[host] = log
            events = lines(log, 'event')
            named = {}
            for frame, name, args in events:
                named.setdefault(name, []).append((int(frame), dict(a.split('=') for a in args.split())))
            once = {n: [v['land'] for _, v in named.get(n, [])] for n in ('land-start', 'checkpoint', 'land-clear')}
            check(
                all(v == ['0'] for v in once.values()),
                f'{host}: land-start, checkpoint and land-clear each fire once in the Amazon ({once})',
                log[-3000:],
            )
            hits, beaten = named.get('boss-hit', []), named.get('boss-defeated', [])
            check(
                len(beaten) == 1 and hits and beaten[0][0] == hits[-1][0] and beaten[0][1] == {'land': '0'},
                f'{host}: boss-defeated fires once, with the last of {len(hits)} boss hits',
                log[-3000:],
            )
            enemies, items = named.get('enemy-defeated', []), named.get('item-collected', [])
            check(
                enemies
                and items
                and all(set(v) == {'slot', 'kind', 'x', 'y'} for _, v in enemies)
                and all(set(v) == {'slot', 'item', 'x', 'y'} for _, v in items),
                f'{host}: {len(enemies)} enemy-defeated and {len(items)} item-collected events carry slot, '
                'kind or item and position',
                log[-3000:],
            )
            money = named.get('money-changed', [])
            total = sum(int(v['delta']) for _, v in money)
            check(
                money and total == int(money[-1][1]['money']),
                f'{host}: {len(money)} money-changed deltas add up to the money shown last ({total})',
                log[-3000:],
            )
            query = (lines(log, 'query') or [[]])[0]
            nums = [int(v) for v in query] if len(query) == 7 else []
            check(
                nums and nums[0] >= 1 and nums[1:3] == nums[3:5] and nums[5] <= nums[1] < nums[5] + 256,
                f'{host}: dt.objects() has Scrooge where dt.state has him, and dt.camera() shows him ({query})',
                log[-3000:],
            )
            check(
                [c[0] for c in lines(log, 'cap')] == ['62']
                and log.count('dt.draw: more than 64 drawings in one frame') == 1,
                f'{host}: dt.draw stops at 64 drawings a frame and says so once',
                log[-3000:],
            )
            pic = png_read(out / 'p_00900.png')[2]
            rect = [pic[y * 256 + x] for y in range(40, 56) for x in range(8, 32)]
            text = [pic[y * 256 + x] for y in range(60, 65) for x in range(8, 24)]
            check(
                all(p == 0xFFFF00FF for p in rect) and 0xFF00FF00 in text,
                f'{host}: dt.draw shows its rectangle and text over the presented picture (frame 900)',
                f'{rect.count(0xFFFF00FF)} of {len(rect)} rectangle pixels magenta, {text.count(0xFF00FF00)} text '
                f'pixels green; text area {sorted({hex(p) for p in text})}',
            )
        if exe:
            base, _ = run(exe, 'base', False, False)
            check(
                (tmp / 'headless' / 'hash.txt').read_bytes() == (base / 'hash.txt').read_bytes(),
                f'headless: with every event handled, dt.objects(), dt.camera() and dt.draw the per-frame '
                f'machine hashes equal a run without mods (frames 0-{frames})',
            )
        if len(logs) == 2:
            a, b = (lines(logs[h], 'event') for h in ('headless', 'native player'))
            check(a == b, f'the native player logs the same {len(b)} events in the same frames as the cycle host')


def script_mod(folder, mod_id, text):
    make_mod(folder, mod_id, {'scripts/main.lua': text.encode()})


def states(log, mod_id):
    found = {}
    for line in log.splitlines():
        parts = line.split()
        if line.startswith(f'[ducktales] Mod {mod_id}: state') and len(parts) >= 9:
            found[int(parts[4])] = [None if p == 'nil' else int(p) for p in parts[5:9]]
    return found


def script_checks(exe, route, native):
    host = 'native player' if native else 'headless'
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)

        data = tmp / 'data'
        data.mkdir()
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(data))

        def run(name, mods, frames=1500, extra=()):
            cmd = [exe, ROM, '--frames', str(frames), '--input', route, *extra]
            if mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            if native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            if p.returncode:
                print(p.stdout + p.stderr)
                raise SystemExit(f'{host} scripts {name}: exit {p.returncode}')
            return p.stdout + p.stderr

        all_dir = tmp / 'mods_all'
        all_dir.mkdir()
        script_mod(all_dir, 'sandbox', SANDBOX_LUA)
        for mod_id, text in LIMIT_LUA.items():
            script_mod(all_dir, mod_id, text)
        log = run('all', all_dir)
        check('7 loaded' in log and '7 scripts' in log, f'{host}: seven script mods load', log)
        check(
            'Mod sandbox: sandbox\tclosed\tbinary-refused\t42' in log,
            f'{host}: io, os, package, debug, require, dofile, loadfile and string.dump are absent; '
            'load refuses a binary chunk and runs text',
            log,
        )
        check('Mod sandbox: event\tnew-game' in log, f'{host}: dt.on("new-game") runs at the title\'s set-up', log)
        reasons = {
            'loop': 'scripts/main.lua:1: ran more than 1000000 instructions in one callback (in a "frame" handler)',
            'pcall-loop': 'scripts/main.lua:1: ran more than 1000000 instructions in one callback (in a "frame" handler)',
            'memory': 'used more than 16 MiB of memory (in a "frame" handler)',
            'error': 'scripts/main.lua:1: boom (in a "frame" handler)',
            'start-error': 'scripts/main.lua:1: early (when the script started)',
        }
        for mod_id, why in reasons.items():
            check(f'Mod {mod_id}: script stopped: {why}' in log, f'{host}: {mod_id}: stopped with "{why}"', log)
        ignored = 'Mod cancel-ignored: a "new-game" handler returned dt.CANCEL, which that event ignores'
        check(
            log.count(ignored) == 1 and 'Mod cancel-ignored: script stopped' not in log,
            f'{host}: dt.CANCEL for an event that cannot be cancelled is reported once and the script runs on',
            log,
        )
        mods_log = (data / 'mods.log').read_bytes() if (data / 'mods.log').exists() else b''
        check(
            b'[ducktales] Mods: on, 7 found, 7 loaded' in mods_log
            and b'Mod sandbox: Sandbox 1.0.0, loaded' in mods_log
            and b'Mod error: script stopped: scripts/main.lua:1: boom' in mods_log
            and b'Mod sandbox: state\t1000' in mods_log,
            f'{host}: mods.log in the data folder holds the catalogue, dt.log lines and stopped scripts '
            f'({len(mods_log)} bytes)',
            mods_log.decode('utf-8', 'replace'),
        )
        cut = b'Mod sandbox: toast: ' + '\u00e9'.encode() * 64 + b'\r\n'
        check(
            cut in mods_log,
            f'{host}: dt.toast cuts 70 two-byte characters at 64 characters, not bytes',
            mods_log.decode('utf-8', 'replace')[-2000:],
        )
        seen = states(log, 'sandbox')
        check(
            sorted(seen) == [1000, 1400] and all(len(v) == 4 for v in seen.values()),
            f'{host}: the other mod still runs after them (dt.state at frames 1000 and 1400: {seen})',
            log,
        )
        check(
            len(seen) == 2 and seen[1000][2] != seen[1400][2], f'{host}: the HUD time runs without a cancel', str(seen)
        )

        cancel_dir = tmp / 'mods_cancel'
        cancel_dir.mkdir()
        script_mod(cancel_dir, 'stop-the-clock', CANCEL_LUA)
        log = run('cancel', cancel_dir)
        held = states(log, 'stop-the-clock')
        check(
            len(held) == 2 and held[1000][2] == held[1400][2] and seen and held[1000][2] != seen[1400][2],
            f'{host}: returning dt.CANCEL from "timer-tick" holds the HUD time ({held})',
            log,
        )

        api_dir = tmp / 'mods_api'
        api_dir.mkdir()
        script_mod(api_dir, 'api', API_LUA)
        shots = tmp / 'shots_api'
        shots.mkdir()
        log = run('api', api_dir, frames=1101, extra=('--present-out', shots / 'p.png', '--present-every', '900'))
        lines = {
            line.split(': ', 1)[1].split('\t')[0]: line.split(': ', 1)[1].split('\t')[1:]
            for line in log.splitlines()
            if line.startswith('[ducktales] Mod api: ') and '\t' in line
        }
        check(
            lines.get('outside', [''])[0] == 'false' and 'dt.set.lives is called in dt.on callbacks' in log,
            f'{host}: dt.set outside a callback is refused',
            log,
        )
        before, after = lines.get('before', []), lines.get('after', [])
        check(
            lines.get('deferred') == before[:1]
            and after[:1] == ['5']
            and after[1:2] in (['123'], ['122'])
            and after[2:3] == ['1']
            and after[3:4] == ['12340'],
            f'{host}: dt.set.lives/time/hp/money apply at the frame\'s end (before {before}, after {after})',
            log,
        )
        check(
            lines.get('range', [''])[0] == 'false' and 'dt.set.lives(10): out of range 0 to 9' in log,
            f'{host}: dt.set checks the range',
            log,
        )
        check(
            lines.get('toasts') == ['true', 'false'] and 'Mod api: toast: Hello from api' in log,
            f'{host}: dt.toast shows a notice and refuses a second one within a second',
            log,
        )
        check(
            lines.get('store') == ['1', 'true', '3.5', 'true'],
            f'{host}: dt.store keeps each type ({lines.get("store")})',
            log,
        )
        check(
            lines.get('store-cap', [''])[0] == 'false'
            and 'more than 65536 bytes' in log
            and lines.get('store-refused') == ['false', 'false'],
            f'{host}: dt.store refuses more than 64 KiB, tables and a key with a path in it',
            log,
        )
        base_dir = tmp / 'shots_base'
        base_dir.mkdir()
        run('plain', None, frames=901, extra=('--present-out', base_dir / 'p.png', '--present-every', '900'))
        shot, plain = png_read(shots / 'p_00900.png')[2], png_read(base_dir / 'p_00900.png')[2]
        rows = sorted({i // 256 for i, (x, y) in enumerate(zip(shot, plain)) if x != y})
        check(
            rows and rows[-1] < 120,
            f'{host}: the toast is drawn over the presented picture near the top (rows {rows[:1]}-{rows[-1:]})',
        )
        log = run('api-again', api_dir, frames=10)
        stored = (data / 'mods-data' / 'api.ini').read_text() if (data / 'mods-data' / 'api.ini').exists() else ''
        check(
            not (data / 'mods-data' / 'api.ini.tmp').exists(),
            f'{host}: dt.store leaves no temporary file after its write',
        )
        check(
            'Mod api: store\t2\ttrue\t3.5\ttrue' in log
            and 'runs = i:2' in stored
            and 'text = s:a\\nb\\\\c = d' in stored,
            f'{host}: dt.store comes back in the next run from mods-data/api.ini',
            log + stored,
        )

        reload_dir = tmp / 'mods_reload'
        script = reload_dir / 'reload' / 'scripts' / 'main.lua'
        script.parent.mkdir(parents=True)
        (reload_dir / 'reload' / 'mod.toml').write_text(mod_toml('reload', 'Reload'))
        versions = [
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version one") end end)',
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version two") end end)',
            'dt.on("frame", function(',
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version three") end end)',
        ]
        stamp = (int(time.time()) - 1) * 10**9

        def save(version):
            pending = script.with_suffix('.tmp')
            pending.write_text(versions[version])
            os.utime(pending, ns=(stamp, stamp + version * 10**8))
            pending.replace(script)

        save(0)
        cmd = [exe, ROM, '--frames', '2000000', '--input', route, '--mods', 'on', '--mods-dir', reload_dir]
        if native:
            cmd += ['--config', tmp / 'x.ini']
        p = subprocess.Popen(
            [str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env
        )
        steps = [
            ('Mod reload: version one', 1),
            ('Mod reload: version two', 2),
            ('Mod reload: script stopped: scripts/main.lua:1:', 3),
            ('Mod reload: version three', None),
        ]
        got = []
        watchdog = threading.Timer(180, p.kill)
        watchdog.start()
        try:
            for want, write in steps:
                for line in p.stdout:
                    if want in line:
                        got.append(want)
                        break
                if write is not None:
                    save(write)
        finally:
            watchdog.cancel()
            p.kill()
            p.wait()
        check(
            len(got) == len(steps),
            f'{host}: hot reload: same-size edit in the same timestamp second runs, a broken script '
            f'stops and the fix runs ({len(got)} of {len(steps)} steps seen)',
        )


def catalogue_checks(exe, route, native):
    """mod.toml validation, names in any case, a byte order mark, two files for one asset and
    --mods-dir errors, from the log of one short run."""
    host = 'native player' if native else 'headless'
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        mods = tmp / 'mods'
        mods.mkdir()
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(*extra):
            cmd = [exe, ROM, '--frames', '30', '--input', route, *extra]
            if native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=600, env=env)

        def raw(name, files):
            with zipfile.ZipFile(mods / f'{name}.dtmod', 'w', zipfile.ZIP_DEFLATED) as z:
                for path, data in files.items():
                    z.writestr(path, data)

        toml = mod_toml('bom', 'Bom').encode()
        raw('bom', {'mod.toml': b'\xef\xbb\xbf' + toml, 'scripts/main.lua': b'\xef\xbb\xbfdt.log("bom", "ok")'})
        raw('case', {'MOD.TOML': mod_toml('case', 'Case') + '   ', 'Scripts/Main.lua': 'dt.log("case", "ok")'})
        raw('wrong-target', {'mod.toml': mod_toml('case', 'A different game', prg='0' * 64)})
        bad = {
            'missing': 'id = "missing"\nname = "Missing"\ngame = "DuckTales (USA)"\n',
            'unquoted': mod_toml('unquoted', 'Unquoted').replace('version = "1.0.0"', 'version = 1.0'),
            'twice': mod_toml('twice', 'Twice') + 'name = "Again"\n',
            'bad-sha': mod_toml('bad-sha', 'Bad Sha', prg='abc'),
            'long-id': mod_toml('a' * 64, 'Long Id'),
            'nul-byte': mod_toml('nul-byte', 'Nul Byte') + '\0name = "Hidden"',
        }
        for name, text in bad.items():
            raw(name, {'mod.toml': text})
        tile = png_bytes(8, 8, [0xFFFF00FF] * 64, tmp)
        wav = tone_wav(tmp / 'tone.wav')
        make_mod(mods, 'dup-tile', {'tiles/a/0123456789abcdef.png': tile, 'tiles/b/0123456789ABCDEF.png': tile})
        make_mod(mods, 'dup-pal', {'palettes/amazon.pal': bytes(192), 'palettes/x/Amazon.pal': bytes(192)})
        make_mod(mods, 'dup-sfx', {'sfx/13.wav': wav, 'sfx/13-swing.wav': wav})
        p = run('--mods', 'on', '--mods-dir', mods)
        log = p.stdout + p.stderr
        check(p.returncode == 0 and '12 found, 2 loaded' in log, f'{host}: two good mods load beside ten bad ones', log)
        check(
            'Mod bom: bom\tok' in log and 'Mod case: case\tok' in log,
            f'{host}: mod.toml and scripts/main.lua with a UTF-8 byte order mark, and MOD.TOML and '
            'Scripts/Main.lua, load',
            log,
        )
        reasons = {
            'missing': 'mod.toml is missing version, prg_sha256.',
            'unquoted': 'Cannot be read: mod.toml line 3: version must be a quoted string (version = "...")',
            'twice': 'Cannot be read: mod.toml line 8: name is set twice',
            'bad-sha': 'mod.toml: prg_sha256 must be the 64 hex digits of the PRG\'s SHA-256.',
            'dup-tile': 'Not loaded: tiles/b/0123456789ABCDEF.png: the mod has another file for this tile and palette.',
            'dup-pal': 'Not loaded: palettes/x/Amazon.pal: the mod has another amazon.pal.',
            'dup-sfx': 'Not loaded: sfx/13-swing.wav: the mod has another file for effect 13.',
            'long-id.dtmod': 'Cannot be read: mod.toml line 1: id is longer than 63 bytes',
            'nul-byte.dtmod': 'Cannot be read: mod.toml contains a NUL byte; save it as UTF-8 text.',
        }
        for mod_id, why in reasons.items():
            check(f'Mod {mod_id}: {why}' in log, f'{host}: {mod_id} is refused with "{why}"', log)
        p = run('--mods', 'on', '--mods-dir', mods, '--mods-order', 'case:off')
        log = p.stdout + p.stderr
        check(
            p.returncode == 0 and '12 found, 1 loaded' in log and 'Mod case: case\tok' not in log,
            f'{host}: a refused package with the same id does not steal the valid mod switch',
            log,
        )
        dump = tmp / 't\u00edles' / 'nested'
        hashes = () if native else ('--hash-out', tmp / 'dump.txt')
        p = run('--mods-dump', dump, *hashes)
        keys = list(dump.glob('*.png'))
        check(
            p.returncode == 0 and keys and png_read(keys[0])[:2] == (8, 8),
            f'{host}: dumping with mods off creates a nested UTF-8 path and writes {len(keys)} templates',
            p.stdout + p.stderr,
        )
        if not native:
            plain = run('--hash-out', tmp / 'plain.txt')
            expected = (tmp / 'plain.txt').read_bytes()
            check(
                plain.returncode == 0 and expected and (tmp / 'dump.txt').read_bytes() == expected,
                'headless: collecting templates leaves all 30 machine hash records identical',
                plain.stdout + plain.stderr,
            )
        p = run('--mods-dir', tmp / 'nowhere')
        check(
            p.returncode != 0 and 'not a folder' in p.stdout + p.stderr,
            f'{host}: --mods-dir with a path that is not a folder is refused (exit {p.returncode})',
            p.stdout + p.stderr,
        )


def headless_checks(exe, route):

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(name, mods=None, order=None, frames=1500, every=300, hashes=False, wav=False, extra=(), on=True):
            out = tmp / name
            out.mkdir()
            cmd = [
                exe,
                ROM,
                '--frames',
                str(frames),
                '--input',
                route,
                '--present-out',
                out / 'p.png',
                '--present-every',
                str(every),
            ]
            if mods:
                cmd += ['--mods-dir', mods]
                if on:
                    cmd += ['--mods', 'on']
            if order:
                cmd += ['--mods-order', order]
            if hashes:
                cmd += ['--hash-out', out / 'hash.txt']
            if wav:
                cmd += ['--wav-out', out / 'a.wav']
            p = subprocess.run([str(c) for c in [*cmd, *extra]], capture_output=True, text=True, env=env)
            if p.returncode:
                print(p.stdout + p.stderr)
                raise SystemExit(f'{name}: exit {p.returncode}')
            pics = {}
            for f in out.glob('p*.png'):
                digits = ''.join(c for c in f.stem if c.isdigit())
                pics[int(digits) if digits else frames] = png_read(f)[2]
            return out, pics, p.stdout + p.stderr

        base_dir, base, _ = run('base', hashes=True)
        frames = sorted(k for k in base if k in (300, 1500))
        check(frames == [300, 1500], 'the presented pictures of frames 300 and 1500 are written', str(sorted(base)))

        dump = tmp / 'dump'
        run('dump', every=1500, extra=('--mods', 'on', '--mods-dump', dump))
        keys = sorted(dump.glob('*.png'))
        check(len(keys) > 20, f'--mods-dump wrote the tiles drawn ({len(keys)} templates)')

        key = pick_drawn_tile(keys, base.get(1500, []))
        check(key is not None, 'a lopsided, opaque tile drawn in frame 1500 is among them')
        if key is None:
            return 1

        pal = nes_palette()
        known = {0xFF000000 | r << 16 | g << 8 | b for r, g, b in pal}
        check(
            all(px in known for px in base[1500]),
            'the 64 colours of the test match the renderer\'s (every pixel of frame 1500)',
        )

        mods = {}

        def folder(name, *entries):
            d = tmp / f'mods_{name}'
            d.mkdir()
            for mod_id, files, *rest in entries:
                make_mod(d, mod_id, files, *rest)
            mods[name] = d
            return d

        inverted = bytes(255 - c for rgb in pal for c in rgb)
        green = bytes(c for _ in pal for c in (0, 200, 0))
        magenta = png_bytes(8, 8, [0xFFFF00FF] * 64, tmp)
        cyan = png_bytes(8, 8, [0xFF00FFFF] * 64, tmp)
        tone = tmp / 'tone.wav'
        tone_wav(tone)

        # 1. machine hashes with every kind of replacement loaded
        folder(
            'all',
            (
                'all-kinds',
                {
                    'palettes/global.pal': inverted,
                    f'tiles/{key.name}': magenta,
                    'music/07-title.wav': tone.read_bytes(),
                },
            ),
        )
        all_dir, _, log = run('all', mods['all'], hashes=True)
        check('1 loaded' in log, 'the mod with a palette, a tile and a music replacement loads', log)
        check(
            (all_dir / 'hash.txt').read_bytes() == (base_dir / 'hash.txt').read_bytes(),
            'per-frame machine hashes (trace, RAM, picture, registers, cycles, hardware) equal the run without mods, frames 0-1500',
        )

        off_dir, off, log = run('off', mods['all'], hashes=True, on=False)
        check(
            '1 loaded' not in log
            and off[300] == base[300]
            and off[1500] == base[1500]
            and (off_dir / 'hash.txt').read_bytes() == (base_dir / 'hash.txt').read_bytes(),
            'mods are off by default: the same folder without --mods on changes neither the pictures nor the hashes',
            log,
        )

        # 2. palettes
        folder('inv', ('inverse', {'palettes/global.pal': inverted}))
        _, inv, log = run('inv', mods['inv'])
        for f in (300, 1500):
            same = all(p == (0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(inv[f], base[f]))
            check(same, f'global palette: every pixel of frame {f} is the inverse of the picture without mods')
        folder('land', ('amazon-only', {'palettes/amazon.pal': inverted}))
        _, land, _ = run('land', mods['land'])
        check(land[300] == base[300], 'amazon.pal leaves Land Select (frame 300) alone')
        check(
            all(p == (0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(land[1500], base[1500])),
            'amazon.pal inverts every pixel of The Amazon (frame 1500)',
        )

        # 3. one tile
        folder(
            'same',
            ('same-tile', {f'tiles/{key.name}': key.read_bytes()}),
        )
        _, same, log = run('same', mods['same'])
        check(
            '1 loaded' in log and same[1500] == base[1500],
            f'tile {key.stem}: its own template as the art leaves the picture identical',
            log,
        )
        folder('mag', ('magenta-tile', {f'tiles/{key.name}': magenta}))
        _, mag, _ = run('mag', mods['mag'])
        changed = [i for i, (p, q) in enumerate(zip(mag[1500], base[1500])) if p != q]
        check(
            changed and all(mag[1500][i] == 0xFFFF00FF for i in changed),
            f'tile {key.stem} in magenta: {len(changed)} pixels changed, every one of them magenta',
        )
        loose_key = key.stem.split('_')[0] + '.png'
        loose = tmp / 'mods_loose'
        loose.mkdir()
        make_mod(loose, 'any-palette', {f'tiles/hud/{loose_key}': magenta}, loose=True)
        _, anyp, log = run('loose', loose)
        check(
            all(anyp[1500][i] == 0xFFFF00FF for i in changed),
            'a loose folder with tiles/<hash>.png (any palette, in a subfolder) replaces the same pixels',
            log,
        )

        # 4. priority
        folder(
            'prio',
            ('first', {f'tiles/{key.name}': magenta, 'palettes/global.pal': inverted}),
            ('second', {f'tiles/{key.name}': cyan, 'palettes/global.pal': green}),
        )
        _, ab, _ = run('ab', mods['prio'], 'first:on,second:on')
        _, ba, _ = run('ba', mods['prio'], 'second:on,first:on')
        _, b_only, _ = run('b_only', mods['prio'], 'first:off,second:on')
        check(
            all(ab[1500][i] == 0xFFFF00FF for i in changed) and ab[300][0] == inv[300][0],
            'order first, second: the first mod\'s tile and palette show',
        )
        check(
            all(ba[1500][i] == 0xFF00FFFF for i in changed) and ba[300][0] == 0xFF00C800,
            'order second, first: the second mod\'s tile and palette show',
        )
        check(b_only[1500] == ba[1500], 'first switched off: the second mod alone, as when it is on top')

        # 5. refusals
        folder('bad', ('other-game', {'palettes/global.pal': inverted}, '0' * 64))
        (mods['bad'] / 'random.dtmod').write_bytes(bytes(random.Random(1).randrange(256) for _ in range(4000)))
        good = tmp / 'good.dtmod'
        make_mod(tmp, 'good', {'palettes/global.pal': inverted})
        data = (tmp / 'good.dtmod').read_bytes()
        (mods['bad'] / 'truncated.dtmod').write_bytes(data[: len(data) // 2])
        make_mod(mods['bad'], 'still-fine', {f'tiles/{key.name}': magenta})
        _, bad, log = run('bad', mods['bad'])
        check(
            'Mod other-game: Made for a different version of the game' in log,
            'a mod for another PRG is refused with a clear message',
            log,
        )
        check(
            'Mod random.dtmod: Cannot be read: Not a zip archive' in log
            and 'Mod truncated.dtmod: Cannot be read' in log,
            'a random file and a truncated archive are listed as unreadable',
            log,
        )
        check(
            '4 found, 1 loaded' in log
            and all(bad[1500][i] == 0xFFFF00FF for i in changed)
            and all(bad[1500][i] == base[1500][i] for i in range(len(base[1500])) if i not in set(changed)),
            'the other mod still loads, and only its tile changes',
            log,
        )

        # 6. music
        quiet = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
        folder('tone', ('title-tone', {'music/07.wav': tone.read_bytes()}))
        a_dir, _, _ = run('wav_base', frames=240, every=240, wav=True, hashes=True)
        b_dir, _, log = run('wav_tone', mods['tone'], frames=240, every=240, wav=True, hashes=True)
        (_, a), (_, b) = samples(a_dir / 'a.wav'), samples(b_dir / 'a.wav')
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)
        check(
            first is not None and 7 * 790 <= first <= 9 * 810,
            f'the sound is unchanged until the title requests music 07 (first difference at sample {first}, frame ~{(first or 0) / 798.7:.1f})',
        )
        sa, sb = share_440(a, 48000), share_440(b, 48000)
        check(
            sb > 0.9 and sa < 0.2,
            f'frames 30-230: the 440 Hz tone carries {sb:.0%} of the energy with the mod, {sa:.0%} without',
        )
        check(
            (a_dir / 'hash.txt').read_bytes() == (b_dir / 'hash.txt').read_bytes(),
            'per-frame machine hashes are equal with the music replaced (frames 0-240)',
        )

        # 7. a script that only reads
        folder('reader', ('sandbox', {'scripts/main.lua': SANDBOX_LUA.encode()}))
        reader_dir, _, log = run('reader', mods['reader'], hashes=True)
        check(
            'state\t1400' in log and (reader_dir / 'hash.txt').read_bytes() == (base_dir / 'hash.txt').read_bytes(),
            'a script with "frame" and "new-game" handlers that reads dt.state leaves the machine hashes equal, frames 0-1500',
            log,
        )


if __name__ == '__main__':
    sys.exit(main())
