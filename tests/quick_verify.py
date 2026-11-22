"""Fast verification tier: golden per-frame hashes, affected routes, alignment 0.

tests/verify_routes.py is the gate for a merge batch or a release (12 routes x
4 alignments x recompiled, interpreter-only and TriCNES oracle, most of an
hour). This is the loop for iterating on a change (docs/validation.md,
"Fast verification"):

  --record   once, from a build that just passed the full verify_routes. For
             every route the cycle build runs at alignment 0 and its frames go
             to build/golden/<ROM SHA-256>/<route>.txt: the --ram-hash-out
             line (CPU RAM, nametables, palette, OAM, CHR RAM, cycles,
             registers) joined with the --hash-out line (bus trace, memories
             and picture, interrupt lines, hardware internals). Each file is
             keyed on the ROM, the route file's SHA-256 and the frame count.
             The run also keeps a save state every --state-every frames
             (<route>.states/<frame>.state) and then replays each segment
             between two states with --interp-only --miss-log: that run must
             reproduce the golden frames from its state, and its miss log is
             the set of PRG addresses the segment executes (<route>.cover).
             ducktales_native, when the build has it, saves its own states at
             the same frames (<frame>.native), each replayed against the golden
             frames in the same way. Never commit any of it: it is derived
             from the ROM.

  (default)  run the candidate on the selected routes at alignment 0, compare
             every frame with the golden file while the run writes it, stop a
             route at its first difference and print the fields that differ.
             --runtime native: ducktales_native --lag faithful, compared on
             CPU RAM, cycles and registers (the fields tests/native_routes.py
             requires; its PPU memories may land a frame apart at a frame's
             edge). --runtime cycle: ducktales.exe, every golden field.

  --changed RANGES / --routes-for ADDRS
             only the routes whose recorded coverage executes an instruction
             in those PRG addresses: [BANK:]ADDR[-ADDR], hex, BANK the 16 KiB
             bank (docs use "bank 6 $BABC" = 6:BABC). $C000-$FFFF is the fixed
             bank; $8000-$BFFF without a bank matches every switchable bank.

  --from-state auto|FRAME
             start each route from a golden save state instead of power-on:
             auto, the state just before the first segment that executes the
             --changed/--routes-for code; FRAME, the latest state at or before
             it. --span N stops N frames after the start.

    py -3 -I tests/quick_verify.py --record --build build/headless
    py -3 -I tests/quick_verify.py --build build/headless
    py -3 -I tests/quick_verify.py --build build/headless --routes-for C10B
    py -3 -I tests/quick_verify.py --build build/headless --changed 6:BA00-BAFF --from-state auto
"""
import argparse
import hashlib
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE_FIELDS = ['ram', 'cycles', 'PC', 'A', 'X', 'Y', 'S', 'P']
GOLDEN_FIELDS = ['ram', 'nt', 'pal', 'oam', 'chr', 'cycles', 'PC', 'A', 'X', 'Y', 'S', 'P', 'trace', 'mem', 'int', 'hw']
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def route_key(route):
    return {'route': route.stem, 'route_sha256': sha256(route.read_bytes().replace(b'\r\n', b'\n')),
            'frames': str(route_frames(route)), 'align': '0'}


def parse_line(line):
    parts = line.split()
    return int(parts[0]), dict(p.split('=', 1) for p in parts[1:])


def read_header(path):
    with open(path) as f:
        first = f.readline()
    return dict(p.split('=', 1) for p in first.split()[2:] if '=' in p)


def load_golden(path):
    frames = {}
    with open(path) as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                n, fields = parse_line(line)
                frames[n] = fields
    return frames


class Follow:
    """The complete lines a running program has written to a file so far."""

    def __init__(self, path):
        self.path, self.f, self.rest = path, None, ''

    def lines(self):
        if self.f is None:
            if not self.path.exists():
                return []
            self.f = open(self.path, newline='')
        self.rest += self.f.read()
        *done, self.rest = self.rest.split('\n')
        return [l for l in done if l.strip()]

    def close(self):
        if self.f:
            self.f.close()


