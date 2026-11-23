"""Native renderer against the PPU (docs/native-render.md).

For each route this runs the headless build with --native-render-check, which
fills the native renderer's input (src/native/native_render.h) from the PPU at
the end of every frame, draws the frame natively with the console's sprite
rules and compares it with the machine's picture pixel for pixel. It prints,
per route, the frames and pixels that differ:

  line model   segments per line as the PPU recorded them at each line's
               first dot (what a runtime without a PPU supplies)
  final        after the split line's mid-line write is placed (searched
               per frame; see native_check.c)

and fails if any pixel differs anywhere but the split line, or (with
--strict) at all.

    py -3 -I tests/native_render_test.py --build build/headless
"""
import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BELOW_NORMAL = 0x00004000


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def run(exe, rom, route, out):
    report = out / f'{route.stem}.txt'
    cmd = [str(exe), rom, '--input', str(route), '--frames', str(route_frames(route)),
           '--native-render-check', str(report)]
    flags = BELOW_NORMAL if os.name == 'nt' else 0
    p = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, creationflags=flags)
    text = report.read_text() if report.exists() else ''
    m = re.search(r'^summary (.*)$', text, re.M)
    if p.returncode != 0 or not m:
        return route.stem, None, p.stderr[-2000:]
    fields = dict(kv.split('=', 1) for kv in m.group(1).split() if '=' in kv)
    off_split = 0
    for line in text.splitlines():
        f = re.match(r'frame=\d+ index_pixels=(\d+) .*?lines=(-?\d+)-(-?\d+).*? segments=\d+(.*)$', line)
        if not f or f.group(1) == '0':
            continue
        starts = {int(s) for s in re.findall(r'\[(\d+) ', f.group(4))}
        top, bottom = int(f.group(2)), int(f.group(3))
        if not (top == bottom and top + 1 in starts):
            off_split += 1
    fields['off_split_frames'] = str(off_split)
    return route.stem, fields, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless', help='headless build directory')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*', help='route files (default: tests/routes/full_*.txt)')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'native-render'))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--strict', action='store_true', help='fail on any differing pixel, the split line included')
    args = ap.parse_args()

    exe = ROOT / args.build / args.config / 'ducktales.exe'
    for p in (exe, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('full_*.txt'))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda r: run(exe, args.rom, r, out), routes))
    failed = []
    print(f'{"route":20s} {"frames":>7s}  {"line model frames/pixels":>25s}  {"final frames/pixels":>20s}  '
          f'{"split $2005/$2006/unexpl.":>26s}  {"off split":>9s}')
    for name, f, err in results:
        if f is None:
            print(f'{name:20s} FAILED TO RUN\n{err}')
            failed.append(name)
            continue
        line = f'{f["line_model_mismatch_frames"]}/{f["line_model_mismatch_pixels"]}'
        final = f'{f["index_mismatch_frames"]}/{f["index_mismatch_pixels"]}'
        split = f'{f["split_fine_x"]}/{f["split_position"]}/{f["split_unexplained"]}'
        print(f'{name:20s} {f["frames"]:>7s}  {line:>25s}  {final:>20s}  {split:>26s}  {f["off_split_frames"]:>9s}')
        bad = f['off_split_frames'] != '0' or f['argb_mismatch_pixels'] != f['index_mismatch_pixels'] or \
            f['mirroring_mismatch_frames'] != '0' or (args.strict and f['index_mismatch_pixels'] != '0')
        if bad:
            failed.append(name)
    if failed:
        sys.exit('failed: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
