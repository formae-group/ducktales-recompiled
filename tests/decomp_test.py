"""Decompilation checks (src/decomp/, docs/DECOMPILATION.md).

  check    every route runs with --decomp check: at every call of every
           decompiled routine (from a site or from another routine's C,
           decomp.c) the original runs in an isolated call and the C
           on the same state, and RAM (the dead stack excepted) and the
           routine's contract registers must agree; the first difference stops
           the run (exit 3). Prints how often each routine was called,
           compared, or given to the original routine.
  replace  (needs --dev-build) every route runs with --decomp replace
           against the check run (a stock run without check mode), both
           with --dt-ram-log: check mode keeps the original's result, so its
           RAM is the stock run's byte for byte, and the check then runs on
           the dev build. CPU RAM outside
           the stack ($0161-$01FF) must be equal after every frame. The stack
           is compared where
           both runs sit in the game's scheduler with every task yielded
           (S = $FF): there the main task's live stack (above its saved S up
           to its base $BF) must be equal. The rest of page 1 holds what
           interrupts pushed, which differs because an NMI the original takes
           inside a replaced routine is taken right after it.
  pokes    (needs --dev-build) states no route reaches, made with
           --dt-poke-at (and extra pad states, written into a copy of the
           route) from a point of re_amazon_moves (POKES, tests/decomp/pokes.py), started
           from the latest golden save state before the first poke where the
           scheduler is idle (power-on without one): each
           case runs in check mode, where the routines it targets must be
           compared at least once, and stock against replace as above.
  events   (needs --dev-build) enhancements whose event a decompiled routine
           raises itself, and a warm reset (EVENTS, tests/decomp/events.py): check mode, and the
           same options with --decomp off against --decomp replace as above. Then the
           routes in SEQUENCES run with --dt-events-log, which handles every
           event and logs its frame and values, with --decomp off, check and
           replace: the three logs must be equal line for line (each event
           once per occurrence: in check mode only the original raises them,
           in replace mode only the C), and in check mode as many
           enemy-defeated and item-collected events as calls of Obj_Defeat
           and Pickup_Take.

    py -3 -I tests/decomp_test.py --build build/headless [--dev-build build/headless-dev]
           [--routes tests/routes/X.txt ...] [--modes check replace pokes events] [--jobs 2]
           [--changed RANGE ... [--from-state [--span N]]] [--golden DIR]

--changed [BANK:]ADDR[-ADDR] ... keeps only the routes whose quick_verify coverage (the golden
directory, tests/quick_verify.py) executes those PRG addresses; --from-state starts each of them
from the latest golden save state at or before the first segment that does where the game's
scheduler is idle (S = $FF on the frame before it: a state inside a task would resume the original's
task while replace mode runs the C), and --span N stops N frames after that segment's start. That is the loop for one routine; the merge gate is every route from power-on.

Every case goes into one queue of --jobs workers, longest first.

DUCKTALES_DECOMP=check py -3 -I tests/verify_routes.py runs the oracle comparison with
the checks on.
"""

import argparse
import concurrent.futures
import itertools
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import quick_verify  # noqa: E402
from romfile import default_rom  # noqa: E402
from decomp.cases import (  # noqa: E402,F401
    route_frames,
    idle_state,
    route_case,
    select_routes,
    poke_case,
    pressed_route,
    event_case,
    sequence_case,
)
from decomp.events import EVENT_CALLS, EVENTS, SEQUENCES  # noqa: E402,F401
from decomp.fixtures import POKE_FRAME, POKE_FRAMES, POKE_ROUTE  # noqa: E402,F401
from decomp.pokes import POKES  # noqa: E402,F401

RAM = 0x800
IDLE_S = 0xFF
TASKS = 0x80
MAIN_TASK_BASE = 0xBF
STACK = 0x161  # page 1 below this is data: palette, consumed-record bits, HUD time (game-over box to $0160)
LINE = re.compile(
    r'^decomp: mode=\S+ routine=(\S+) addr=\$([0-9A-F]{4}) calls=(\d+) compared=(\d+) original=(\d+)$', re.M
)

SYMBOLS = {}
for _name, _addr in re.findall(
    r'^#define DT_ADDR_(\w+)\s+0x([0-9A-Fa-f]{4})', (ROOT / 'src' / 'game' / 'ducktales_symbols.h').read_text(), re.M
):
    SYMBOLS[int(_addr, 16)] = SYMBOLS[int(_addr, 16)] + '/' + _name if int(_addr, 16) in SYMBOLS else _name
