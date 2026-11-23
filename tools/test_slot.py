"""Run a command in one of a few machine-wide test slots, so the heavy test runs of several
worktrees take turns instead of all sharing the CPU at once.

    py -3 -I tools/test_slot.py [--slots 3] -- COMMAND [ARGS ...]

A slot is a byte-range lock on a file in %LOCALAPPDATA%\\ducktales-test-slots (DUCKTALES_SLOT_DIR
overrides it). Windows releases the lock when the holder exits, crashed or killed included, so a
slot is never left stuck. The command's exit code is returned; the wait and the run times go to
stderr. Every worktree must use the same --slots for the limit to hold.
"""

import argparse
import msvcrt
import os
import subprocess
import sys
import time
from pathlib import Path


def take(folder, slots):
    while True:
        for i in range(slots):
            f = open(folder / f'slot{i}.lock', 'a+b')
            try:
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                return i, f
            except OSError:
                f.close()
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser(usage='test_slot.py [--slots N] -- COMMAND ...')
    ap.add_argument('--slots', type=int, default=3)
    ap.add_argument('command', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        ap.error('no command')
    folder = Path(os.environ.get('DUCKTALES_SLOT_DIR') or Path(os.environ['LOCALAPPDATA']) / 'ducktales-test-slots')
    folder.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    slot, handle = take(folder, args.slots)
    waited = time.monotonic() - start
    print(f'test_slot: slot {slot} of {args.slots} after {waited:.0f} s', file=sys.stderr, flush=True)
    try:
        code = subprocess.call(command)
    finally:
        handle.close()
    print(f'test_slot: ran {time.monotonic() - start - waited:.0f} s, exit {code}', file=sys.stderr, flush=True)
    return code


if __name__ == '__main__':
    sys.exit(main())
