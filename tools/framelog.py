"""Read NESRecomp --frame-log files (format: runner/cyc/cyc_host.c write_frame_log).

    py -3 -I tools/framelog.py LOG ram ADDR[,ADDR...] [--first N --last N]
    py -3 -I tools/framelog.py LOG changes A:B C:D   bytes that change in frames A..B but not C..D
    py -3 -I tools/framelog.py LOG events --max N      every change of bytes that change at most N times
"""
import argparse
import struct


def frames(path):
    with open(path, 'rb') as f:
        if f.read(8) != b'CYCFRAME':
            raise SystemExit('not a frame log')
        version, = struct.unpack('<I', f.read(4))
        nlen = 6 if version >= 2 else 5
        while True:
            head = f.read(12 + 4 * nlen)
            if len(head) < 12 + 4 * nlen:
                return
            frame, _, _, *lens = struct.unpack('<III%dI' % nlen, head)
            palette = f.read(32)
            oam = f.read(256)
            ram = f.read(lens[0])
            f.seek(sum(lens[1:]), 1)
            yield frame, ram, oam, palette


def parse_range(s):
    a, b = s.split(':')
    return int(a), int(b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('ram')
    r.add_argument('addrs')
    r.add_argument('--first', type=int, default=0)
    r.add_argument('--last', type=int, default=1 << 30)
    r.add_argument('--every', type=int, default=1)
    c = sub.add_parser('changes')
    c.add_argument('active', type=parse_range)
    c.add_argument('quiet', type=parse_range)
    e = sub.add_parser('events')
    e.add_argument('--max', type=int, default=4)
    a = ap.parse_args()
    if a.cmd == 'events':
        prev, changes = None, {}
        for frame, ram, _, _ in frames(a.log):
            if prev is not None:
                for i, (old, new) in enumerate(zip(prev, ram)):
                    if old != new:
                        changes.setdefault(i, []).append((frame, old, new))
            prev = ram
        for i in sorted(changes):
            if len(changes[i]) <= a.max:
                print('%04X ' % i + ' '.join('%d:%02X>%02X' % c for c in changes[i]))
        return
    if a.cmd == 'ram':
        addrs = [int(x, 16) for x in a.addrs.split(',')]
        print('frame  ' + ' '.join('%04X' % x for x in addrs))
        for frame, ram, _, _ in frames(a.log):
            if a.first <= frame <= a.last and (frame - a.first) % a.every == 0:
                print('%5d  ' % frame + ' '.join('  %02X' % ram[x] for x in addrs))
        return
    act, quiet = {}, {}
    for frame, ram, _, _ in frames(a.log):
        for rng, acc in ((a.active, act), (a.quiet, quiet)):
            if rng[0] <= frame <= rng[1]:
                for i, v in enumerate(ram):
                    acc.setdefault(i, set()).add(v)
    for i in sorted(act):
        if len(act[i]) > 1 and len(quiet.get(i, ())) <= 1:
            print('%04X active=%d values quiet=%s' % (i, len(act[i]), sorted(quiet.get(i, ()))))


if __name__ == '__main__':
    main()
