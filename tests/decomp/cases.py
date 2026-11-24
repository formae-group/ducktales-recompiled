"""The case builders of tests/decomp_test.py: routes (all, or those that run --changed code, from
a golden save state), poke cases with their pad states, events and event sequences."""

import re
import sys
from pathlib import Path

import quick_verify

from decomp.events import EVENT_CALLS
from decomp.fixtures import (
    POKE_ROUTE,
    POKE_FRAME,
    POKE_FRAMES,
    ROOT,
)


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def idle_state(golden, route, frame):
    log = golden / f'{route.stem}.txt'
    if not log.exists():
        return 0, None
    idle = {int(m[1]) + 1 for m in re.finditer(r'^(\d+) .*\bS=FF\b', log.read_text(), re.M)}
    start = max((s for s in idle if s <= frame and quick_verify.state_path(golden, route, s).exists()), default=0)
    return start, quick_verify.state_path(golden, route, start) if start else None


def route_case(route, start=0, state=None, span=None):
    case = {'name': route.stem, 'route': route, 'frames': route_frames(route), 'extra': [], 'want': []}
    if span:
        case['frames'] = min(case['frames'], start + span)
    if state:
        case['extra'] = ['--load-state', str(state)]
    return case


def select_routes(routes, args, golden):
    ranges = quick_verify.parse_specs(args.changed, Path(args.rom).read_bytes()[4])
    selected = []
    for route in routes:
        cover = golden / f'{route.stem}.cover'
        if not cover.exists():
            sys.exit(f'{route.stem}: no coverage in {golden}; record it with tests/quick_verify.py --record')
        reach = [s for s, offs in quick_verify.read_cover(cover) if quick_verify.hits(offs, ranges)]
        if reach:
            start, state = idle_state(golden, route, reach[0]) if args.from_state else (0, None)
            selected.append(route_case(route, reach[0] if args.from_state else 0, state, args.span))
            print(
                f'{route.stem}: executes {" ".join(args.changed)} from the segment at frame {reach[0]}, run from frame {start}'
            )
    if not selected:
        sys.exit(f'no route executes {" ".join(args.changed)}; add a route that does')
    return selected


def poke_case(name, pokes, want, presses=None, frames=POKE_FRAMES):
    if not all(isinstance(v, dict) for v in pokes.values()):
        pokes = {POKE_FRAME: pokes}
    extra = [
        arg
        for frame, at in pokes.items()
        for addr, value in at.items()
        for arg in ('--dt-poke-at', f'{frame}:{addr:X}={value:02X}')
    ]
    first = min([*pokes, *(presses or {})], default=POKE_FRAME)
    case = {
        'name': name,
        'route': POKE_ROUTE,
        'frames': POKE_FRAME + frames,
        'extra': extra,
        'want': want,
        'first': first,
    }
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


def event_case(name, route, options, want, frames=None):
    path = ROOT / 'tests' / 'routes' / f'{route}.txt'
    return {'name': name, 'route': path, 'frames': frames or route_frames(path), 'extra': options, 'want': want}


def sequence_case(route):
    path = ROOT / 'tests' / 'routes' / f'{route}.txt'
    want = list(EVENT_CALLS.values())
    return {'name': f'{route}-sequence', 'route': path, 'frames': route_frames(path), 'extra': [], 'want': want}
