"""Find where one frame's CPU execution first differs between ducktales.exe and Mesen2.

    py -3 -I tests/mesen_compare.py --routes ROUTE --align 1 --trace-frame N --frames N+2
    py -3 -I tools/mesen/trace_diff.py captures/mesen/ROUTE.zeros_a1.ours.trace.txt captures/mesen/ROUTE.zeros.mesen.trace.txt

Both traces cover the same window (frame N: from the controller poll at the
start of VBlank to the next one) but count cycles from different origins, so
they are re-based on the first NMI handler instruction (by default the first
address that both traces share). Reports every change of the relative cycle
offset, then the first instruction whose address or registers differ, with a
few instructions of context.
"""
import argparse
import re


def ours_trace(path):
    out = []
    for line in open(path):
        m = re.match(r'(\d+) INSN PC=([0-9A-F]{4}) A=([0-9A-F]{2}) X=([0-9A-F]{2}) Y=([0-9A-F]{2}) S=([0-9A-F]{2})', line)
        if m:
            out.append((int(m[1]), int(m[2], 16), tuple(int(m[i], 16) for i in range(3, 7)), ''))
    return out


def mesen_trace(path):
    out = []
    for line in open(path):
        f = line.split()
        if len(f) >= 8:
            out.append((int(f[0]), int(f[1], 16), tuple(int(x, 16) for x in f[2:6]), 'line %s dot %s' % (f[6], f[7])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ours')
    ap.add_argument('mesen')
    ap.add_argument('--align-pc', type=lambda s: int(s, 16), default=None,
                    help='address to align on (default: the first address both traces share)')
    ap.add_argument('--context', type=int, default=6)
    ap.add_argument('--regs', action='store_true', help='also stop at the first difference in A, X, Y or S '
                    '(the registers an NMI interrupts with differ when the idle loop position does)')
    a = ap.parse_args()
    o, m = ours_trace(a.ours), mesen_trace(a.mesen)
    if not o or not m:
        raise SystemExit('empty trace')
    if a.align_pc is None:
        mesen_pcs = {x[1] for x in m[:64]}
        a.align_pc = next(x[1] for x in o if x[1] in mesen_pcs)
    oi = next(i for i, x in enumerate(o) if x[1] == a.align_pc)
    mi = next(i for i, x in enumerate(m) if x[1] == a.align_pc)
    print('aligned on %04X: ours instruction %d, Mesen instruction %d (%s)' % (a.align_pc, oi, mi, m[mi][3]))
    o, m = o[oi:], m[mi:]
    o0, m0 = o[0][0], m[0][0]

    def show(k):
        for j in range(max(0, k - a.context), min(k + 3, len(o), len(m))):
            print('   ours  +%-6d %04X A=%02X X=%02X Y=%02X S=%02X' % ((o[j][0] - o0, o[j][1]) + o[j][2]),
                  '| Mesen +%-6d %04X A=%02X X=%02X Y=%02X S=%02X %s' % ((m[j][0] - m0, m[j][1]) + m[j][2] + (m[j][3],)))

    prev = 0
    for k, (x, y) in enumerate(zip(o, m)):
        if x[1] != y[1] or (a.regs and x[2] != y[2]):
            print('first difference at instruction %d after the aligned one:' % k)
            show(k)
            return
        d = (x[0] - o0) - (y[0] - m0)
        if d != prev:
            print('relative cycle offset ours-Mesen %+d -> %+d at %04X (%s)' % (prev, d, x[1], y[3]))
            prev = d
    print('no difference in %d instructions' % min(len(o), len(m)))


if __name__ == '__main__':
    main()
