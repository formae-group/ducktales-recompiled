"""Native synth vs the cycle host's APU on the input routes (docs/audio.md, "Native synth").

For every selected route this records the cycle host (ducktales.exe --wav-out, the
cycle-level APU) and the native runtime with its synth (ducktales_native.exe --lag
faithful --wav-out, src/native/native_audio.c), both at 48 kHz from power-on with the
same input. Both runs also write --ram-hash-out; the native runtime's APU writes are
the cycle host's only if RAM and cycle count agree on every frame, which the report
checks first (as tests/native_routes.py does).

Each 800-sample block (one frame) of the two WAVs is then compared with measures that
do not depend on waveform phase, since the synth's pulse and noise phases are not
required to match the APU's sample for sample:

  level      |20 log10(rms_native / rms_apu)|, audible frames only (APU rms > 150)
  spectral   log-spectral distance: rms over 1/3-octave bands (50 Hz-12.5 kHz) of the
             dB difference of the band energies, 2048-sample Hann window per frame,
             bands more than 60 dB under the frame's loudest band floored there
  pitch      per tone channel (pulse 1, pulse 2, triangle): where the driver's channel
             record holds the same period for 5 frames, the spectral peak within half a
             semitone of the expected fundamental, found in both WAVs (4096-sample Hann
             window, 8x zero padding, parabolic interpolation) when the APU's peak
             stands 10 dB over the frame's median and no harmonic (1-8) of another
             tone channel lies within a semitone; the difference in cents

Frames are grouped by track: the music id in $C5 while $D0's low nibble says music is
playing, "no music" otherwise. A group passes when level median <= 0.5 dB and p95 <= 2 dB,
spectral median <= 3 dB and p95 <= 7 dB and every channel's pitch median <= 3 cents, or,
with --floor, when a value that misses its limit is still within 1.1 x the noise floor:
the same measures between two recordings of the APU itself at CPU/PPU alignments 0 and 1
(power-on alignment is random on a console, so either is what a player hears). A route's
floor counts only if the two APU recordings stayed in step (its own level and spectral
medians within the limits); on some full_* routes the timing change sends the game down
another path and the floor is not used. The pitch
p95 is reported but not judged: peaks in a mix of four channels make it as large in the
noise floor (docs/validation.md gives the numbers).

    py -3 -I tests/audio_compare.py --build build/headless [--routes R ...] [--jobs 2] [--keep]

Needs numpy. WAVs, RAM dumps and hash files go to captures/audio_compare and are deleted
unless --keep is given.
"""

import argparse
import subprocess
import sys
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
COMPARE = ROOT / 'tools' / 'native_compare.py'
RATE = 48000
SPF = 800
CPU_HZ = 21477272.0 / 12.0
QUIET = 150.0
DEFAULT_ROUTES = [
    'amazon_intro',
    'transylvania_intro',
    'african_mines_intro',
    'himalayas_intro',
    'moon_intro',
    'full_amazon',
    'full_transylvania',
    'full_african_mines',
    'full_himalayas',
    'full_moon',
]
LIMITS = {'level': (0.5, 2.0), 'spectral': (3.0, 7.0), 'pitch': (3.0, None)}
CHANNELS = ['pulse1', 'pulse2', 'triangle']
TRACKS = {
    0x00: 'The Amazon',
    0x01: 'Transylvania',
    0x02: 'African Mines',
    0x03: 'The Himalayas',
    0x04: 'The Moon',
    0x05: 'Land Select',
    0x06: 'Boss fight',
    0x07: 'Title',
    0x08: 'Invincibility',
    0x09: 'Game over',
    0x0A: 'Death',
    0x0B: 'Launchpad ride',
}


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def read_wav(path):
    with wave.open(str(path)) as w:
        return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)


