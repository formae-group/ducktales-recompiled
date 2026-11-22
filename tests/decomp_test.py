"""Decompilation checks (src/decomp/, docs/decompilation.md).

  check    every route runs with --decomp check: at every call of every
           decompiled routine the original runs in an isolated call and the C
           on the same state, and RAM (the dead stack excepted) and the
           routine's contract registers must agree; the first difference stops
           the run (exit 3). Prints how often each routine was called,
           compared, or given to the original routine.
  replace  (needs --dev-build) every route runs stock and with
           --decomp replace, both with --dt-ram-log, and CPU RAM outside
           the stack ($0160-$01FF) must be equal after every frame. The stack
           is compared where
           both runs sit in the game's scheduler with every task yielded
           (S = $FF): there the main task's live stack (above its saved S up
           to its base $BF) must be equal. The rest of page 1 holds what
           interrupts pushed, which differs because an NMI the original takes
           inside a replaced routine is taken right after it.
  pokes    (needs --dev-build) states no route reaches, made with
           --dt-poke-at from a point of re_amazon_moves (POKES below): each
           case runs in check mode, where the routines it targets must be
           compared at least once, and stock against replace as above.
  events   (needs --dev-build) enhancements whose event a decompiled routine
           raises itself (EVENTS below): check mode, and the same options
           with --decomp off against --decomp replace as above.

    py -3 -I tests/decomp_test.py --build build/headless [--dev-build build/headless-dev]
           [--routes tests/routes/X.txt ...] [--modes check replace pokes events] [--jobs 2]

DUCKTALES_DECOMP=check py -3 -I tests/verify_routes.py runs the oracle comparison with
the checks on.
"""
import argparse
import concurrent.futures
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAM = 0x800
IDLE_S = 0xFF
TASKS = 0x80
MAIN_TASK_BASE = 0xBF
STACK = 0x160      # page 1 below this is data: palette, consumed-record bits, HUD time
LINE = re.compile(r'^decomp: mode=\S+ routine=(\S+) addr=\$([0-9A-F]{4}) calls=(\d+) compared=(\d+) original=(\d+)$', re.M)

POKE_ROUTE = ROOT / 'tests' / 'routes' / 're_amazon_moves.txt'
POKE_FRAME = 1700   # Scrooge on the ground in the Amazon
POKE_FRAMES = 220
# name, {address: value} stored before POKE_FRAME (or {frame: {...}}), routines that must be compared
POKES = [
    ('hurt-wait', {0x39: 0x05, 0x7D: 0x20}, ['Player_Hurt']),
    ('hurt-now', {0x39: 0x05, 0x7D: 0x00}, ['Player_Hurt', 'Player_TakeDamage']),
    ('hurt-last-dot', {0x39: 0x05, 0x7D: 0x00, 0x346: 0x01, 0x34A: 0x01}, ['Player_Hurt', 'Player_Death']),
    ('pit-falling', {0x39: 0x08}, ['Player_Pit']),
    ('pit-death', {0x39: 0x08, 0x640: 0xD8}, ['Player_Pit', 'Player_PitDeath']),
    ('carried-10', {0x39: 0x0A}, ['Player_Carried']),
    ('carried-11', {0x39: 0x0B}, ['Player_Carried']),
    ('mode12', {0x39: 0x0C}, ['Player_Mode12']),
    ('mode12-page7', {0x39: 0x0C, 0x780: 0x00, 0x730: 0x07}, ['Player_Mode12']),
    ('invulnerable-held', {0x7D: 0x90, 0x75: 0x80}, ['Player_Update']),
    ('slippery', {POKE_FRAME + 10 * i: {0x71: 0x01} for i in range(20)}, ['Player_Walk']),
]

# name, route, options, routines that must be compared
EVENTS = [
    ('no-damage', 'moon_intro', ['--no-damage', 'on'], ['Player_Knockback']),
    ('jump-scale-125', 're_amazon_moves', ['--jump-scale', '125'], ['Player_StartJump']),
]


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def route_case(route):
    return {'name': route.stem, 'route': route, 'frames': route_frames(route), 'extra': [], 'want': []}


def poke_case(name, pokes, want):
    if not all(isinstance(v, dict) for v in pokes.values()):
        pokes = {POKE_FRAME: pokes}
    extra = [arg for frame, at in pokes.items() for addr, value in at.items()
             for arg in ('--dt-poke-at', f'{frame}:{addr:X}={value:02X}')]
    return {'name': name, 'route': POKE_ROUTE, 'frames': POKE_FRAME + POKE_FRAMES, 'extra': extra, 'want': want}


