"""Summarise a window run's frame pacing (docs/presentation.md).

    NESRECOMP_PACING_LOG=captures/p.csv CYC_PACING_LOG=captures/p.txt build/windows/Release/ducktales.exe ROM ...
    py -3 -I tools/pacing_report.py captures/p [--skip 120]

p.csv (NESRECOMP_PACING_LOG): per loop, the frame, the counter at the loop's end
and when the last picture was handed to the display (shown). p.txt
(CYC_PACING_LOG, optional): per picture, the work before it and how long
SDL_RenderPresent took.

Reports the intervals between pictures, presents the host skipped, and a
model of what a display without variable refresh shows: each picture appears
at the first refresh after it was handed over, so at 144 Hz a steady 60.1 Hz
source gives 2 or 3 refreshes per picture and at 60 Hz exactly 1; any other
count is an irregular frame (a visible hitch). The model is the average over
16 refresh phases. A VSync present that waits in the swap chain's queue
(a 60 Hz display with VSync on) is shown one refresh per picture whatever the
model says.
"""

import argparse
import math
import re
import statistics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stem', help='the logs without .csv / .txt')
    ap.add_argument('--skip', type=int, default=120, help='loops to leave out at the start')
    a = ap.parse_args()
    lines = open(a.stem + '.csv').read().split()
    head = lines[0].split(',')
    freq, period = int(head[1]), float(head[3]) * 1000
    rows = [tuple(map(int, line.split(','))) for line in lines[1:]][a.skip :]
    shown = [r[2] for r in rows]
    iv = [(b - s) * 1000 / freq for s, b in zip(shown, shown[1:])]
    skipped = sum(1 for x in iv if x == 0)
    iv = [x for x in iv if x > 0]
    print(f'{len(rows)} loops, period {period:.4f} ms, presents skipped {skipped}')
    print(
        f'picture interval mean {statistics.mean(iv):.3f} sd {statistics.pstdev(iv):.3f} '
        f'min {min(iv):.2f} max {max(iv):.2f} ms; >20 ms {sum(1 for x in iv if x > 20)}, <13 ms {sum(1 for x in iv if x < 13)}'
    )
    t = []
    for s in shown:
        if not t or s != t[-1]:
            t.append(s)
    t = [s / freq for s in t]
    for hz in (144.0, 60.0):
        bad = n = 0
        for k in range(16):
            v = [math.ceil((x - k / hz / 16) * hz) for x in t]
            d = [q - p for p, q in zip(v, v[1:])]
            bad += sum(1 for x in d if x not in ({1} if hz < 70 else {2, 3}))
            n += len(d)
        print(f'model {hz:.0f} Hz: irregular pictures {100 * bad / n:.1f}%')
    try:
        work, present = [], []
        for line in open(a.stem + '.txt'):
            m = re.search(r'work ([-\d.]+) present ([-\d.]+)', line)
            if m:
                work.append(float(m.group(1)))
                present.append(float(m.group(2)))
        work, present = work[a.skip :], present[a.skip :]
        print(
            f'work before the present mean {statistics.mean(work):.2f} max {max(work):.2f} ms; '
            f'SDL_RenderPresent mean {statistics.mean(present):.2f} max {max(present):.2f} ms'
        )
    except (OSError, statistics.StatisticsError):
        pass


if __name__ == '__main__':
    main()
