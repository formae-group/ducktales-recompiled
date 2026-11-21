"""Tile headless screenshots (shot_NNNNN.png) into one labelled sheet for review."""
import argparse
import re
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('shots', nargs='+', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--cols', type=int, default=4)
    ap.add_argument('--scale', type=int, default=1)
    args = ap.parse_args()
    shots = sorted(args.shots)
    w, h = 256 * args.scale, 240 * args.scale
    rows = (len(shots) + args.cols - 1) // args.cols
    sheet = Image.new('RGB', (w * args.cols, (h + 12) * rows), (32, 32, 32))
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(shots):
        x, y = (i % args.cols) * w, (i // args.cols) * (h + 12)
        sheet.paste(Image.open(path).convert('RGB').resize((w, h), Image.NEAREST), (x, y + 12))
        m = re.search(r'_(\d+)\.png$', path.name)
        draw.text((x + 2, y), m.group(1) if m else path.stem, fill=(255, 255, 0))
    sheet.save(args.out)


if __name__ == '__main__':
    main()
