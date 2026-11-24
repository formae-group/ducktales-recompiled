"""Timing marks for a decompiled routine (docs/DECOMPILATION.md "Timing").

For a routine of the tables in src/decomp/*.c, walks its original code in the ROM from its entry
(fall-through, both ways of each branch, JMP; not into the routines it calls) and lists every
instruction with its cycles and what the routine's C must say about it to be timed:

  DC_BR      a conditional branch: DC_BR(pc, cond) where the C decides it
  DC_JUMP    an indirect jump: DC_JUMP(pc, target)
  DC_EA?     an indexed read that can cross a page (abs,X or abs,Y with a nonzero low byte, or
             (zp),Y): DC_EA(pc, addr) where it can cross; check mode tells which ones do
  bus        a device access: the C's Decomp_BusWrite, Decomp_BusRead or Decomp_BusLoad
  calls      a JSR: the callee's C is walked too (it must be timed or carry its own marks)

and whether the routine's C function (its body only, not the helpers it calls) has each mark.
Reads the user's ROM locally; output is for investigation and is not committed.

    py -3 -I tools/decomp_timing.py Task_Pass
    py -3 -I tools/decomp_timing.py '$FF38' [--bank 0]
    py -3 -I tools/decomp_timing.py --summary [--file system.c]
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from disasm6502 import FMT, MODES, OPS, Rom  # noqa: E402
from romfile import default_rom  # noqa: E402

BRANCHES = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'}
READS = {'LDA', 'LDX', 'LDY', 'ADC', 'SBC', 'AND', 'ORA', 'EOR', 'CMP', 'CPX', 'CPY', 'BIT'}
STORES = {'STA', 'STX', 'STY'}
RMW = {'ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'}
READ_CYCLES = {'imm': 2, 'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'abx': 4, 'aby': 4, 'izx': 6, 'izy': 5}
STORE_CYCLES = {'zp': 3, 'zpx': 4, 'zpy': 4, 'abs': 4, 'abx': 5, 'aby': 5, 'izx': 6, 'izy': 6}
RMW_CYCLES = {'zp': 5, 'zpx': 6, 'abs': 6, 'abx': 7}
OTHER_CYCLES = {'JSR': 6, 'RTS': 6, 'RTI': 6, 'PHA': 3, 'PHP': 3, 'PLA': 4, 'PLP': 4}
MARK = re.compile(r'\b(DC_BR|DC_JUMP|DC_EA)\s*\(\s*(0x[0-9A-Fa-f]+)')


def cycles(mn, mode):
    if mn in READS:
        return READ_CYCLES[mode]
    if mn in STORES:
        return STORE_CYCLES[mode]
    if mn in RMW and mode != 'acc':
        return RMW_CYCLES[mode]
    if mn == 'JMP':
        return 3 if mode == 'abs' else 5
    return OTHER_CYCLES.get(mn, 2)


def symbols():
    out = {}
    for line in (ROOT / 'src' / 'game' / 'ducktales_symbols.h').read_text(encoding='utf-8').splitlines():
        m = re.match(r'#define\s+(DT_\w+)\s+(0x[0-9A-Fa-f]+)', line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def split_fields(text):
    fields, depth, cur = [], 0, ''
    for ch in text:
        if ch in '({':
            depth += 1
        elif ch in ')}':
            depth -= 1
        if ch == ',' and depth == 0:
            fields.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        fields.append(cur.strip())
    return fields


POSITIONAL = ['addr', 'site', 'name', 'run', 'out', 'task', 'locals', 'resume', 'bank', 'interrupt', 'timed', 'polls']
ENTRY = re.compile(r'\{\s*((?:DT_\w+|0x[0-9A-Fa-f]+)\s*,\s*(?:"[^"]*"|NULL)\s*,\s*"(\w+)"[^{}]*)\}')


MACRO = re.compile(r'^#define\s+([A-Z_]+)\(([^)]*)\)\s*\{(.*)\}\s*$', re.M)


def table_entries(text):
    """The field lists of a file's table entries, written out or through its entry macros (flow.c's TASK, ...)."""
    for m in ENTRY.finditer(text):
        yield split_fields(m.group(1))
    for mac in MACRO.finditer(text):
        params = [p.strip() for p in mac.group(2).split(',')]
        fields = split_fields(mac.group(3))
        for call in re.finditer(r'^\s*' + mac.group(1) + r'\((.*)\),?\s*$', text, re.M):
            args = dict(zip(params, split_fields(call.group(1))))
            yield [args.get(f, f) for f in fields]


def routines():
    """name -> dict(addr, bank, func, file, timed) from every table entry."""
    syms, found = symbols(), {}
    base = ROOT / 'src' / 'decomp'
    for path in sorted(base.rglob('*.c')):
        for fields in table_entries(path.read_text(encoding='utf-8')):
            entry = {}
            if len(fields) < 4 or not fields[2].startswith('"'):
                continue
            for k, f in enumerate(fields):
                if f.startswith('.'):
                    key, _, val = f[1:].partition('=')
                    entry[key.strip()] = val.strip()
                elif k < len(POSITIONAL):
                    entry[POSITIONAL[k]] = f
            addr = entry.get('addr', '')
            addr = syms.get(addr) if addr.startswith('DT_') else int(addr, 16) if addr.startswith('0x') else None
            if addr is None:
                continue
            bank = entry.get('bank', '0')
            found[entry['name'].strip('"')] = {
                'addr': addr,
                'bank': int(bank, 0) if re.fullmatch(r'\d+|0x[0-9A-Fa-f]+', bank) else 0,
                'func': entry.get('run', ''),
                'file': path.relative_to(base).as_posix(),
                'timed': entry.get('timed', 'false') == 'true',
            }
    return found


def function(text, func):
    m = re.search(r'^[\w ]*\b' + re.escape(func) + r'\s*\([^;{]*\)\s*\{', text, re.M) if func else None
    if not m:
        return '', 0
    depth, i = 0, m.end() - 1
    while True:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            break
    return text[m.start() : i], text.count('\n', 0, m.start()) + 1


def body(file, func):
    """The C of func with the functions of the same file it calls, two calls deep (an adapter's helper).
    A file in a subfolder (objects/) also finds them in the other files of its folder."""
    path = ROOT / 'src' / 'decomp' / file
    text = path.read_text(encoding='utf-8')
    near = [text]
    if '/' in file:
        near += [p.read_text(encoding='utf-8') for p in sorted(path.parent.glob('*.c')) if p != path]
    src, line = function(text, func)
    seen, todo = {func}, [src]
    for _ in range(2):
        found = []
        for part in todo:
            for name in set(re.findall(r'\b([A-Z]\w+)\s*\(', part)) - seen:
                seen.add(name)
                more = next((m for m in (function(t, name)[0] for t in near) if m), '')
                if more:
                    found.append(more)
        src += ''.join(found)
        todo = found
    return src, line


def target(pc, arg):
    return (pc + 2 + (arg - 256 if arg & 0x80 else arg)) & 0xFFFF


def walk(rom, bank, entry, stops):
    """The instructions reachable from entry, stopping at other routines' entries: pc -> (mn, mode, arg)."""
    seen, todo, out = set(), [entry], {}
    while todo:
        pc = todo.pop()
        if pc in seen or not 0x8000 <= pc <= 0xFFFF or (pc in stops and pc != entry):
            continue
        seen.add(pc)
        op = OPS.get(rom.read(bank, pc))
        if not op:
            out[pc] = ('???', 'imp', 0)
            continue
        mn, mode = op
        n = 1 + MODES[mode]
        arg = sum(rom.read(bank, pc + 1 + k) << (8 * k) for k in range(n - 1))
        out[pc] = (mn, mode, arg)
        if mn in ('RTS', 'RTI', 'BRK') or (mn == 'JMP' and mode == 'ind'):
            continue
        if mn == 'JMP':
            todo.append(arg)
            continue
        if mn in BRANCHES:
            todo.append(target(pc, arg))
        todo.append(pc + n)
    return out


def needs(mn, mode, arg):
    if mn in BRANCHES:
        return 'DC_BR'
    if mn == 'JMP' and mode == 'ind':
        return 'DC_JUMP'
    if mode in ('abs', 'abx', 'aby') and 0x2000 <= arg < 0x4020 and mn in READS | STORES | RMW:
        return 'bus'
    if mn in READS and ((mode in ('abx', 'aby') and arg & 0xFF) or mode == 'izy'):
        return 'DC_EA?'
    return ''


def report(rom, name, info, table, by_addr):
    entries = {r['addr'] for r in table.values()}
    code = walk(rom, info['bank'], info['addr'], entries)
    src, line = body(info['file'], info['func'])
    marks = {}
    for m in MARK.finditer(src):
        marks.setdefault(int(m.group(2), 16), set()).add(m.group(1))
    timed = 'timed' if info['timed'] else 'not timed'
    print('%s $%04X (bank %d) in src/decomp/%s:%d, %s' % (name, info['addr'], info['bank'], info['file'], line, timed))
    missing, used = 0, set()
    for pc in sorted(code):
        mn, mode, arg = code[pc]
        if mode == 'rel':
            text = '%s $%04X' % (mn, target(pc, arg))
        else:
            text = mn + (' ' + FMT[mode] % arg if FMT.get(mode) else '')
        need, note = needs(mn, mode, arg), ''
        if need in ('DC_BR', 'DC_JUMP'):
            have = need in marks.get(pc, ())
            used.add(pc)
            missing += not have
            note = '%s(0x%04X) %s' % (need, pc, 'ok' if have else 'MISSING')
        elif need == 'DC_EA?':
            used.add(pc)
            note = 'DC_EA(0x%04X) %s' % (pc, 'present' if 'DC_EA' in marks.get(pc, ()) else 'if it can cross a page')
        elif need == 'bus':
            note = 'bus operation'
        elif mn == 'JSR':
            callee = by_addr.get(arg)
            note = (
                'calls %s (%s)' % (callee, 'timed' if table[callee]['timed'] else 'not timed')
                if callee
                else 'calls original code'
            )
        elif mn == 'JMP' and arg in entries and arg != info['addr']:
            note = 'continues into %s' % by_addr.get(arg, '?')
        cyc = cycles(mn, mode) if mn != '???' else 0
        plus = '+' if need in ('DC_BR', 'DC_EA?') else ' '
        print('  $%04X  %-16s %d%s  %s' % (pc, text, cyc, plus, note))
    extra = sorted(set(marks) - used)
    tail = ''
    if extra:
        tail = ', marks outside its own code (a callee or inlined code): ' + ' '.join('$%04X' % pc for pc in extra)
    print('  %d mark(s) missing%s' % (missing, tail))
    return missing


def summary(rom, table, only):
    entries = {r['addr'] for r in table.values()}
    print('%-28s%-8s%-22s%-7s%9s%9s' % ('routine', 'addr', 'file', 'timed', 'branches', 'missing'))
    for name, r in sorted(table.items(), key=lambda kv: (kv[1]['file'], kv[1]['addr'])):
        if only and r['file'] != only:
            continue
        code = walk(rom, r['bank'], r['addr'], entries)
        src, _ = body(r['file'], r['func'])
        marked = {int(m.group(2), 16) for m in MARK.finditer(src) if m.group(1) == 'DC_BR'}
        branches = [pc for pc, c in code.items() if c[0] in BRANCHES]
        missing = len([pc for pc in branches if pc not in marked])
        timed = 'yes' if r['timed'] else 'no'
        print('%-28s$%04X   %-22s%-7s%9d%9d' % (name, r['addr'], r['file'], timed, len(branches), missing))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('routine', nargs='?', help='a routine name, or $ADDR')
    ap.add_argument('--bank', type=int, help='the bank at $8000 for an address below $C000')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--summary', action='store_true', help='every routine: timed, branches and missing DC_BR')
    ap.add_argument('--file', help='with --summary: only the routines of this file (system.c, objects/walkers.c, ...)')
    args = ap.parse_args()
    rom, table = Rom(args.rom), routines()
    by_addr = {}
    for name, r in table.items():
        by_addr.setdefault(r['addr'], name)
    if args.summary:
        summary(rom, table, args.file)
        return 0
    if not args.routine:
        ap.error('name a routine, or use --summary')
    if args.routine in table:
        return 1 if report(rom, args.routine, table[args.routine], table, by_addr) else 0
    addr = int(args.routine.lstrip('$'), 16)
    names = [n for n, r in table.items() if r['addr'] == addr and (args.bank is None or r['bank'] == args.bank)]
    if names:
        return 1 if report(rom, names[0], table[names[0]], table, by_addr) else 0
    info = {'addr': addr, 'bank': args.bank or 0, 'file': 'system.c', 'func': '', 'timed': False}
    return 1 if report(rom, '$%04X' % addr, info, table, by_addr) else 0


if __name__ == '__main__':
    sys.exit(main())
