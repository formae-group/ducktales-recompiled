"""Sprite presentation checks (src/host/ducktales_sprites.c, docs/rendering.md "Sprites without limits").

The window shows every sprite the game wants, in a fixed order, with no per-line or OAM limit,
while the machine keeps drawing the console's picture. On the headless build:

  1. the machine is untouched: --no-sprite-flicker on and off give identical per-frame hashes
     (trace, RAM, picture, registers, cycles, hardware) over the first 3,300 frames of full_amazon;
  2. off: in every window below, the presented picture (--present-out) is the machine's picture
     (--screenshot) on every frame;
  3. on: every pixel where the two differ lies inside a sprite: one of the frame's OAM entries, a
     HUD record, or a sprite the builder makes from the RAM the frame's OAM was built from
     (tools/render_oam.py replay); and every frame where the console drops, cuts, reorders or
     leaves out nothing (its OAM is the builder's, no line over 8, at most 63 entries wanted, no
     two objects overlap, HUD not left out) is identical;
  4. on, HUD: while Scrooge climbs the Amazon vine shaft high enough that the builder leaves the
     HUD out, every opaque pixel of every HUD record (pattern table $1000) that Scrooge's own
     opaque pixels do not cover differs from the machine's picture, which has no HUD there;
  5. on: frames whose lines carry more than 8 sprites, or that want more than 63 entries, are
     drawn differently from the machine's picture (the dropped sprites show).

With --dev-build (DUCKTALES_DEV_DIAGNOSTICS) also:
  6. the compositor's log (--dt-sprite-log) rebuilt or reused the sprite list on every frame of
     every window, and drew lines over 8 sprites and sprites past the 63 entries.

    py -3 -I tests/sprites_test.py --build build/headless [--dev-build build/headless-dev]
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402

sys.path.insert(0, str(ROOT / 'tools'))
from render_map import Rom  # noqa: E402
from render_oam import hud_allowed, hud_in_oam, hud_records, log_records, slot_order, slot_sprites, build  # noqa: E402

ROUTES = ROOT / 'tests' / 'routes'
# route, first frame, frames: the Amazon vine shaft (HUD left out for Scrooge's Y) and the quieter screen after it, then the busiest
# windows of four lands measured with render_oam.py limits (docs/rendering.md)
WINDOWS = [
    ('full_amazon', 1215, 90),
    ('full_amazon', 3200, 30),
    ('full_himalayas', 4371, 30),
    ('full_transylvania', 1581, 30),
]
failures = []


def check(ok, what, detail=''):
    print(('PASS ' if ok else 'FAIL ') + what + (('\n    ' + detail) if detail and not ok else ''))
    if not ok:
        failures.append(what)


def run(cmd):
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if p.returncode:
        sys.exit('%s: exit %d\n%s' % (' '.join(map(str, cmd)), p.returncode, p.stderr[-2000:]))
    return p.stdout


def hashes(exe, rom, tmp, frames, extra):
    out = tmp / 'h.txt'
    run([exe, rom, '--frames', frames, '--input', ROUTES / 'full_amazon.txt', '--hash-out', out, *extra])
    lines = [line for line in out.read_text().splitlines() if line[:1].isdigit()]
    out.unlink()
    return lines


def window(exe, rom, tmp, route, start, count, extra=(), log=False):
    """Native and presented PNGs (and the frame log) of frames start..start+count-1, from a state."""
    d = tmp / ('%s_%d_%s' % (route, start, 'on' if not extra else 'x'))
    d.mkdir(exist_ok=True)
    state = d / 'start.state'
    run(
        [
            exe,
            rom,
            '--frames',
            start,
            '--input',
            ROUTES / (route + '.txt'),
            '--save-state',
            '%d:%s' % (start - 3, state),
        ]
    )
    cmd = [
        exe,
        rom,
        '--load-state',
        state,
        '--frames',
        start + count,
        '--input',
        ROUTES / (route + '.txt'),
        '--present-out',
        d / 'pres.png',
        '--present-every',
        1,
        '--screenshot',
        d / 'nat.png',
        '--shot-every',
        1,
        *extra,
    ]
    if log:
        cmd += ['--frame-log', d / 'fl.bin', '--frame-log-frames', '%d:%d' % (start - 1, start + count - 1)]
    run(cmd)
    return d


def picture(d, name, frame):
    return Image.open(d / ('%s_%05d.png' % (name, frame))).convert('RGB')


def differing(a, b):
    diff = ImageChops.difference(a, b).convert('L')
    return [i for i, v in enumerate(diff.tobytes()) if v]


def opaque_pixels(chr_ram, table, sprite, h):
    """Picture offsets of an 8x8 sprite's opaque pixels (OAM Y, tile, attribute, X)."""
    y, tile, attr, x = sprite
    out = []
    for r in range(h):
        row = h - 1 - r if attr & 0x80 else r
        lo, hi = chr_ram[table + tile * 16 + row], chr_ram[table + tile * 16 + row + 8]
        for c in range(8):
            bit = c if attr & 0x40 else 7 - c
            if ((lo >> bit) & 1 or (hi >> bit) & 1) and y + 1 + r < 240 and x + c < 256:
                out.append((y + 1 + r) * 256 + x + c)
    return out


