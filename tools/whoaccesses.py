"""Report which instructions read or write given CPU addresses during one frame.

Runs the headless build with --trace-frame/--trace-out and scans the per-cycle trace
(runner/cyc/cyc_trace.h). The current PRG bank is followed from writes to $8000-$FFFF.

    py -3 -I tools/whoaccesses.py --input ROUTE --frame N ADDR[,ADDR...] [--reads]
"""

import argparse
import sys
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfile import default_rom  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('addrs')
    ap.add_argument('--frame', type=int, required=True)
    ap.add_argument('--input', required=True)
    ap.add_argument('--reads', action='store_true', help='also report reads')
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'headless' / 'Release' / 'ducktales.exe'))
    ap.add_argument('--rom', default=default_rom())
    a = ap.parse_args()
    want = {int(x, 16) for x in a.addrs.split(',')}
    with tempfile.TemporaryDirectory() as tmp:
        trace = Path(tmp) / 'trace.txt'
        subprocess.run(
            [
                a.exe,
                a.rom,
                '--frames',
                str(a.frame + 1),
                '--input',
                a.input,
                '--trace-frame',
                str(a.frame),
                '--trace-out',
                str(trace),
            ],
            check=True,
            capture_output=True,
        )
        insn, bank = '?', None
        for line in trace.read_text().splitlines():
            parts = line.split()
            if len(parts) < 3:
                continue
            if parts[1] == 'INSN':
                insn = ' '.join(parts[2:])
                continue
            if parts[1] not in ('R', 'W'):
                continue
            addr, value = int(parts[2], 16), parts[3]
            if parts[1] == 'W' and addr >= 0x8000:
                bank = int(value, 16)
            if addr in want and (parts[1] == 'W' or a.reads):
                print(
                    'cycle %s %s %04X=%s bank=%s  %s'
                    % (parts[0], parts[1], addr, value, '?' if bank is None else bank, insn)
                )


if __name__ == '__main__':
    main()
