"""Find instructions that reference given absolute or zero-page addresses, in every PRG bank.

Byte-pattern search over the user's ROM (UxROM, 16 KiB banks, bank 7 fixed at $C000). Matches are
candidates: a hit can fall inside data or in the middle of another instruction, so confirm each one
with tools/disasm6502.py or a trace (tools/whoaccesses.py) before relying on it.

    py -3 -I tools/player_xref.py ROM ADDR[,ADDR...] [--stores] [--bank N]
"""

import argparse
from pathlib import Path

# opcode -> (mnemonic, mode); absolute and zero-page memory operands only
ABS = {
    0x0D: 'ORA',
    0x0E: 'ASL',
    0x19: 'ORA,Y',
    0x1D: 'ORA,X',
    0x1E: 'ASL,X',
    0x2C: 'BIT',
    0x2D: 'AND',
    0x2E: 'ROL',
    0x39: 'AND,Y',
    0x3D: 'AND,X',
    0x3E: 'ROL,X',
    0x4D: 'EOR',
    0x4E: 'LSR',
    0x59: 'EOR,Y',
    0x5D: 'EOR,X',
    0x5E: 'LSR,X',
    0x6D: 'ADC',
    0x6E: 'ROR',
    0x79: 'ADC,Y',
    0x7D: 'ADC,X',
    0x7E: 'ROR,X',
    0x8C: 'STY',
    0x8D: 'STA',
    0x8E: 'STX',
    0x99: 'STA,Y',
    0x9D: 'STA,X',
    0xAC: 'LDY',
    0xAD: 'LDA',
    0xAE: 'LDX',
    0xB9: 'LDA,Y',
    0xBC: 'LDY,X',
    0xBD: 'LDA,X',
    0xBE: 'LDX,Y',
    0xCC: 'CPY',
    0xCD: 'CMP',
    0xCE: 'DEC',
    0xD9: 'CMP,Y',
    0xDD: 'CMP,X',
    0xDE: 'DEC,X',
    0xEC: 'CPX',
    0xED: 'SBC',
    0xEE: 'INC',
    0xF9: 'SBC,Y',
    0xFD: 'SBC,X',
    0xFE: 'INC,X',
}
ZP = {
    0x05: 'ORA',
    0x06: 'ASL',
    0x15: 'ORA,X',
    0x16: 'ASL,X',
    0x24: 'BIT',
    0x25: 'AND',
    0x26: 'ROL',
    0x35: 'AND,X',
    0x36: 'ROL,X',
    0x45: 'EOR',
    0x46: 'LSR',
    0x55: 'EOR,X',
    0x56: 'LSR,X',
    0x65: 'ADC',
    0x66: 'ROR',
    0x75: 'ADC,X',
    0x76: 'ROR,X',
    0x84: 'STY',
    0x85: 'STA',
    0x86: 'STX',
    0x94: 'STY,X',
    0x95: 'STA,X',
    0x96: 'STX,Y',
    0xA4: 'LDY',
    0xA5: 'LDA',
    0xA6: 'LDX',
    0xB4: 'LDY,X',
    0xB5: 'LDA,X',
    0xB6: 'LDX,Y',
    0xC4: 'CPY',
    0xC5: 'CMP',
    0xC6: 'DEC',
    0xD5: 'CMP,X',
    0xD6: 'DEC,X',
    0xE4: 'CPX',
    0xE5: 'SBC',
    0xE6: 'INC',
    0xF5: 'SBC,X',
    0xF6: 'INC,X',
}
WRITES = ('STA', 'STX', 'STY', 'INC', 'DEC', 'ASL', 'LSR', 'ROL', 'ROR')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('addrs')
    ap.add_argument('--stores', action='store_true', help='only instructions that write memory')
    ap.add_argument('--bank', type=int, help='only this bank (7 = fixed bank)')
    a = ap.parse_args()
    data = Path(a.rom).read_bytes()
    prg = data[16 : 16 + data[4] * 0x4000]
    nbanks = len(prg) // 0x4000
    for want in (int(x, 16) for x in a.addrs.split(',')):
        for bank in range(nbanks):
            if a.bank is not None and bank != a.bank:
                continue
            base = 0xC000 if bank == nbanks - 1 else 0x8000
            chunk = prg[bank * 0x4000 : (bank + 1) * 0x4000]
            for i in range(len(chunk) - 1):
                op = chunk[i]
                if op in ABS and i + 2 < len(chunk) and chunk[i + 1] | chunk[i + 2] << 8 == want:
                    name, fmt = ABS[op], '$%04X' % want
                elif want < 0x100 and op in ZP and chunk[i + 1] == want:
                    name, fmt = ZP[op], '$%02X' % want
                else:
                    continue
                if a.stores and not name.startswith(WRITES):
                    continue
                mn, _, idx = name.partition(',')
                print('%d:%04X  %s %s%s' % (bank, base + i, mn, fmt, ',' + idx if idx else ''))


if __name__ == '__main__':
    main()
