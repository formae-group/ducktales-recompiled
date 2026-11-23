"""Print each map's screen grid from the user's ROM (local investigation output only).

The fixed bank holds, per map index (0-4 = the five lands in Land Select order,
5-9 = boss arenas and scenes), the row count, the row stride and a pointer to a
byte grid of rows x stride screen ids, $FF meaning no screen there. The loader
is the routine at $C7B0, which copies them to $2A, $2B and $20/$21 (see
docs/levels.md). Screen index = row * stride + column, the value kept in $2D.

    py -3 -I tools/world_maps.py ROM [--ids]

Without --ids each cell prints as # (screen) or . (none); --ids prints the
screen numbers instead. Output derives from the ROM and is not committed.
"""

import argparse
from pathlib import Path

ROWS, STRIDE, PTR_LO, PTR_HI = 0xCE57, 0xCE61, 0xCF7F, 0xCF89
NAMES = [
    'The Amazon',
    'Transylvania',
    'African Mines',
    'The Himalayas',
    'The Moon',
    'map 5',
    'map 6',
    'map 7',
    'map 8',
    'map 9',
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom', type=Path)
    ap.add_argument('--ids', action='store_true')
    a = ap.parse_args()
    data = a.rom.read_bytes()
    prg = data[16 : 16 + data[4] * 0x4000]
    fixed = prg[-0x4000:]

    def rd(addr):
        return fixed[addr - 0xC000]

    for m, name in enumerate(NAMES):
        rows, stride = rd(ROWS + m), rd(STRIDE + m)
        ptr = rd(PTR_LO + m) | rd(PTR_HI + m) << 8
        print('map %d %s: %d rows x %d columns at $%04X' % (m, name, rows, stride, ptr))
        print('      ' + ' '.join('%2d' % c for c in range(stride)))
        for r in range(rows):
            cells = [rd(ptr + r * stride + c) for c in range(stride)]
            if a.ids:
                txt = ' '.join(' .' if v == 0xFF else '%02X' % v for v in cells)
            else:
                txt = ' '.join(' .' if v == 0xFF else ' #' for v in cells)
            print('  r%-2d %s' % (r, txt))


if __name__ == '__main__':
    main()
