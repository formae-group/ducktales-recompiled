"""Which code outside the decompiled ranges reaches a routine's entry (docs/decompilation.md).

A decompiled routine needs a [[mod_function_hook]] site only if original code
reaches it. This reads the generated code of a build (the recompiler's view of
every instruction) and lists, for each entry address, the JSR, JMP, branch and
RTS-table instructions outside the given ranges that go there. Indirect jumps
through RAM (JMP ($00) with a table the code reads itself) do not appear: a
routine reached that way from original code needs its site too.

    py -3 -I tools/decomp_callers.py --inside E354-ED04 F234-F2C7 F752-F76F -- E354 E3B8 ...
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LABEL = re.compile(r'L_([0-9A-F]{4}): /\* \w\w (\S+)')
TARGET = re.compile(r'goto L_([0-9A-F]{4})|cpu\.pc = 0x([0-9A-F]{4}); return; \}')
TRANSFERS = {'JSR', 'JMP', 'RTS', 'RTI', 'BCC', 'BCS', 'BEQ', 'BNE', 'BMI', 'BPL', 'BVC', 'BVS'}


def generated_dir(build):
    dirs = sorted((ROOT / build / 'cycle-ducktales').glob('*/generated/game_cyc.c'), key=lambda p: p.stat().st_mtime)
    if not dirs:
        sys.exit(f'no generated code under {build}/cycle-ducktales; build it first')
    return dirs[-1].parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--inside', nargs='*', default=[], help='decompiled code ranges, LO-HI in hex')
    ap.add_argument('entries', nargs='+', help='routine entry addresses in hex')
    args = ap.parse_args()
    ranges = [tuple(int(v, 16) for v in r.split('-')) for r in args.inside]
    entries = {int(a, 16) for a in args.entries}
    callers = defaultdict(set)
    for path in generated_dir(args.build).glob('*.c'):
        at = None
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            m = LABEL.match(line)
            if m:
                at = (int(m.group(1), 16), m.group(2)) if m.group(2) in TRANSFERS else None
                continue
            if at is None or any(lo <= at[0] <= hi for lo, hi in ranges):
                continue
            for g in TARGET.finditer(line):
                target = int(g.group(1) or g.group(2), 16)
                if target in entries and target != at[0]:
                    callers[target].add(f'{at[1]} ${at[0]:04X}')
    for entry in sorted(entries):
        found = sorted(callers[entry])
        print(f'${entry:04X}: ' + (', '.join(found) if found else 'only decompiled code'))


if __name__ == '__main__':
    main()
