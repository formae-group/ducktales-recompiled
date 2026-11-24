"""Inspect DuckTales' sound driver (bank 3) from the user's ROM; see docs/AUDIO.md.

Output is for local investigation and is not committed (it is derived from the ROM).

    py -3 -I tools/audio_driver.py ROM table              every sound id: kind, priority,
                                                          channels, simulated length, loop
    py -3 -I tools/audio_driver.py ROM decode ID [--ch N] event listing of one id
    py -3 -I tools/audio_driver.py ROM xref               request call sites in all banks
    py -3 -I tools/audio_driver.py ROM apu                every instruction that writes an
                                                          APU register (DMC check)

The simulator follows the code at bank 3 $86AC-$892B (music channels) and
$856B-$8698 (effect stream) and counts frames of one driver tick per NMI.
"""

import argparse
import math
import re
from pathlib import Path

TABLE = 0x8A40  # 2-byte pointers, one per sound id
PERIODS = 0x897B  # 2-byte APU periods indexed by transpose + pitch
LEN = (0x896B, 0x8973)  # note lengths by bits 5-7 of a note byte: plain, dotted
CHANNELS = ('pulse1', 'pulse2', 'triangle', 'noise')
SHORT = ('sq1', 'sq2', 'tri', 'noi')
MUSIC_CMDS = ('tempo', 'slide', 'duty', 'volume', 'loop', 'transpose', 'dotted', 'fade-env', 'instrument', 'end')
SFX_CMDS = ('wait', 'slide', 'duty', 'volume', 'loop', 'vibrato/env', 'end')
NOTE_NAMES = ('C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B')


class Bank3:
    def __init__(self, path):
        data = Path(path).read_bytes()
        if data[:4] != b'NES\x1a':
            raise SystemExit('not an iNES image')
        self.prg = data[16 : 16 + data[4] * 0x4000]
        self.b3 = self.prg[3 * 0x4000 : 4 * 0x4000]

    def byte(self, a):
        return self.b3[a - 0x8000]

    def word(self, a):
        return self.byte(a) | self.byte(a + 1) << 8

    def ids(self):
        n, a, first = [], TABLE, 0xFFFF
        while a < first:
            p = self.word(a)
            n.append(p)
            first = min(first, p)
            a += 2
        return n

    def header(self, sid):
        h = self.ids()[sid]
        b0 = self.byte(h)
        if b0 & 0x0F:
            ptrs = [self.word(h + 1 + 2 * c) for c in range(4)]
            return {
                'addr': h,
                'kind': 'music',
                'prio': b0 & 0x0F,
                'mask': 0xF,
                'streams': ptrs,
                'inst': self.word(h + 9),
            }
        return {'addr': h, 'kind': 'sfx', 'prio': b0 >> 4, 'mask': self.byte(h + 1) & 0x0F, 'streams': [h + 2]}


