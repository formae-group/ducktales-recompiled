"""Whether files carry any of the ROM: the leak scan of the build with no ROM (docs/ROMLESS.md).

Looks, at every byte offset of each file and of each member of a zip, for a
32-byte run of the PRG with at least 8 distinct byte values (runs of fill and
padding are left out). The runs come from one of:

  --rom ROM          every run of the PRG, at every offset: any copy of 32
                     bytes or more is found
  --windows FILE     tests/rom_windows.txt, the BLAKE2b-64 digests of the runs
                     at every 16th offset of the PRG: any copy of 47 bytes or
                     more is found, and no ROM is needed (the release workflow)

    py -3 -I tools/rom_scan.py --rom ROM FILE...
    py -3 -I tools/rom_scan.py --windows tests/rom_windows.txt FILE...
    py -3 -I tools/rom_scan.py --rom ROM --write-windows tests/rom_windows.txt

The digests carry no ROM byte. Exit status 1 when any file holds a run.
"""

import argparse
import hashlib
import sys
import zipfile
from pathlib import Path

RUN = 32
STRIDE = 16
DISTINCT = 8
HEADER = 16


def digest(run):
    return hashlib.blake2b(run, digest_size=8).digest()


def prg_runs(rom, stride):
    data = Path(rom).read_bytes()
    prg = data[HEADER + (512 if data[6] & 4 else 0) :][: data[4] * 0x4000]
    for i in range(0, len(prg) - RUN + 1, stride):
        run = prg[i : i + RUN]
        if len(set(run)) >= DISTINCT:
            yield i, digest(run)


def read_windows(path):
    lines = Path(path).read_text().splitlines()
    return {bytes.fromhex(line) for line in lines if line and not line.startswith('#')}


def scan(name, data, runs):
    hits = []
    for i in range(len(data) - RUN + 1):
        if digest(data[i : i + RUN]) in runs:
            hits.append(i)
            if len(hits) == 5:
                break
    print(f'{name}: {len(data):,} bytes, ' + (f'PRG runs at {", ".join(map(hex, hits))}' if hits else 'clean'))
    return not hits


def main():
    ap = argparse.ArgumentParser()
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument('--rom')
    source.add_argument('--windows')
    ap.add_argument('--write-windows', metavar='FILE', help='with --rom: write the digests at every 16th offset')
    ap.add_argument('files', nargs='*')
    args = ap.parse_args()
    if args.write_windows:
        if not args.rom:
            sys.exit('--write-windows needs --rom')
        rows = [d.hex() for _, d in prg_runs(args.rom, STRIDE)]
        text = '# tools/rom_scan.py: BLAKE2b-64 of the 32-byte PRG runs at every 16th offset (no ROM byte)\n'
        Path(args.write_windows).write_text(text + '\n'.join(rows) + '\n', newline='\n')
        print(f'{args.write_windows}: {len(rows)} runs')
    if not args.files:
        return 0
    runs = {d for _, d in prg_runs(args.rom, 1)} if args.rom else read_windows(args.windows)
    clean = True
    for f in map(Path, args.files):
        data = f.read_bytes()
        clean = scan(str(f), data, runs) and clean
        if zipfile.is_zipfile(f):
            with zipfile.ZipFile(f) as z:
                for member in z.infolist():
                    if not member.is_dir():
                        clean = scan(f'{f}!{member.filename}', z.read(member), runs) and clean
    return 0 if clean else 1


if __name__ == '__main__':
    sys.exit(main())
