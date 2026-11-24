"""The decompiled routines on the native runtime (src/decomp/decomp_native.c, docs/DECOMPILATION.md).

For each route this runs ducktales_native twice with No slowdown (--lag none), the only lag mode
the decompiled code runs in there:

  off   the recompiled program alone: records the buttons latched at each controller strobe, RAM
        each time the task yields (one logic step) and which RAM bytes only the NMI handler uses
  on    --decomp replace (every decompiled routine runs alone in place of the recompiled one),
        fed the off run's buttons strobe by strobe, so both runs see the same input on every
        logic step (the C's cycles are not the original's, so VBlank falls elsewhere in a step
        and a route's per-frame input would go somewhere else); RAM per step, and the routines'
        call counts

and compares RAM after every logic step of the off run: it must be equal except the stack, bytes
only the NMI handler uses, the NMI-facing bytes tests/lag_modes.py allows (ALLOWED) and the NMI's
own counters in NMI_STATE. The C costs no CPU cycles, so a land load behind a blank screen ends in
fewer frames and the NMI runs a different number of times before a logic step; what it counts
differs, what the task computes does not. For the same reason the VBlank NMI can fall between
task_yield's stores and the scheduler's pass where the step is taken, and count the yield's wait out
before it: the yielding slot (in $91) is then (state 4, wait 0) on where it is (1, 1) off. Those two
bytes are excepted at that step only, the slot must be equal on the next step, and the steps where
it happened are counted ("NMI-counted yields"). It also fails when the on run calls no decompiled
routine.

    py -3 -I tests/native_decomp_test.py --build build/headless [--routes R ...] [--jobs 2]
"""

import argparse
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import lag_modes  # noqa: E402
from romfile import default_rom  # noqa: E402

NMI_STATE = {
    0x92: 'frame counter, incremented by every NMI',
    **{a: 'sound driver zero page (bank 3, run by the NMI; docs/AUDIO.md "Driver RAM")' for a in range(0xD0, 0xE6)},
}
TASK_SLOTS = 0x80
TASK_RUNNING = 0x91
YIELDED = (1, 1)  # task_yield's state and one-frame wait
COUNTED = (4, 0)  # the same slot after the NMI counted the wait out
CALLS = re.compile(
    r'^decomp: mode=replace routine=\S+ addr=\$[0-9A-F]{4} calls=(\d+) compared=\d+ original=(\d+)$', re.M
)


def run(native, rom, route, out):
    s = out / route.stem
    base = [str(native), rom, '--frames', str(lag_modes.route_frames(route)), '--lag', 'none']
    off = base + ['--input', str(route), '--latch-log', f'{s}.latch', '--step-dump', f'{s}.off.steps']
    subprocess.run(off + ['--nmi-audit', f'{s}.audit'], capture_output=True, check=True)
    on = base + ['--latch-input', f'{s}.latch', '--decomp', 'replace', '--step-dump', f'{s}.on.steps']
    p = subprocess.run(on, capture_output=True, text=True)
    return route, p


def at(items):
    return ' '.join(f'${a:03X}@{k}' for a, k in sorted(items, key=lambda t: t[1]))


def report(route, p, out):
    s = out / route.stem
    if p.returncode:
        print(f'== {route.stem}: the decompiled run exited {p.returncode}\n{p.stderr[-2000:]}')
        return False
    calls = [tuple(map(int, m)) for m in CALLS.findall(p.stdout)]
    total, original = sum(c for c, _ in calls), sum(o for _, o in calls)
    private = set()
    for line in Path(f'{s}.audit').read_text().splitlines():
        addr, _, reads = line.split()
        if reads == 'task_reads=0':
            private.add(int(addr, 16))
    keep = 0
    for a in range(0x800):
        if not (0x100 <= a < 0x200) and a not in private:
            keep |= 0xFF << (8 * (0x7FF - a))
    off, on = lag_modes.read_steps(Path(f'{s}.off.steps')), lag_modes.read_steps(Path(f'{s}.on.steps'))
    n = min(len(off), len(on))
    first = {}
    counted, unequal_after = 0, []
    for k in range(n):
        x = (int.from_bytes(off[k], 'big') ^ int.from_bytes(on[k], 'big')) & keep
        slot = TASK_SLOTS + 4 * (off[k][TASK_RUNNING] & 3)
        if tuple(off[k][slot : slot + 2]) == YIELDED and tuple(on[k][slot : slot + 2]) == COUNTED:
            counted += 1
            x &= ~(0xFFFF << (8 * (0x7FF - slot - 1)))
            if k + 1 >= n or off[k + 1][slot : slot + 4] != on[k + 1][slot : slot + 4]:
                unequal_after.append(k)
        while x:
            byte = (x.bit_length() - 1) // 8
            first.setdefault(0x7FF - byte, k)
            x &= ~(0xFF << (8 * byte))
    bad = {a: k for a, k in first.items() if a not in lag_modes.ALLOWED and a not in NMI_STATE}
    print(
        f'== {route.stem}: {n} logic steps compared ({len(off)} off, {len(on)} on); decompiled calls {total:,}, '
        f'given to the original {original:,}; {len(private)} NMI-only bytes and the stack ignored; '
        f'NMI-counted yields {counted}'
    )
    if first:
        print('    differ (first step): ' + at(first.items()))
    if bad:
        print('    NOT ALLOWED: ' + at(bad.items()))
    if unequal_after:
        print('    NMI-counted yield with its slot unequal on the next step: ' + ' '.join(map(str, unequal_after)))
    if n < len(off):
        print(f'    the decompiled run ended {len(off) - n} logic steps early')
    return not bad and not unequal_after and total > 0 and n == len(off) > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'native-decomp'))
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    native = ROOT / args.build / args.config / 'ducktales_native.exe'
    for p in (native, Path(args.rom)):
        if not p.exists():
            sys.exit(f'missing {p} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_NATIVE=ON)')
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(lambda r: run(native, args.rom, r, out), routes))
    failed = [r.stem for r, p in done if not report(r, p, out)]
    print('failed: ' + ', '.join(failed) if failed else f'all {len(done)} routes: logic steps equal')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
