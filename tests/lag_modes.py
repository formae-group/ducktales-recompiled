"""The native runtime's two lag modes on the full_* routes (docs/NATIVE-RUNTIME.md).

For each route this runs ducktales_native twice:

  faithful   the console's lag (--lag faithful): records every lag frame, how
             many CPU cycles of task work ran past its VBlank, the buttons
             latched at each controller strobe, and RAM each time the task
             yields (one logic step)
  replay     the no-slowdown default, fed the faithful run's buttons strobe by
             strobe, so both runs see the same input on every logic step. (A
             route's per-frame input cannot be replayed as it is: each frame
             the console lags shifts every later button press by one logic
             step, and the route soon goes elsewhere.) It records the lag
             frames left, how long VBlank was held back, and RAM per step

and reports:

  - gameplay lag (picture on) per land and screen, with the cycles over budget
  - lag behind a blank screen (land loads, transitions), which both modes keep
  - the lag left in the no-slowdown mode (must be none with the picture on)
  - the logic-step comparison: RAM after every logic step must be equal in
    both modes except the stack, bytes only the NMI handler uses, and the
    NMI-facing bytes in ALLOWED

    py -3 -I tests/lag_modes.py --build build/headless [--routes R ...]
"""

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402

LANDS = {0: 'Amazon', 1: 'Transylvania', 2: 'African Mines', 3: 'Himalayas', 4: 'Moon', 5: 'select', 9: 'ending'}
# RAM the NMI handler writes and the tasks also read. A lag frame runs one
# more NMI during a logic step, so these may differ between the modes; any
# other difference fails the comparison.
ALLOWED = {
    0x19: 'PPU update request, cleared by the NMI that performs it',
    0x90: 'scheduler flag the NMI sets ($FF4E restarts a pass)',
    **{a: 'sound request queue ($C7 index, $C8-$CF slots) drained by the NMI' for a in range(0xC7, 0xD0)},
    **{a: 'sound engine state (bank 3, run by the NMI)' for a in range(0x370, 0x3F0)},
}
LINE = re.compile(
    r'(\d+) nmi=(\d) pic=(\d) lag=(\d) held=(\d+) end=(\S+) land=(\w+) row=(\w+) screen=(\w+)(?: overrun=(\d+):(\d+))?'
)


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def read_lag(path):
    frames, overrun = [], {}
    for line in path.read_text().splitlines():
        m = LINE.match(line)
        f = dict(
            frame=int(m[1]),
            nmi=m[2] == '1',
            pic=m[3] == '1',
            lag=m[4] == '1',
            held=int(m[5]),
            end=m[6],
            land=int(m[7], 16),
            row=int(m[8], 16),
            screen=int(m[9], 16),
        )
        if m[10]:
            overrun[int(m[10])] = int(m[11])
        frames.append(f)
    return frames, overrun


def read_steps(path):
    data = path.read_bytes()
    return [data[i : i + 0x800] for i in range(0, len(data), 0x800)]


def run(native, rom, route, out):
    n = str(route_frames(route))
    base = [str(native), rom, '--frames', n]
    s = out / route.stem
    subprocess.run(
        base
        + [
            '--input',
            str(route),
            '--lag',
            'faithful',
            '--lag-log',
            f'{s}.faithful.lag',
            '--latch-log',
            f'{s}.latch',
            '--step-dump',
            f'{s}.faithful.steps',
            '--nmi-audit',
            f'{s}.audit',
        ],
        capture_output=True,
        check=True,
    )
    subprocess.run(
        base
        + [
            '--latch-input',
            f'{s}.latch',
            '--lag',
            'none',
            '--lag-log',
            f'{s}.replay.lag',
            '--step-dump',
            f'{s}.replay.steps',
        ],
        capture_output=True,
        check=True,
    )
    return route


def where(f):
    return f"{LANDS.get(f['land'], hex(f['land']))} row {f['row']} screen {f['screen']:02X}"


