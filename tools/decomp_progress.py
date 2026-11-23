"""How much of the code the game runs is decompiled C (docs/decompilation.md).

    py -3 -I tools/decomp_progress.py [--golden DIR] [--rev REV] [--top N]
    py -3 -I tools/decomp_progress.py --record-pokes [--dev-build build/headless-dev] [--golden DIR]

The measure is executed PRG bytes: every instruction the routes of tests/routes/ and the poke cases
of tests/decomp_test.py run, counted once with its length, and the share of them inside a routine
that has C (a DecompRoutine table entry of src/decomp/, read from the working tree or from --rev).

Executed code. The routes' coverage is quick_verify's (<golden>/<ROM SHA-256>/<route>.cover: the
instruction starts an --interp-only run executes). The poke cases are not routes: --record-pokes
runs each one from the latest golden state of their route at or before its first poke or press,
with --interp-only --miss-log, and writes <golden>/<ROM SHA-256>/decomp_pokes.cover.

Routine extents. A DecompRoutine has an entry and no end, so the end is derived from the ROM.

- Entries: the DecompRoutines (their bank from .bank, bank 7 at $C000 and above), the original
  routines the C calls or hands off to (the constant address argument of Decomp_Call,
  Decomp_CallP, Decomp_Handoff and flow.c's Call, Jsr and Jump), the vectors, every target of an
  executed JSR and every executed instruction no executed instruction flows into (dispatch targets).
- A routine's own code is what its entry reaches by fall-through, branches and JMP abs without
  entering another entry. It stops at RTS, RTI, BRK, JMP (ind), a JSR to a callee that never
  returns (no executed JSR to it is followed by the next instruction: the jump-table dispatchers),
  a jump from the fixed bank into the switchable window (bank unknown) and the end of a bank; other
  JSRs go on after the call.
- A dispatch target belongs to the routines whose code holds a table naming it (indexed loads of a
  word or split table, or the words after a dispatcher JSR), else to every routine of its bank
  that executes an indirect jump or a PHA/RTS.
- Code the original still runs: what the vectors, the unowned dispatch targets and the routines
  the C calls reach, never entering a routine with C. The C also hands every callee of a routine
  that can give a call back to the original (Decomp_RunOriginal, or Decomp_Call on a computed
  address) to the original; a routine whose replace mode always does (Decomp_Mode() ==
  DECOMP_REPLACE) does not count as C at all.
- Code with C: a C routine's own code and the routines only it reaches. Code both sides reach
  (a branch into another routine's tail) counts as C; the report gives how much.

Measured on the current routes, this is a lower bound where the C is open (above) and depends on
the executed code: a routine no route or poke runs adds nothing to either side.
"""

import argparse
import concurrent.futures
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import decomp_test  # noqa: E402
import quick_verify  # noqa: E402
from disasm6502 import MODES, OPS  # noqa: E402
from romfile import default_rom  # noqa: E402

GROUPS = {
    'player': ('player', 'motion', 'sound'),
    'objects': ('objects', 'objcommon'),
    'flow': ('flow',),
    'level': ('level', 'collision'),
    'system': ('system', 'sprites', 'audio'),
}
BRANCHES = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'}
ENDS = {'RTS', 'RTI', 'BRK', 'JMP'}
DEFINE = re.compile(r'^#define (DT_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b(.*)$', re.M)
TABLE = re.compile(r'^const DecompRoutine g\w+\[\] = \{(.*?)^\};', re.M | re.S)
ENTRY = re.compile(r'\{\s*(\w+)\s*,\s*(?:NULL|"[^"]*")\s*,\s*"(\w+)"([^}]*)\}')
MACRO = re.compile(
    r'\b(?:ROUTINE|TASK)\(\s*(\w+)\s*,\s*(?:NULL|"[^"]*")\s*,\s*"(\w+)"|\bRESUME\(\s*(\w+)\s*,\s*"(\w+)"'
)
CALLED = re.compile(r'\b(?:Decomp_Call|Decomp_CallP|Decomp_Handoff|Call|Jsr|Jump)\(\s*(?:r\s*,\s*)?(DT_\w+)')
POKES = 'decomp_pokes.cover'
FUNCTION = re.compile(r'^\w[\w *]*?\b(\w+)\(([^;{]*?)\)\s*\{(.*?)^\}', re.M | re.S)
CALL_ARG = re.compile(r'\bDecomp_CallP?\(\s*([^,]+?)\s*,')
LOADS = {'LDA', 'LDX', 'LDY'}
ENTER = re.compile(r'\bDecomp_Enter\(\s*(DT_\w+)')