def record(route, args, exe, native):
    out = Path(args.out)
    frames = str(route_frames(route))
    base = out / route.stem
    files = {
        k: Path(f'{base}.{k}') for k in ('apu.wav', 'native.wav', 'cycle.txt', 'native.txt', 'ram.bin', 'apu1.wav')
    }
    if not (args.reuse and files['apu.wav'].exists()):
        subprocess.run(
            [
                str(exe),
                args.rom,
                '--input',
                str(route),
                '--frames',
                frames,
                '--wav-out',
                str(files['apu.wav']),
                '--ram-hash-out',
                str(files['cycle.txt']),
            ],
            capture_output=True,
            check=True,
        )
    if args.floor and not (args.reuse and files['apu1.wav'].exists()):
        subprocess.run(
            [
                str(exe),
                args.rom,
                '--input',
                str(route),
                '--frames',
                frames,
                '--align',
                '1',
                '--wav-out',
                str(files['apu1.wav']),
            ],
            capture_output=True,
            check=True,
        )
    subprocess.run(
        [
            str(native),
            args.rom,
            '--input',
            str(route),
            '--frames',
            frames,
            '--lag',
            'faithful',
            '--wav-out',
            str(files['native.wav']),
            '--ram-hash-out',
            str(files['native.txt']),
            '--ram-dump',
            str(files['ram.bin']),
        ],
        capture_output=True,
        check=True,
    )
    same = (
        subprocess.run(
            [
                sys.executable,
                '-I',
                str(COMPARE),
                str(files['cycle.txt']),
                str(files['native.txt']),
                '--fields',
                'ram,cycles',
                '--list',
                '0',
            ],
            capture_output=True,
            text=True,
        ).returncode
        == 0
    )
    return route, files, same


def band_edges():
    centres = 1000.0 * 2.0 ** (np.arange(-13, 11) / 3.0)
    return [(c * 2 ** (-1 / 6), c * 2 ** (1 / 6)) for c in centres if 45 <= c <= 13000]


def band_energies(blocks, edges):
    win = np.hanning(2048)
    spec = np.abs(np.fft.rfft(blocks * win, axis=1)) ** 2
    freqs = np.fft.rfftfreq(2048, 1.0 / RATE)
    bands = np.stack([spec[:, (freqs >= lo) & (freqs < hi)].sum(1) for lo, hi in edges], axis=1)
    db = 10 * np.log10(bands + 1e-3)
    return np.maximum(db, db.max(1, keepdims=True) - 60)


