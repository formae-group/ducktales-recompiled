"""Steps 1-7 of the mods test (tests/mods_test.py): tiles, palettes, priority, refusals and music on
the headless build, compared with the machine hashes and pictures of a run without mods, and the
tile, palette and music checks on the native player."""

import os
import random
import subprocess
import tempfile
from pathlib import Path

from .common import (
    ROM,
    ROOT,
    check,
    make_mod,
    nes_palette,
    pick_drawn_tile,
    png_bytes,
    png_read,
    samples,
    share_440,
    tone_wav,
)
from .scripts import SANDBOX_LUA


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
