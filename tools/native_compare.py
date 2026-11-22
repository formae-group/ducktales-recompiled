"""Compare the native runtime with the cycle host, frame by frame.

Both write one line per frame with --ram-hash-out (src/native/native_hash.h):
CPU RAM, nametables, palette, OAM and CHR RAM hashes, the cycle count and the
CPU registers at the frame's end. A golden file of tests/quick_verify.py
(its # header line is skipped) compares the same way.

    py -3 -I tools/native_compare.py CYCLE.txt NATIVE.txt [--fields ram,cycles,...] [--list N]

Prints, for each field, how many frames match and the first frame that does
not, then the first --list differing frames. Exit code 0 only when every
selected field matches on every frame both files have.
"""
import argparse
import sys

FIELDS = ['ram', 'nt', 'pal', 'oam', 'chr', 'cycles', 'PC', 'A', 'X', 'Y', 'S', 'P']


def load(path):
    out = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts or parts[0].startswith('#'):
                continue
            out.append((int(parts[0]), dict(p.split('=', 1) for p in parts[1:])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cycle')
    ap.add_argument('native')
    ap.add_argument('--fields', default=','.join(FIELDS))
    ap.add_argument('--list', type=int, default=5)
    a = ap.parse_args()
    fields = a.fields.split(',')
    cyc, nat = load(a.cycle), load(a.native)
    n = min(len(cyc), len(nat))
    first = {k: None for k in fields}
    match = {k: 0 for k in fields}
    bad = []
    for i in range(n):
        fc, c = cyc[i]
        fn, d = nat[i]
        if fc != fn:
            sys.exit(f'frame numbers differ at line {i + 1}: {fc} vs {fn}')
        diff = [k for k in fields if c.get(k) != d.get(k)]
        for k in fields:
            if k in diff:
                if first[k] is None:
                    first[k] = fc
            else:
                match[k] += 1
        if diff:
            bad.append((fc, diff, c, d))
    print(f'frames compared: {n} (cycle {len(cyc)}, native {len(nat)})')
    for k in fields:
        print(f'  {k:6s} {match[k]:6d}/{n} match' + (f', first difference at frame {first[k]}' if first[k] is not None else ''))
    for fc, diff, c, d in bad[:a.list]:
        print(f'frame {fc}: ' + ' '.join(f'{k}={c.get(k)}/{d.get(k)}' for k in diff))
    print('IDENTICAL' if not bad and n else f'DIFFERENT on {len(bad)} frames')
    sys.exit(0 if not bad and n else 1)


if __name__ == '__main__':
    main()
