"""Step 12 of the mods test (tests/mods_test.py): sprite replacements (docs/MODS.md "Sprites") on
both runtimes, the headless build (the cycle host) and the native player, with art made from the
player's own ROM through --mods-dump in a temporary folder.

  - --mods-dump writes every object frame drawn as sprites/<kind>/<frame>_<x>_<y>.png, Scrooge's
    (kind 00) among them;
  - the dumped frames themselves as the art (an identity mod): the picture is the one without mods
    on frames 900-1100 and 1500, out of fades and edges (placement, anchor, mirroring and order
    are right), with No sprite flicker on and off; the per-frame machine hashes (--hash-out,
    headless) equal the run without mods, with this mod and with the magenta one below;
  - every Scrooge frame as a magenta box 4 pixels larger on each side: in the last frame that
    shows him every pixel that changed is magenta and the box is whole; with a cyan _flip file as
    well, every presented frame shows as many box pixels, cyan where he is flipped, and the
    frames compared with the identity mod include flipped ones (the mirroring is checked);
  - with every tile template magenta and an inverted global palette also loaded, the identity art
    still shows the colours of the run without mods in frame 1500: a sprite replacement wins over
    tiles and palettes;
  - a file under sprites/ with a bad name refuses the mod with the reason.
"""

import os
import re
import subprocess
import tempfile
from pathlib import Path

from .common import ROM, check, make_mod, nes_palette, png_bytes, png_read

FRAMES = 1500
SAME = [*range(900, 1101, 25), 1500]
MAGENTA, CYAN = 0xFFFF00FF, 0xFF00FFFF


def sprite_checks(exe, native, route):
    if exe:
        one_host('headless', exe, route, (), hashes=True)
    if native:
        one_host('native player', native, route, ('--lag', 'faithful'), hashes=False)


def boxes(dump, color, tmp, flip=False):
    """Every kind 00 frame of the dump as a box of color, 4 pixels larger on each side."""
    files = {}
    for f in (dump / 'sprites' / '00').glob('*.png'):
        w, h, _ = png_read(f)
        frame, x, y = f.stem.split('_')
        name = f'sprites/00/{frame}_{int(x) + 4}_{int(y) + 4}{"_flip" if flip else ""}.png'
        files[name] = png_bytes(w + 8, h + 8, [color] * ((w + 8) * (h + 8)), tmp)
    return files


