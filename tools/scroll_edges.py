"""Look for scrolling garbage at the picture's edges (docs/PRESENTATION.md).

    build/headless/Release/ducktales.exe ROM --input ROUTE --frames N --frame-log captures/fl.bin --frame-log-frames A:B
    py -3 -I tools/scroll_edges.py captures/fl.bin [--first-row 56]

For each pair of frames the playfield (rows from --first-row, below the HUD)
is matched against the previous frame shifted by up to 8 pixels across and 4
down; when one shift explains all but 8% of it, the camera moved by that
much. Then every pixel that was already on screen in the previous frame should
be unchanged, so a mismatch is a sprite, an animation, or a tile or attribute
the game had not finished writing when it scrolled into view. Garbage at an
edge shows up as mismatches concentrated in the edge columns or rows, which
the report compares with the middle of the picture. Needs numpy.
"""

import argparse
import struct

import numpy as np


def frames(path):
    with open(path, 'rb') as f:
        if f.read(8) != b'CYCFRAME':
            raise SystemExit('not a frame log')
        (version,) = struct.unpack('<I', f.read(4))
        nlen = 6 if version >= 2 else 5
        while True:
            head = f.read(12 + 4 * nlen)
            if len(head) < 12 + 4 * nlen:
                return
            frame, _, _, *lens = struct.unpack('<III%dI' % nlen, head)
            f.read(32 + 256)
            blobs = [f.read(n) for n in lens]
            yield frame, np.frombuffer(blobs[4], '<u2').reshape(240, 256) & 0x3F


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--first-row', type=int, default=56)
    a = ap.parse_args()
    y_top = a.first_row
    col_m, col_n, row_m, row_n = np.zeros(256), np.zeros(256), np.zeros(240), np.zeros(240)
    worst, moved, prev = [], 0, None
    for frame, pic in frames(a.log):
        if prev is not None:
            best = (1.0, 0, 0)
            for sy in range(-4, 5):
                for sx in range(-8, 9):
                    y0, y1, x0, x1 = max(y_top, y_top - sy), min(240, 240 - sy), max(0, -sx), min(256, 256 - sx)
                    d = np.count_nonzero(pic[y0:y1, x0:x1] != prev[y0 + sy : y1 + sy, x0 + sx : x1 + sx]) / (
                        (y1 - y0) * (x1 - x0)
                    )
                    if d < best[0]:
                        best = (d, sy, sx)
            d, sy, sx = best
            if d < 0.08 and (sx or sy):
                moved += 1
                y0, y1, x0, x1 = max(y_top, y_top - sy), min(240, 240 - sy), max(0, -sx), min(256, 256 - sx)
                m = pic[y0:y1, x0:x1] != prev[y0 + sy : y1 + sy, x0 + sx : x1 + sx]
                col_m[x0:x1] += m.sum(0)
                col_n[x0:x1] += m.shape[0]
                row_m[y0:y1] += m.sum(1)
                row_n[y0:y1] += m.shape[1]
                edge = max(
                    m[:, :8].mean() if x0 == 0 else 0,
                    m[:, -8:].mean() if x1 == 256 else 0,
                    m[-8:, :].mean() if y1 == 240 else 0,
                )
                worst.append((round(float(edge), 3), frame, sx, sy))
        prev = pic
    cols, rows = col_m / np.maximum(col_n, 1), row_m / np.maximum(row_n, 1)
    print(f'{moved} frames where the camera moved')
    print(
        f'mismatch rate: middle columns {cols[16:240].mean():.4f}, left 8 {cols[:8].mean():.4f}, '
        f'right 8 {cols[248:].mean():.4f}; middle rows {rows[80:220].mean():.4f}, '
        f'first 8 playfield rows {rows[y_top:y_top + 8].mean():.4f}, bottom 8 {rows[232:].mean():.4f}'
    )
    print('frames with the most edge mismatch (rate in the worst 8-pixel edge, frame, dx, dy):')
    for w in sorted(worst, reverse=True)[:8]:
        print('  ', w)


if __name__ == '__main__':
    main()
