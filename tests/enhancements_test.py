"""Enhancement checks (src/game/enhancements.c, docs/architecture.md "Enhancements").

For every enhancement, on the headless build:
  1. with the option at its stock value the run is identical to no option
     (per-frame hashes: trace, RAM, picture, registers, cycles, hardware), so
     "off" is the stock code path;
  2. with the option on, the effect shows in RAM at frames where the stock run
     differs (read from short --frame-log windows).
With a DUCKTALES_DEV_DIAGNOSTICS build (--dev-build) also:
  3. switching the enhancement on at frame 0 and off before it first acts
     leaves every frame identical to stock (it can be toggled at run time);
  4. the menu's Reset (dt_soft_reset, --dt-reset-at) returns to the title,
     and a new game after it starts with the --start-lives count.

    py -3 -I tests/enhancements_test.py --build build/headless [--dev-build build/headless-dev]

Frames come from tests/routes/moon_intro.txt (lives set at frame 44 by the
title set-up, first HP dot lost at frame 1004, first life lost at 1592) and
amazon_intro.txt (the HUD timer runs from about frame 900).
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from framelog import frames  # noqa: E402

ROUTES = ROOT / 'tests' / 'routes'
LIVES, TIME, MUSIC = 0x0361, (0x0151, 0x0155, 0x0159), 0x00C5


def hp(ram):
    """Full HP dots: HUD records at $0340+4i up to Y = $FF, attribute bit 0 clear."""
    n = 0
    for i in range(10):
        base = 0x0340 + 4 * i
        if ram[base] == 0xFF:
            break
        n += not (ram[base + 2] & 1)
    return n


def timer(ram):
    return tuple(ram[a] for a in TIME)


class Runner:
    def __init__(self, exe, rom, tmp):
        self.exe, self.rom, self.tmp, self.n = exe, rom, tmp, 0

    def run(self, route, nframes, extra=(), log=None):
        """Per-frame hash lines, and {frame: ram} for the log window (first, last)."""
        self.n += 1
        hashes, out = self.tmp / f'{self.n}.hash', self.tmp / f'{self.n}.bin'
        cmd = [str(self.exe), self.rom, '--frames', str(nframes), '--input', str(ROUTES / route),
               '--hash-out', str(hashes), *extra]
        if log:
            cmd += ['--frame-log', str(out), '--frame-log-frames', f'{log[0]}:{log[1]}']
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode:
            sys.exit(f'{" ".join(cmd)}: exit {p.returncode}\n{p.stderr[-2000:]}')
        lines = [line for line in hashes.read_text().splitlines() if line[:1].isdigit()]
        rams = {f: ram for f, ram, _, _ in frames(out)} if log else {}
        hashes.unlink()
        if log:
            out.unlink()
        return lines, rams


def first_difference(a, b):
    for x, y in zip(a, b):
        if x != y:
            return int(x.split()[0])
    return None if len(a) == len(b) else min(len(a), len(b))


# option, stock text, on text, config key, route, frames, log window, effect(stock_ram, on_ram) -> (ok, text),
# frame before which the enhancement has not acted yet (for the dev-build toggle check)
CASES = [
    ('--no-damage', 'off', 'on', 'NoDamage', 'moon_intro.txt', 1301, (1000, 1300),
     lambda s, o: (hp(s[1010]) == 2 and hp(s[1300]) == 1 and hp(o[1010]) == 3 and hp(o[1300]) == 3,
                   f'HP at 1010/1300: stock {hp(s[1010])}/{hp(s[1300])}, on {hp(o[1010])}/{hp(o[1300])}'),
     1000),
    ('--infinite-lives', 'off', 'on', 'InfiniteLives', 'moon_intro.txt', 1601, (1590, 1600),
     lambda s, o: (s[1600][LIVES] == 1 and o[1600][LIVES] == 2,
                   f'lives at 1600: stock {s[1600][LIVES]}, on {o[1600][LIVES]}'),
     1500),
    ('--infinite-time', 'off', 'on', 'InfiniteTime', 'amazon_intro.txt', 3001, (2990, 3000),
     lambda s, o: (timer(s[3000]) != (5, 0, 0) and timer(o[3000]) == (5, 0, 0) and timer(o[2990]) == (5, 0, 0),
                   f'HUD time at 3000: stock {timer(s[3000])}, on {timer(o[3000])}'),
     600),
    ('--start-lives', 'off', '7', 'StartLives', 'moon_intro.txt', 1601, (100, 100),
     lambda s, o: (s[100][LIVES] == 2 and o[100][LIVES] == 7,
                   f'lives at the title (frame 100): stock {s[100][LIVES]}, on {o[100][LIVES]}'),
     30),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build', help='a -DDUCKTALES_DEV_DIAGNOSTICS=ON headless build')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    dev = ROOT / args.dev_build / args.config / 'ducktales.exe' if args.dev_build else None
    failed = []

    def check(ok, what):
        print(f'{"PASS" if ok else "FAIL"}  {what}')
        if not ok:
            failed.append(what)

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        r = Runner(exe, args.rom, tmp)
        for opt, stock_v, on_v, key, route, n, window, effect, quiet_until in CASES:
            stock, s_ram = r.run(route, n, log=window)
            off, _ = r.run(route, n, [opt, stock_v])
            on, o_ram = r.run(route, n, [opt, on_v], log=window)
            check(off == stock, f'{opt} {stock_v}: identical to stock on all {len(stock)} frames of {route}')
            ok, text = effect(s_ram, o_ram)
            first = first_difference(stock, on)
            check(ok, f'{opt} {on_v}: {text} (first differing frame {first})')
            if dev:
                d = Runner(dev, args.rom, tmp)
                toggled, _ = d.run(route, n, ['--dt-enh-at', f'0:{key}={on_v}', '--dt-enh-at', f'{quiet_until}:{key}={stock_v}'])
                check(toggled == stock, f'{key} on at frame 0, off at {quiet_until}: identical to stock')
        # also the start-lives count at the first title: frame 100
        _, s_ram = r.run('moon_intro.txt', 101, log=(100, 100))
        _, o_ram = r.run('moon_intro.txt', 101, ['--start-lives', '0'], log=(100, 100))
        check(s_ram[100][LIVES] == 2 and o_ram[100][LIVES] == 0,
              f'--start-lives 0: lives at frame 100 stock {s_ram[100][LIVES]}, on {o_ram[100][LIVES]}')
        if dev:
            d = Runner(dev, args.rom, tmp)
            _, s_ram = d.run('amazon_intro.txt', 1301, log=(1300, 1300))
            _, z_ram = d.run('amazon_intro.txt', 1301, ['--dt-reset-at', '1200'], log=(1300, 1300))
            check(s_ram[1300][MUSIC] == 0 and z_ram[1300][MUSIC] == 7 and z_ram[1300][LIVES] == 2,
                  f'reset at 1200: at 1300 music request stock {s_ram[1300][MUSIC]}, after reset {z_ram[1300][MUSIC]} '
                  f'(7 = title), lives {z_ram[1300][LIVES]}')
            _, n_ram = d.run('amazon_intro.txt', 1301, ['--dt-reset-at', '1200', '--start-lives', '7'], log=(1300, 1300))
            check(n_ram[1300][LIVES] == 7, f'--start-lives 7 after a reset at 1200: lives {n_ram[1300][LIVES]} at 1300')
    print('all passed' if not failed else f'{len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
