"""Differential regression test for the DuckTales recompilation.

For every route in tests/routes/*.txt this runs, at each CPU/PPU alignment:
  - the recompiled build (native code, interpreter only for unseen code),
  - the same build with --interp-only,
  - the TriCNES reference machine (ducktales_oracle),
and requires their per-frame hashes (bus trace, RAM, CIRAM, OAM, palette,
CHR RAM, picture, CPU registers) to match on every frame. It also reports the
share of CPU cycles that ran recompiled code and fails if any route falls
below --min-native.

    py -3 -I tests/verify_routes.py --build build/headless
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY = ROOT / 'external' / 'nesrecomp' / 'tools' / 'cyc' / 'cyc_verify.py'


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless', help='headless build directory')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*', help='route files (default: tests/routes/*.txt)')
    ap.add_argument('--align', type=int, nargs='+', default=[0, 1, 2, 3])
    ap.add_argument('--min-native', type=float, default=99.9,
                    help='minimum percentage of CPU cycles executed by recompiled code')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'verify'))
    ap.add_argument('--jobs', type=int, default=8)
    args = ap.parse_args()

    build = (ROOT / args.build / args.config)
    exe, oracle = build / 'ducktales.exe', build / 'ducktales_oracle.exe'
    for p in (exe, oracle, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    failed = []
    for route in routes:
        cmd = [sys.executable, '-I', str(VERIFY), '--exe', str(exe), '--oracle', str(oracle), '--rom', args.rom,
               '--input', str(route), '--frames', str(route_frames(route)), '--interp',
               '--out', str(Path(args.out) / route.stem), '--jobs', str(args.jobs), '--align', *map(str, args.align)]
        p = subprocess.run(cmd, capture_output=True, text=True)
        natives = [float(m) for m in re.findall(r'mode=native .*\(([\d.]+)%\)', p.stdout)]
        ok = p.returncode == 0 and natives and min(natives) >= args.min_native
        print(f'{route.stem:24s} {"PASS" if ok else "FAIL"}  native={min(natives) if natives else 0:.1f}%  '
              f'oracle/interp match={"yes" if p.returncode == 0 else "NO"}')
        if not ok:
            failed.append(route.stem)
            print(p.stdout[-3000:], p.stderr[-2000:])
    if failed:
        sys.exit('failed: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
