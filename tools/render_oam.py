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
    py -3 -I tools/render_oam.py ROM limits LOG     what the console's sprite limits cost over a log:
                                                   lines over 8 sprites, entries past 63, HUD left out
                                                   for Scrooge's Y, overlapping objects (by land/row/page)
"""

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render_map import Rom, read_log  # noqa: E402

B = 5


HUD_GROUPS = (0x340, 0x358, 0x140)


def hud_allowed(ram):
    """The builder's HUD conditions apart from Scrooge's Y ($7F bit 7, no message, ending, pause, $39 != $0F)."""
    return bool(ram[0x7F] & 0x80 and not ram[0x6C] and not ram[0xAC] and not ram[0xE6] and ram[0x39] != 0x0F)


def hud_in_oam(ram):
    """Whether the builder copies the HUD groups: the conditions above and Scrooge's Y >= $44."""
    return hud_allowed(ram) and ram[0x640] >= 0x44


def hud_records(ram):
    """The three HUD groups' 4-byte records, $FF ends a group."""
    out = []
    for base in HUD_GROUPS:
        i = base
        while ram[i] != 0xFF and i < base + 0x40:
            out.append(tuple(ram[i : i + 4]))
            i += 4
    return out


def slot_sprites(rom, ram, n):
    """(sprites, note) for object slot n as bank 5 $8000 writes them; sprites is None when it draws nothing."""
    rd = lambda a: rom.rd(a, B)
    if not ram[0x700 + n] & 0x80 or ram[0x600 + n] == 0xFF:
        return None, None
    f600, f700 = ram[0x600 + n], ram[0x700 + n]
    if f700 & 0x20:
        x = (ram[0x720 + n] - ram[0xF2]) & 0xFF
        borrow = ram[0x720 + n] < ram[0xF2]
        if (ram[0x730 + n] - ram[0xF3] - borrow) & 0xFF:
            return None, None
        if f700 & 0x10:
            return None, None
    else:
        x = ram[0x720 + n]
    if f600 & 2 and not ram[0x95] & 2:
        return None, 'blink-off'
    flip = f600 & 0x40
    typ = ram[0x620 + n]
    script = rd(0xB1C7 + typ) | rd(0xB257 + typ) << 8
    msid = rd(script + 2 + (ram[0x610 + n] & 0x7F))
    ms = rd(0x9681 + msid) | rd(0x9779 + msid) << 8
    cnt = rd(ms) + 1
    tid = (rd(ms + 1) + (1 if flip else 0)) & 0xFF
    off = ((rd(0xADE1 + tid) | rd(0xAEC1 + tid) << 8) - 2) & 0xFFFF
    y0, out = ram[0x640 + n], []
    for k in range(cnt):
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
        out.append((y, rd(ms + yi), (rd(ms + yi + 1) ^ flip) | ((f600 & 4) << 3), xx & 0xFF))
    return out, 'type %02X metasprite %02X: %d of %d sprites' % (typ, msid, len(out), cnt)


def slot_order(ram):
    return range(16) if ram[0x95] & 1 == 0 else range(15, -1, -1)


def build(rom, ram, cap=True):
    """Return (oam bytes from index 4, per-slot notes) the way bank 5 $8000 writes them; cap=False
    keeps going past the 63 entries."""
    limit = 252 if cap else 1 << 30
    oam, notes = [], []
    if hud_in_oam(ram):
        for rec in hud_records(ram):
            if len(oam) < limit:
                oam += rec
    for n in slot_order(ram):
        sprites, note = slot_sprites(rom, ram, n)
        if note:
            notes.append((n, note))
        for s in sprites or ():
            if len(oam) >= limit:
                break
            oam += s
    return oam, notes


