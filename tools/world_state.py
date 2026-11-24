"""Decode the game-state bytes documented in docs/GAME-STATE.md from a --frame-log.

    py -3 -I tools/world_state.py LOG [--first N --last N --every K] [--changes]

One line per selected frame: land, map row and screen, camera X, Scrooge's world
position, HUD time, money, total, lives, treasure and item flags, and the
transition/exit requests. --changes prints a line only when one of the decoded
fields (other than camera, position and time) changes.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framelog import frames  # noqa: E402

LANDS = ['Amazon', 'Transylvania', 'Mines', 'Himalayas', 'Moon']


def digits(ram, lo, n):
    s = ''.join(' ' if ram[a] == 0x24 else str(ram[a]) if ram[a] < 10 else '?' for a in range(lo, lo + n))
    return (s.strip() or '') + '0'


def decode(ram):
    land = ram[0xF6]
    return {
        'land': LANDS[land] if land < 5 else 'id%d' % land,
        'row': ram[0xF1],
        'screen': '%02X' % ram[0x2D],
        'cam': '%04X' % (ram[0xF3] << 8 | ram[0xF2]),
        'pos': '%04X,%02X' % (ram[0x730] << 8 | ram[0x720], ram[0x640]),
        'time': ''.join(str(ram[a]) if ram[a] < 10 else '?' for a in (0x151, 0x155, 0x159)),
        'money': digits(ram, 0x324, 6),
        'total': digits(ram, 0x334, 7),
        'lives': ram[0x361],
        'treasure': '%02X' % ram[0xA0],
        'items': '%02X' % ram[0xA3],
        'boss': 'Y' if ram[0xFB] & 0x80 else '-',
        'chk': ram[0x7C],
        'req': '7E=%02X A4=%02X 78=%02X 5D=%02X AC=%02X' % (ram[0x7E], ram[0xA4], ram[0x78], ram[0x5D], ram[0xAC]),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--last', type=int, default=1 << 30)
    ap.add_argument('--every', type=int, default=60)
    ap.add_argument('--changes', action='store_true')
    a = ap.parse_args()
    prev = None
    for frame, ram, _, _ in frames(a.log):
        if not a.first <= frame <= a.last:
            continue
        d = decode(ram)
        if a.changes:
            key = {k: v for k, v in d.items() if k not in ('cam', 'pos', 'time')}
            if key == prev:
                continue
            prev = key
        elif (frame - a.first) % a.every:
            continue
        print('%6d ' % frame + ' '.join('%s=%s' % kv for kv in d.items()))


if __name__ == '__main__':
    main()
