"""The build with no ROM (docs/ROMLESS.md) against the ROM build, on the native runtime.

The ROM-less build (-DDUCKTALES_ROMLESS=ON) links no generated code, so every
instruction the decompiled routines leave to the original runs in NESRecomp's
instruction templates over the ROM given at run time. For each route
ducktales_native from both builds (--build, the ROM build; --romless) runs:

  original    Original mode: --lag faithful and the route's input; CPU RAM,
              cycles and registers every frame (--ram-hash-out)
  recompiled  No slowdown (--lag none): every frame, the logic steps
              (--step-dump) and the buttons latched at each strobe
              (--latch-log)
  decompiled  Decompiled code: --decomp replace --lag none, fed the ROM
              build's latches; every frame, the logic steps and each
              routine's decomp: line (its calls and those given to the
              original)

and every output must be byte-equal between the two builds, and the ROM-less
runs must report their instructions as run in the templates
(fallback_instructions above 0). The report gives each build's time per frame in Original
mode, the cost of the instruction templates. With --golden, the ROM-less build
then runs tests/quick_verify.py --runtime native against the golden frames.

    py -3 -I tests/romless_test.py --build build/headless --romless build/romless-headless
        [--golden build/golden] [--routes R ...] [--jobs 2]
"""

import argparse
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
from quick_verify import route_frames  # noqa: E402
from romfile import default_rom  # noqa: E402

SUMMARY = re.compile(r'^native: frames=(\d+) .*fallback_instructions=(\d+) .*seconds=([\d.]+)', re.M)
DECOMP = re.compile(r'^decomp: .*$', re.M)
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def run(native, args, route, out, name, extra):
    frames = route_frames(route)
    s = out / f'{route.stem}.{name}'
    cmd = [str(native), args.rom, '--frames', str(frames), '--ram-hash-out', f'{s}.frames'] + extra(s)
    p = subprocess.run(cmd, capture_output=True, text=True, creationflags=NO_WINDOW)
    m = SUMMARY.search(p.stdout)
    return {
        'code': p.returncode,
        'stderr': p.stderr[-2000:],
        'frames': int(m.group(1)) if m else 0,
        'fallback': int(m.group(2)) if m else -1,
        'seconds': float(m.group(3)) if m else 0.0,
        'decomp': DECOMP.findall(p.stdout),
    }


def run_route(route, args, builds, out):
    results = {}
    for build, native in builds.items():
        d = out / build
        d.mkdir(parents=True, exist_ok=True)
        latch = out / 'rom' / f'{route.stem}.recompiled.latch'
        results[build, 'original'] = run(
            native, args, route, d, 'original', lambda s: ['--input', str(route), '--lag', 'faithful']
        )
        results[build, 'recompiled'] = run(
            native,
            args,
            route,
            d,
            'recompiled',
            lambda s: [
                '--input',
                str(route),
                '--lag',
                'none',
                '--latch-log',
                f'{s}.latch',
                '--step-dump',
                f'{s}.steps',
            ],
        )
        results[build, 'decompiled'] = run(
            native,
            args,
            route,
            d,
            'decompiled',
            lambda s: [
                '--latch-input',
                str(latch),
                '--lag',
                'none',
                '--decomp',
                'replace',
                '--step-dump',
                f'{s}.steps',
            ],
        )
    return route, results


def report(route, results, out):
    problems = []
    for mode, files in (
        ('original', ['frames']),
        ('recompiled', ['frames', 'steps', 'latch']),
        ('decompiled', ['frames', 'steps']),
    ):
        rom, romless = results['rom', mode], results['romless', mode]
        for build, r in (('ROM', rom), ('ROM-less', romless)):
            if r['code'] or not r['frames']:
                problems.append(f'{mode}: the {build} run exited {r["code"]}\n{r["stderr"]}')
        if problems:
            continue
        for ext in files:
            a = (out / 'rom' / f'{route.stem}.{mode}.{ext}').read_bytes()
            b = (out / 'romless' / f'{route.stem}.{mode}.{ext}').read_bytes()
            if a != b:
                problems.append(f'{mode}: {ext} differ ({len(a)} and {len(b)} bytes)')
        if rom['decomp'] != romless['decomp']:
            problems.append(f'{mode}: decomp: lines differ')
        if romless['fallback'] <= 0:
            problems.append(f'{mode}: the ROM-less run reports fallback_instructions {romless["fallback"]}')
    rom, romless = results['rom', 'original'], results['romless', 'original']
    print(
        f'== {route.stem}: {rom["frames"]} frames; Original mode {rom["seconds"]:.2f} s (ROM), '
        f'{romless["seconds"]:.2f} s (ROM-less); {romless["fallback"]:,} instructions in the templates'
        + ('' if problems else '; equal')
    )
    for p in problems:
        print('    ' + p)
    return not problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless', help='the ROM build (headless, DUCKTALES_NATIVE)')
    ap.add_argument('--romless', default='build/romless-headless', help='the DUCKTALES_ROMLESS build (headless)')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--golden', help='also run tests/quick_verify.py --runtime native on the ROM-less build')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'romless-test'))
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    builds = {
        'rom': ROOT / args.build / args.config / 'ducktales_native.exe',
        'romless': ROOT / args.romless / args.config / 'ducktales_native.exe',
    }
    for p in (*builds.values(), Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    out = Path(args.out)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(lambda r: run_route(r, args, builds, out), routes))
    failed = [r.stem for r, results in done if not report(r, results, out)]
    frames = sum(results['rom', 'original']['frames'] for _, results in done)
    for build, name in (('rom', 'ROM'), ('romless', 'ROM-less')):
        seconds = sum(results[build, 'original']['seconds'] for _, results in done)
        if frames and seconds:
            print(
                f'Original mode, {name} build: {1000 * seconds / frames:.3f} ms per frame ({frames / seconds:.0f} fps)'
            )
    print('failed: ' + ', '.join(failed) if failed else f'all {len(done)} routes: equal in every mode')
    if failed:
        return 1
    if args.golden:
        cmd = [sys.executable, '-I', str(ROOT / 'tests' / 'quick_verify.py'), '--build', args.romless]
        cmd += ['--config', args.config, '--rom', args.rom, '--golden', args.golden, '--runtime', 'native']
        cmd += ['--jobs', str(args.jobs)] + (['--routes', *args.routes] if args.routes else [])
        return subprocess.run(cmd).returncode
    return 0


if __name__ == '__main__':
    sys.exit(main())