NAMED = re.compile(r'\$([0-9A-F]{4})(?= original)')


def run(exe, rom, case, extra):
    cmd = [str(exe), rom, '--frames', str(case['frames']), '--input', str(case['route']), *case['extra'], *extra]
    return subprocess.run(cmd, capture_output=True, text=True)


def check_result(p, case):
    stats = {(m[0], m[1]): tuple(map(int, m[2:])) for m in LINE.findall(p.stdout)}
    err = [
        NAMED.sub(lambda m: f'${m[1]} {SYMBOLS.get(int(m[1], 16), "")}'.rstrip(), l)
        for l in p.stderr.splitlines()
        if l.startswith('[decomp]')
    ]
    compared = {name for (name, _), s in stats.items() if s[1]}
    err += [f'{name} was not compared' for name in case['want'] if name not in compared]
    return p.returncode == 0 and bool(stats) and not err, stats, err


def ram_frames(path):
    with open(path, 'rb') as f:
        while True:
            rec = f.read(RAM + 1)
            if len(rec) < RAM + 1:
                return
            yield rec[:RAM], rec[RAM]


def ram_diff(logs):
    frames = idle = 0
    result = None
    for frame, ((a, sa), (b, sb)) in enumerate(zip(ram_frames(logs[0]), ram_frames(logs[1]))):
        frames += 1
        if result:
            continue
        live = range(0)
        if sa == sb == IDLE_S:
            idle += 1
            if a[TASKS]:
                live = range(0x101 + a[TASKS + 2], 0x100 + MAIN_TASK_BASE + 1)
        diff = [i for i in range(RAM) if a[i] != b[i] and (not STACK <= i < 0x200 or i in live)]
        if diff:
            shown = ' '.join(
                f'${i:04X}{"(" + SYMBOLS[i] + ")" if i in SYMBOLS else ""}:{a[i]:02X}/{b[i]:02X}' for i in diff[:12]
            )
            result = f'frame {frame}: {shown}'
    for log in logs:
        log.unlink()
    return result, f'frames={frames} idle={idle}'


def run_sequence(dev, rom, case, tmp, label):
    """The event logs of --decomp off, check and replace, which must be equal."""
    out, logs = {}, {}
    for mode in ('off', 'check', 'replace'):
        path = Path(tmp) / f'{label}.{case["name"]}.{mode}.events'
        p = run(dev, rom, case, ['--decomp', mode, '--dt-events-log', str(path)])
        logs[mode] = path.read_text().splitlines() if path.exists() else []
        path.unlink(missing_ok=True)
        if mode == 'check':
            ok, stats, err = check_result(p, case)
            calls = {name: s[0] for (name, _), s in stats.items()}
            for event, routine in EVENT_CALLS.items():
                n = sum(f' event={event} ' in f'{line} ' for line in logs['check'])
                if n != calls.get(routine, 0):
                    err.append(f'{n} {event} events, {calls.get(routine, 0)} calls of {routine}')
            out['check'] = (ok and not err, stats, err)
        elif p.returncode:
            failure = f'{mode} run failed (exit {p.returncode}): {p.stderr[-500:]}'
            out.setdefault('check', (False, {}, [failure]))
            out['replace'] = failure, ''
            return out
    off = logs['off']
    first = next(
        (
            f'{mode}, event {i + 1}: {a} / {b}'
            for mode in ('check', 'replace')
            for i, (a, b) in enumerate(itertools.zip_longest(off, logs[mode], fillvalue='(none)'))
            if a != b
        ),
        None,
    )
    kinds = len({line.split(' event=')[1].split()[0] for line in off if ' event=' in line})
    out['replace'] = first, f'events={len(off)} kinds={kinds}'
    return out


def run_case(exe, dev, rom, case, modes, tmp, label):
    if case['name'].endswith('-sequence'):
        return run_sequence(dev, rom, case, tmp, label)
    out = {}
    logs = [Path(tmp) / f'{label}.{case["name"]}.{mode}.ram' for mode in ('base', 'replace')]
    base = None
    if 'check' in modes:
        log = ['--dt-ram-log', str(logs[0])] if 'replace' in modes else []
        base = run(dev if log else exe, rom, case, ['--decomp', 'check', *log])
        out['check'] = check_result(base, case)
    if 'replace' not in modes:
        return out
    if base is None or base.returncode:
        base = run(dev, rom, case, ['--decomp', 'off', '--dt-ram-log', str(logs[0])])
        if base.returncode:
            out['replace'] = f'off run failed (exit {base.returncode}): {base.stderr[-500:]}', 0
            return out
    p = run(dev, rom, case, ['--decomp', 'replace', '--dt-ram-log', str(logs[1])])
    if p.returncode:
        out['replace'] = f'replace run failed (exit {p.returncode}): {p.stderr[-500:]}', 0
        return out
    out['replace'] = ram_diff(logs)
    return out


