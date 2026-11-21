"""Jump experiment check (src/game/ducktales_jump.c, docs/architecture.md).

Runs tests/routes/re_amazon_moves.txt on the headless build and measures the
first jump (A pressed at frame 1780) from Scrooge's Y ($0640) in a frame log:

  1. --jump-scale 100 must give the same per-frame hashes as no option (stock);
  2. --jump-scale PCT must leave every frame before the jump identical and
     change the apex;
  3. with a DUCKTALES_DEV_DIAGNOSTICS build (--dev-build), switching the
     experiment on and back off before the jump must leave every frame
     identical to stock, and switching it on just before the jump must give
     exactly the --jump-scale PCT run.

    py -3 -I tests/jump_experiment.py --build build/headless [--dev-build build/headless-dev] [--scale 125]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from framelog import frames  # noqa: E402

ROUTE = ROOT / 'tests' / 'routes' / 're_amazon_moves.txt'
FRAMES = 1900
LOG_FROM, LOG_TO = 1760, 1830
SCROOGE_Y = 0x0640


def run(exe, rom, tmp, name, extra):
    hashes, log = tmp / f'{name}.hash', tmp / f'{name}.bin'
    cmd = [str(exe), rom, '--frames', str(FRAMES), '--input', str(ROUTE), '--hash-out', str(hashes),
           '--frame-log', str(log), '--frame-log-frames', f'{LOG_FROM}:{LOG_TO}', *extra]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        sys.exit(f'{name}: exit {p.returncode}\n{p.stderr[-2000:]}')
    lines = [line for line in hashes.read_text().splitlines() if line[:1].isdigit()]
    ys = {f: ram[SCROOGE_Y] for f, ram, _, _ in frames(log)}
    return lines, ys


def first_jump(ys):
    """(take-off frame, apex frame, rise in pixels) of the first jump in the log."""
    fs = sorted(ys)
    start = next(f for f in fs[1:] if ys[f] < ys[f - 1]) - 1
    apex = start
    while apex + 1 in ys and ys[apex + 1] <= ys[apex]:
        apex += 1
    return start, apex, ys[start] - ys[apex]


def first_difference(a, b):
    for x, y in zip(a, b):
        if x != y:
            return int(x.split()[0])
    return None if len(a) == len(b) else min(len(a), len(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build', help='a -DDUCKTALES_DEV_DIAGNOSTICS=ON headless build')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--scale', type=int, default=125)
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    failed = []

    def check(ok, what):
        print(f'{"PASS" if ok else "FAIL"}  {what}')
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        stock, stock_y = run(exe, args.rom, tmp, 'stock', [])
        same, _ = run(exe, args.rom, tmp, 'same', ['--jump-scale', '100'])
        scaled, scaled_y = run(exe, args.rom, tmp, 'scaled', ['--jump-scale', str(args.scale)])
        s0, s1, s_rise = first_jump(stock_y)
        x0, x1, x_rise = first_jump(scaled_y)
        print(f'stock      : take-off frame {s0}, Y {stock_y[s0]} -> apex {stock_y[s1]} at frame {s1}, rise {s_rise} px')
        print(f'scale {args.scale:3d}% : take-off frame {x0}, Y {scaled_y[x0]} -> apex {scaled_y[x1]} at frame {x1}, '
              f'rise {x_rise} px')
        check(stock == same, '--jump-scale 100 matches stock on every frame')
        diverge = first_difference(stock, scaled)
        check(diverge == s0 + 1, f'--jump-scale {args.scale} identical before the jump, first difference at frame '
              f'{diverge} (jump frame {s0 + 1})')
        check((x_rise > s_rise) == (args.scale > 100) and x_rise != s_rise,
              f'apex rise changed: {s_rise} -> {x_rise} px')

        if args.dev_build:
            dev = ROOT / args.dev_build / args.config / 'ducktales.exe'
            off, _ = run(dev, args.rom, tmp, 'onoff', ['--dt-jump-at', f'0:{args.scale}', '--dt-jump-at', f'{s0}:100'])
            check(off == stock, f'on at frame 0, off at frame {s0}: every frame matches stock')
            late, late_y = run(dev, args.rom, tmp, 'late', ['--dt-jump-at', f'{s0}:{args.scale}'])
            check(late == scaled, f'on at frame {s0}: every frame matches --jump-scale {args.scale}')
    if failed:
        sys.exit('failed: ' + '; '.join(failed))


if __name__ == '__main__':
    main()