def run_compared(cmd, outputs, golden, fields, start, end, log):
    """Run cmd and compare each line of every file in outputs with golden as it appears.

    Returns (first difference or None, frames compared, error or None); the
    program is stopped at the first difference."""
    for path in outputs:
        path.unlink(missing_ok=True)
    with open(log, 'w') as logf:
        proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
        follows = [Follow(p) for p in outputs]
        expect = [start] * len(outputs)
        first, error = None, None

        def drain():
            nonlocal first, error
            for i, fol in enumerate(follows):
                for line in fol.lines():
                    n, got = parse_line(line)
                    if n != expect[i]:
                        error = error or f'{fol.path.name}: frame {n} where {expect[i]} was expected'
                        continue
                    expect[i] += 1
                    want = golden.get(n)
                    if want is None:
                        error = error or f'frame {n} is past the golden file'
                        continue
                    diff = {k: (want.get(k), got[k]) for k in fields if k in got and got[k] != want.get(k)}
                    if diff and (first is None or n < first[0]):
                        first = (n, diff)
                    elif diff and n == first[0]:
                        first[1].update(diff)

        while proc.poll() is None:
            drain()
            if first or error:
                proc.kill()
                proc.wait()
                break
            time.sleep(0.05)
        drain()
        for fol in follows:
            fol.close()
            if fol.path.exists():
                data = fol.path.read_bytes()
                fol.path.write_bytes(data[:data.rfind(b'\n') + 1])
    if first:
        return first, first[0] - start, None
    if error:
        return None, min(expect) - start, error
    if proc.returncode:
        return None, min(expect) - start, f'exit code {proc.returncode} (see {log})'
    if min(expect) != end:
        return None, min(expect) - start, f'stopped after frame {min(expect) - 1}, expected {end - 1} (see {log})'
    return None, end - start, None


def parse_specs(specs, prg_banks):
    """[BANK:]ADDR[-ADDR] -> PRG ROM offset ranges, inclusive."""
    fixed = prg_banks - 1
    ranges = []
    for spec in ' '.join(specs).replace(',', ' ').split():
        m = re.fullmatch(r'(?:([0-9]+):)?\$?(?:0x)?([0-9A-Fa-f]{4})(?:-\$?(?:0x)?([0-9A-Fa-f]{4}))?', spec)
        if not m:
            sys.exit(f'{spec}: expected [BANK:]ADDR[-ADDR], hex addresses from 8000 to FFFF')
        lo, hi = int(m.group(2), 16), int(m.group(3) or m.group(2), 16)
        if lo < 0x8000 or hi < lo or (lo < 0xC000 <= hi):
            sys.exit(f'{spec}: the range must lie inside $8000-$BFFF or $C000-$FFFF')
        if lo >= 0xC000:
            if m.group(1) and int(m.group(1)) != fixed:
                sys.exit(f'{spec}: $C000-$FFFF is always bank {fixed}')
            banks = [fixed]
        elif m.group(1):
            if int(m.group(1)) >= fixed:
                sys.exit(f'{spec}: switchable banks are 0-{fixed - 1}')
            banks = [int(m.group(1))]
        else:
            banks = range(fixed)
        for b in banks:
            ranges.append((b * 0x4000 + (lo & 0x3FFF), b * 0x4000 + (hi & 0x3FFF)))
    return ranges


def read_miss_log(path):
    out = set()
    for line in path.read_text().splitlines():
        m = re.match(r'4k:([0-9A-Fa-f]+):([0-9A-Fa-f]+) ', line)
        if m:
            out.add(int(m.group(1), 16) * 0x1000 + (int(m.group(2), 16) & 0xFFF))
    return out