def report(route, out):
    s = out / route.stem
    faithful, overrun = read_lag(Path(f'{s}.faithful.lag'))
    none, _ = read_lag(Path(f'{s}.replay.lag'))
    ok = True
    play = [f for f in faithful if f['lag'] and f['pic']]
    blank = [f for f in faithful if f['lag'] and not f['pic']]
    print(f'== {route.stem}: {len(faithful)} frames')
    over = [overrun.get(f['frame'], 0) for f in play]
    if play:
        print(
            f'  faithful: {len(play)} gameplay lag frames, {sum(over)} cycles over budget in all '
            f'(mean {sum(over) // len(over)}, max {max(over)}; a frame has 29781)'
        )
        by = defaultdict(list)
        for f, o in zip(play, over):
            by[where(f)].append((f['frame'], o))
        for w, items in sorted(by.items(), key=lambda kv: kv[1][0][0]):
            os_ = [o for _, o in items]
            print(
                f'    {w:34s} {len(items):3d} frames ({items[0][0]}-{items[-1][0]})  over: max {max(os_)}, '
                f'mean {sum(os_) // len(os_)}'
            )
    else:
        print('  faithful: no gameplay lag')
    runs = sum(1 for i, f in enumerate(blank) if i == 0 or blank[i - 1]['frame'] != f['frame'] - 1)
    print(f'  faithful: {len(blank)} lag frames behind a blank screen in {runs} stretches (kept in both modes)')
    left = [f for f in none if f['lag'] and f['pic']]
    held = [f for f in none if f['held']]
    ends = defaultdict(int)
    for f in held:
        ends[f['end']] += 1
    print(
        f'  no-slowdown: {len(left)} gameplay lag frames left; VBlank held on {len(held)} frames'
        + (
            f" (max {max(f['held'] for f in held)} cycles, "
            + ', '.join(f'{k} {v}' for k, v in sorted(ends.items()))
            + ')'
            if held
            else ''
        )
    )
    if left:
        ok = False
        print('    left at frames ' + ' '.join(str(f['frame']) for f in left[:20]))
    private = set()
    for line in Path(f'{s}.audit').read_text().splitlines():
        addr, writes, reads = line.split()
        if reads == 'task_reads=0':
            private.add(int(addr, 16))
    keep = 0
    for a in range(0x800):
        if not (0x100 <= a < 0x200) and a not in private:
            keep |= 0xFF << (8 * (0x7FF - a))
    a_steps, b_steps = read_steps(Path(f'{s}.faithful.steps')), read_steps(Path(f'{s}.replay.steps'))
    n = min(len(a_steps), len(b_steps))
    first = {}
    for k in range(n):
        x = (int.from_bytes(a_steps[k], 'big') ^ int.from_bytes(b_steps[k], 'big')) & keep
        while x:
            byte = (x.bit_length() - 1) // 8
            first.setdefault(0x7FF - byte, k)
            x &= ~(0xFF << (8 * byte))
    bad = {a: k for a, k in first.items() if a not in ALLOWED}
    print(
        f'  logic steps compared: {n} ({len(a_steps)} faithful, {len(b_steps)} no-slowdown); '
        f'{len(private)} NMI-only bytes and the stack ignored'
    )
    if first:
        print(
            '    differ (first step): '
            + ' '.join(f'${a:03X}@{k}' for a, k in sorted(first.items(), key=lambda t: t[1]))
        )
    if bad:
        ok = False
        print('    NOT ALLOWED: ' + ' '.join(f'${a:03X}' for a in sorted(bad)))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'lag-modes'))
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    native = ROOT / args.build / args.config / 'ducktales_native.exe'
    for p in (native, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('full_*.txt'))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(lambda r: run(native, args.rom, r, out), routes))
    failed = [r.stem for r in done if not report(r, out)]
    print('failed: ' + ', '.join(failed) if failed else 'all routes: no gameplay lag left; logic steps equal')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