def rect_mask(mask, y, x, h):
    for yy in range(y + 1, min(y + 1 + h, 240)):
        for xx in range(x, min(x + 8, 256)):
            mask[yy * 256 + xx] = 1


def analyse(rom, ram, oam):
    """What the console loses on a frame, from the RAM its OAM was built from."""
    h = 16 if ram[0xFF] & 0x20 else 8
    lines = [0] * 240
    for i in range(0, 256, 4):
        if oam[i] < 0xEF:
            for y in range(oam[i] + 1, min(oam[i] + 1 + h, 240)):
                lines[y] += 1
    groups = [('split', [tuple(oam[0:4])])]
    groups += [('hud', hud_records(ram))] if hud_allowed(ram) else []
    groups += [(n, s) for n in slot_order(ram) for s in [slot_sprites(rom, ram, n)[0]] if s]
    mask = bytearray(256 * 240)
    for i in range(0, 256, 4):
        if oam[i] < 0xEF:
            rect_mask(mask, oam[i], oam[i + 3], h)
    for _, sprites in groups:
        for s in sprites:
            if s[0] < 0xEF:
                rect_mask(mask, s[0], s[3], h)
    rects = [(g, s[0], s[3]) for g, ss in groups for s in ss if s[0] < 0xEF]
    overlap = any(
        p[0] != q[0] and abs(p[1] - q[1]) < h and abs(p[2] - q[2]) < 8
        for i, p in enumerate(rects)
        for q in rects[i + 1 :]
    )
    built = bytes(build(rom, ram)[0])
    exact = oam[4 : 4 + len(built)] == built and all(oam[i] >= 0xF0 for i in range(4 + len(built), 256, 4))
    want = sum(len(s) for g, s in groups if g != 'split')
    crowded = max(lines) > 8
    hud_left_out = hud_allowed(ram) and not hud_in_oam(ram)
    clean = exact and not crowded and want <= 63 and not overlap and not hud_left_out
    return dict(mask=mask, clean=clean, crowded=crowded, cut=want > 63, hud_left_out=hud_left_out, h=h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    rom = Rom(args.rom)
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        on = hashes(exe, args.rom, tmp, 3300, ())
        off = hashes(exe, args.rom, tmp, 3300, ('--no-sprite-flicker', 'off'))
        check(
            len(on) == 3300 and on == off,
            'machine hashes are identical with the feature on and off (full_amazon 0-3299)',
            'on %d lines, off %d lines' % (len(on), len(off)),
        )

        crowded_frames = crowded_drawn = cut_frames = cut_drawn = hud_frames = clean_total = 0
        for route, start, count in WINDOWS:
            name = '%s %d-%d' % (route, start, start + count - 1)
            d_off = window(exe, args.rom, tmp, route, start, count, ('--no-sprite-flicker', 'off'))
            same_off = [
                f
                for f in range(start, start + count)
                if picture(d_off, 'pres', f).tobytes() == picture(d_off, 'nat', f).tobytes()
            ]
            check(
                len(same_off) == count,
                'off: presented = machine picture on every frame of %s' % name,
                '%d of %d frames' % (len(same_off), count),
            )
            d = window(exe, args.rom, tmp, route, start, count, log=True)
            recs = {f: (ram, oam, chr_ram) for f, ram, oam, chr_ram in log_records(d / 'fl.bin', chr_ram=True)}
            outside, unclean_diff, clean_n, hud_bad = [], [], 0, []
            for f in range(start, start + count):
                a = analyse(rom, recs[f - 1][0], recs[f][1])
                pres, nat = picture(d, 'pres', f), picture(d, 'nat', f)
                diff = differing(pres, nat)
                if any(not a['mask'][i] for i in diff):
                    outside.append(f)
                if a['clean']:
                    clean_n += 1
                    if diff:
                        unclean_diff.append(f)
                crowded_frames += a['crowded']
                crowded_drawn += a['crowded'] and bool(diff)
                cut_frames += a['cut']
                cut_drawn += a['cut'] and bool(diff)
                if a['hud_left_out']:
                    hud_frames += 1
                    ram, chr_ram = recs[f - 1][0], recs[f][2]
                    scrooge = set()
                    for s in slot_sprites(rom, ram, 0)[0] or ():
                        scrooge.update(opaque_pixels(chr_ram, 0, s, a['h']))
                    ds = set(diff)
                    for rec in hud_records(ram):
                        shown = [i for i in opaque_pixels(chr_ram, 0x1000, rec, 8) if i not in scrooge]
                        if any(i not in ds for i in shown):
                            hud_bad.append((f, rec[3], rec[0], len(shown)))
            check(not outside, 'on: %s differs from the machine picture only inside sprites' % name, str(outside[:10]))
            clean_total += clean_n
            check(
                not unclean_diff,
                'on: %s is identical on the %d frames where the console loses nothing' % (name, clean_n),
                str(unclean_diff[:10]),
            )
            check(not hud_bad, 'on: %s draws every HUD record the builder left out' % name, str(hud_bad[:10]))
        check(clean_total > 0, 'on: %d frames where the console loses nothing were compared' % clean_total)
        check(hud_frames > 0, 'on: the vine shaft has %d frames with the HUD left out for Scrooge' % hud_frames)
        check(
            crowded_frames > 0 and crowded_drawn == crowded_frames,
            'on: all %d frames with lines over 8 sprites are drawn with the dropped sprites' % crowded_frames,
            '%d of %d differ' % (crowded_drawn, crowded_frames),
        )
        check(
            cut_frames > 0 and cut_drawn == cut_frames,
            'on: all %d frames wanting more than 63 entries are drawn with the cut ones' % cut_frames,
            '%d of %d differ' % (cut_drawn, cut_frames),
        )

        if args.dev_build:
            dev = ROOT / args.dev_build / args.config / 'ducktales.exe'
            totals = {'lines_over_8': 0, 'sprites_past_63': 0}
            for route, start, count in WINDOWS:
                log = tmp / 'sprites.log'
                d = tmp / 'dev'
                d.mkdir(exist_ok=True)
                state = d / 'start.state'
                run(
                    [
                        dev,
                        args.rom,
                        '--frames',
                        start,
                        '--input',
                        ROUTES / (route + '.txt'),
                        '--save-state',
                        '%d:%s' % (start - 3, state),
                    ]
                )
                run(
                    [
                        dev,
                        args.rom,
                        '--load-state',
                        state,
                        '--frames',
                        start + count,
                        '--input',
                        ROUTES / (route + '.txt'),
                        '--dt-sprite-log',
                        log,
                    ]
                )
                bad = []
                for line in log.read_text().splitlines():
                    m = re.match(r'frame=(\d+) (\w+)(?: sprites=(\d+) lines_over_8=(\d+))?', line)
                    f = int(m.group(1))
                    if f < start:
                        continue
                    if m.group(2) not in ('rebuilt', 'reused'):
                        bad.append(f)
                        continue
                    totals['lines_over_8'] += int(m.group(4))
                    totals['sprites_past_63'] += max(0, int(m.group(3)) - 63)
                check(
                    not bad,
                    'dev: %s %d-%d: the sprite list is rebuilt or reused on every frame'
                    % (route, start, start + count - 1),
                    str(bad[:10]),
                )
            check(
                totals['lines_over_8'] > 0 and totals['sprites_past_63'] > 0,
                'dev: the compositor drew %(lines_over_8)d lines over 8 sprites and %(sprites_past_63)d sprites past 63'
                % totals,
            )
    print('%d failure(s)' % len(failures) if failures else 'all checks passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
