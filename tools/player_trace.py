"""Print Scrooge's movement variables per frame from a NESRecomp --frame-log file.

Columns (see docs/PLAYER.md): held buttons $16, player mode $39, action $0620, Y $0640 with its
fraction $0660, vertical velocity $07C0:$07B0 (signed 8.8, positive = up), screen X $0650, and any
extra addresses given with --extra. --summary reports the highest point (smallest Y) and the
frames spent in the air for each airborne stretch.

    py -3 -I tools/player_trace.py LOG [--first N --last N] [--extra ADDR,...] [--summary]
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framelog import frames  # noqa: E402


def vel(ram):
    v = ram[0x7C0] << 8 | ram[0x7B0]
    return v - 0x10000 if v & 0x8000 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--last', type=int, default=1 << 30)
    ap.add_argument('--extra', default='')
    ap.add_argument('--summary', action='store_true')
    a = ap.parse_args()
    extra = [int(x, 16) for x in a.extra.split(',') if x]
    print('frame  $16 $39 $620  Y.frac   vel(8.8)     X  ' + ' '.join('%04X' % e for e in extra))
    air, start, top = False, None, None
    for f, ram, _, _ in frames(a.log):
        if f < a.first or f > a.last:
            continue
        v = vel(ram)
        print(
            '%5d  %02X  %02X  %02X   %02X.%02X  %+6.3f %04X  %02X  %s'
            % (
                f,
                ram[0x16],
                ram[0x39],
                ram[0x620],
                ram[0x640],
                ram[0x660],
                v / 256,
                v & 0xFFFF,
                ram[0x650],
                ' '.join('%02X  ' % ram[e] for e in extra),
            )
        )
        if not a.summary:
            continue
        if ram[0x39] != 0 and not air:
            air, start, top = True, f, ram[0x640]
        elif air and ram[0x39] != 0:
            top = min(top, ram[0x640])
        elif air:
            print('# airborne %d-%d (%d frames), highest Y $%02X' % (start, f - 1, f - start, top))
            air = False
    if a.summary and air:
        print('# airborne from %d, still in the air at the end of the log, highest Y $%02X' % (start, top))


if __name__ == '__main__':
    main()