def source(rev, path):
    if not rev:
        return (ROOT / path).read_text(encoding='utf-8')
    return subprocess.run(['git', 'show', f'{rev}:{path}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def decomp_sources(rev):
    if not rev:
        return {p.stem: p.read_text(encoding='utf-8') for p in sorted((ROOT / 'src' / 'decomp').glob('*.c'))}
    names = subprocess.run(
        ['git', 'ls-tree', '--name-only', rev, 'src/decomp/'], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.split()
    return {Path(n).stem: source(rev, n) for n in names if n.endswith('.c')}


class Code:
    def __init__(self, rom):
        data = Path(rom).read_bytes()
        self.prg = data[16 : 16 + data[4] * 0x4000]
        self.fixed = data[4] - 1

    def offset(self, bank, addr):
        if addr >= 0xC000:
            return self.fixed * 0x4000 + addr - 0xC000
        if 0x8000 <= addr and bank is not None and bank != self.fixed:
            return bank * 0x4000 + addr - 0x8000
        return None

    def cpu(self, off):
        bank = off >> 14
        return bank, (0xC000 if bank == self.fixed else 0x8000) + (off & 0x3FFF)

    def decode(self, off):
        """(mnemonic, mode, length, operand, offsets it flows to, JSR target or None); None if no opcode."""
        op = OPS.get(self.prg[off])
        if not op:
            return None
        mn, mode = op
        n = 1 + MODES[mode]
        if (off + n - 1) >> 14 != off >> 14:
            return None
        arg = int.from_bytes(self.prg[off + 1 : off + n], 'little')
        bank, pc = self.cpu(off)
        flow = [] if mn in ENDS or (off + n) >> 14 != bank else [off + n]
        if mn in BRANCHES:
            flow.append(self.offset(bank, (pc + n + (arg - 256 if arg & 0x80 else arg)) & 0xFFFF))
        if mn == 'JMP' and mode == 'abs':
            flow.append(self.offset(bank, arg))
        return mn, mode, n, arg, [o for o in flow if o is not None], arg if mn == 'JSR' else None


def pokes_key():
    return hashlib.sha256((repr(decomp_test.POKES) + decomp_test.POKE_ROUTE.read_text()).encode()).hexdigest()[:16]


def read_pokes(path):
    if not path.exists():
        sys.exit(f'no {path}; record it with --record-pokes')
    lines = path.read_text().splitlines()
    if f'key={pokes_key()} ' not in lines[0]:
        sys.exit(f'{path} is for other poke cases; record it again with --record-pokes')
    out = {}
    for line in lines[1:]:
        if line.strip():
            name, _, *spans = line.split()
            out[name] = set()
            for s in spans:
                a, _, b = s.partition('-')
                out[name].update(range(int(a, 16), int(b or a, 16) + 1))
    return out


def record_pokes(args, golden_dir):
    exe = ROOT / args.dev_build / args.config / 'ducktales.exe'
    if not exe.exists():
        sys.exit(f'missing {exe} (configure with -DDUCKTALES_HEADLESS=ON -DDUCKTALES_DEV_DIAGNOSTICS=ON)')
    route = decomp_test.POKE_ROUTE
    states = [int(p.stem) for p in (golden_dir / f'{route.stem}.states').glob('*.state')]
    cases = [decomp_test.poke_case(*p) for p in decomp_test.POKES]
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:

        def run(case):
            frames = [int(a.split(':')[0]) for a in case['extra'][1::2]] + list(case.get('presses', {}))
            start = max((s for s in states if s <= min(frames)), default=0)
            miss = Path(tmp) / f'{case["name"]}.miss.txt'
            cmd = [str(exe), args.rom, '--input', str(case['route']), '--frames', str(case['frames'])]
            cmd += ['--align', '0', '--interp-only', '--miss-log', str(miss), *case['extra']]
            if start:
                cmd += ['--load-state', str(quick_verify.state_path(golden_dir, route, start))]
            p = subprocess.run(cmd, capture_output=True, text=True)
            if p.returncode or not miss.exists():
                sys.exit(f'{case["name"]}: exit {p.returncode}\n{p.stderr[-2000:]}')
            return case['name'], start, quick_verify.read_miss_log(miss)

        for case in cases:
            if 'presses' in case:
                case['route'] = decomp_test.pressed_route(case, tmp)
        done = list(pool.map(run, cases))
    with open(golden_dir / POKES, 'w') as f:
        f.write(
            f'# decomp_progress poke coverage key={pokes_key()} route={route.stem}: per case, the frame its run '
            'starts from and the PRG ROM offsets of the instructions it executes\n'
        )
        for name, start, offsets in done:
            f.write(f'{name} {start} {quick_verify.write_ranges(offsets)}\n')
    print(f'recorded {len(done)} poke cases in {golden_dir / POKES}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--golden', default=str(ROOT / 'build' / 'golden'), help='quick_verify golden root')
    ap.add_argument('--rev', help='read src/decomp/ and the symbols from this git revision')
    ap.add_argument('--top', type=int, default=15, help='still-original routines to list')
    ap.add_argument('--record-pokes', action='store_true')
    ap.add_argument('--dev-build', default='build/headless-dev')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--jobs', type=int, default=2)
    args = ap.parse_args()
    golden_dir = Path(args.golden) / quick_verify.file_sha256(Path(args.rom))
    if args.record_pokes:
        return record_pokes(args, golden_dir)
    code = Code(args.rom)

    executed = set()
    routes = sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    for route in routes:
        cover = golden_dir / f'{route.stem}.cover'
        if not cover.exists():
            sys.exit(f'{route.stem}: no coverage in {golden_dir}; record it with tests/quick_verify.py --record')
        for _, offsets in quick_verify.read_cover(cover):
            executed |= offsets
    pokes = read_pokes(golden_dir / POKES)
    for offsets in pokes.values():
        executed |= offsets
    insn = {o: code.decode(o) for o in executed}
    bad = sorted(o for o, d in insn.items() if d is None)
    if bad:
        sys.exit('executed offsets that are no instruction: ' + ' '.join(f'{o:05X}' for o in bad[:10]))
    size = {o: d[2] for o, d in insn.items()}

    def jsr_targets(off, addr):
        bank = off >> 14
        if addr >= 0xC000 or bank != code.fixed:
            return [o for o in [code.offset(bank, addr)] if o is not None]
        return [o for b in range(code.fixed) if (o := code.offset(b, addr)) in executed]

    flowed, returns = set(), {}
    for o, (mn, mode, n, arg, flow, call) in insn.items():
        flowed.update(flow)
        if call is not None:
            for t in jsr_targets(o, call):
                flowed.add(t)
                returns[t] = returns.get(t, False) or o + n in executed
    noreturn = {t for t, back in returns.items() if not back}

    symbols = source(args.rev, 'src/game/ducktales_symbols.h')
    sym = {m[1]: int(m[2], 0) for m in DEFINE.finditer(symbols)}
    sym_bank = {
        m[1]: int(k[1])
        for m in DEFINE.finditer(symbols)
        if (k := re.match(r'DT_B(\d+)_', m[1]) or re.search(r'bank (\d+)', m[3]))
    }
    sources = decomp_sources(args.rev)
    c_entries, placed, hand, by_original, partial = {}, {}, set(), set(), set()
    for stem, text in sources.items():
        local = sym | {m[1]: m[2] for m in re.finditer(r'^#define (\w+)[ \t]+(\w+)[ \t]*(?:/\*.*)?$', text, re.M)}

        def value(token):
            while isinstance(token, str) and token in local:
                token = local[token]
            return token if isinstance(token, int) else int(token, 0)

        for block in TABLE.findall(text):
            found = [(m[1], m[2], re.search(r'\.bank\s*=\s*(\w+)', m[3])) for m in ENTRY.finditer(block)]
            found += [(m[1] or m[3], m[2] or m[4], None) for m in MACRO.finditer(block)]
            for token, name, bank in found:
                off = code.offset(value(bank[1]) if bank else None, value(token))
                if off is None:
                    sys.exit(f'{stem}.c: {name} at ${value(token):04X} has no bank')
                c_entries[off] = (stem, name)
                placed[token] = [off]

    def place(token):
        if token in placed:
            return placed[token]
        if sym[token] >= 0xC000 or token in sym_bank:
            return [code.offset(sym_bank.get(token), sym[token])]
        return jsr_targets(code.fixed << 14, sym[token])

    for stem, text in sources.items():
        if not TABLE.search(text):
            continue
        hand.update(o for token in CALLED.findall(text) if token in sym for o in place(token))
        functions = {m[1]: (m[2], m[3]) for m in FUNCTION.finditer(text)}
        for fn, (params, body) in functions.items():
            own = set(re.findall(r'(\w+)\s*(?:,|$)', params))
            variable = [a for a in CALL_ARG.findall(body) if not a.startswith('DT_') and a not in own]
            if 'Decomp_RunOriginal()' not in body and not variable:
                continue
            names, seen, routines = {fn}, set(), []
            while names and not routines:
                seen |= names
                routines = [t for n in names for t in ENTER.findall(functions.get(n, ('', ''))[1])]
                names = {c for c, (_, b) in functions.items() if c not in seen and any(f'{n}(' in b for n in names)}
            for token in routines:
                (by_original if 'DECOMP_REPLACE' in body else partial).update(place(token))
    vectors = {code.offset(None, int.from_bytes(code.prg[-6 + 2 * i :][:2], 'little')) for i in range(3)} - {None}
    orphans = executed - flowed
    entries = set(c_entries) | hand | set(returns) | orphans | vectors
    replaced_by_original = sorted(v[1] for e, v in c_entries.items() if e in by_original)
    c_entries = {e: v for e, v in c_entries.items() if e not in by_original}

    def walk(entry):
        """The routine's executed instructions, the entries they go to and the orphans its dispatch tables name."""
        seen, exits, loads, tables, todo = set(), set(), set(), set(), [(entry, None)]
        indirect = False
        while todo:
            o, src = todo.pop()
            if o != entry and o in entries:
                if src in executed:
                    exits.add(o)
                continue
            d = o not in seen and (insn.get(o) or code.decode(o))
            if not d:
                continue
            seen.add(o)
            mn, mode, n, arg, flow, call = d
            targets = jsr_targets(o, call) if call is not None else []
            stop = any(t in noreturn for t in targets)
            if o in executed:
                exits.update(targets)
                indirect |= mode == 'ind' or (mn == 'RTS' and o - 1 in executed and code.prg[o - 1] == 0x48)
                if mn in LOADS and mode in ('abx', 'aby') and (t := code.offset(o >> 14, arg)) is not None:
                    loads.add(t)
                if stop:
                    tables.update(code.prg[o + 3 + i] | code.prg[o + 4 + i] << 8 for i in range(0, 256, 2))
            todo += [(t, o) for t in ([] if stop else flow)]
        for lo in loads:
            for hi in loads:
                if 0 < hi - lo <= 256 or 0 < lo - hi <= 256:
                    tables.update(
                        code.prg[lo + i] | code.prg[hi + i] << 8 for i in range(min(256, len(code.prg) - max(lo, hi)))
                    )
        bank = entry >> 14
        named = {o for a in tables for o in (code.offset(bank, a), code.offset(bank, (a + 1) & 0xFFFF)) if o in orphans}
        return seen & executed, (exits | named) - {entry}, named, indirect

    walks = {e: walk(e) for e in entries}
    dispatched = set().union(*(w[2] for w in walks.values()))
    for o in orphans - dispatched:
        for e, w in walks.items():
            if w[3] and e >> 14 == o >> 14 and e != o:
                w[1].add(o)
                dispatched.add(o)
    seeds = [e for e in (vectors | (orphans - dispatched) | hand | by_original) if e not in c_entries]
    seeds += [x for p in partial if p in walks for x in walks[p][1] if x not in c_entries]
    run_original, todo = set(), seeds
    while todo:
        e = todo.pop()
        if e not in run_original and e not in c_entries:
            run_original.add(e)
            todo += walks[e][1]

    def c_reach(entry):
        found, todo = set(), [entry]
        while todo:
            e = todo.pop()
            if e not in found and (e == entry or (e not in c_entries and e not in run_original)):
                found.add(e)
                todo += walks[e][1]
        return set().union(*(walks[e][0] for e in found))

    def nbytes(offsets):
        return sum(size[o] for o in offsets)

    reach = {e: c_reach(e) for e in c_entries}
    in_c = set().union(*reach.values())
    original = set().union(*(walks[e][0] for e in run_original))
    total, c = nbytes(executed), nbytes(in_c)
    print(
        f'executed: {len(routes)} routes and {len(pokes)} poke cases, {total:,} PRG bytes '
        f'({len(executed):,} instructions); {len(c_entries)} routines with C' + (f' at {args.rev}' if args.rev else '')
    )
    print(f'{"group":8} {"routines":>8} {"bytes":>8} {"of all":>7}')
    for group, stems in GROUPS.items():
        mine = [e for e, (stem, _) in c_entries.items() if stem in stems]
        b = nbytes(set().union(*(reach[e] for e in mine)))
        print(f'{group:8} {len(mine):8} {b:8,} {100 * b / total:6.1f}%')
    print(f'{"total":8} {len(c_entries):8} {c:8,} {100 * c / total:6.1f}%  ({c:,} of {total:,} executed bytes)')
    print(f'shared: {nbytes(in_c & original):,} of those bytes are also reached by original routines')
    print(f'in no routine: {nbytes(executed - in_c - original):,} bytes')
    if replaced_by_original:
        print('not counted (replace mode gives every call to the original): ' + ', '.join(replaced_by_original))
    names = {}
    for m in DEFINE.finditer(symbols):
        bank = re.match(r'DT_B(\d+)_', m[1]) or re.search(r'bank (\d+)', m[3])
        off = code.offset(int(bank[1]) if bank else None, int(m[2], 0))
        if m[1].startswith(('DT_CODE_', 'DT_B')) and off is not None:
            names.setdefault(off, m[1][3:])
    rows = sorted((-nbytes(walks[e][0] - in_c), e) for e in run_original)
    print('largest still-original routines (executed bytes outside C):')
    for b, e in rows[: args.top]:
        bank, addr = code.cpu(e)
        print(f'  {bank}:${addr:04X} {names.get(e, ""):30} {-b:6,}')


if __name__ == '__main__':
    sys.exit(main())
