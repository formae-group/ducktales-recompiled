"""Extend a route by searching over short input macros from save states.

Starting from STATE (saved after frame F of ROUTE by --save-state; the frame is the number at the
end of the file stem, e.g. am_00400.bin), each step runs every macro for its length from the
current state in parallel, scores the frame-logged result and appends the best macro to the
route. When no single macro makes safe progress, every macro is also tried after one of the
--prep macros. A macro that costs HP or a life is only taken when every alternative also does,
after up to --backtrack steps of backtracking.

Score: progress along --goal (world X = camera $F3:$F2 + screen X $0650 for right/left; world
Y = screen row $F1 * 240 + screen Y $0640 for up/down), optionally minus --ypenalty per pixel of
descent; for to:X,Y the negated distance to that point; for min:ADDR 300 per unit the byte drops), minus heavy penalties for HP dots and lives lost. Two delayed losses are detected early:
being frozen at a screen Y of $D0 or more is a fall into a pit (Scrooge hangs there for about 200
frames before the life is taken), and action $2C (held by a plant) costs a dot about 60 frames
later.

    py -3 -I tools/route_search.py ROUTE STATE --steps N [--goal right] [--out ROUTE2]
"""
import argparse
import concurrent.futures as cf
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from framelog import frames  # noqa: E402
from route_seg import hp_dots  # noqa: E402

MACROS = {
    'walk': [(0, 'RIGHT')],
    'jump': [(0, 'RIGHT+A')],
    'hop': [(0, 'RIGHT+A'), (8, 'RIGHT')],
    'pogo': [(0, 'RIGHT+A'), (6, 'RIGHT+A+B+DOWN')],
    'pogo_still': [(0, 'A'), (6, 'A+B+DOWN')],
    'wait': [(0, '-')],
    'back': [(0, 'LEFT')],
    'back_jump': [(0, 'LEFT+A')],
    'swing': [(0, 'B'), (8, '-')],
    'jump_up': [(0, 'A')],
    'climb': [(0, 'UP')],
    'down': [(0, 'DOWN')],
    'pogo_back': [(0, 'LEFT+A'), (6, 'LEFT+A+B+DOWN')],
}