def write_ranges(offsets):
    out, run = [], None
    for o in sorted(offsets):
        if run and o == run[1] + 1:
            run[1] = o
        else:
            run = [o, o]
            out.append(run)
    return ' '.join(f'{a:05X}' if a == b else f'{a:05X}-{b:05X}' for a, b in out)


def read_cover(path):
    """<route>.cover -> [(start frame, set of PRG offsets)] per segment."""
    segments = []
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        start, *spans = line.split()
        offs = set()
        for s in spans:
            a, _, b = s.partition('-')
            offs.update(range(int(a, 16), int(b or a, 16) + 1))
        segments.append((int(start), offs))
    return segments


def hits(offsets, ranges):
    return any(lo <= o <= hi for o in offsets for lo, hi in ranges)


def state_path(golden_dir, route, start, runtime='cycle'):
    return golden_dir / f'{route.stem}.states' / f'{start}.{"native" if runtime == "native" else "state"}'


def record_route(route, args, exe, golden_dir, rom_sha):
    key = route_key(route)
    frames = int(key['frames'])
    work = Path(args.out) / 'record'
    work.mkdir(parents=True, exist_ok=True)
    states = golden_dir / f'{route.stem}.states'
    states.mkdir(parents=True, exist_ok=True)
    for old in states.iterdir():
        old.unlink()
    ram, hsh = work / f'{route.stem}.ram.txt', work / f'{route.stem}.hash.txt'
    cmd = [str(exe), args.rom, '--input', str(route), '--frames', str(frames), '--align', '0',
           '--ram-hash-out', str(ram), '--hash-out', str(hsh)]
    for s in range(args.state_every, frames, args.state_every):
        cmd += ['--save-state', f'{s - 1}:{state_path(golden_dir, route, s)}']
    p = subprocess.run(cmd, capture_output=True, text=True, creationflags=NO_WINDOW)
    (work / f'{route.stem}.log').write_text(p.stdout + p.stderr)
    if p.returncode:
        return f'{route.stem}: the cycle build exited with {p.returncode} (see {work / route.stem}.log)'
    lines = {}
    for path in (ram, hsh):
        with open(path) as f:
            for line in f:
                if line.strip():
                    n, fields = parse_line(line)
                    merged = lines.setdefault(n, {})
                    clash = [k for k in fields if k in merged and merged[k] != fields[k]]
                    if clash:
                        return f'{route.stem}: --ram-hash-out and --hash-out disagree on {clash} at frame {n}'
                    merged.update(fields)
    if sorted(lines) != list(range(frames)) or any(set(GOLDEN_FIELDS) - set(v) for v in lines.values()):
        return f'{route.stem}: the cycle build wrote {len(lines)} complete frames of {frames}'
    header = ' '.join(f'{k}={v}' for k, v in key.items())
    with open(golden_dir / f'{route.stem}.txt', 'w') as f:
        f.write(f'# quick_verify {header} rom_sha256={rom_sha} build_sha256={file_sha256(exe)} '
                f'state_every={args.state_every}\n')
        for n in range(frames):
            f.write(f'{n} ' + ' '.join(f'{k}={lines[n][k]}' for k in GOLDEN_FIELDS) + '\n')
    ram.unlink()
    hsh.unlink()
    return None


def cover_segment(route, start, end, args, exe, golden_dir, golden):
    work = Path(args.out) / 'record'
    tag = f'{route.stem}.{start}'
    miss, ram, hsh, log = (work / f'{tag}.{ext}' for ext in ('miss.txt', 'ram.txt', 'hash.txt', 'log'))
    miss.unlink(missing_ok=True)
    cmd = [str(exe), args.rom, '--input', str(route), '--frames', str(end), '--align', '0', '--interp-only',
           '--miss-log', str(miss), '--ram-hash-out', str(ram), '--hash-out', str(hsh)]
    if start:
        cmd += ['--load-state', str(state_path(golden_dir, route, start))]
    first, _, error = run_compared(cmd, [ram, hsh], golden, GOLDEN_FIELDS, start, end, log)
    if first or error:
        what = error or f'frame {first[0]} differs in {", ".join(first[1])}'
        return start, None, (f'{route.stem}: the interpreter run of frames {start}-{end - 1} from '
                             f'{"its state" if start else "power-on"} does not match the golden file: {what}')
    offsets = read_miss_log(miss)
    for path in (miss, ram, hsh, log):
        path.unlink()
    return start, offsets, None


