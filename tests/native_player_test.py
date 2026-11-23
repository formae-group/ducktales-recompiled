"""Off-screen self-test of the native player build (docs/native-runtime.md
"The player build").

Runs both window executables of build/windows off screen on route frames, no
window opening:

  ducktales.exe (native)        ROM --frames N --input ROUTE --lag faithful
                                --present-out DIR/n.png --present-every K
  ducktales_cycle.exe (cycle)   ROM --frames N --input ROUTE --extra-scanlines 0
                                --present-out DIR/c.png --present-every K

Both write the picture the window would show at 1x (ui/app_present.h with
its default settings: clean HUD split, No sprite flicker on) after every Kth
frame. The native runtime in its faithful lag mode and the cycle host without
extra scanlines run the same frames (tests/native_routes.py), so the pictures
must be identical pixel for pixel. Pass --player-build/--cycle-exe to compare
other executables.

    py -3 -I tests/native_player_test.py --build build/windows [--routes full_amazon ...]
                                         [--frames 3000] [--every 50] [--keep DIR]
"""

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402


def read_png(path):
    data = path.read_bytes()
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = {2: 3, 6: 4}[ctype]
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1 : i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def differing(a, b):
    wa, ha, bpa, ra = read_png(a)
    wb, hb, bpb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        return None
    n, lines = 0, set()
    for y in range(ha):
        if ra[y] == rb[y]:
            continue
        for x in range(wa):
            if ra[y][x * bpa : x * bpa + 3] != rb[y][x * bpb : x * bpb + 3]:
                n += 1
                lines.add(y)
    return n, sorted(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/windows')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--native-exe', help='default: <build>/<config>/ducktales.exe')
    ap.add_argument('--cycle-exe', help='default: <build>/<config>/ducktales_cycle.exe')
    ap.add_argument('--routes', nargs='*', default=['full_amazon', 'full_moon'])
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--every', type=int, default=50)
    ap.add_argument('--keep', help='keep the pictures here (never commit them)')
    args = ap.parse_args()
    out = ROOT / args.build / args.config
    native = Path(args.native_exe) if args.native_exe else out / 'ducktales.exe'
    cycle = Path(args.cycle_exe) if args.cycle_exe else out / 'ducktales_cycle.exe'
    failed = 0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(args.keep).resolve() if args.keep else Path(t)
        for route in args.routes:
            d = tmp / route
            if d.exists():
                shutil.rmtree(d)
            d.mkdir(parents=True)
            route_file = str(ROOT / 'tests' / 'routes' / f'{route}.txt')
            common = [
                '--frames',
                str(args.frames),
                '--input',
                route_file,
                '--config',
                str(d / 'x.ini'),
                '--present-every',
                str(args.every),
            ]
            runs = (
                (native, ['--lag', 'faithful', '--present-out', str(d / 'n.png')]),
                (cycle, ['--extra-scanlines', '0', '--present-out', str(d / 'c.png')]),
            )
            for exe, extra in runs:
                p = subprocess.run([str(exe), args.rom, *common, *extra], capture_output=True, text=True, timeout=3600)
                if p.returncode != 0:
                    print(f'FAIL  {route}: {exe.name} exited {p.returncode}\n{(p.stdout + p.stderr)[-1500:]}')
                    failed += 1
            pics = sorted(d.glob('n_*.png'))
            same, diffs = 0, []
            for n in pics:
                c = d / ('c_' + n.name[2:])
                if not c.exists():
                    diffs.append(f'{n.name}: no cycle picture')
                    continue
                if n.read_bytes() == c.read_bytes():
                    same += 1
                    continue
                r = differing(n, c)
                if r is None:
                    diffs.append(f'{n.name}: sizes differ')
                elif r[0] == 0:
                    same += 1
                else:
                    diffs.append(f'{n.name[2:-4]}: {r[0]} pixels on lines {r[1][:8]}')
            ok = pics and not diffs
            print(
                f'{"PASS" if ok else "FAIL"}  {route}: {same} of {len(pics)} pictures identical '
                f'(frames 0-{args.frames - 1}, every {args.every})'
            )
            for line in diffs[:20]:
                print('      ' + line)
            failed += not ok
    print('all passed' if not failed else f'{failed} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
