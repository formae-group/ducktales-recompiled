"""Write bytes into the CPU RAM of a NESRecomp save state, for reversible physics experiments.

The state format is runner/cyc/cyc_state.c: a header, then tagged chunks; CPU RAM is the 2 KiB
array inside the 'HW  ' chunk (HwMachine.ram). Its offset is not hard-coded: the tool finds the one
place in that chunk equal to the RAM a --frame-log recorded at the frame the state was saved
after (the state and the log must come from the same run). The input state is not modified.

    ducktales.exe ROM --input R --frames F+1 --save-state F:s.state --frame-log s.bin --frame-log-frames F:F
    py -3 -I tools/player_poke_state.py s.state s.bin out.state 07A0=06 0790=00
    ducktales.exe ROM --load-state out.state --input R --frames N ...
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framelog import frames  # noqa: E402

HEADER = 8 + 6 * 4
TAG_HW = struct.unpack('<I', b'HW  ')[0]


def ram_offset(state, ram):
    at = HEADER
    while at + 8 <= len(state):
        tag, n = struct.unpack_from('<II', state, at)
        at += 8
        if tag == TAG_HW:
            body = state[at:at + n]
            hits = [i for i in range(len(body) - len(ram) + 1) if body[i:i + len(ram)] == ram]
            if len(hits) != 1:
                raise SystemExit('RAM found %d times in the HW chunk; is the log from the same run?' % len(hits))
            return at + hits[0]
        at += n
    raise SystemExit('no HW chunk')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state')
    ap.add_argument('ramlog', help='frame log holding the frame the state was saved after (its last frame is used)')
    ap.add_argument('out')
    ap.add_argument('pokes', nargs='+', help='ADDR=VALUE in hex, ADDR < $0800')
    a = ap.parse_args()
    state = bytearray(Path(a.state).read_bytes())
    ram = None
    for _, r, _, _ in frames(a.ramlog):
        ram = r
    if ram is None or len(ram) != 0x800:
        raise SystemExit('frame log has no 2 KiB CPU RAM')
    base = ram_offset(state, bytes(ram))
    for p in a.pokes:
        addr, value = (int(x, 16) for x in p.split('='))
        if addr >= 0x800 or value > 0xFF:
            raise SystemExit('bad poke ' + p)
        print('$%04X: %02X -> %02X' % (addr, state[base + addr], value))
        state[base + addr] = value
    Path(a.out).write_bytes(state)


if __name__ == '__main__':
    main()
