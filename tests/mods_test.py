"""Mods (src/mods, docs/mods.md) on the headless build, with mods made on the fly.

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
     the run without mods (step 1 covers it).

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
import wave
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / 'roms' / 'DuckTales (USA).nes'
PRG_SHA256 = '8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013'
failures = []


def check(ok, what, detail=''):
    print(('PASS ' if ok else 'FAIL ') + what + (('\n    ' + detail[-1500:]) if detail and not ok else ''))
    if not ok:
        failures.append(what)


def png_write(path, w, h, pixels):
    raw = b''.join(b'\0' + b''.join(struct.pack('>I', p)[1:] + bytes([p >> 24]) for p in pixels[y * w:(y + 1) * w])
                   for y in range(h))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def png_read(path):
    """(w, h, [0xAARRGGBB]) of an 8-bit RGB or RGBA, non-interlaced PNG."""
    data = Path(path).read_bytes()
    at, idat, w = 8, b'', 0
    while at < len(data):
        n, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            bpp = {2: 3, 6: 4}[color]
            assert depth == 8 and body[12] == 0
        elif kind == b'IDAT':
            idat += body
        at += 12 + n
    raw, stride, prev, out = zlib.decompress(idat), w * bpp, bytearray(w * bpp), []
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
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
            px = line[x * bpp:x * bpp + bpp]
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
    return (f'id = "{mod_id}"\nname = "{name}"\nversion = "1.0.0"\nauthor = "mods_test"\n'
            f'description = """Generated by tests/mods_test.py."""\ngame = "{game}"\nprg_sha256 = "{prg}"\n')


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    route = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)

        def run(name, mods=None, order=None, frames=1500, every=300, hashes=False, wav=False, extra=(), on=True):
            out = tmp / name
            out.mkdir()
            cmd = [exe, ROM, '--frames', str(frames), '--input', route, '--present-out', out / 'p.png',
                   '--present-every', str(every)]
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
            p = subprocess.run([str(c) for c in [*cmd, *extra]], capture_output=True, text=True)
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

        def lopsided(path):
            w, h, px = png_read(path)
            rows = [px[y * 8:(y + 1) * 8] for y in range(8)]
            return (len(set(px)) >= 3 and rows != rows[::-1] and rows != [r[::-1] for r in rows]
                    and all(p >> 24 == 255 for p in px))
        key = next((k for k in keys if lopsided(k)), None)
        check(key is not None, 'a lopsided, opaque tile is among them', '')
        if key is None:
            return 1

        pal = nes_palette()
        known = {0xFF000000 | r << 16 | g << 8 | b for r, g, b in pal}
        check(all(px in known for px in base[1500]), 'the 64 colours of the test match the renderer\'s (every pixel of frame 1500)')

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
        with wave.open(str(tone), 'wb') as wv:
            wv.setnchannels(1)
            wv.setsampwidth(2)
            wv.setframerate(48000)
            wv.writeframes(b''.join(struct.pack('<h', int(12000 * math.sin(2 * math.pi * 440 * i / 48000)))
                                    for i in range(48000 * 2)))

        # 1. machine hashes with every kind of replacement loaded
        folder('all', ('all-kinds', {'palettes/global.pal': inverted, f'tiles/{key.name}': magenta,
                                     'music/07-title.wav': tone.read_bytes()}))
        all_dir, _, log = run('all', mods['all'], hashes=True)
        check('1 loaded' in log, 'the mod with a palette, a tile and a music replacement loads', log)
        check((all_dir / 'hash.txt').read_bytes() == (base_dir / 'hash.txt').read_bytes(),
              'per-frame machine hashes (trace, RAM, picture, registers, cycles, hardware) equal the run without mods, frames 0-1500')

        off_dir, off, log = run('off', mods['all'], hashes=True, on=False)
        check('1 loaded' not in log and off[300] == base[300] and off[1500] == base[1500]
              and (off_dir / 'hash.txt').read_bytes() == (base_dir / 'hash.txt').read_bytes(),
              'mods are off by default: the same folder without --mods on changes neither the pictures nor the hashes', log)

        # 2. palettes
        folder('inv', ('inverse', {'palettes/global.pal': inverted}))
        _, inv, log = run('inv', mods['inv'])
        for f in (300, 1500):
            same = all(p == (0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(inv[f], base[f]))
            check(same, f'global palette: every pixel of frame {f} is the inverse of the picture without mods')
        folder('land', ('amazon-only', {'palettes/amazon.pal': inverted}))
        _, land, _ = run('land', mods['land'])
        check(land[300] == base[300], 'amazon.pal leaves Land Select (frame 300) alone')
        check(all(p == (0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(land[1500], base[1500])),
              'amazon.pal inverts every pixel of The Amazon (frame 1500)')

        # 3. one tile
        folder('same', ('same-tile', {f'tiles/{key.name}': key.read_bytes()}), )
        _, same, log = run('same', mods['same'])
        check('1 loaded' in log and same[1500] == base[1500],
              f'tile {key.stem}: its own template as the art leaves the picture identical', log)
        folder('mag', ('magenta-tile', {f'tiles/{key.name}': magenta}))
        _, mag, _ = run('mag', mods['mag'])
        changed = [i for i, (p, q) in enumerate(zip(mag[1500], base[1500])) if p != q]
        check(changed and all(mag[1500][i] == 0xFFFF00FF for i in changed),
              f'tile {key.stem} in magenta: {len(changed)} pixels changed, every one of them magenta')
        loose_key = key.stem.split('_')[0] + '.png'
        loose = tmp / 'mods_loose'
        loose.mkdir()
        make_mod(loose, 'any-palette', {f'tiles/hud/{loose_key}': magenta}, loose=True)
        _, anyp, log = run('loose', loose)
        check(all(anyp[1500][i] == 0xFFFF00FF for i in changed),
              'a loose folder with tiles/<hash>.png (any palette, in a subfolder) replaces the same pixels', log)

        # 4. priority
        folder('prio', ('first', {f'tiles/{key.name}': magenta, 'palettes/global.pal': inverted}),
               ('second', {f'tiles/{key.name}': cyan, 'palettes/global.pal': green}))
        _, ab, _ = run('ab', mods['prio'], 'first:on,second:on')
        _, ba, _ = run('ba', mods['prio'], 'second:on,first:on')
        _, b_only, _ = run('b_only', mods['prio'], 'first:off,second:on')
        check(all(ab[1500][i] == 0xFFFF00FF for i in changed) and ab[300][0] == inv[300][0],
              'order first, second: the first mod\'s tile and palette show')
        check(all(ba[1500][i] == 0xFF00FFFF for i in changed) and ba[300][0] == 0xFF00C800,
              'order second, first: the second mod\'s tile and palette show')
        check(b_only[1500] == ba[1500], 'first switched off: the second mod alone, as when it is on top')

        # 5. refusals
        folder('bad', ('other-game', {'palettes/global.pal': inverted}, '0' * 64))
        (mods['bad'] / 'random.dtmod').write_bytes(bytes(random.Random(1).randrange(256) for _ in range(4000)))
        good = tmp / 'good.dtmod'
        make_mod(tmp, 'good', {'palettes/global.pal': inverted})
        data = (tmp / 'good.dtmod').read_bytes()
        (mods['bad'] / 'truncated.dtmod').write_bytes(data[:len(data) // 2])
        make_mod(mods['bad'], 'still-fine', {f'tiles/{key.name}': magenta})
        _, bad, log = run('bad', mods['bad'])
        check('Mod other-game: Made for a different version of the game' in log,
              'a mod for another PRG is refused with a clear message', log)
        check('Mod random.dtmod: Cannot be read: Not a zip archive' in log and 'Mod truncated.dtmod: Cannot be read' in log,
              'a random file and a truncated archive are listed as unreadable', log)
        check('4 found, 1 loaded' in log and all(bad[1500][i] == 0xFFFF00FF for i in changed)
              and all(bad[1500][i] == base[1500][i] for i in range(len(base[1500])) if i not in set(changed)),
              'the other mod still loads, and only its tile changes', log)

        # 6. music
        quiet = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
        folder('tone', ('title-tone', {'music/07.wav': tone.read_bytes()}))
        a_dir, _, _ = run('wav_base', frames=240, every=240, wav=True, hashes=True)
        b_dir, _, log = run('wav_tone', mods['tone'], frames=240, every=240, wav=True, hashes=True)

        def samples(p):
            with wave.open(str(p)) as wv:
                return struct.unpack(f'<{wv.getnframes()}h', wv.readframes(wv.getnframes()))
        a, b = samples(a_dir / 'a.wav'), samples(b_dir / 'a.wav')
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)
        check(first is not None and 7 * 790 <= first <= 9 * 810,
              f'the sound is unchanged until the title requests music 07 (first difference at sample {first}, frame ~{(first or 0) / 798.7:.1f})')

        def share_440(s):
            s = s[48000 // 60 * 30:48000 // 60 * 230]
            c = sum(x * math.cos(2 * math.pi * 440 * i / 48000) for i, x in enumerate(s))
            d = sum(x * math.sin(2 * math.pi * 440 * i / 48000) for i, x in enumerate(s))
            power = 2 * (c * c + d * d) / len(s)
            total = sum(x * x for x in s) or 1
            return power / total
        sa, sb = share_440(a), share_440(b)
        check(sb > 0.9 and sa < 0.2, f'frames 30-230: the 440 Hz tone carries {sb:.0%} of the energy with the mod, {sa:.0%} without')
        check((a_dir / 'hash.txt').read_bytes() == (b_dir / 'hash.txt').read_bytes(),
              'per-frame machine hashes are equal with the music replaced (frames 0-240)')

    print(f'\n{"FAILED: " + ", ".join(failures) if failures else "all mods checks passed"}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