def read_route(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            f, *b = line.split()
            rows.append((int(f), b[0] if b else '-'))
    return rows


def write_route(path, rows):
    Path(path).write_text(''.join(f'{f} {b}\n' for f, b in rows))


def flip(btn, goal):
    if goal == 'left':
        return btn.replace('RIGHT', 'TMP').replace('LEFT', 'RIGHT').replace('TMP', 'LEFT')
    return btn


def losses(log):
    first = log[0][1]
    hp_loss = max(0, max(hp_dots(first) - hp_dots(r) for f, r, _, _ in log))
    if any(r[0x620] == 0x2C for f, r, _, _ in log):
        hp_loss = max(hp_loss, 1)
    lv_loss = max(0, first[0x361] - min(r[0x361] for f, r, _, _ in log))
    still = 0
    for i in range(1, len(log)):
        r, p = log[i][1], log[i - 1][1]
        still = still + 1 if r[0x640] >= 0xD0 and r[0x640] == p[0x640] else 0
        if still >= 6:
            lv_loss = max(lv_loss, 1)
    return hp_loss, lv_loss


def run_one(a, work, rows, state, frame, name, length):
    d = work / name
    d.mkdir(parents=True, exist_ok=True)
    macro = []
    parts = name.split('+')
    for i, part in enumerate(parts):
        macro += [(frame + 1 + i * length + o, flip(b, a.goal)) for o, b in MACROS[part]]
    end = frame + length * len(parts)
    look = end + a.lookahead
    write_route(d / 'route.txt', [r for r in rows if r[0] <= frame] + macro)
    if a.commit_first:
        end = frame + length
        macro = [m for m in macro if m[0] <= end]
    st = d / ('s_%05d.bin' % end)
    cmd = [a.exe, a.rom, '--frames', str(look + 1), '--input', str(d / 'route.txt'), '--load-state', str(state),
           '--frame-log', str(d / 'fl.bin'), '--frame-log-frames', f'{frame}:{look}', '--save-state', f'{end}:{st}']
    for _ in range(3):
        p = subprocess.run(cmd, capture_output=True, text=True)
        log = list(frames(str(d / 'fl.bin'))) if p.returncode == 0 and (d / 'fl.bin').exists() else []
        if log and log[-1][0] == look and st.exists():
            break
    else:
        raise SystemExit(f'{name}: run failed: {p.stdout[-600:]} {p.stderr[-600:]}')
    (d / 'fl.bin').unlink()
    first = log[0][1]
    endram = [r for f, r, _, _ in log if f == look - a.lookahead][0]
    hp_loss, lv_loss = losses(log)

    def pos(r):
        y = r[0xF1] * 240 + r[0x640]
        if a.goal.startswith('min:'):
            return -300 * r[int(a.goal[4:], 16)]
        if a.goal.startswith('to:'):
            tx, ty = (int(v, 16) for v in a.goal[3:].split(','))
            return -abs(r[0xF3] * 256 + r[0xF2] + r[0x650] - tx) - abs(y - ty)
        if a.goal in ('right', 'left'):
            x = r[0xF3] * 256 + r[0xF2] + r[0x650]
            return (x if a.goal == 'right' else -x) - a.ypenalty * y
        return -y if a.goal == 'up' else y
    score = int(pos(endram) - pos(first)) - 1000 * hp_loss - 100000 * lv_loss
    return name, score, hp_loss + 10 * lv_loss, macro, st, end


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('route')
    ap.add_argument('state')
    ap.add_argument('--steps', type=int, default=20)
    ap.add_argument('--length', type=int, default=24, help='frames per macro')
    ap.add_argument('--lookahead', type=int, default=16,
                    help="frames the macro's last input is held on that must also be safe")
    ap.add_argument('--stall', type=int, default=8, help='stop after this many steps without progress')
    ap.add_argument('--goal', default='right',
                    help='right, left, up, down, to:X,Y (hex world coordinates, macros as for right) '
                         'or min:ADDR (drive a RAM byte down, e.g. a boss hit counter)')
    ap.add_argument('--macros', help='comma-separated subset of: ' + ','.join(MACROS))
    ap.add_argument('--prep', default='wait,back,climb,down',
                    help='macros tried before every other macro when no single macro makes safe progress')
    ap.add_argument('--ypenalty', type=float, default=0.0, help='for left/right goals, cost per pixel of descent')
    ap.add_argument('--backtrack', type=int, default=4)
    ap.add_argument('--depth2', action='store_true',
                    help='score every pair of macros and append only the first macro of the best pair')
    ap.add_argument('--out', help='route to write (default: overwrite ROUTE)')
    ap.add_argument('--states', default=str(ROOT / 'captures' / 'search' / 'states'))
    ap.add_argument('--prefix', default='st')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'headless' / 'Release' / 'ducktales.exe'))
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--add', action='append', default=[],
                    help='extra macro NAME=OFF:BUTTONS,OFF:BUTTONS (offsets in frames from the macro start)')
    a = ap.parse_args()
    a.commit_first = a.depth2
    for spec in a.add:
        n, body = spec.split('=')
        MACROS[n] = [(int(o), b) for o, b in (part.split(':') for part in body.split(','))]
    names = a.macros.split(',') if a.macros else list(MACROS)
    out = Path(a.out or a.route)
    states = Path(a.states)
    states.mkdir(parents=True, exist_ok=True)
    base = ROOT / 'captures' / 'search' / 'work'
    shutil.rmtree(base, ignore_errors=True)
    frame = int(Path(a.state).stem.split('_')[-1])
    rows = [r for r in read_route(a.route) if r[0] <= frame]
    stack = [{'rows': rows, 'state': Path(a.state), 'frame': frame, 'banned': set(), 'chosen': None}]
    backtracks = stalled = it = 0
    frontier = frame
    pool = cf.ThreadPoolExecutor(a.jobs)
    while len(stack) - 1 < a.steps and stalled < a.stall:
        node = stack[-1]
        shutil.rmtree(base / str(it - 1), ignore_errors=True)
        it += 1
        work = base / str(it)

        def trial(n):
            return run_one(a, work, node['rows'], node['state'], node['frame'], n, a.length)
        if a.depth2:
            cands = [f'{p}+{n}' for p in names if p not in node['banned'] for n in names] or \
                [f'{p}+{n}' for p in names for n in names]
            best = {}
            for r in pool.map(trial, cands):
                k = r[0].split('+')[0]
                if k not in best or r[1] > best[k][1]:
                    best[k] = r
            res = sorted(best.values(), key=lambda r: -r[1])
        else:
            res = list(pool.map(trial, [n for n in names if n not in node['banned']] or names))
            res.sort(key=lambda r: -r[1])
        if not a.depth2 and (res[0][1] <= 0 or res[0][2]):
            pairs = [f'{p}+{n}' for p in a.prep.split(',') for n in names if f'{p}+{n}' not in node['banned']]
            res += list(pool.map(trial, pairs))
            res.sort(key=lambda r: -r[1])
        name, score, loss, macro, st, end = res[0]
        if loss and backtracks < a.backtrack and len(stack) > 1:
            stack.pop()
            stack[-1]['banned'].add(stack[-1]['chosen'])
            backtracks += 1
            print(f'  frame {node["frame"]}: every macro costs HP, backing up', flush=True)
            continue
        if not loss and end > frontier:
            backtracks = 0
        frontier = max(frontier, end)
        stalled = stalled + 1 if score <= 5 else 0
        keep = states / f'{a.prefix}_{end:05d}.bin'
        shutil.copy(st, keep)
        node['chosen'] = name.split('+')[0] if a.depth2 else name
        stack.append({'rows': node['rows'] + macro, 'state': keep, 'frame': end, 'banned': set(), 'chosen': None})
        write_route(out, stack[-1]['rows'])
        print(f'{end:5d} {name:10s} score={score:6d} loss={loss} '
              + ' '.join(f'{n}:{s}' for n, s, *_ in res[1:4]), flush=True)
    shutil.rmtree(base, ignore_errors=True)
    print(f'route -> {out}, last state {stack[-1]["state"]}')


if __name__ == '__main__':
    main()