def note_name(rom, index):
    p = rom.word(PERIODS + 2 * index) if 0 <= index < 96 else 0
    if not p or p == 0xFFFF:
        return 'idx%d' % index
    n = round(12 * math.log2(1789773 / (16 * (p + 1)) / 440)) + 69
    return '%s%d' % (NOTE_NAMES[n % 12], n // 12 - 1)


class MusicChannel:
    """One music channel, mirroring the per-channel bytes the driver keeps at $0370+."""

    def __init__(self, rom, ptr, ch, log=None):
        self.rom, self.ptr, self.ch, self.log = rom, ptr, ch, log
        self.dur = self.tempo = self.loop = self.rep = self.transpose = 0
        self.triplet = False
        self.frame = self.notes = 0
        self.ended = self.looped = None
        self.events = {}
        self.seen = {}

    def next(self):
        b = self.rom.byte(self.ptr)
        self.ptr += 1
        return b

    def emit(self, text):
        if self.log is not None:
            self.log.append('%6d %s %s' % (self.frame, SHORT[self.ch], text))

    def count(self, k):
        self.events[k] = self.events.get(k, 0) + 1

    def tick(self):
        if self.ptr == 0 or (self.looped and self.looped[0] != self.frame):
            return
        if self.dur:
            self.dur = (self.dur - 4) & 0xFFFF
            if self.dur:
                return
        self.triplet = False
        self.parse()

    def parse(self, table=LEN[0]):
        while self.ptr:
            at = self.ptr
            self.seen.setdefault(at, self.frame)
            b = self.next()
            hi = b & 0xF0
            if hi == 0x00:
                cmd = b & 0x0F
                name = MUSIC_CMDS[cmd] if cmd < len(MUSIC_CMDS) else 'cmd%X' % cmd
                self.count(name)
                if cmd == 0:
                    self.tempo = self.next()
                    self.emit('tempo %d' % self.tempo)
                elif cmd in (1, 2, 3, 5, 8):
                    v = self.next()
                    if cmd == 5:
                        self.transpose = v
                    self.emit('%s %02X' % (name, v))
                elif cmd == 4:
                    n = self.next()
                    target = self.rom.word(self.ptr)
                    if n == 0:
                        self.emit('jump %04X' % target)
                        self.ptr = target
                        if target in self.seen and self.looped is None:
                            self.looped = (self.frame, self.seen[target])
                            return 'loop'
                    elif self.loop == n:
                        self.loop = 0
                        self.ptr += 2
                        self.emit('loop x%d done' % (n + 1))
                    else:
                        self.loop += 1
                        self.emit('loop %d/%d -> %04X' % (self.loop, n + 1, target))
                        self.ptr = target
                elif cmd == 6:
                    return self.parse(LEN[1])
                elif cmd == 7:
                    a, c = self.next(), self.next()
                    self.emit('fade-env %02X %02X' % (a, c))
                elif cmd == 9:
                    self.emit('end')
                    self.ptr = 0
                    self.ended = self.frame
                    return 'end'
                else:
                    self.emit('unknown command %02X' % b)
                    self.ptr = 0
                    return 'bad'
                continue
            if hi == 0x20:
                self.count('repeat-pitch')
                r = self.parse(table)
                self.rep = b & 7
                return r
            if hi == 0x30:
                self.count('triplet')
                self.triplet = True
                continue
            units = self.rom.byte(table + ((b >> 5) & 7))
            self.dur = (units * (self.tempo or 256)) & 0xFFFF
            self.notes += 1
            if units == 0:
                self.count('zero-length')
            if self.rep:
                self.rep -= 1
                self.emit('same pitch len %d' % units)
                return 'note'
            p = b & 0x1F
            if p == 0:
                self.emit('rest len %d' % units)
            elif self.ch == 3:
                self.emit('noise %d len %d' % (p, units))
            else:
                self.emit('%s len %d' % (note_name(self.rom, self.transpose + p), units))
            return 'note'


def simulate_music(rom, hdr, frames=30000, log=None):
    chans = [MusicChannel(rom, p, c, log) for c, p in enumerate(hdr['streams'])]
    for f in range(frames):
        for c in chans:
            c.frame = f
            c.tick()
            if c.triplet and f & 1:
                c.tick()
        if all(c.ptr == 0 or c.looped for c in chans):
            break
    return chans


def simulate_sfx(rom, hdr, frames=5000, log=None):
    ptr, mask = hdr['streams'][0], hdr['mask']
    wait = loopc = notes = 0
    counts, looped = {}, None
    for f in range(frames):
        for ch in range(4):
            if not mask & (1 << ch) or wait:
                continue
            while True:
                at = ptr
                b = rom.byte(ptr)
                ptr += 1
                if b & 0x80:
                    if b & 0x0F == 0x0F:
                        ptr += 1
                        counts['retrigger'] = counts.get('retrigger', 0) + 1
                        if log is not None:
                            log.append('%6d %s retrigger' % (f, SHORT[ch]))
                    else:
                        period = (b & 7) << 8 | rom.byte(ptr)
                        ptr += 1
                        notes += 1
                        if log is not None:
                            log.append('%6d %s period %03X' % (f, SHORT[ch], period))
                    break
                name = SFX_CMDS[b] if b < len(SFX_CMDS) else 'cmd%02X' % b
                counts[name] = counts.get(name, 0) + 1
                if log is not None:
                    log.append('%6d %s %s' % (f, SHORT[ch], name))
                if b == 0:
                    wait = rom.byte(ptr)
                    ptr += 1
                elif b in (1, 2, 3):
                    ptr += 1
                elif b == 5:
                    ptr += 4
                elif b == 4:
                    n, target = rom.byte(ptr), rom.word(ptr + 1)
                    if n == 0:
                        return {'frames': None, 'notes': notes, 'counts': counts, 'looped': f}
                    if n == loopc:
                        loopc = 0
                        ptr += 3
                    else:
                        loopc += 1
                        ptr = target
                elif b == 6:
                    ptr = at
                    mask &= ~(1 << ch)
                    break
                else:
                    return {'frames': f, 'notes': notes, 'counts': counts, 'bad': b, 'looped': None}
        if wait:
            wait -= 1
        if not mask:
            return {'frames': f + 1, 'notes': notes, 'counts': counts, 'looped': None}
    return {'frames': None, 'notes': notes, 'counts': counts, 'looped': looped}


def chan_names(mask):
    return '+'.join(CHANNELS[c] for c in range(4) if mask & (1 << c)) or '-'


def cmd_table(rom):
    ptrs = rom.ids()
    print('id  addr  kind  prio channels                     length (frames)')
    for sid, addr in enumerate(ptrs):
        alias = ptrs.index(addr)
        if alias != sid:
            print('%02X  %04X  same as %02X' % (sid, addr, alias))
            continue
        h = rom.header(sid)
        if h['kind'] == 'music':
            parts = []
            for c in simulate_music(rom, h):
                if c.ptr == 0 and c.ended is None:
                    parts.append('%s unused' % SHORT[c.ch])
                elif c.looped:
                    parts.append('%s %d+loop %d' % (SHORT[c.ch], c.looped[1], c.looped[0] - c.looped[1]))
                elif c.ended is not None:
                    parts.append('%s ends %d' % (SHORT[c.ch], c.ended))
                else:
                    parts.append('%s ?' % SHORT[c.ch])
            print(
                '%02X  %04X  music  %d   all four, inst table %04X   %s'
                % (sid, addr, h['prio'], h['inst'], ', '.join(parts))
            )
        else:
            s = simulate_sfx(rom, h)
            length = (
                ('%d' % s['frames'])
                if s['frames']
                else ('loops forever (jump at frame %s)' % s['looped'] if s['looped'] is not None else 'open')
            )
            print('%02X  %04X  sfx    %d   %-28s %s' % (sid, addr, h['prio'], chan_names(h['mask']), length))


def cmd_decode(rom, sid, only):
    h = rom.header(sid)
    log = []
    if h['kind'] == 'music':
        for c in simulate_music(rom, h, log=log):
            print('%s: %d notes, events %s, loop %s, end %s' % (CHANNELS[c.ch], c.notes, c.events, c.looped, c.ended))
    else:
        print('sfx prio %d channels %s: %s' % (h['prio'], chan_names(h['mask']), simulate_sfx(rom, h, log=log)))
    for line in log:
        if only is None or SHORT[only] in line:
            print(line)


def bank_addr(rom, off):
    bank = off // 0x4000
    return bank, (0xC000 if bank == len(rom.prg) // 0x4000 - 1 else 0x8000) + off % 0x4000


def cmd_xref(rom):
    prg = rom.prg
    for name, pat in (
        ('$F752 music_request (sets $C5)', b'\x20\x52\xf7'),
        ('$F754 sound_request', b'\x20\x54\xf7'),
        ('$F756', b'\x20\x56\xf7'),
        ('$8003 direct', b'\x20\x03\x80'),
    ):
        for m in re.finditer(re.escape(pat), prg):
            bank, a = bank_addr(rom, m.start())
            p = prg[m.start() - 2 : m.start()]
            imm = '#$%02X' % p[1] if p[0] == 0xA9 else 'A computed'
            print('%-32s %d:%04X  %s' % (name, bank, a, imm))


def cmd_apu(rom):
    prg = rom.prg
    modes = {0x8C: 'STY', 0x8D: 'STA', 0x8E: 'STX', 0x99: 'STA ,Y', 0x9D: 'STA ,X'}
    for m in re.finditer(rb'[\x8c\x8d\x8e\x99\x9d][\x00-\x17]\x40', prg):
        bank, a = bank_addr(rom, m.start())
        p = prg[m.start() - 2 : m.start()]
        imm = ' value #$%02X' % p[1] if p[0] in (0xA9, 0xA2, 0xA0) else ''
        print('%d:%04X  %s $40%02X%s' % (bank, a, modes[prg[m.start()]], prg[m.start() + 1], imm))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('table')
    d = sub.add_parser('decode')
    d.add_argument('id', type=lambda s: int(s, 16))
    d.add_argument('--ch', type=int)
    sub.add_parser('xref')
    sub.add_parser('apu')
    a = ap.parse_args()
    rom = Bank3(a.rom)
    {
        'table': lambda: cmd_table(rom),
        'decode': lambda: cmd_decode(rom, a.id, a.ch),
        'xref': lambda: cmd_xref(rom),
        'apu': lambda: cmd_apu(rom),
    }[a.cmd]()


if __name__ == '__main__':
    main()
