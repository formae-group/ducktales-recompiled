"""Differential test of the DuckTales build against Mesen2.

For every route in tests/routes/*.txt this replays the same input schedule in
  - ducktales.exe --frame-log FILE --frame-log-at mesen (records at scanline
    240 dot 0 of each frame), and
  - Mesen2's headless test runner with tools/mesen/replay_route.lua (records at
    Mesen's EndFrame event, also scanline 240 dot 0),
then compares the records frame by frame and reports, per route and domain,
the first divergent frame and byte. Domains: CPU RAM (always), OAM and palette
RAM (--domains), nametable RAM (--domains ciram).

    py -3 -I tools/mesen/fetch_mesen2.py
    py -3 -I tests/mesen_compare.py [--routes tests/routes/amazon_intro.txt] [--align 1] [--ignore 0100-01FF]

Power-on state: Mesen2 fills CPU RAM with zeros by default (NesConfig
RamPowerOnState = AllZeros); ducktales.exe defaults to the AccuracyCoin
console pattern. --ram-init picks the same fill for both (zeros, ones), or
"native" to leave each emulator at its default. See docs/mesen.md.
Exit status: 0 when every compared domain (with --ignore, CPU RAM outside the
ignored ranges instead of all of it) matched on every frame of every route and
alignment, 1 otherwise, 2 on a setup error.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LUA = ROOT / 'tools' / 'mesen' / 'replay_route.lua'
MESEN_RAM = {'zeros': 'AllZeros', 'ones': 'AllOnes'}
# Every Mesen2 start re-extracts its native DLLs into the portable folder, so two
# instances sharing that folder make each other fail; run one at a time.
MESEN_LOCK = threading.Lock()


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def read_log(path):
    """frame -> dict(cycles, palette, oam, ram, ciram) from a CYCFRAME log."""
    out = {}
    with open(path, 'rb') as f:
        if f.read(8) != b'CYCFRAME':
            raise SystemExit(f'{path}: not a frame log')
        (version,) = struct.unpack('<I', f.read(4))
        nlen = 6 if version >= 2 else 5
        while True:
            head = f.read(12 + 4 * nlen)
            if len(head) < 12 + 4 * nlen:
                return out
            frame, lo, hi, *lens = struct.unpack('<III%dI' % nlen, head)
            rec = {
                'cycles': lo | hi << 32,
                'palette': f.read(32),
                'oam': f.read(256),
                'ram': f.read(lens[0]),
                'ciram': f.read(lens[1]),
            }
            f.seek(sum(lens[2:]), 1)
            out[frame] = rec


def compact_log(src, dst, keep_ciram):
    """Rewrite a ducktales.exe frame log without CHR RAM and picture (about 130 KB a frame) and delete it."""
    with open(src, 'rb') as f, open(dst, 'wb') as out:
        head = f.read(12)
        if head[:8] != b'CYCFRAME':
            raise RuntimeError(f'{src}: not a frame log')
        nlen = 6 if struct.unpack('<I', head[8:])[0] >= 2 else 5
        out.write(b'CYCFRAME' + struct.pack('<I', 2))
        while True:
            h = f.read(12 + 4 * nlen)
            if len(h) < 12 + 4 * nlen:
                break
            frame, lo, hi, *lens = struct.unpack('<III%dI' % nlen, h)
            fixed = f.read(32 + 256 + lens[0])
            ciram = f.read(lens[1])
            f.seek(sum(lens[2:]), 1)
            ciram = ciram if keep_ciram else b''
            out.write(struct.pack('<9I', frame, lo, hi, lens[0], len(ciram), 0, 0, 0, 0) + fixed + ciram)
    os.unlink(src)


def palette_canonical(p):
    """Palette RAM as the PPU reads it: entries 10/14/18/1C are entries 00/04/08/0C."""
    return bytes(p[i & ~0x10 if (i & 0x13) == 0x10 else i] for i in range(32))


def domain_bytes(rec, domain):
    if domain == 'palette':
        return palette_canonical(rec['palette'])
    return rec[domain]


def run_ours(args, route, frames, out, align):
    raw = out.with_suffix('.raw')
    cmd = [
        str(args.exe),
        args.rom,
        '--frames',
        str(frames),
        '--input',
        str(route),
        '--frame-log',
        str(raw),
        '--frame-log-at',
        'mesen',
        '--align',
        str(align),
    ]
    if args.trace_frame is not None:
        cmd += ['--trace-frame', str(args.trace_frame), '--trace-out', str(out.with_suffix('.trace.txt'))]
    if args.ram_init != 'native':
        cmd += ['--ram-init', args.ram_init]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise RuntimeError(f'ducktales.exe failed ({p.returncode}): {p.stderr.strip()[-400:]}')
        compact_log(raw, out, 'ciram' in args.domains)
    finally:
        if raw.exists():
            raw.unlink()


def run_mesen(args, route, frames, out, status):
    env = dict(
        os.environ,
        DT_MESEN_ROUTE=str(route),
        DT_MESEN_OUT=str(out),
        DT_MESEN_FRAMES=str(frames),
        DT_MESEN_STATUS=str(status),
        DT_MESEN_CIRAM='1' if 'ciram' in args.domains else '0',
    )
    if args.trace_frame is not None:
        env.update(DT_MESEN_TRACE_FRAME=str(args.trace_frame), DT_MESEN_TRACE_OUT=str(out.with_suffix('.trace.txt')))
    if status.exists():
        status.unlink()
    cmd = [
        str(args.mesen),
        '--testrunner',
        str(LUA),
        args.rom,
        '--debug.scriptWindow.allowIoOsAccess=true',
        f'--timeout={args.timeout}',
        '--nes.region=Ntsc',
        '--nes.randomizeCpuPpuAlignment=false',
        '--nes.randomizeMapperPowerOnState=false',
        '--emulation.runAheadFrames=0',
    ]
    if args.ram_init != 'native':
        cmd.append(f'--nes.ramPowerOnState={MESEN_RAM[args.ram_init]}')
    with MESEN_LOCK:
        p = subprocess.run(cmd, capture_output=True, text=True, env=env)
    msg = status.read_text().strip() if status.exists() else 'no status (timeout or script not loaded)'
    if p.returncode != 0 or not msg.startswith('ok'):
        raise RuntimeError(f'Mesen exit {p.returncode}: {msg}')


def parse_range(text):
    a, _, b = text.partition('-')
    return int(a, 16), int(b or a, 16)


def compare(ours, mesen, domains, frames, first_frame, ignore):
    """Per domain: first divergent frame with its differing bytes, and the number of divergent frames.

    'ram' is all of CPU RAM; 'ram*' is CPU RAM without the --ignore ranges (when any are given)."""
    result = {}
    for d in domains:
        base = d.rstrip('*')
        skip = set()
        if d.endswith('*'):
            for lo, hi in ignore:
                skip.update(range(lo, hi + 1))
        first, count = None, 0
        for fr in range(first_frame, frames):
            a, b = ours.get(fr), mesen.get(fr)
            if a is None or b is None:
                result[d] = {'error': f'frame {fr} missing from {"ours" if a is None else "Mesen"}'}
                break
            x, y = domain_bytes(a, base), domain_bytes(b, base)
            if x == y:
                continue
            diffs = [(i, x[i], y[i]) for i in range(min(len(x), len(y))) if x[i] != y[i] and i not in skip]
            if not diffs:
                continue
            count += 1
            if first is None:
                first = (fr, diffs)
        else:
            result[d] = {'first': first, 'count': count}
    return result


def describe(frames, first_frame, res, ours, mesen, show):
    lines = []
    span = frames - first_frame
    for d, r in res.items():
        if 'error' in r:
            lines.append(f'  {d:8s} ERROR {r["error"]}')
        elif r['first'] is None:
            lines.append(f'  {d:8s} match on all {span} frames')
        else:
            fr, diffs = r['first']
            head = ', '.join('$%04X ours=%02X mesen=%02X' % t for t in diffs[:show])
            more = f' (+{len(diffs) - show} more)' if len(diffs) > show else ''
            line = (
                f'  {d:8s} FIRST DIVERGENCE frame {fr}: {len(diffs)} bytes: {head}{more}; '
                f'{r["count"]}/{span} frames differ'
            )
            base = d.rstrip('*')
            for k, word in ((1, 'ahead of'), (-1, 'behind')):
                if fr + k in ours and domain_bytes(ours[fr + k], base) == domain_bytes(mesen[fr], base):
                    line += f' [Mesen frame {fr} equals our frame {fr + k}: Mesen runs one frame {word} us]'
                    break
            lines.append(line)
    c = [ours[f]['cycles'] - mesen[f]['cycles'] for f in (0, frames - 1) if f in ours and f in mesen]
    if c:
        lines.append(f'  cycles   ours-mesen at record 0: {c[0]:+d}, at record {frames - 1}: {c[-1]:+d}')
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--mesen', default=str(ROOT / 'build' / 'tools' / 'mesen2' / 'Mesen.exe'))
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*', help='route files (default: tests/routes/*.txt)')
    ap.add_argument('--frames', type=int, help='frames per route (default: last route line + 120)')
    ap.add_argument('--from-frame', type=int, default=0, help='first frame to compare (default 0)')
    ap.add_argument('--ram-init', choices=['zeros', 'ones', 'native'], default='zeros')
    ap.add_argument(
        '--align',
        type=int,
        nargs='+',
        default=[0, 1, 2, 3],
        choices=range(4),
        help='ducktales.exe CPU/PPU alignments to compare (default all four; one Mesen run serves all)',
    )
    ap.add_argument('--domains', default='ram,oam,palette', help='comma list of ram, oam, palette, ciram')
    ap.add_argument(
        '--ignore',
        type=parse_range,
        action='append',
        default=[],
        metavar='LO-HI',
        help='hex CPU RAM range left out of an extra "ram*" line and of the exit status, '
        'e.g. --ignore 0100-01FF for the stack page',
    )
    ap.add_argument('--show', type=int, default=8, help='divergent bytes to list per domain')
    ap.add_argument('--timeout', type=int, default=900, help='Mesen test runner timeout, seconds')
    ap.add_argument('--out', default=str(ROOT / 'captures' / 'mesen'))
    ap.add_argument(
        '--jobs',
        type=int,
        default=2,
        help='parallel runs; each ducktales.exe run writes a '
        'temporary frame log of about 130 KB per frame (up to 1 GB) before it is compacted',
    )
    ap.add_argument(
        '--min-free-gb',
        type=float,
        default=8,
        help='refuse to start unless this much disk stays free ' 'after the temporary frame logs (default 8)',
    )
    ap.add_argument('--reuse', action='store_true', help='compare existing logs in --out without running')
    ap.add_argument(
        '--trace-frame',
        type=int,
        help='also write both instruction traces of this frame next to the '
        'logs (*.trace.txt), for tools/mesen/trace_diff.py',
    )
    args = ap.parse_args()

    args.exe = ROOT / args.build / args.config / 'ducktales.exe'
    args.domains = [d.strip() for d in args.domains.split(',') if d.strip()]
    if 'ram' not in args.domains:
        args.domains.insert(0, 'ram')
    bad = set(args.domains) - {'ram', 'oam', 'palette', 'ciram'}
    if bad:
        sys.exit(f'unknown domain(s): {", ".join(sorted(bad))}')
    if args.ignore:
        args.domains.insert(args.domains.index('ram') + 1, 'ram*')
    gating = [d for d in args.domains if not (args.ignore and d == 'ram')]
    for p in ((args.exe, args.mesen, args.rom) if not args.reuse else ()):
        if not Path(p).exists():
            print(f'missing {p} (build/tools/mesen2 comes from tools/mesen/fetch_mesen2.py)', file=sys.stderr)
            sys.exit(2)
    routes = (
        [Path(r).resolve() for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    )
    # Mesen2 changes its working directory to its home folder: give it absolute paths only.
    out_dir = Path(args.out).resolve()
    args.rom = str(Path(args.rom).resolve())
    out_dir.mkdir(parents=True, exist_ok=True)
    need = args.jobs * 140_000 * max((args.frames or route_frames(r)) for r in routes) * 1.2
    free = shutil.disk_usage(out_dir).free
    if not args.reuse and free - need < args.min_free_gb * 1e9:
        print(
            f'not enough free disk space in {out_dir}: {free / 1e9:.1f} GB free, the temporary frame logs of '
            f'{args.jobs} parallel runs need about {need / 1e9:.1f} GB and --min-free-gb {args.min_free_gb} must '
            f'stay free (lower --jobs or --frames)',
            file=sys.stderr,
        )
        sys.exit(2)

    def mesen_log(route):
        return out_dir / f'{route.stem}.{args.ram_init}.mesen.bin'

    def ours_log(route, align):
        return out_dir / f'{route.stem}.{args.ram_init}_a{align}.ours.bin'

    def frames_of(route):
        return args.frames or route_frames(route)

    failed = False
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        runs = {}
        if not args.reuse:
            for route in routes:
                runs[route, None] = pool.submit(
                    run_mesen,
                    args,
                    route,
                    frames_of(route),
                    mesen_log(route),
                    out_dir / f'{route.stem}.{args.ram_init}.mesen.status',
                )
                for align in args.align:
                    runs[route, align] = pool.submit(
                        run_ours, args, route, frames_of(route), ours_log(route, align), align
                    )
        for route in routes:
            frames = frames_of(route)
            try:
                if (route, None) in runs:
                    runs[route, None].result()
                mesen = read_log(mesen_log(route))
            except Exception as e:  # report and carry on with the other routes
                print(f'{route.stem}: ERROR {e}')
                failed = True
                continue
            for align in args.align:
                try:
                    if (route, align) in runs:
                        runs[route, align].result()
                    ours = read_log(ours_log(route, align))
                except Exception as e:
                    print(f'{route.stem} align {align}: ERROR {e}')
                    failed = True
                    continue
                res = compare(ours, mesen, args.domains, frames, args.from_frame, args.ignore)
                ok = all('error' not in res[d] and res[d]['first'] is None for d in gating)
                failed |= not ok
                print(
                    f'{route.stem}: {"MATCH" if ok else "DIVERGES"} ({frames} frames, ram-init {args.ram_init}, '
                    f'align {align})'
                )
                print('\n'.join(describe(frames, args.from_frame, res, ours, mesen, args.show)))
                sys.stdout.flush()
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
