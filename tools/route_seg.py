"""Play one segment of a route headless and summarise it, for building routes segment by segment.

Runs the headless build on ROUTE (from power-on, or from a --state saved by an earlier run; input
lines before the state's frame are skipped by the host), captures a frame log of the segment and
prints one row every --every frames: screen row ($F1), camera X ($F3:$F2), Scrooge's screen X/Y ($0650/$0640),
action ($0620), HP dots left, extra lives ($0361) and money. Screenshots every --shots frames are
tiled into OUT/sheet.png.

    py -3 -I tools/route_seg.py ROUTE --to FRAME [--state FILE] [--save F:FILE ...] [--out DIR]
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from framelog import frames  # noqa: E402


def hp_dots(ram):
    full = 0
    for i in range(10):
        rec = 0x340 + 4 * i
        if ram[rec] == 0xFF:
            break
        if ram[rec + 2] == 0:
            full += 1
    return full


def money(ram):
    return ''.join(' ' if d == 0x24 else str(d) for d in ram[0x324:0x32A]).strip() or '0'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('route')
    ap.add_argument('--to', type=int, required=True, help='last frame to run')
    ap.add_argument('--state', help='start from this --save-state file')
    ap.add_argument('--save', action='append', default=[], help='F:FILE, save a state after frame F')
    ap.add_argument('--out', default=str(ROOT / 'captures' / 'seg'))
    ap.add_argument('--shots', type=int, default=30)
    ap.add_argument('--every', type=int, default=10)
    ap.add_argument('--cols', type=int, default=6)
    ap.add_argument('--no-log', action='store_true', help='skip the frame log and table')
    ap.add_argument('--objs', action='store_true', help='also list live object slots 1-15 (slot:type x y)')
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'headless' / 'Release' / 'ducktales.exe'))
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob('s_*.png'):
        old.unlink()
    start = 0
    cmd = [a.exe, a.rom, '--frames', str(a.to + 1), '--input', a.route,
           '--screenshot', str(out / 's.png'), '--shot-every', str(a.shots)]
    if a.state:
        cmd += ['--load-state', a.state]
        start = int(Path(a.state).stem.split('_')[-1]) if Path(a.state).stem.split('_')[-1].isdigit() else 0
    if not a.no_log:
        cmd += ['--frame-log', str(out / 'fl.bin'), '--frame-log-frames', f'{start}:{a.to}']
    for s in a.save:
        cmd += ['--save-state', s]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(p.stdout[-2000:] + p.stderr[-2000:])
    if not a.no_log:
        print('frame row  camX  sx sy act hp lv money')
        prev = None
        for f, ram, _, _ in frames(str(out / 'fl.bin')):
            row = (ram[0xF3] * 256 + ram[0xF2], ram[0x650], ram[0x640], ram[0x620], hp_dots(ram), ram[0x361], money(ram))
            if f % a.every == 0 or (prev and (row[4], row[5]) != (prev[4], prev[5])):
                objs = ' '.join(f'{n}:{ram[0x620 + n]:02X}@{ram[0x650 + n]:02X},{ram[0x640 + n]:02X}'
                                for n in range(1, 16) if ram[0x600 + n]) if a.objs else ''
                print(f'{f:5d} {ram[0xF1]:3X} {row[0]:5X}  {row[1]:02X} {row[2]:02X}  {row[3]:02X} {row[4]:2d} {row[5]:2d} {row[6]} {objs}')
            prev = row
        (out / 'fl.bin').unlink()
    shots = sorted(out.glob('s_*.png'))
    if shots:
        subprocess.run([sys.executable, '-I', str(ROOT / 'tools' / 'contact_sheet.py'), *map(str, shots),
                        '--out', str(out / 'sheet.png'), '--cols', str(a.cols)], check=True)


if __name__ == '__main__':
    main()