def event_case(name, route, options, want):
    path = ROOT / 'tests' / 'routes' / f'{route}.txt'
    return {'name': name, 'route': path, 'frames': route_frames(path), 'extra': options, 'want': want}


def run(exe, rom, case, extra):
    cmd = [str(exe), rom, '--frames', str(case['frames']), '--input', str(case['route']), *case['extra'], *extra]
    return subprocess.run(cmd, capture_output=True, text=True)


def check_case(exe, rom, case):
    p = run(exe, rom, case, ['--decomp', 'check'])
    stats = {(m[0], m[1]): tuple(map(int, m[2:])) for m in LINE.findall(p.stdout)}
    err = [l for l in p.stderr.splitlines() if l.startswith('[decomp]')]
    compared = {name for (name, _), s in stats.items() if s[1]}
    err += [f'{name} was not compared' for name in case['want'] if name not in compared]
    return case['name'], p.returncode == 0 and bool(stats) and not err, stats, err


def ram_frames(path):
    with open(path, 'rb') as f:
        while True:
            rec = f.read(RAM + 1)
            if len(rec) < RAM + 1:
                return
            yield rec[:RAM], rec[RAM]


def replace_case(exe, rom, case, tmp):
    logs = []
    for mode in ('off', 'replace'):
        log = Path(tmp) / f'{case["name"]}.{mode}.ram'
        p = run(exe, rom, case, ['--decomp', mode, '--dt-ram-log', str(log)])
        if p.returncode:
            return case['name'], f'{mode} run failed (exit {p.returncode}): {p.stderr[-500:]}', 0
        logs.append(log)
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
            shown = ' '.join(f'${i:04X}:{a[i]:02X}/{b[i]:02X}' for i in diff[:12])
            result = f'frame {frame}: {shown}'
    for log in logs:
        log.unlink()
    return case['name'], result, f'{frames} idle={idle}'


def run_checks(pool, exe, rom, cases, label, failed):
    total = {}
    for name, ok, stats, err in pool.map(lambda c: check_case(exe, rom, c), cases):
        calls = sum(s[0] for s in stats.values())
        print(f'{label:8s} {name:24s} {"PASS" if ok else "FAIL"}  calls={calls}')
        if not ok:
            failed.append(f'{label} {name}')
            print('\n'.join(err[:30]))
        for key, s in stats.items():
            t = total.setdefault(key, [0, 0, 0, set()])
            for i in range(3):
                t[i] += s[i]
            if s[1]:
                t[3].add(name)
    print(f'{"routine":22s} {"addr":5s} {"calls":>9s} {"compared":>9s} {"original":>9s}  cases compared')
    for (name, addr), (calls, compared, orig, rs) in sorted(total.items(), key=lambda kv: kv[0][1]):
        print(f'{name:22s} ${addr} {calls:9d} {compared:9d} {orig:9d}  {len(rs)}')


def run_replaces(pool, exe, rom, cases, label, failed):
    with tempfile.TemporaryDirectory(dir=ROOT / 'captures' if (ROOT / 'captures').is_dir() else None) as tmp:
        for name, result, frames in pool.map(lambda c: replace_case(exe, rom, c, tmp), cases):
            print(f'{label:8s} {name:24s} {"PASS" if result is None else "FAIL"}  frames={frames}'
                  + ('' if result is None else f'  first difference: {result}'))
            if result is not None:
                failed.append(f'{label} {name}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build', help='a -DDUCKTALES_DEV_DIAGNOSTICS=ON headless build (replace, pokes, events)')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--modes', nargs='+', default=['check', 'replace', 'pokes', 'events'])
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    cases = [route_case(r) for r in routes]
    pokes = [poke_case(*p) for p in POKES]
    events = [event_case(*e) for e in EVENTS]
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    dev = ROOT / args.dev_build / args.config / 'ducktales.exe' if args.dev_build else None
    failed = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        if 'check' in args.modes:
            run_checks(pool, exe, args.rom, cases, 'check', failed)
        if 'replace' in args.modes and dev:
            run_replaces(pool, dev, args.rom, cases, 'replace', failed)
        if 'pokes' in args.modes and dev:
            run_checks(pool, dev, args.rom, pokes, 'pokes', failed)
            run_replaces(pool, dev, args.rom, pokes, 'pokes', failed)
        if 'events' in args.modes and dev:
            run_checks(pool, dev, args.rom, events, 'events', failed)
            run_replaces(pool, dev, args.rom, events, 'events', failed)
    print('all passed' if not failed else 'failed: ' + ', '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
