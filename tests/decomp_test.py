"""Decompilation checks (src/decomp/, docs/decompilation.md).

  check    every route runs with --decomp check: at every call of every
           decompiled routine (from a site or from another routine's C,
           decomp.c) the original runs in an isolated call and the C
           on the same state, and RAM (the dead stack excepted) and the
           routine's contract registers must agree; the first difference stops
           the run (exit 3). Prints how often each routine was called,
           compared, or given to the original routine.
  replace  (needs --dev-build) every route runs stock and with
           --decomp replace, both with --dt-ram-log, and CPU RAM outside
           the stack ($0161-$01FF) must be equal after every frame. The stack
           is compared where
           both runs sit in the game's scheduler with every task yielded
           (S = $FF): there the main task's live stack (above its saved S up
           to its base $BF) must be equal. The rest of page 1 holds what
           interrupts pushed, which differs because an NMI the original takes
           inside a replaced routine is taken right after it.
  pokes    (needs --dev-build) states no route reaches, made with
           --dt-poke-at (and extra pad states, written into a copy of the
           route) from a point of re_amazon_moves (POKES below): each
           case runs in check mode, where the routines it targets must be
           compared at least once, and stock against replace as above.
  events   (needs --dev-build) enhancements whose event a decompiled routine
           raises itself (EVENTS below): check mode, and the same options
           with --decomp off against --decomp replace as above.

    py -3 -I tests/decomp_test.py --build build/headless [--dev-build build/headless-dev]
           [--routes tests/routes/X.txt ...] [--modes check replace pokes events] [--jobs 2]
           [--changed RANGE ... [--from-state [--span N]]] [--golden DIR]

--changed [BANK:]ADDR[-ADDR] ... keeps only the routes whose quick_verify coverage (the golden
directory, tests/quick_verify.py) executes those PRG addresses; --from-state starts each of them
from the latest golden save state at or before the first segment that does where the game's
scheduler is idle (S = $FF on the frame before it: a state inside a task would resume the original's
task while replace mode runs the C), and --span N stops N frames after that segment's start. That is the loop for one routine; the merge gate is every route from power-on.

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
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import quick_verify  # noqa: E402
from romfile import default_rom  # noqa: E402

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

POKE_ROUTE = ROOT / 'tests' / 'routes' / 're_amazon_moves.txt'
POKE_FRAME = 1700  # Scrooge on the ground in the Amazon
POKE_FRAMES = 220
# name, {address: value} stored before POKE_FRAME (or {frame: {...}}), routines that must be compared,
# optionally {frame: buttons} pad states added to the route
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
    (
        'select-refill',
        {0x34A: 0x01, 0x334: 0x01},
        ['Player_SelectRefill'],
        {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'},
    ),
    ('select-poor', {0x34A: 0x01}, ['Player_SelectRefill'], {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'}),
    ('select-full', {}, ['Player_SelectRefill'], {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'}),
    ('hud-total', {0xAB: 0x01}, ['Hud_RefreshTotal']),
    ('scene-1', {0x78: 0x81}, ['Scene_Placed', 'Scene_Done']),
    ('scene-2', {0x78: 0x82}, ['Scene_Placed', 'Scene_Done']),
    ('final-trip', {100: {0x5E: 0x01}}, ['LandSelect_FinalScene', 'FinalTrip_Next', 'FinalTrip_Flashed']),
    ('all-treasures', {100: {0x5D: 0x01, 0xA0: 0x1F}}, ['LandSelect_Banked', 'LandSelect_FinalWait']),
    (
        'total-max',
        {
            100: {
                0x5D: 0x01,
                0x322: 0x07,
                0x323: 0x2C,
                0x324: 0x24,
                0x325: 0x24,
                0x326: 0x01,
                0x327: 0x05,
                0x328: 0x00,
                0x329: 0x00,
                0x32A: 0x00,
                **{0x334 + i: 0x09 for i in range(5)},
            }
        },
        ['BankTransfer_Next'],
    ),
    # slot 9 made a pickup on Scrooge (step 2, waiting): the extra HP dot; a big diamond with money at 999...
    (
        'extra-dot',
        {0x709: 0xA2, 0x719: 0x13, 0x629: 0x2B, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x00, 0x7D9: 0x01, 0x609: 0x80},
        ['Pickup_Take', 'Pickup_ExtraDot', 'Hud_RefillHp'],
    ),
    (
        'money-max',
        {
            0x709: 0xA2,
            0x719: 0x13,
            0x629: 0x61,
            0x6B9: 0xFF,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x7D9: 0x01,
            0x609: 0x80,
            0x324: 0x09,
            0x325: 0x09,
            0x326: 0x09,
        },
        ['Money_Add', 'Money_Max'],
    ),
    # slot 9 made a lift (kind $0F) under Scrooge
    ('lift', {0x709: 0xA0, 0x719: 0x0F, 0x649: 0xB0, 0x729: 0x62, 0x739: 0x00, 0x609: 0x80}, ['Obj_Lift']),
    # the ending flag ($AC): the frame loop starts the ending, whose first scene loads its palettes ($E08F)
    ('ending', {0xAC: 0x01}, ['Palette_LoadScene', 'Ending_Waited', 'EndingScreen_Streamed', 'EndingScreen_Lit']),
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


def route_case(route, start=0, state=None, span=None):
    case = {'name': route.stem, 'route': route, 'frames': route_frames(route), 'extra': [], 'want': []}
    if span:
        case['frames'] = min(case['frames'], start + span)
    if state:
        case['extra'] = ['--load-state', str(state)]
    return case


def select_routes(routes, args):
    rom = Path(args.rom)
    golden = Path(args.golden) / quick_verify.file_sha256(rom)
    ranges = quick_verify.parse_specs(args.changed, rom.read_bytes()[4])
    selected = []
    for route in routes:
        cover = golden / f'{route.stem}.cover'
        if not cover.exists():
            sys.exit(f'{route.stem}: no coverage in {golden}; record it with tests/quick_verify.py --record')
        reach = [s for s, offs in quick_verify.read_cover(cover) if quick_verify.hits(offs, ranges)]
        if reach:
            start = 0
            if args.from_state:
                idle = {
                    int(m[1]) + 1
                    for m in re.finditer(r'^(\d+) .*\bS=FF\b', (golden / f'{route.stem}.txt').read_text(), re.M)
                }
                start = max(
                    (s for s in idle if s <= reach[0] and quick_verify.state_path(golden, route, s).exists()), default=0
                )
            state = quick_verify.state_path(golden, route, start) if start else None
            selected.append(route_case(route, reach[0] if args.from_state else 0, state, args.span))
            print(
                f'{route.stem}: executes {" ".join(args.changed)} from the segment at frame {reach[0]}, run from frame {start}'
            )
    if not selected:
        sys.exit(f'no route executes {" ".join(args.changed)}; add a route that does')
    return selected


def poke_case(name, pokes, want, presses=None):
    if not all(isinstance(v, dict) for v in pokes.values()):
        pokes = {POKE_FRAME: pokes}
    extra = [
        arg
        for frame, at in pokes.items()
        for addr, value in at.items()
        for arg in ('--dt-poke-at', f'{frame}:{addr:X}={value:02X}')
    ]
    case = {'name': name, 'route': POKE_ROUTE, 'frames': POKE_FRAME + POKE_FRAMES, 'extra': extra, 'want': want}
    return case | {'presses': presses} if presses else case


def pressed_route(case, tmp):
    events = {}
    for line in POKE_ROUTE.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            frame, buttons = line.split(None, 1)
            events[int(frame)] = buttons.strip()
    events.update(case['presses'])
    path = Path(tmp) / f'{case["name"]}.txt'
    path.write_text(''.join(f'{frame} {buttons}\n' for frame, buttons in sorted(events.items())))
    return path


def event_case(name, route, options, want):
    path = ROOT / 'tests' / 'routes' / f'{route}.txt'
    return {'name': name, 'route': path, 'frames': route_frames(path), 'extra': options, 'want': want}


def run(exe, rom, case, extra):
    cmd = [str(exe), rom, '--frames', str(case['frames']), '--input', str(case['route']), *case['extra'], *extra]
    return subprocess.run(cmd, capture_output=True, text=True)


def check_case(exe, rom, case):
    p = run(exe, rom, case, ['--decomp', 'check'])
    stats = {(m[0], m[1]): tuple(map(int, m[2:])) for m in LINE.findall(p.stdout)}
    err = [
        NAMED.sub(lambda m: f'${m[1]} {SYMBOLS.get(int(m[1], 16), "")}'.rstrip(), l)
        for l in p.stderr.splitlines()
        if l.startswith('[decomp]')
    ]
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
            shown = ' '.join(
                f'${i:04X}{"(" + SYMBOLS[i] + ")" if i in SYMBOLS else ""}:{a[i]:02X}/{b[i]:02X}' for i in diff[:12]
            )
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
            print(
                f'{label:8s} {name:24s} {"PASS" if result is None else "FAIL"}  frames={frames}'
                + ('' if result is None else f'  first difference: {result}')
            )
            if result is not None:
                failed.append(f'{label} {name}')


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
    args = ap.parse_args()
    if (args.from_state and not args.changed) or (args.span and not args.from_state):
        ap.error('--from-state needs --changed, --span needs --from-state')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    cases = select_routes(routes, args) if args.changed else [route_case(r) for r in routes]
    pokes = [poke_case(*p) for p in POKES]
    events = [event_case(*e) for e in EVENTS]
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    dev = ROOT / args.dev_build / args.config / 'ducktales.exe' if args.dev_build else None
    failed = []
    scratch = ROOT / 'captures' if (ROOT / 'captures').is_dir() else None
    with tempfile.TemporaryDirectory(dir=scratch) as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for case in pokes:
            if 'presses' in case:
                case['route'] = pressed_route(case, tmp)
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