def cmd_check(rom, a):
    log = read_log(a.log, a.frame)
    ram = log['ram']
    oam, notes = build(rom, ram)
    shadow = ram[0x204:0x300]
    used = len(oam) // 4
    same = sum(oam[i] == shadow[i] for i in range(len(oam)))
    rest = sum(1 for i in range(len(oam), 252, 4) if shadow[i] >= 0xF0)
    print(
        'frame %d: $95=%02X ($97=%02X) predicted %d entries after sprite 0; %d/%d bytes equal $0204+;'
        ' %d/%d remaining entries parked at Y>=$F0'
        % (log['frame'], ram[0x95], ram[0x97], used, same, len(oam), rest, 63 - used)
    )
    print(
        'sprite 0 in shadow: Y=%02X tile=%02X attr=%02X X=%02X (NMI writes $FA=%02X,$21,$20,$08 when $EA)'
        % (tuple(ram[0x200:0x204]) + (ram[0xFA],))
    )
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
            print(
                'frame %d: %2d/63 OAM entries visible, $97=%02X, slot order %s'
                % (frame, used, ram[0x97], '0->15' if ram[0x95] & 1 == 0 else '15->0')
            )


def log_records(path, chr_ram=False):
    """(frame, ram, oam[, chr]) per frame-log record, without the picture."""
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
            f.read(32)
            oam = f.read(256)
            ram = f.read(lens[0])
            if not chr_ram:
                f.seek(sum(lens[1:]), 1)
                yield frame, ram, oam
                continue
            f.seek(lens[1] + lens[2], 1)
            chr_bytes = f.read(lens[3])
            f.seek(sum(lens[4:]), 1)
            yield frame, ram, oam, chr_bytes


LANDS = ('Amazon', 'Transylvania', 'African Mines', 'Himalayas', 'Moon', 'Land Select')


def cmd_limits(rom, a):
    """Each frame against the RAM saved at the end of the frame before (the state its OAM was built from)."""
    st, over8, over63, prev = Counter(), Counter(), Counter(), None
    worst_line = worst_want = 0
    for frame, ram, oam in log_records(a.log):
        pram, prev = prev, ram
        if pram is None or ram[0xF6] > 5 or frame < 10:
            continue
        st['frames'] += 1
        place = '%s row %d page %d' % (LANDS[ram[0xF6]], ram[0xF1], ram[0xF3])
        h = 16 if pram[0xFF] & 0x20 else 8
        lines = [0] * 240
        for i in range(0, 256, 4):
            if oam[i] < 0xEF:
                for y in range(oam[i] + 1, min(oam[i] + 1 + h, 240)):
                    lines[y] += 1
        crowded = [c for c in lines if c > 8]
        if crowded:
            st['frames with a line over 8'] += 1
            st['lines over 8'] += len(crowded)
            st['sprite-lines dropped'] += sum(c - 8 for c in crowded)
            over8[place] += 1
            worst_line = max(worst_line, max(crowded))
        groups = [('hud', hud_records(pram))] if hud_allowed(pram) else []
        groups += [(n, s) for n in slot_order(pram) for s in [slot_sprites(rom, pram, n)[0]] if s]
        want = sum(len(s) for _, s in groups)
        worst_want = max(worst_want, want)
        if want > 63:
            st['frames wanting more than 63'] += 1
            st['entries cut'] += want - 63
            over63[place] += 1
        if hud_allowed(pram) and not hud_in_oam(pram):
            st["frames without HUD for Scrooge's Y"] += 1
        rects = [(g, s[0], s[3]) for g, ss in groups for s in ss if s[0] < 0xEF]
        if any(
            p[0] != q[0] and abs(p[1] - q[1]) < h and abs(p[2] - q[2]) < 8
            for i, p in enumerate(rects)
            for q in rects[i + 1 :]
        ):
            st['frames with overlapping objects'] += 1
    for k, v in st.items():
        print('%s: %d' % (k, v))
    print('most sprites on a line: %d; most entries wanted: %d' % (worst_line, worst_want))
    print('lines over 8 by place:', ', '.join('%s %d' % kv for kv in over8.most_common(8)))
    print('over 63 by place:', ', '.join('%s %d' % kv for kv in over63.most_common(8)))


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
    lim = sub.add_parser('limits')
    lim.add_argument('log')
    a = ap.parse_args()
    dict(check=cmd_check, budget=cmd_budget, limits=cmd_limits)[a.cmd](Rom(a.rom), a)


if __name__ == '__main__':
    main()
