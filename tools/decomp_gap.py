"""Estimate the PRG code that neither the routes nor the poke cases execute (docs/decompilation.md).

    py -3 -I tools/decomp_gap.py [--golden DIR] [--top N]

tools/decomp_progress.py counts only executed bytes. This script sizes the rest from the ROM: it
starts from every instruction the routes and the poke cases execute (the same coverage the meter
reads) and follows what the code itself names.

- Branches both ways, JMP abs, and JSR targets (from the fixed bank into the switchable window,
  only into the banks that execute that address). The instruction after an executed JSR counts
  only when the runs executed it, and after any other JSR only when the routine called returns.
- Dispatch tables: split tables (two indexed loads of the code, at most 128 bytes apart), word
  tables at an indexed load, and the words after a JSR to a dispatcher that never returns. A table
  counts only when at least two of its entries are executed instructions and every entry starts
  code (no entry inside an executed instruction, no BRK or invalid opcode before its first RTS,
  RTI or JMP); a word table that breaks that keeps its leading run of good entries.

The report lists each block of code reached that way but not executed, with how it was reached
('branch': a path of an executed routine, 'table': a dispatch entry no route or poke runs) and the
routine symbol at or before it (src/game/ducktales_symbols.h; a symbol with no bank of its own
takes the one its section heading names). Then it lists the runs of unreached bytes that decode as
code, end in RTS, RTI or JMP, and call or jump to reached code: code nothing reaches (dead, or
reached through a table the checks above refused).

Limits: a lower bound. Tables the checks refuse (wrong pairings, data) are not followed, an
indirect jump through RAM other than these tables is not followed, and the code a fixed-bank JSR
reaches in a bank that never executes that address is not counted. Data that decodes as code can
show up among the unreached runs.
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import decomp_progress as dp  # noqa: E402
import quick_verify  # noqa: E402
from romfile import default_rom  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--golden', default=str(ROOT / 'build' / 'golden'), help='quick_verify golden root')
    ap.add_argument('--top', type=int, default=40, help='blocks to list')
    args = ap.parse_args()
    golden = Path(args.golden) / quick_verify.file_sha256(Path(args.rom))
    code = dp.Code(args.rom)
    prg = code.prg

    executed = set()
    for route in sorted((ROOT / 'tests' / 'routes').glob('*.txt')):
        for _, offsets in quick_verify.read_cover(golden / f'{route.stem}.cover'):
            executed |= offsets
    for offsets in dp.read_pokes(golden / dp.POKES).values():
        executed |= offsets
    insn = {o: code.decode(o) for o in executed}

    def decode(o):
        return insn.get(o) or code.decode(o)

    def length(o):
        return decode(o)[2]

    def cpu(o):
        bank, addr = code.cpu(o)
        return f'{bank}:{addr:04X}'

    def jsr_targets(off, addr):
        bank = off >> 14
        if addr >= 0xC000 or bank != code.fixed:
            return [t for t in [code.offset(bank, addr)] if t is not None]
        return [o for b in range(code.fixed) if (o := code.offset(b, addr)) in executed]

    def window(bank, addr):
        return code.offset(None if bank == code.fixed else bank, addr) if addr >= 0x8000 else None

    def starts_code(off):
        o = off
        for _ in range(24):
            d = code.decode(o)
            if not d or d[0] == 'BRK':
                return False
            if d[0] in dp.ENDS:
                return True
            o += d[2]
            if o >> 14 != off >> 14:
                return False
        return True

    returns = defaultdict(bool)
    for o, d in insn.items():
        if d[5] is not None:
            for t in jsr_targets(o, d[5]):
                returns[t] |= o + d[2] in executed
    noreturn = {t for t, back in returns.items() if not back}

    reached, how = set(), {}

    def follow(seeds):
        todo = list(seeds)
        while todo:
            o, kind = todo.pop()
            if o in reached:
                continue
            d = decode(o)
            if not d:
                continue
            reached.add(o)
            if o not in executed:
                how[o] = kind
            mn, mode, n, arg, flow, call = d
            nxt = 'branch' if o in executed else kind
            if call is not None:
                targets = jsr_targets(o, call)
                todo += [(t, nxt) for t in targets]
                if o in executed or (targets and all(t in noreturn for t in targets)):
                    flow = [t for t in flow if t in executed]
            todo += [(t, nxt) for t in flow]

    follow((o, 'exec') for o in executed)

    loads = defaultdict(set)
    for o in reached:
        mn, mode, n, arg, flow, call = decode(o)
        if mn in dp.LOADS and mode in ('abx', 'aby') and (t := code.offset(o >> 14, arg)) is not None:
            loads[o >> 14].add(t)

    tables = []
    for bank, starts in loads.items():
        starts = sorted(starts)
        for lo in starts:
            for hi in starts:
                if 0 < hi - lo <= 128:
                    words = [prg[lo + i] | prg[hi + i] << 8 for i in range(hi - lo)]
                    tables += [(False, [window(bank, (w + plus) & 0xFFFF) for w in words]) for plus in (0, 1)]
            for plus in (0, 1):
                words = [prg[lo + i] | prg[lo + i + 1] << 8 for i in range(0, 64, 2) if lo + i + 1 < len(prg)]
                tables.append((True, [window(bank, (w + plus) & 0xFFFF) for w in words]))
    for o, d in insn.items():
        if d[5] is not None and any(t in noreturn for t in jsr_targets(o, d[5])):
            base = o + d[2]
            words = [
                prg[base + i] | prg[base + i + 1] << 8 for i in range(0, 512, 2) if (base + i + 1) >> 14 == o >> 14
            ]
            tables.append((True, [window(o >> 14, w) for w in words]))

    inside = {i for o in executed for i in range(o + 1, o + length(o))}
    seeds = []
    for word, entries in tables:
        if word:
            good = []
            for e in entries:
                if e is None or e in inside or not starts_code(e):
                    break
                good.append(e)
            entries = good
        elif any(e is None or e in inside or not starts_code(e) for e in entries):
            continue
        if sum(e in executed for e in entries) >= 2:
            seeds += [(e, 'table') for e in entries if e not in reached]
    follow(seeds)

    named, section = [], None
    for line in (ROOT / 'src' / 'game' / 'ducktales_symbols.h').read_text().splitlines():
        if line.startswith('/* ----'):
            section = re.search(r'\(bank (\d+)', line)
        if not (m := dp.DEFINE.match(line)) or not m[1].startswith(('DT_CODE_', 'DT_B')):
            continue
        bank = re.match(r'DT_B(\d+)_', m[1]) or re.search(r'bank (\d+)', m[3]) or section
        if (o := code.offset(int(bank[1]) if bank else None, int(m[2], 0))) is not None:
            named.append((o, m[1][3:]))
    named.sort()

    def owner(o):
        best = [name for at, name in named if at <= o and at >> 14 == o >> 14]
        return best[-1] if best else '-'

    blocks = []
    for o in sorted(how):
        if blocks and o <= blocks[-1][1] + 3 and o >> 14 == blocks[-1][0] >> 14:
            blocks[-1][1] = o + length(o)
            blocks[-1][2] += length(o)
            blocks[-1][3].add(how[o])
        else:
            blocks.append([o, o + length(o), length(o), {how[o]}])

    covered = {i for o in reached for i in range(o, o + length(o))}
    dead, o = [], 0
    while o < len(prg):
        if o in covered:
            o += 1
            continue
        p, count, calls, ends = o, 0, 0, False
        while p < len(prg) and p not in covered and p >> 14 == o >> 14:
            d = code.decode(p)
            if not d or d[0] == 'BRK':
                break
            count += 1
            if d[5] is not None and any(t in reached for t in jsr_targets(p, d[5])):
                calls += 1
            if d[0] == 'JMP' and d[1] == 'abs' and window(p >> 14, d[3]) in reached:
                calls += 1
            p += d[2]
            if d[0] in dp.ENDS:
                ends = True
                break
        if ends and count >= 6 and calls:
            dead.append((o, p))
            o = p
        else:
            o += 1

    total = sum(length(o) for o in executed)
    unexecuted = sum(b[2] for b in blocks)
    print(f'executed: {total:,} PRG bytes; reached but not executed: {unexecuted:,} bytes in {len(blocks)} blocks')
    for kind in ('branch', 'table'):
        print(f'  {kind:7} {sum(length(o) for o in how if how[o] == kind):6,}')
    print(f'{"bank":>4} {"executed":>9} {"reached":>8}')
    for bank in range(code.fixed + 1):
        ex = sum(length(o) for o in executed if o >> 14 == bank)
        un = sum(length(o) for o in how if o >> 14 == bank)
        print(f'{bank:>4} {ex:>9,} {un:>8,}')
    print('blocks, largest first:')
    for start, end, n, kinds in sorted(blocks, key=lambda b: -b[2])[: args.top]:
        print(f'  {cpu(start)}-{code.cpu(end - 1)[1]:04X} {n:5} {",".join(sorted(kinds)):12} {owner(start)}')
    print(f'unreached runs that decode as code and call reached code: {sum(e - s for s, e in dead)} bytes')
    for start, end in dead:
        print(f'  {cpu(start)}-{code.cpu(end - 1)[1]:04X} {end - start:5} {owner(start)}')


if __name__ == '__main__':
    main()
