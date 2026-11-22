"""Differential regression test for the DuckTales recompilation.

For every route in tests/routes/*.txt this runs, at each CPU/PPU alignment:
  - the recompiled build (native code, interpreter only for unseen code),
  - the same build with --interp-only,
  - the TriCNES reference machine (ducktales_oracle),
and requires their per-frame hashes (bus trace, RAM, CIRAM, OAM, palette,
CHR RAM, picture, CPU registers) to match on every frame, with the same rules
as NESRecomp's tools/cyc/cyc_verify.py, whose run and comparison functions it
uses. It also reports the share of CPU cycles that ran recompiled code and
fails if any route falls below --min-native.

The oracle's hashes depend only on the oracle executable, the ROM, the route,
the frame count and the alignment, so a completed oracle run is kept in
--cache and reused while all of those are unchanged; --no-cache runs it again.

    py -3 -I tests/verify_routes.py --build build/headless
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY = ROOT / 'external' / 'nesrecomp' / 'tools' / 'cyc' / 'cyc_verify.py'


def load_cyc_verify():
    spec = importlib.util.spec_from_file_location('cyc_verify', VERIFY)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


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
    ap.add_argument('--cache', default=str(ROOT / 'build' / 'verify-cache'), help='oracle hash cache')
    ap.add_argument('--no-cache', action='store_true', help='run the oracle even when a cached run exists')
    ap.add_argument('--jobs', type=int, default=8)
    args = ap.parse_args()

    build = ROOT / args.build / args.config
    exe, oracle = build / 'ducktales.exe', build / 'ducktales_oracle.exe'
    for p in (exe, oracle, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_ORACLE=ON)')
    cv = load_cyc_verify()
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    oracle_id = file_sha256(oracle) + file_sha256(args.rom)
    cache = Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)

    runs, cached = {}, {}
    for route in routes:
        frames = route_frames(route)
        out = Path(args.out) / route.stem
        out.mkdir(parents=True, exist_ok=True)
        common = [args.rom, '--frames', str(frames), '--input', str(route.resolve())]
        schedule = route.read_bytes().replace(b'\r\n', b'\n')
        for align in args.align:
            key = hashlib.sha256(f'{oracle_id} {frames} {align} '.encode() + schedule).hexdigest()[:32]
            stored = cache / f'{key}.txt'
            modes = [('native', exe, []), ('interp', exe, ['--interp-only'])]
            if args.no_cache or not stored.exists():
                modes.append(('oracle', oracle, []))
            else:
                cached[(route.stem, align)] = stored
            for mode, program, extra in modes:
                hash_file = out / f'a{align}_{mode}.txt'
                hash_file.unlink(missing_ok=True)
                cmd = [str(program)] + common + ['--align', str(align), '--hash-out', str(hash_file)] + extra
                runs[(route.stem, align, mode)] = (cmd, hash_file, frames, stored)

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(cv.run, cmd): key for key, (cmd, *_) in runs.items()}
        for f in concurrent.futures.as_completed(futures):
            results[futures[f]] = f.result()

    hashes, errors = {}, {}
    for key, (code, output) in results.items():
        _, hash_file, frames, stored = runs[key]
        hash_file.with_name(hash_file.name + '.log').write_text(output)
        errors[key] = cv.run_error(code, output, str(hash_file), argparse.Namespace(acccoin=False, frames=frames))
        hashes[key] = hash_file
        if key[2] == 'oracle' and errors[key] is None:
            shutil.copyfile(hash_file, stored)
    for (stem, align), stored in cached.items():
        hashes[(stem, align, 'oracle')] = stored
        errors[(stem, align, 'oracle')] = None

    failed = []
    for route in routes:
        report, natives, matched = [], [], True
        for align in args.align:
            for mode in ('oracle', 'native', 'interp'):
                key = (route.stem, align, mode)
                if errors[key]:
                    matched = False
                    report.append(f'alignment {align} {mode}: ERROR {errors[key]} (see {hashes[key]}.log)')
            if not errors[(route.stem, align, 'native')]:
                natives += [float(m) for m in re.findall(r'mode=native .*\(([\d.]+)%\)',
                                                         results[(route.stem, align, 'native')][1])]
            for mode, other, with_hw in (('native', 'oracle', False), ('interp', 'oracle', False),
                                         ('interp', 'native', True)):
                if errors[(route.stem, align, mode)] or errors[(route.stem, align, other)]:
                    continue
                diff = cv.first_difference(str(hashes[(route.stem, align, other)]),
                                           str(hashes[(route.stem, align, mode)]), with_hw)
                if diff:
                    matched = False
                    frame, x, y = diff
                    report.append(f'alignment {align}: {mode} differs from {other} at frame {frame}\n'
                                  f'  {other}: {x}\n  {mode}:  {y}')
        ok = matched and natives and min(natives) >= args.min_native
        oracle_src = 'cached' if all((route.stem, a) in cached for a in args.align) else 'run'
        print(f'{route.stem:24s} {"PASS" if ok else "FAIL"}  native={min(natives) if natives else 0:.1f}%  '
              f'oracle/interp match={"yes" if matched else "NO"}  oracle={oracle_src}')
        if not ok:
            failed.append(route.stem)
            print('\n'.join(report))
    if failed:
        sys.exit('failed: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