def native_states(route, args, native, golden_dir, golden):
    """Save ducktales_native's states at the golden state frames, then replay each segment from its state."""
    frames = route_frames(route)
    work = Path(args.out) / 'record'
    base = [str(native), args.rom, '--input', str(route), '--lag', 'faithful']
    starts = list(range(args.state_every, frames, args.state_every))
    cmd = base + ['--frames', str(frames)]
    for s in starts:
        cmd += ['--save-state', f'{s - 1}:{state_path(golden_dir, route, s, "native")}']
    p = subprocess.run(cmd, capture_output=True, text=True, creationflags=NO_WINDOW)
    if p.returncode:
        return f'{route.stem}: ducktales_native exited with {p.returncode}: {p.stderr.strip()}'
    for s, end in zip(starts, starts[1:] + [frames]):
        ram, log = work / f'{route.stem}.{s}.native.txt', work / f'{route.stem}.{s}.native.log'
        first, _, error = run_compared(base + ['--frames', str(end), '--ram-hash-out', str(ram), '--load-state',
                                               str(state_path(golden_dir, route, s, 'native'))],
                                       [ram], golden, NATIVE_FIELDS, s, end, log)
        if first or error:
            what = error or f'frame {first[0]} differs in {", ".join(first[1])}'
            return f'{route.stem}: ducktales_native from its state at frame {s} does not match the golden file: {what}'
        ram.unlink()
        log.unlink()
    return None


def record(args, routes, exe, golden_dir, rom_sha):
    golden_dir.mkdir(parents=True, exist_ok=True)
    t0 = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        errors = [e for e in pool.map(lambda r: record_route(r, args, exe, golden_dir, rom_sha), routes) if e]
    if errors:
        sys.exit('\n'.join(errors))
    print(f'golden frames: {len(routes)} routes in {time.monotonic() - t0:.0f} s -> {golden_dir}', flush=True)
    t1 = time.monotonic()
    jobs, goldens = [], {}
    for route in routes:
        frames = route_frames(route)
        goldens[route.stem] = load_golden(golden_dir / f'{route.stem}.txt')
        bounds = list(range(0, frames, args.state_every)) + [frames]
        jobs += [(route, a, b) for a, b in zip(bounds, bounds[1:])]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(lambda j: (j[0],) + cover_segment(*j, args, exe, golden_dir, goldens[j[0].stem]), jobs))
    errors = [e for *_, e in done if e]
    if errors:
        sys.exit('\n'.join(errors))
    for route in routes:
        segs = sorted((s, o) for r, s, o, _ in done if r is route)
        total = set().union(*(o for _, o in segs))
        with open(golden_dir / f'{route.stem}.cover', 'w') as f:
            f.write(f'# quick_verify coverage {" ".join(f"{k}={v}" for k, v in route_key(route).items())} '
                    f'state_every={args.state_every}: per segment, its first frame and the PRG ROM offsets '
                    f'of the instructions it executes\n')
            for s, o in segs:
                f.write(f'{s} {write_ranges(o)}\n')
        print(f'{route.stem:24s} {len(segs)} segments replayed from their states match the golden frames; '
              f'{len(total)} PRG instruction addresses executed')
    print(f'coverage: {len(jobs)} segments in {time.monotonic() - t1:.0f} s', flush=True)
    native = exe.with_name('ducktales_native.exe')
    if not native.exists():
        print(f'no {native}: no native states, so --from-state runs need --runtime cycle')
        return
    t2 = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        errors = [e for e in pool.map(lambda r: native_states(r, args, native, golden_dir, goldens[r.stem]), routes) if e]
    if errors:
        sys.exit('\n'.join(errors))
    print(f'native states: {len(jobs) - len(routes)} states, each replayed to the next against the golden frames, '
          f'in {time.monotonic() - t2:.0f} s')