def print_checks(label, cases, results, failed):
    total = {}
    for case in cases:
        ok, stats, err = results[id(case)].result()['check']
        calls = sum(s[0] for s in stats.values())
        print(f'{label:8s} {case["name"]:24s} {"PASS" if ok else "FAIL"}  calls={calls}', flush=True)
        if not ok:
            failed.append(f'{label} {case["name"]}')
            print('\n'.join(err[:30]))
        for key, s in stats.items():
            t = total.setdefault(key, [0, 0, 0, set()])
            for i in range(3):
                t[i] += s[i]
            if s[1]:
                t[3].add(case['name'])
    print(f'{"routine":22s} {"addr":5s} {"calls":>9s} {"compared":>9s} {"original":>9s}  cases compared')
    for (name, addr), (calls, compared, orig, rs) in sorted(total.items(), key=lambda kv: kv[0][1]):
        print(f'{name:22s} ${addr} {calls:9d} {compared:9d} {orig:9d}  {len(rs)}')


def print_replaces(label, cases, results, failed):
    for case in cases:
        result, frames = results[id(case)].result()['replace']
        print(
            f'{label:8s} {case["name"]:24s} {"PASS" if result is None else "FAIL"}  {frames}'
            + ('' if result is None else f'  first difference: {result}'),
            flush=True,
        )
        if result is not None:
            failed.append(f'{label} {case["name"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build', help='a -DDUCKTALES_DEV_DIAGNOSTICS=ON headless build (replace, pokes, events)')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--modes', nargs='+', default=['check', 'replace', 'pokes', 'events'])
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--golden', default=str(ROOT / 'build' / 'golden'), help='quick_verify golden root')
    ap.add_argument('--changed', nargs='+', metavar='RANGE', help='only routes executing these PRG addresses')
    ap.add_argument('--from-state', action='store_true', help='with --changed: start from the golden state')
    ap.add_argument('--span', type=int, help='with --from-state: frames to run from the start')
    ap.add_argument('--pokes', nargs='+', metavar='NAME', help='only these poke cases')
    args = ap.parse_args()
    if (args.from_state and not args.changed) or (args.span and not args.from_state):
        ap.error('--from-state needs --changed, --span needs --from-state')
    golden = Path(args.golden) / quick_verify.file_sha256(Path(args.rom))
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    cases = select_routes(routes, args, golden) if args.changed else [route_case(r) for r in routes]
    pokes = [poke_case(*p) for p in POKES if not args.pokes or p[0] in args.pokes]
    for case in pokes:
        start, state = idle_state(golden, POKE_ROUTE, case['first'])
        if state:
            case['extra'] = ['--load-state', str(state), *case['extra']]
    events = [event_case(*e) for e in EVENTS] + [sequence_case(r) for r in SEQUENCES]
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    dev = ROOT / args.dev_build / args.config / 'ducktales.exe' if args.dev_build else None
    groups = [
        ('check', 'replace', cases, [m for m in ('check', 'replace') if m in args.modes and (m == 'check' or dev)])
    ]
    if dev:
        groups += [
            (n, n, g, ['check', 'replace'] if n in args.modes else [])
            for n, g in (('pokes', pokes), ('events', events))
        ]
    failed = []
    scratch = ROOT / 'captures' if (ROOT / 'captures').is_dir() else None
    with tempfile.TemporaryDirectory(dir=scratch) as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for case in pokes:
            if 'presses' in case:
                case['route'] = pressed_route(case, tmp)
        jobs = sorted(
            ((case, modes, label) for label, _, g, modes in groups if modes for case in g),
            key=lambda j: -j[0]['frames'],
        )
        results = {
            id(case): pool.submit(run_case, exe, dev, args.rom, case, modes, tmp, label) for case, modes, label in jobs
        }
        for check, replace, g, modes in groups:
            if 'check' in modes:
                print_checks(check, g, results, failed)
            if 'replace' in modes:
                print_replaces(replace, g, results, failed)
    print('all passed' if not failed else 'failed: ' + ', '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
