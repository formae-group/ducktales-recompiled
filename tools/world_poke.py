"""Read or patch CPU RAM inside a NESRecomp save state (runner/cyc/cyc_state.c).

A state is a header followed by tagged chunks; the 'HW  ' chunk holds HwMachine
(runner/cyc/hw_internal.h), whose 2 KiB CPU RAM starts at byte 23 of the struct
in the pinned submodule (tick, align, padding, the 64-bit cycle count, then
seven bytes of bus state); the struct's tail padding follows it. Patching RAM
and loading the result with --load-state starts a run from a chosen game state
(land, room, flags) for controlled experiments. The state carries no checksum
over RAM, so a patched state loads in the build that wrote it.

    py -3 -I tools/world_poke.py STATE peek ADDR[,ADDR...]
    py -3 -I tools/world_poke.py STATE poke OUT ADDR=VAL [ADDR=VAL ...]
"""
import argparse
import struct
from pathlib import Path

HEADER = 8 + 4 * 6
RAM_FIELD = 23


def ram_offset(data):
    if data[:8] != b'CYCSTATE':
        raise SystemExit('not a save state')
    pos = HEADER
    while pos + 8 <= len(data):
        tag, length = data[pos:pos + 4], struct.unpack_from('<I', data, pos + 4)[0]
        if tag == b'HW  ':
            if length < RAM_FIELD + 0x800:
                raise SystemExit('HW chunk too small: HwMachine layout changed')
            return pos + 8 + RAM_FIELD
        if tag == b'END ':
            break
        pos += 8 + length
    raise SystemExit('no HW chunk')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state', type=Path)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('peek')
    p.add_argument('addrs')
    w = sub.add_parser('poke')
    w.add_argument('out', type=Path)
    w.add_argument('sets', nargs='+')
    a = ap.parse_args()
    data = bytearray(a.state.read_bytes())
    base = ram_offset(data)
    if a.cmd == 'peek':
        for x in a.addrs.split(','):
            addr = int(x, 16)
            print('%04X=%02X' % (addr, data[base + addr]))
        return
    for item in a.sets:
        addr, val = item.split('=')
        addr, val = int(addr, 16), int(val, 16)
        if not 0 <= addr < 0x800 or not 0 <= val < 0x100:
            raise SystemExit('bad assignment ' + item)
        data[base + addr] = val
    a.out.write_bytes(bytes(data))


if __name__ == '__main__':
    main()