def check_golden(route, golden_dir):
    path = golden_dir / f'{route.stem}.txt'
    if not path.exists():
        return f'{route.stem}: no golden file in {golden_dir}; record it with --record'
    head, key = read_header(path), route_key(route)
    stale = [k for k in key if head.get(k) != key[k]]
    if stale:
        return f'{route.stem}: the golden file was recorded for another {", ".join(stale)}; record it again'
    return None


def verify_route(route, start, args, golden_dir):
    frames = route_frames(route)
    end = min(frames, start + args.span) if args.span else frames
    golden = load_golden(golden_dir / f'{route.stem}.txt')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    build = ROOT / args.build / args.config
    base = [args.rom, '--input', str(route), '--frames', str(end)]
    if args.runtime == 'native':
        ram = out / f'{route.stem}.native.txt'
        cmd = [str(build / 'ducktales_native.exe')] + base + ['--lag', 'faithful', '--ram-hash-out', str(ram)]
        outputs, fields = [ram], NATIVE_FIELDS
    else:
        ram, hsh = out / f'{route.stem}.cycle.ram.txt', out / f'{route.stem}.cycle.hash.txt'
        cmd = [str(build / 'ducktales.exe')] + base + ['--align', '0', '--ram-hash-out', str(ram),
                                                        '--hash-out', str(hsh)]
        outputs, fields = [ram, hsh], GOLDEN_FIELDS
    if start:
        cmd += ['--load-state', str(state_path(golden_dir, route, start, args.runtime))]
    t0 = time.monotonic()
    first, compared, error = run_compared(cmd, outputs, golden, fields, start, end,
                                          out / f'{route.stem}.{args.runtime}.log')
    return route, start, end, first, compared, error, time.monotonic() - t0, outputs