def one_host(host, exe, route, options, hashes):
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(name, mods=None, flicker=True, frames=FRAMES + 1, extra=()):
            out = tmp / name
            out.mkdir()
            cmd = [exe, ROM, '--frames', str(frames), '--input', route, '--config', tmp / 'x.ini']
            cmd += ['--present-out', out / 'p.png', '--present-every', '25', '--no-sprite-flicker']
            cmd += ['on' if flicker else 'off', *options]
            if hashes:
                cmd += ['--hash-out', out / 'hash.txt']
            if mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            p = subprocess.run([str(c) for c in [*cmd, *extra]], capture_output=True, text=True, timeout=3600, env=env)
            if p.returncode:
                print(p.stdout + p.stderr)
                raise SystemExit(f'{host} {name}: exit {p.returncode}')
            pics = {}
            for f in out.glob('p*.png'):
                digits = ''.join(c for c in f.stem if c.isdigit())
                pics[int(digits) if digits else frames] = png_read(f)[2]
            return out, pics, p.stdout + p.stderr

        def mod(name, files):
            d = tmp / f'mods_{name}'
            d.mkdir()
            make_mod(d, name, files)
            return d

        def same_hashes(a, b, what):
            if hashes:
                check((a / 'hash.txt').read_bytes() == (b / 'hash.txt').read_bytes(), f'{host}: {what}')

        base_dir, base, _ = run('base')
        _, base_off, _ = run('base_off', flicker=False)
        dump = tmp / 'templates'
        run('dump', extra=('--mods', 'on', '--mods-dump', dump))
        frames = sorted(dump.glob('sprites/*/*.png'))
        named = all(
            re.fullmatch(r'[0-9a-f]{2}/[0-9a-f]{2}_-?\d+_-?\d+\.png', f'{f.parent.name}/{f.name}') for f in frames
        )
        scrooge = list((dump / 'sprites' / '00').glob('*.png'))
        check(
            len(frames) > 10 and named and len(scrooge) > 5,
            f'{host}: --mods-dump wrote {len(frames)} object frames as sprites/<kind>/<frame>_<x>_<y>.png, '
            f'{len(scrooge)} of them kind 00 (Scrooge)',
        )
        if not scrooge:
            return
        identity = {f'sprites/{f.parent.name}/{f.name}': f.read_bytes() for f in frames}

        same_dir, same, log = run('identity', mod('identity', identity))
        _, same_off, _ = run('identity_off', mod('identity-off', identity), flicker=False)
        check('1 loaded' in log, f'{host}: the mod with {len(identity)} sprite replacements loads', log)
        check(
            all(same.get(n) == base.get(n) is not None for n in SAME),
            f'{host}: with the dumped frames as the art, frames {SAME[0]}-{SAME[-2]} and {SAME[-1]} equal the run '
            'without mods',
            str([n for n in SAME if same.get(n) != base.get(n)]),
        )
        check(
            all(same_off.get(n) == base_off.get(n) is not None for n in SAME),
            f'{host}: the same with No sprite flicker off (the OAM\'s order, 8 a line)',
            str([n for n in SAME if same_off.get(n) != base_off.get(n)]),
        )
        same_hashes(same_dir, base_dir, 'per-frame machine hashes equal the run without mods')

        mag_dir, mag, _ = run('magenta', mod('magenta', boxes(dump, MAGENTA, tmp)))
        count = {n: sum(1 for p in pic if p == MAGENTA) for n, pic in mag.items()}
        at = max((n for n in count if count[n]), default=None)
        changed = [i for i, (p, q) in enumerate(zip(mag[at], base[at])) if p != q] if at else []
        check(
            changed and all(mag[at][i] == MAGENTA for i in changed) and count[at] >= 32 * 40,
            f'{host}: Scrooge as a magenta box: frame {at} changed {len(changed)} pixels, every one magenta, '
            f'{count[at] if at else 0} magenta',
        )
        same_hashes(mag_dir, base_dir, 'per-frame machine hashes equal the run without mods (magenta)')
        _, flip, _ = run('flip', mod('flip', {**boxes(dump, MAGENTA, tmp), **boxes(dump, CYAN, tmp, flip=True)}))
        both = {n: sum(1 for p in pic if p in (MAGENTA, CYAN)) for n, pic in flip.items()}
        flipped = [n for n in SAME if CYAN in flip.get(n, ())]
        check(
            both == count and flipped,
            f'{host}: with a _flip file every frame shows as many box pixels, cyan in the flipped frames '
            f'({len(flipped)} of the compared frames)',
            str([n for n in count if both.get(n) != count[n]]),
        )

        inverted = bytes(255 - c for rgb in nes_palette() for c in rgb)
        magenta_tile = png_bytes(8, 8, [MAGENTA] * 64, tmp)
        tiles = {f'tiles/{f.name}': magenta_tile for f in dump.glob('*.png')}
        _, mixed, log = run('mixed', mod('mixed', {**identity, **tiles, 'palettes/global.pal': inverted}))
        kept = [i for i, (p, q) in enumerate(zip(mixed[FRAMES], base[FRAMES])) if p == q]
        painted = sum(1 for p in mixed[FRAMES] if p == MAGENTA)
        check(
            '1 loaded' in log
            and painted > 20000
            and 200 < len(kept) < 5000
            and all(p in (q, MAGENTA, 0xFF000000 | (~q & 0xFFFFFF)) for p, q in zip(mixed[FRAMES], base[FRAMES])),
            f'{host}: with every tile magenta ({painted} pixels) and the palette inverted, {len(kept)} pixels of '
            f'frame {FRAMES} keep the colours of the art',
            log,
        )

        _, _, log = run('bad', mod('bad-sprite', {'sprites/0/1_2_3.png': magenta_tile}), frames=2)
        check(
            '0 loaded' in log and 'a sprite is sprites/<kind in 2 hex digits>' in log,
            f'{host}: sprites/0/1_2_3.png refuses the mod with the reason',
            log,
        )
