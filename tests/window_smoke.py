"""Smoke test of the window build (application, docs/architecture.md).

Runs build/windows/Release/ducktales.exe hidden (--hidden --exit-after N), so
nothing appears on screen, with a scratch config.ini (--config) and data
folder (DUCKTALES_DATA_DIR):

  1. the game starts and exits cleanly with a ROM on the command line;
  2. without a ROM and nothing imported, a hidden start refuses with a message;
  3. --import-rom refuses a modified ROM (one byte changed, in a temporary
     copy) and a non-ROM file with a clear message, and imports the real one
     into the data folder;
  4. without a ROM, the imported copy starts the game directly;
  5. enhancement settings in config.ini [Game] survive a run;
  6. presentation: the [Game] keys survive a run, FramePacing sets the host's
     [Display] VSync before it starts, and --present-out shows the picture
     at the size the overscan crop, the pixel aspect and the window scale
     give (docs/presentation.md);
  7. DUCKTALES_UI_SELFTEST: the in-game menu opens by itself at frame 120, walks
     its pages and closes, and the first-run screen shows a refused ROM; each
     state is written as a PNG (into --shots, default a temporary folder) and
     the game still reaches its exit frame.

    py -3 -I tests/window_smoke.py --build build/windows [--shots captures/shots]

What a hidden window cannot show: real keyboard, mouse and controller input in
the menu, the file dialog, full-screen switching and VSync are not exercised.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/windows')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=str(ROOT / 'roms' / 'DuckTales (USA).nes'))
    ap.add_argument('--shots', help='keep the self-test PNGs here (never commit them)')
    args = ap.parse_args()
    exe = str(ROOT / args.build / args.config / 'ducktales.exe')
    failed = []

    def check(ok, what, detail=''):
        print(f'{"PASS" if ok else "FAIL"}  {what}')
        if not ok:
            failed.append(what)
            if detail:
                print('      ' + detail[-1500:].replace('\n', '\n      '))

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        data = tmp / 'data'
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(data))
        env.pop('DUCKTALES_UI_SELFTEST', None)

        def run(*argv, extra_env=None, timeout=180):
            e = dict(env, **(extra_env or {}))
            p = subprocess.run([exe, *argv], capture_output=True, text=True, env=e, timeout=timeout)
            return p.returncode, p.stdout + p.stderr

        code, out = run(args.rom, '--hidden', '--exit-after', '300', '--config', str(tmp / 'a.ini'))
        check(code == 0, 'starts with a ROM argument and exits after 300 frames', out)

        code, out = run('--hidden', '--exit-after', '60', '--config', str(tmp / 'b.ini'))
        check(code == 2 and 'No DuckTales (USA) ROM is set up yet' in out, 'hidden start without a ROM refuses clearly', out)

        modified = tmp / 'modified.nes'
        shutil.copyfile(args.rom, modified)
        with open(modified, 'r+b') as f:
            f.seek(40000)
            b = f.read(1)
            f.seek(40000)
            f.write(bytes([b[0] ^ 1]))
        code, out = run('--import-rom', str(modified))
        check(code == 1 and 'not an unmodified DuckTales (USA)' in out and 'expected 97d472f7' in out,
              'a modified ROM is refused with its SHA-256', out)
        modified.unlink()
        code, out = run('--import-rom', str(ROOT / 'README.md'))
        check(code == 1 and 'not an NES ROM image' in out, 'a file that is not a ROM is refused', out)
        check(not (data / 'DuckTales (USA).nes').exists(), 'nothing was stored by the refused imports')
        code, out = run('--import-rom', args.rom)
        check(code == 0 and (data / 'DuckTales (USA).nes').exists(), 'the ROM is imported into the data folder', out)

        code, out = run('--hidden', '--exit-after', '300', '--config', str(tmp / 'c.ini'))
        check(code == 0, 'without a ROM argument the imported copy starts the game', out)

        ini = tmp / 'd.ini'
        ini.write_text('[Game]\nNoDamage = on\nStartLives = 5\nJumpScale = 150\n')
        code, out = run(args.rom, '--hidden', '--exit-after', '120', '--config', str(ini))
        text = ini.read_text()
        check(code == 0 and 'NoDamage = on' in text and 'StartLives = 5' in text and 'JumpScale = 150' in text
              and 'InfiniteLives = off' in text, 'enhancement settings survive a run in config.ini [Game]', text)

        def png_size(path):
            with open(path, 'rb') as f:
                head = f.read(24)
            return struct.unpack('>II', head[16:24])

        for name, game, display, vsync, size in (
                ('steady', 'FramePacing = steady\nOverscan = all\nPixelAspect = 8:7\nPixels = sharp\n', 'VSync = 1\n', 0,
                 (2 * round(240 * 8 / 7), 2 * 224)),
                ('vsync', 'FramePacing = vsync\nOverscan = top-bottom\nHudSplit = original\n', 'VSync = 0\n', 1, (512, 448))):
            ini = tmp / f'p_{name}.ini'
            ini.write_text(f'[Display]\nWindowScale = 2\nIntegerScale = 1\n{display}[Game]\n{game}')
            out = tmp / f'p_{name}.png'
            code, log = run(args.rom, '--hidden', '--exit-after', '61', '--config', str(ini), '--present-out', str(out),
                            '--present-every', '60')
            text = ini.read_text()
            pics = sorted(tmp.glob(f'p_{name}_*.png'))
            got = png_size(pics[-1]) if pics else None
            keys = all(line in text for line in game.strip().split('\n'))
            check(code == 0 and keys and f'VSync = {vsync}' in text and got == size,
                  f'presentation {name}: keys kept, VSync = {vsync}, picture {size[0]}x{size[1]} (got {got})', log + text)

        shots = Path(args.shots).resolve() if args.shots else tmp / 'shots'
        shots.mkdir(parents=True, exist_ok=True)
        for old in shots.glob('*.png'):
            old.unlink()
        code, out = run(args.rom, '--hidden', '--exit-after', '400', '--config', str(tmp / 'e.ini'),
                        extra_env={'DUCKTALES_UI_SELFTEST': str(shots)})
        menu = sorted(p.name for p in shots.glob('menu_*.png'))
        check(code == 0 and len(menu) == 5 and '[ducktales] menu closed' in out,
              f'the menu opens, draws its 5 pages ({", ".join(menu)}) and closes; the run still ends', out)
        empty = tmp / 'empty-data'
        bad = tmp / 'bad.nes'
        shutil.copyfile(args.rom, bad)
        with open(bad, 'r+b') as f:
            f.seek(40000)
            b = f.read(1)
            f.seek(40000)
            f.write(bytes([b[0] ^ 1]))
        code, out = run('--config', str(tmp / 'f.ini'), '--exit-after', '120',
                        extra_env={'DUCKTALES_UI_SELFTEST': str(shots), 'DUCKTALES_UI_SELFTEST_PICK': str(bad),
                                   'DUCKTALES_DATA_DIR': str(empty)})
        first = sorted(p.name for p in shots.glob('first_run_*.png'))
        check(code == 0 and len(first) == 2 and 'not an unmodified' in out and not (empty / 'DuckTales (USA).nes').exists(),
              f'the first-run screen draws and refuses a modified ROM ({", ".join(first)})', out)
        bad.unlink()
    print('all passed' if not failed else f'{len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
