"""Native runtime vs the cycle host on the input routes (docs/native-runtime.md).

For every route in tests/routes/*.txt this runs the cycle build (ducktales.exe)
and the native-runtime prototype (ducktales_native.exe) with --ram-hash-out
and requires the same CPU RAM, cycle count and CPU registers at the end of
every frame (tools/native_compare.py). The native runtime runs in its faithful
lag mode: the no-slowdown default changes timing on purpose.

    py -3 -I tests/native_routes.py --build build/headless [--routes R ...] [--reuse]

--reuse keeps an existing cycle-host hash file instead of running it again
(the native run takes seconds, the cycle host minutes).
"""

import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPARE = ROOT / 'tools' / 'native_compare.py'
# PPU memories are hashed as each runtime holds them when the frame ends. The
# console writes a $2007 byte a few dots after the store instruction, so a
# store in the frame's last instruction lands in the next frame there and in
# this one here; the next frame agrees again. RAM, cycles and registers have
# no such lag.
STRICT = 'ram,cycles,PC,A,X,Y,S,P'


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def run_route(route, args, exe, native):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    frames = str(route_frames(route))
    cyc, nat = out / f'{route.stem}.cycle.txt', out / f'{route.stem}.native.txt'
    if not (args.reuse and cyc.exists()):
        subprocess.run(
            [str(exe), args.rom, '--input', str(route), '--frames', frames, '--ram-hash-out', str(cyc)],
            capture_output=True,
            check=True,
        )
    p = subprocess.run(
        [
            str(native),
            args.rom,
            '--input',
            str(route),
            '--frames',
            frames,
            '--lag',
            'faithful',
            '--ram-hash-out',
            str(nat),
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    summary = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ''
    strict = subprocess.run(
        [sys.executable, '-I', str(COMPARE), str(cyc), str(nat), '--fields', STRICT, '--list', '3'],
        capture_output=True,
        text=True,
    )
    memory = subprocess.run(
        [sys.executable, '-I', str(COMPARE), str(cyc), str(nat), '--fields', 'nt,pal,oam,chr', '--list', '0'],
        capture_output=True,
        text=True,
    )
    return route, frames, strict, memory, summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'native-verify'))
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    build = ROOT / args.build / args.config
    exe, native = build / 'ducktales.exe', build / 'ducktales_native.exe'
    for p in (exe, native, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for route, frames, strict, memory, summary in pool.map(lambda r: run_route(r, args, exe, native), routes):
            ok = strict.returncode == 0
            mem_line = [l for l in memory.stdout.splitlines() if l.startswith(('IDENTICAL', 'DIFFERENT'))]
            print(
                f'{route.stem:24s} {"PASS" if ok else "FAIL"}  frames={frames}  ram/cycles/registers: '
                f'{"identical" if ok else "differ"}  ppu memories: {mem_line[0] if mem_line else "?"}  [{summary}]'
            )
            if not ok:
                failed.append(route.stem)
                print(strict.stdout)
    if failed:
        sys.exit('failed: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
