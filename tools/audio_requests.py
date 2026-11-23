"""List every sound request a route makes (docs/audio.md).

Runs the headless build on an input route with --frame-log in chunks (deleted after
reading, so disk use stays near 130 KB x CHUNK) and prints each new entry of the
request ring $C8-$CF: the write index $C6 advances when $F752/$F754 queue an id, and
the frame log, taken at the end of the frame, still holds the id because the NMI only
drains the ring at the start of the next frame. Context columns: $C5 (last music),
$F6 (land), $39 (Scrooge's mode), $0620 (his animation state), $E6 (pause).

    py -3 -I tools/audio_requests.py ROUTE --frames N [--first A] [--chunk 500]
                                     [--load-state STATE] [--callers]

--callers re-runs the first request of each id with --trace-frame and reports the
instruction that called $F752/$F754 (the INSN record before the entry), its X register
(the object slot for object code) with that slot's $0620 type at the end of the frame,
and the PRG bank last written to the bank register in that frame.
"""

import tempfile
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framelog import frames as log_frames  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfile import default_rom  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('route')
    ap.add_argument('--frames', type=int, required=True)
    ap.add_argument('--first', type=int, default=0)
    ap.add_argument('--chunk', type=int, default=500)
    ap.add_argument('--load-state')
    ap.add_argument('--callers', action='store_true')
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'headless' / 'Release' / 'ducktales.exe'))
    ap.add_argument('--rom', default=default_rom())
    a = ap.parse_args()
    out = ROOT / 'captures'
    out.mkdir(exist_ok=True)
    log = out / 'audio_requests.bin'
    prev = None
    counts = {}
    first_seen = {}
    for lo in range(a.first, a.frames, a.chunk):
        hi = min(lo + a.chunk, a.frames) - 1
        cmd = [
            a.exe,
            a.rom,
            '--frames',
            str(hi + 1),
            '--input',
            a.route,
            '--frame-log',
            str(log),
            '--frame-log-frames',
            '%d:%d' % (lo, hi),
        ]
        if a.load_state:
            cmd += ['--load-state', a.load_state]
        subprocess.run(cmd, check=True, capture_output=True)
        for frame, ram, _, _ in log_frames(log):
            if prev is not None and ram[0xC6] != prev[0xC6]:
                slot, new = prev[0xC6], []
                while slot != ram[0xC6]:
                    new.append(ram[0xC8 + slot])
                    slot = (slot + 1) & 7
                for sid in new:
                    counts[sid] = counts.get(sid, 0) + 1
                    first_seen.setdefault(sid, (frame, bytes(ram)))
                print(
                    '%6d  %s  C5=%02X F6=%02X 39=%02X 0620=%02X E6=%02X'
                    % (frame, ' '.join('%02X' % s for s in new), ram[0xC5], ram[0xF6], ram[0x39], ram[0x620], ram[0xE6])
                )
            prev = ram
        log.unlink()
    print('totals: ' + ' '.join('%02X:%d' % kv for kv in sorted(counts.items())))
    if a.callers:
        for sid, (frame, ram) in sorted(first_seen.items()):
            for pc, x, bank, req in callers(a, frame):
                if req == sid:
                    where = '7:%04X' % pc if pc >= 0xC000 else '%s:%04X' % (bank, pc)
                    print(
                        'caller %02X  frame %d  %s  X=%02X  $0620+X=%02X'
                        % (sid, frame, where, x, ram[0x620 + (x & 15)])
                    )


def callers(a, frame):
    with tempfile.TemporaryDirectory() as tmp:
        trace = Path(tmp) / 'trace.txt'
        cmd = [
            a.exe,
            a.rom,
            '--frames',
            str(frame + 1),
            '--input',
            a.route,
            '--trace-frame',
            str(frame),
            '--trace-out',
            str(trace),
        ]
        if a.load_state:
            cmd += ['--load-state', a.load_state]
        subprocess.run(cmd, check=True, capture_output=True)
        out, last, bank = [], None, '?'
        for line in trace.open():
            f = line.split()
            if len(f) >= 4 and f[1] == 'W' and int(f[2], 16) >= 0x8000:
                bank = str(int(f[3], 16) & 7)
            if len(f) >= 3 and f[1] == 'INSN':
                regs = dict(kv.split('=') for kv in f[2:])
                pc = int(regs['PC'], 16)
                if pc in (0xF752, 0xF754) and last and last[0] != 0xF752:
                    out.append((last[0], last[1], bank, int(regs['A'], 16)))
                last = (pc, int(regs['X'], 16))
        return out


if __name__ == '__main__':
    main()
