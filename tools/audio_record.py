"""Play sound ids from a save state and measure them (docs/audio.md).

For each id the state's request ring ($C6-$CF) is patched so the next NMI hands the id
to the driver ($8003), the headless build runs with --wav-out, and the WAV is reduced
to per-frame loudness (frame 0 = first frame after the state). With --ram the run also writes a short --frame-log and reports
the driver bytes that show who owns the channels; the log is deleted afterwards.

    py -3 -I tools/audio_record.py STATE IDS [--frames N] [--stop-music] [--ram N]
                                   [--loop I:P] [--keep] [--out captures/audio]

IDS: hex ids and ranges, e.g. 05 or 10-37 or 00,07,10-12; A+B queues A then B in the same
frame (priority tests). --stop-music queues $FF
(stop music) first, so effects play alone. WAVs stay under captures/ and are never
committed; they are deleted unless --keep is given. --loop I:P saves the machine at
frames I and I+P and compares the sequencer state of the four music channels (stream
pointer, note time left, loop counter, period: bytes 0-3, 5, 10-11 of each record at
$0370 + 31 x channel); identical state means the song repeats with period P.
"""

import argparse
import math
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from framelog import frames as log_frames  # noqa: E402
from world_poke import ram_offset  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfile import default_rom  # noqa: E402

SPF = 800  # 48000 Hz / 60 frames
QUIET = 150  # per-frame RMS below this counts as silence


def parse_ids(text):
    out = []
    for part in text.split(','):
        if '-' in part:
            a, b = (int(x, 16) for x in part.split('-'))
            out.extend([n] for n in range(a, b + 1))
        else:
            out.append([int(x, 16) for x in part.split('+')])
    return out


def patch(state, out, requests, pokes=()):
    data = bytearray(state.read_bytes())
    ram = ram_offset(data)
    for addr, val in pokes:
        data[ram + addr] = val
    ring = list(requests) + [0x88] * (8 - len(requests))
    data[ram + 0xC6] = len(requests) & 7
    data[ram + 0xC7] = 0
    data[ram + 0xC8 : ram + 0xD0] = bytes(ring)
    out.write_bytes(bytes(data))


def loudness(path):
    with wave.open(str(path)) as w:
        raw = w.readframes(w.getnframes())
    n = len(raw) // 2
    samples = struct.unpack('<%dh' % n, raw[: n * 2])
    rms = []
    for i in range(0, n - SPF + 1, SPF):
        chunk = samples[i : i + SPF]
        mean = sum(chunk) / SPF
        rms.append(math.sqrt(sum((s - mean) ** 2 for s in chunk) / SPF))
    return rms


def remove(path):
    for _ in range(20):
        try:
            path.unlink()
            return
        except PermissionError:
            time.sleep(0.5)
    print('could not delete %s' % path)


def channel_state(path):
    data = Path(path).read_bytes()
    ram = ram_offset(data)
    out = []
    for c in range(4):
        rec = data[ram + 0x370 + 0x1F * c : ram + 0x370 + 0x1F * (c + 1)]
        out.append(bytes(rec[i] for i in (0, 1, 2, 3, 5, 10, 11)))
    return out


def spans(active):
    out, start = [], None
    for i, a in enumerate(active + [False]):
        if a and start is None:
            start = i
        elif not a and start is not None:
            out.append((start, i - 1))
            start = None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state', type=Path)
    ap.add_argument('ids')
    ap.add_argument('--frames', type=int, default=240)
    ap.add_argument('--stop-music', action='store_true')
    ap.add_argument('--ram', type=int, default=0, help='frame-log this many frames for driver bytes')
    ap.add_argument('--loop', help='I:P intro and period in frames')
    ap.add_argument('--poke', default='', help='extra RAM pokes ADDR=VAL,... (hex) before the run')
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('--out', type=Path, default=ROOT / 'captures' / 'audio')
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'headless' / 'Release' / 'ducktales.exe'))
    ap.add_argument('--rom', default=default_rom())
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    idle = a.out / 'idle.txt'
    idle.write_text('0 -\n')
    start = struct.unpack_from('<I', a.state.read_bytes(), 28)[0]
    for group in parse_ids(a.ids):
        tag = '+'.join('%02X' % s for s in group)
        st, wav, log = a.out / ('req_%s.state' % tag), a.out / ('id_%s.wav' % tag), a.out / ('id_%s.bin' % tag)
        patch(
            a.state,
            st,
            ([0xFF] if a.stop_music else []) + group,
            [tuple(int(x, 16) for x in p.split('=')) for p in a.poke.split(',') if p],
        )
        cmd = [
            a.exe,
            a.rom,
            '--load-state',
            str(st),
            '--frames',
            str(start + a.frames),
            '--input',
            str(idle),
            '--wav-out',
            str(wav),
        ]
        if a.loop:
            i, p = (int(x) for x in a.loop.split(':'))
            s1, s2 = a.out / ('loop_%s_a.state' % tag), a.out / ('loop_%s_b.state' % tag)
            cmd += ['--save-state', '%d:%s' % (start + i, s1), '--save-state', '%d:%s' % (start + i + p, s2)]
        if a.ram:
            cmd += ['--frame-log', str(log), '--frame-log-frames', '%d:%d' % (start, start + a.ram - 1)]
        subprocess.run(cmd, check=True, capture_output=True)
        rms = loudness(wav)
        act = [r >= QUIET for r in rms]
        sp = spans(act)
        line = '%s  peak %5d  active %s' % (
            tag,
            int(max(rms) if rms else 0),
            ' '.join('%d-%d' % s for s in sp[:6]) + (' ...' if len(sp) > 6 else ''),
        )
        if a.loop:
            same = [x == y for x, y in zip(channel_state(s1), channel_state(s2))]
            line += '  state at %d vs %d: %s' % (i, i + p, ' '.join('same' if s else 'diff' for s in same))
            s1.unlink()
            s2.unlink()
        if a.ram:
            rows = []
            for i, (_, ram, _, _) in enumerate(log_frames(log)):
                ptrs = ''.join('m' if ram[0x370 + 0x1F * c] | ram[0x371 + 0x1F * c] else '.' for c in range(4))
                rows.append('D0=%02X DF=%X ch=%s' % (ram[0xD0], ram[0xDF] & 0xF, ptrs))
            changes = [(i, r) for i, r in enumerate(rows) if i == 0 or r != rows[i - 1]]
            line += '\n    ' + '; '.join('f%d %s' % c for c in changes[:12])
        print(line, flush=True)
        st.unlink()
        if log.exists():
            log.unlink()
        if not a.keep:
            remove(wav)


if __name__ == '__main__':
    main()