def report(result, args, golden_dir):
    route, start, end, first, compared, error, seconds, outputs = result
    span = f'frames {start}-{end - 1}' + (' from its golden state' if start else '')
    if not first and not error:
        print(f'{route.stem:24s} PASS  {span}  {args.runtime}  {seconds:.1f} s ({compared / max(seconds, 1e-3):.0f} fps)')
        return True
    if error:
        print(f'{route.stem:24s} ERROR {span}  {args.runtime}: {error}')
        return False
    n, diff = first
    print(f'{route.stem:24s} FAIL  {span}  {args.runtime}: first difference at frame {n} '
          f'({n - start} frames matched): {" ".join(diff)}')
    for k, (want, got) in diff.items():
        print(f'    {k:7s} golden {want}  candidate {got}')
    for path in outputs:
        with open(path) as f:
            here = [k for k in diff if k in parse_line(f.readline())[1]]
        if here:
            print(f'    py -3 -I tools/native_compare.py "{golden_dir / route.stem}.txt" "{path}" --fields {",".join(here)}')
    print(f'    both builds trace that frame with --trace-frame {n} --trace-out FILE')
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', default='build/headless', help='headless build directory')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*', help='route files (default: tests/routes/*.txt)')
    ap.add_argument('--golden', default=str(ROOT / 'build' / 'golden'), help='golden root; files go in <ROM SHA-256>/')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'quick-verify'))
    ap.add_argument('--jobs', type=int, default=2, help='routes run at once (default 2: the PC is shared)')
    ap.add_argument('--record', action='store_true', help='write the golden files from this build')
    ap.add_argument('--state-every', type=int, default=500, help='with --record: frames between save states')
    ap.add_argument('--runtime', choices=['native', 'cycle'],
                    help='native (ducktales_native, the default) or cycle (ducktales.exe, every field)')
    ap.add_argument('--changed', nargs='+', metavar='RANGE', help='[BANK:]ADDR[-ADDR] ranges a change touches')
    ap.add_argument('--routes-for', nargs='+', metavar='ADDR', help='routine addresses, [BANK:]ADDR')
    ap.add_argument('--from-state', metavar='auto|FRAME', help='start from a golden save state')
    ap.add_argument('--span', type=int, help='with --from-state: frames to run from the start')
    ap.add_argument('--list', action='store_true', help='print the selected routes and start frames, run nothing')
    args = ap.parse_args()

    rom = Path(args.rom)
    if not rom.exists():
        sys.exit(f'missing {rom}')
    rom_sha = file_sha256(rom)
    golden_dir = Path(args.golden) / rom_sha
    routes = [Path(r).resolve() for r in args.routes] if args.routes else \
        sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    build = ROOT / args.build / args.config

    if args.record:
        exe = build / 'ducktales.exe'
        if not exe.exists():
            sys.exit(f'missing {exe} (configure with -DDUCKTALES_HEADLESS=ON)')
        record(args, routes, exe, golden_dir, rom_sha)
        return

    args.runtime = args.runtime or 'native'
    exe = build / ('ducktales_native.exe' if args.runtime == 'native' else 'ducktales.exe')
    if not exe.exists():
        sys.exit(f'missing {exe} (configure with -DDUCKTALES_HEADLESS=ON'
                 + (' -DDUCKTALES_NATIVE=ON)' if args.runtime == 'native' else ')'))
    errors = [e for e in (check_golden(r, golden_dir) for r in routes) if e]
    if errors:
        sys.exit('\n'.join(errors))

    starts = {r.stem: 0 for r in routes}
    specs = (args.changed or []) + (args.routes_for or [])
    if specs:
        ranges = parse_specs(specs, rom.read_bytes()[4])
        selected = []
        for route in routes:
            cover = golden_dir / f'{route.stem}.cover'
            if not cover.exists():
                sys.exit(f'{route.stem}: no coverage in {golden_dir}; record it with --record')
            reach = [s for s, offs in read_cover(cover) if hits(offs, ranges)]
            if reach:
                selected.append(route)
                starts[route.stem] = reach[0]
        if not selected:
            sys.exit(f'no route executes {" ".join(specs)}: quick_verify cannot test it; add a route that does')
        print(f'{len(selected)} of {len(routes)} routes execute {" ".join(specs)}: '
              + ', '.join(f'{r.stem} (first in the segment from frame {starts[r.stem]})' for r in selected))
        routes = selected
    if args.from_state is None:
        starts = {r.stem: 0 for r in routes}
    elif args.from_state != 'auto':
        every = int(read_header(golden_dir / f'{routes[0].stem}.txt')['state_every'])
        starts = {r.stem: min(int(args.from_state), route_frames(r) - 1) // every * every for r in routes}
    elif not specs:
        sys.exit('--from-state auto needs --changed or --routes-for')
    missing = [str(state_path(golden_dir, r, s, args.runtime)) for r in routes
               for s in [starts[r.stem]] if s and not state_path(golden_dir, r, s, args.runtime).exists()]
    if missing:
        sys.exit('no golden state ' + ', '.join(missing) + '; record with --record from a build with this runtime')
    if args.list:
        for r in routes:
            print(f'{r.stem:24s} from frame {starts[r.stem]}')
        return

    t0 = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda r: verify_route(r, starts[r.stem], args, golden_dir), routes))
    ok = [report(r, args, golden_dir) for r in results]
    print(f'{sum(ok)}/{len(ok)} routes pass, {sum(r[4] for r in results)} frames compared ({args.runtime}, '
          f'alignment 0, {args.jobs} jobs) in {time.monotonic() - t0:.1f} s')
    if not all(ok):
        sys.exit(1)


if __name__ == '__main__':
    main()
