"""Replay DuckTales' sprite builder (bank 5 $8000) from a frame-log record and compare its
output with the OAM shadow at $0200. Prints numbers only; nothing from the ROM is written out.

  object slot n      $0700+n bit 7 live, bit 5 world coordinates, bit 4 hidden;
                     $0600+n bit 6 h-flip, bit 2 behind background, bit 1 blink, bit 4 may cover the HUD
  animation          $0620+n type -> script ($B1C7/$B257): [0] last frame, [1] frame length,
                     [2+f] metasprite id; $0610+n frame, $0630+n frame timer
  metasprite         id -> $9681/$9779: [0] sprites - 1, [1] offset-table id (+1 when flipped),
                     then tile, attribute pairs; offset table id -> $ADE1/$AEC1 (minus 2): dy, dx pairs

    py -3 -I tools/render_oam.py ROM check LOG [--frame N] [--slots]
    py -3 -I tools/render_oam.py ROM budget LOG     OAM entries used per frame, slot order parity
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render_map import Rom, read_log  # noqa: E402

B = 5


def build(rom, ram):
    """Return (oam bytes from index 4, per-slot notes) the way bank 5 $8000 writes them."""
    rd = lambda a: rom.rd(a, B)
    oam, notes = [], []
    if (ram[0x7F] & 0x80 and not ram[0x6C] and not ram[0xAC] and not ram[0xE6]
            and ram[0x39] != 0x0F and ram[0x640] >= 0x44):
        for base in (0x340, 0x358, 0x140):
            i = base
            while ram[i] != 0xFF and len(oam) < 252:
                oam.append(ram[i])
                i += 1
    order = range(16) if ram[0x95] & 1 == 0 else range(15, -1, -1)
    for n in order:
        if not ram[0x700 + n] & 0x80 or ram[0x600 + n] == 0xFF:
            continue
        f600, f700 = ram[0x600 + n], ram[0x700 + n]
        if f700 & 0x20:
            x = (ram[0x720 + n] - ram[0xF2]) & 0xFF
            borrow = ram[0x720 + n] < ram[0xF2]
            if (ram[0x730 + n] - ram[0xF3] - borrow) & 0xFF:
                continue
            if f700 & 0x10:
                continue
        else:
            x = ram[0x720 + n]
        if f600 & 2 and not ram[0x95] & 2:
            notes.append((n, 'blink-off'))
            continue
        flip = f600 & 0x40
        typ = ram[0x620 + n]
        script = rd(0xB1C7 + typ) | rd(0xB257 + typ) << 8
        msid = rd(script + 2 + (ram[0x610 + n] & 0x7F))
        ms = rd(0x9681 + msid) | rd(0x9779 + msid) << 8
        cnt = rd(ms) + 1
        tid = (rd(ms + 1) + (1 if flip else 0)) & 0xFF
        off = ((rd(0xADE1 + tid) | rd(0xAEC1 + tid) << 8) - 2) & 0xFFFF
        y0, shown = ram[0x640 + n], 0
        for k in range(cnt):
            if len(oam) >= 252:
                break
            yi = 2 + 2 * k
            dy, dx = rd(off + yi), rd(off + yi + 1)
            y = y0 + dy
            if (dy < 0x80 and y > 0xFF) or (dy >= 0x80 and y <= 0xFF):
                continue
            y &= 0xFF
            if n and not f600 & 0x10 and y < 0x30:
                continue
            xx = x + dx
            if (dx < 0x80 and xx > 0xFF) or (dx >= 0x80 and xx <= 0xFF):
                continue
            oam += [y, rd(ms + yi), (rd(ms + yi + 1) ^ flip) | ((f600 & 4) << 3), xx & 0xFF]
            shown += 1
        notes.append((n, 'type %02X metasprite %02X: %d of %d sprites' % (typ, msid, shown, cnt)))
    return oam, notes


def cmd_check(rom, a):
    log = read_log(a.log, a.frame)
    ram = log['ram']
    oam, notes = build(rom, ram)
    shadow = ram[0x204:0x300]
    used = len(oam) // 4
    same = sum(oam[i] == shadow[i] for i in range(len(oam)))
    rest = sum(1 for i in range(len(oam), 252, 4) if shadow[i] >= 0xF0)
    print('frame %d: $95=%02X ($97=%02X) predicted %d entries after sprite 0; %d/%d bytes equal $0204+;'
          ' %d/%d remaining entries parked at Y>=$F0' % (log['frame'], ram[0x95], ram[0x97], used, same, len(oam),
                                                       rest, 63 - used))
    print('sprite 0 in shadow: Y=%02X tile=%02X attr=%02X X=%02X (NMI writes $FA=%02X,$21,$20,$08 when $EA)'
          % (tuple(ram[0x200:0x204]) + (ram[0xFA],)))
    if a.slots:
        for n, t in notes:
            print('  slot %2d: %s' % (n, t))


def cmd_budget(_rom, a):
    with open(a.log, 'rb') as f:
        f.read(12)
        while True:
            head = f.read(36)
            if len(head) < 36:
                break
            frame, _, _, *lens = struct.unpack('<III6I', head)
            f.read(32 + 256)
            ram = f.read(lens[0])
            f.seek(sum(lens[1:]), 1)
            used = sum(1 for i in range(4, 256, 4) if ram[0x200 + i] < 0xF0)
            print('frame %d: %2d/63 OAM entries visible, $97=%02X, slot order %s' % (
                frame, used, ram[0x97], '0->15' if ram[0x95] & 1 == 0 else '15->0'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('log')
    c.add_argument('--frame', type=int)
    c.add_argument('--slots', action='store_true')
    b = sub.add_parser('budget')
    b.add_argument('log')
    a = ap.parse_args()
    dict(check=cmd_check, budget=cmd_budget)[a.cmd](Rom(a.rom), a)


if __name__ == '__main__':
    main()