def framed(signal, frames, size):
    pad = np.concatenate([np.zeros(size), signal, np.zeros(size)])
    centres = np.arange(frames) * SPF + SPF // 2 + size
    idx = centres[:, None] + np.arange(-size // 2, size // 2)[None, :]
    return pad[idx]


def channel_periods(ram):
    owner = ram[:, 0xDF].astype(int)
    out = []
    for ch in range(3):
        rec = 0x370 + 31 * ch
        music = ram[:, rec + 0x0A].astype(int) | (ram[:, rec + 0x0B].astype(int) << 8)
        effect = ram[:, rec + 0x11].astype(int) | (ram[:, rec + 0x12].astype(int) << 8)
        vib = (ram[:, rec + 0x1A].astype(int) | (ram[:, rec + 0x1B].astype(int) << 8)).astype(np.int16).astype(int)
        mine = ((owner >> ch) & 1).astype(bool)
        period = np.where(mine, effect, music) + vib
        playing = ((ram[:, rec] | ram[:, rec + 1]) != 0) | mine
        out.append(np.where(playing & (period >= 8) & (period <= 0x7FF), period, 0))
    return out


def peak(mag, freqs, f0):
    lo, hi = f0 * 2 ** (-50 / 1200), f0 * 2 ** (50 / 1200)
    sel = np.nonzero((freqs >= lo) & (freqs <= hi))[0]
    if len(sel) < 3:
        return None, -1e9
    i = sel[np.argmax(mag[sel])]
    a, b, c = np.log(mag[i - 1] + 1e-9), np.log(mag[i] + 1e-9), np.log(mag[i + 1] + 1e-9)
    d = 0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c < 0 else 0.0
    d = min(max(d, -0.5), 0.5)
    return (i + d) * (freqs[1] - freqs[0]), 20 * np.log10(mag[i] + 1e-9)


def pitch_errors(apu, nat, periods):
    size, nfft = 4096, 32768
    win = np.hanning(size)
    freqs = np.fft.rfftfreq(nfft, 1.0 / RATE)
    stable = []
    for p in periods:
        ok = p > 0
        for k in (1, 2):
            ok &= np.roll(p, k) == p
            ok &= np.roll(p, -k) == p
        ok[:2] = ok[-2:] = False
        stable.append(ok)
    fund = [np.where(p > 0, CPU_HZ / ((16 if ch < 2 else 32) * (p + 1)), 0.0) for ch, p in enumerate(periods)]
    for ch in range(3):
        for other in range(3):
            if other == ch:
                continue
            for h in range(1, 9):
                ratio = np.abs(np.log2(np.maximum(fund[other] * h, 1e-9) / np.maximum(fund[ch], 1e-9)))
                stable[ch] &= ~((fund[other] > 0) & (ratio < 100 / 1200))
    want = np.nonzero(np.any(stable, axis=0))[0]
    errors = {c: [] for c in CHANNELS}
    pads = {name: np.concatenate([np.zeros(size), sig, np.zeros(size)]) for name, sig in (('apu', apu), ('nat', nat))}
    offsets = np.arange(size)
    for start in range(0, len(want), 256):
        idx = want[start : start + 256]
        base = idx * SPF + SPF // 2 + size // 2
        spec = {
            name: np.abs(np.fft.rfft(pad[base[:, None] + offsets[None, :]] * win, n=nfft, axis=1))
            for name, pad in pads.items()
        }
        for row, f in enumerate(idx):
            floor = 20 * np.log10(np.median(spec['apu'][row]) + 1e-9)
            for ch, cname in enumerate(CHANNELS):
                if not stable[ch][f]:
                    continue
                f0 = CPU_HZ / ((16 if ch < 2 else 32) * (periods[ch][f] + 1))
                fa, la = peak(spec['apu'][row], freqs, f0)
                fn, _ = peak(spec['nat'][row], freqs, f0)
                if fa is None or fn is None or la < floor + 10:
                    continue
                errors[cname].append((int(f), 1200 * np.log2(fn / fa)))
    return errors


def analyse(route, files, same, other='native.wav'):
    apu, nat = read_wav(files['apu.wav']), read_wav(files[other])
    ram = np.fromfile(files['ram.bin'], dtype=np.uint8).reshape(-1, 0x800)
    frames = min(len(apu) // SPF, len(nat) // SPF, len(ram))
    apu, nat, ram = apu[: frames * SPF], nat[: frames * SPF], ram[:frames]
    a, n = apu.reshape(frames, SPF), nat.reshape(frames, SPF)
    ra, rn = np.sqrt((a**2).mean(1)), np.sqrt((n**2).mean(1))
    level = np.abs(20 * np.log10((rn + 1) / (ra + 1)))
    edges = band_edges()
    lsd = np.sqrt(
        ((band_energies(framed(apu, frames, 2048), edges) - band_energies(framed(nat, frames, 2048), edges)) ** 2).mean(
            1
        )
    )
    music = np.where(ram[:, 0xD0] & 0x0F, ram[:, 0xC5].astype(int), -1)
    return {
        'route': route.stem,
        'same': same,
        'audible': ra > QUIET,
        'level': level,
        'lsd': lsd,
        'track': music,
        'pitch': pitch_errors(apu, nat, channel_periods(ram)),
    }


def stats(values):
    if len(values) == 0:
        return None
    v = np.abs(np.asarray(values, dtype=np.float64))
    return float(np.median(v)), float(np.percentile(v, 95))


def fmt(s):
    return '-' if s is None else f'{s[0]:.2f} / {s[1]:.2f}'


def within(value, limit, floor):
    return limit is None or value <= limit or (floor is not None and value <= 1.1 * floor)


def verdict(row, floor=None):
    ok = True
    for key in ('level', 'spectral'):
        if row[key] is not None:
            f = floor[key] if floor and floor[key] else (None, None)
            ok &= within(row[key][0], LIMITS[key][0], f[0]) and within(row[key][1], LIMITS[key][1], f[1])
    for c in CHANNELS:
        s = row['pitch'][c]
        if s is not None:
            f = floor['pitch'][c] if floor and floor['pitch'][c] else (None, None)
            ok &= within(s[0], LIMITS['pitch'][0], f[0])
    return ok


def summarize(groups):
    rows = []
    for name, parts in groups:
        aud = np.concatenate([p['audible'] for p in parts])
        lev = np.concatenate([p['level'][p['audible']] for p in parts])
        lsd = np.concatenate([p['lsd'][p['audible']] for p in parts])
        pitch = {c: stats([e for p in parts for e in p['pitch'][c]]) for c in CHANNELS}
        counts = {c: sum(len(p['pitch'][c]) for p in parts) for c in CHANNELS}
        row = {
            'name': name,
            'frames': len(aud),
            'audible': int(aud.sum()),
            'level': stats(lev),
            'spectral': stats(lsd),
            'pitch': pitch,
            'counts': counts,
        }
        rows.append(row)
    return rows


def judge(rows, floors=None):
    floor = {r['name']: r for r in floors or []}
    for r in rows:
        r['ok'] = verdict(r, floor.get(r['name']))
    return rows


def print_rows(title, rows):
    print('\n' + title)
    print(
        f'{"":24} {"frames":>7} {"audible":>7}  {"level dB":>13}  {"spectral dB":>13}  '
        + '  '.join(f'{c + " cents (n)":>24}' for c in CHANNELS)
        + '  result'
    )
    for r in rows:
        pitch = '  '.join(f'{fmt(r["pitch"][c]) + " (" + str(r["counts"][c]) + ")":>24}' for c in CHANNELS)
        result = ('pass' if r['ok'] else 'FAIL') if 'ok' in r else ''
        print(
            f'{r["name"]:24} {r["frames"]:7} {r["audible"]:7}  {fmt(r["level"]):>13}  {fmt(r["spectral"]):>13}  '
            f'{pitch}  {result}'
        )


def track_slice(res, track):
    sel = res['track'] == track
    pitch = {c: [e for f, e in res['pitch'][c] if sel[f]] for c in CHANNELS}
    return {'audible': res['audible'][sel], 'level': res['level'][sel], 'lsd': res['lsd'][sel], 'pitch': pitch}


def track_name(t):
    return '$%02X %s' % (t, TRACKS.get(t, '')) if t >= 0 else 'no music'


def groups(results):
    route_rows = summarize(
        [
            (
                r['route'],
                [
                    {
                        'audible': r['audible'],
                        'level': r['level'],
                        'lsd': r['lsd'],
                        'pitch': {c: [e for _, e in r['pitch'][c]] for c in CHANNELS},
                    }
                ],
            )
            for r in results
        ]
    )
    tracks = sorted({int(t) for r in results for t in np.unique(r['track'])})
    track_rows = summarize(
        [(track_name(t), [track_slice(r, t) for r in results if (r['track'] == t).any()]) for t in tracks]
    )
    return route_rows, track_rows, tracks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--routes', nargs='*', default=DEFAULT_ROUTES)
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--out', default=str(ROOT / 'captures' / 'audio_compare'))
    ap.add_argument('--keep', action='store_true')
    ap.add_argument(
        '--reuse', action='store_true', help='keep an existing cycle-host WAV instead of recording it again'
    )
    ap.add_argument(
        '--floor',
        action='store_true',
        help='also record the cycle host at CPU/PPU alignment 1 and report APU vs APU as the noise floor',
    )
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    native = ROOT / args.build / args.config / 'ducktales_native.exe'
    Path(args.out).mkdir(parents=True, exist_ok=True)
    routes = [ROOT / 'tests' / 'routes' / f'{r}.txt' for r in args.routes]
    with ThreadPoolExecutor(args.jobs) as pool:
        recorded = list(pool.map(lambda r: record(r, args, exe, native), routes))
    results, floors = [], []
    for route, files, same in recorded:
        results.append(analyse(route, files, same))
        if args.floor:
            floors.append(analyse(route, files, True, 'apu1.wav'))
        if not args.keep:
            for f in files.values():
                f.unlink(missing_ok=True)
    print(
        'Native runtime vs cycle host (RAM and cycles on every frame): '
        + ', '.join(r['route'] + (' same' if r['same'] else ' DIFFERENT') for r in results)
    )
    print('Columns: median / p95 of |difference|; level and spectral over audible frames.')
    route_rows, track_rows, tracks = groups(results)
    floor_routes, floor_tracks = groups(floors)[:2] if floors else (None, None)
    if floors:
        keep = [
            i
            for i, row in enumerate(floor_routes)
            if row['level']
            and row['spectral']
            and row['level'][0] <= LIMITS['level'][0]
            and row['spectral'][0] <= LIMITS['spectral'][0]
        ]
        print('Noise floor used from: ' + (', '.join(floors[i]['route'] for i in keep) or 'no route'))
        floor_routes = [floor_routes[i] for i in keep]
        floor_tracks = groups([floors[i] for i in keep])[1] if keep else []
    print_rows('Native synth vs APU, per route', judge(route_rows, floor_routes))
    print_rows('Native synth vs APU, per track', judge(track_rows, floor_tracks))
    if floors:
        print_rows('Noise floor: APU at alignment 0 vs APU at alignment 1, per route', floor_routes)
        print_rows('Noise floor: APU at alignment 0 vs APU at alignment 1, per track', floor_tracks)
    missing = ['$%02X' % t for t in TRACKS if t not in tracks]
    print('\nTracks not reached by these routes: ' + (', '.join(missing) if missing else 'none'))
    ok = all(r['same'] for r in results) and all(r['ok'] for r in route_rows + track_rows)
    print('\nRESULT:', 'pass' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
