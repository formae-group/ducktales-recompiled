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
  5. enhancement, sprite and NoSlowdown settings in config.ini [Game] survive a
     run (step 1 also checks that No slowdown is on by default, and on the
     native player that the decompiled routines run by default);
  6. presentation: the [Game] keys survive a run, FramePacing sets the host's
     [Display] VSync before it starts, and --present-out shows the picture
     at the size the overscan crop, the pixel aspect and the window scale
     give (docs/presentation.md);
  7. DUCKTALES_UI_SELFTEST: the in-game menu opens by itself at frame 120, walks
     every tab and category (Settings: General, Graphics, Audio, Controls with
     a capture, Gameplay, Mods; Enhancements: Gameplay, Restore defaults; About),
     draws Graphics and About off screen at 1280x720 and 3840x2160, opens the
     Reset and Quit confirmations without confirming them, and closes; the
     first-run screen shows a refused ROM. Each state is written as a PNG (into
     --shots, default a temporary folder) and the game still reaches its exit
     frame. That run sets [Game] MenuPausesGame = on, so the PNGs are the same
     on every run, and no game frame runs while the menu is open; a second
     self-test with the default (off) checks that the native player keeps
     running the game behind the menu (the cycle host always pauses);
  8. the menu's own [Game] keys (MenuOpacity, MenuControllerNav,
     MenuPausesGame) survive a run;
  9. Decompiled code ([Game] DecompiledCode) is on by default and saved with
     SettingsVersion = 2; on the native player, set on, the decompiled
     routines run with No slowdown on and not with it off, and the key is kept
     either way; the session that ran them writes decomp-session-<time>.txt
     (NESRECOMP_DIAGNOSTICS_DIR), the other writes none;
 10. a config.ini without SettingsVersion (NoSlowdown and DecompiledCode off)
     gets both on once and keeps its other keys; then Original mode as the
     menu saves it (both off, with SettingsVersion) survives a restart and
     runs neither.

    py -3 -I tests/window_smoke.py --build build/windows [--shots captures/shots] [--exe NAME]

The player build (ducktales.exe) runs on the native runtime or, with
-DDUCKTALES_PLAYER=cycle, on NESRecomp's cycle host; --exe ducktales_cycle.exe
(or ducktales_player.exe) tests the other one. Each runtime reports its own No
slowdown: 128 extra scanlines (cycle) or the held VBlank (native).

What a hidden window cannot show: real keyboard, mouse and controller input in
the menu, the file dialog, full-screen switching and VSync are not exercised.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/windows')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--shots', help='keep the self-test PNGs here (never commit them)')
    ap.add_argument('--exe', default='ducktales.exe', help='the executable in <build>/<config> to test')
    args = ap.parse_args()
    exe = str(ROOT / args.build / args.config / args.exe)
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
        native = 'the native runtime holds VBlank' in out
        check(
            (
                '[ducktales] No slowdown: 128 extra scanlines per frame' in out
                or '[ducktales] No slowdown: the native runtime holds VBlank' in out
            )
            and 'NoSlowdown = on' in (tmp / 'a.ini').read_text(),
            'No slowdown is on by default and saved as NoSlowdown = on',
            out,
        )
        check(
            ('[ducktales] Decompiled code:' in out) == native,
            f'the decompiled routines {"run" if native else "do not run"} by default',
            out,
        )

        code, out = run('--hidden', '--exit-after', '60', '--config', str(tmp / 'b.ini'))
        check(
            code == 2 and 'No DuckTales (USA) ROM is set up yet' in out,
            'hidden start without a ROM refuses clearly',
            out,
        )

        modified = tmp / 'modified.nes'
        shutil.copyfile(args.rom, modified)
        with open(modified, 'r+b') as f:
            f.seek(40000)
            b = f.read(1)
            f.seek(40000)
            f.write(bytes([b[0] ^ 1]))
        code, out = run('--import-rom', str(modified))
        check(
            code == 1 and 'not an unmodified DuckTales (USA)' in out and 'expected 97d472f7' in out,
            'a modified ROM is refused with its SHA-256',
            out,
        )
        modified.unlink()
        code, out = run('--import-rom', str(ROOT / 'README.md'))
        check(code == 1 and 'not an NES ROM image' in out, 'a file that is not a ROM is refused', out)
        check(not (data / 'DuckTales (USA).nes').exists(), 'nothing was stored by the refused imports')
        code, out = run('--import-rom', args.rom)
        check(code == 0 and (data / 'DuckTales (USA).nes').exists(), 'the ROM is imported into the data folder', out)

        code, out = run('--hidden', '--exit-after', '300', '--config', str(tmp / 'c.ini'))
        check(code == 0, 'without a ROM argument the imported copy starts the game', out)

        ini = tmp / 'd.ini'
        ini.write_text(
            '[Game]\nSettingsVersion = 2\nNoDamage = on\nStartLives = 5\nJumpScale = 150\nNoSpriteFlicker = off\n'
            'NoSlowdown = off\n'
            'MenuOpacity = 55\nMenuControllerNav = off\nMenuPausesGame = on\n'
        )
        code, out = run(args.rom, '--hidden', '--exit-after', '120', '--config', str(ini))
        text = ini.read_text()
        check(
            code == 0
            and 'NoDamage = on' in text
            and 'StartLives = 5' in text
            and 'JumpScale = 150' in text
            and 'InfiniteLives = off' in text
            and 'NoSpriteFlicker = off' in text
            and 'NoSlowdown = off' in text
            and 'No slowdown:' not in out,
            'enhancement and sprite settings survive a run in config.ini [Game]',
            text,
        )
        check(
            'MenuOpacity = 55' in text and 'MenuControllerNav = off' in text and 'MenuPausesGame = on' in text,
            'the menu settings survive a run in config.ini [Game]',
            text,
        )
        check(
            'MenuOpacity = 80' in (tmp / 'a.ini').read_text()
            and 'MenuControllerNav = on' in (tmp / 'a.ini').read_text()
            and 'MenuPausesGame = off' in (tmp / 'a.ini').read_text(),
            'the menu settings default to 80% opacity, controller navigation on and no pause',
            (tmp / 'a.ini').read_text(),
        )
        check(
            'DecompiledCode = on' in (tmp / 'a.ini').read_text()
            and 'SettingsVersion = 2' in (tmp / 'a.ini').read_text(),
            'Decompiled code is on by default and saved as DecompiledCode = on, with SettingsVersion = 2',
            (tmp / 'a.ini').read_text(),
        )
        for slowdown, runs in (('on', native), ('off', False)):
            ini = tmp / f'decomp_{slowdown}.ini'
            ini.write_text(f'[Game]\nSettingsVersion = 2\nNoSlowdown = {slowdown}\nDecompiledCode = on\n')
            diag = tmp / f'diag_{slowdown}'
            code, out = run(
                args.rom,
                '--hidden',
                '--exit-after',
                '300',
                '--config',
                str(ini),
                extra_env={'NESRECOMP_DIAGNOSTICS_DIR': str(diag), 'NESRECOMP_REPLAY_DIR': ''},
            )
            calls = sum(int(c) for c in re.findall(r'^decomp: mode=replace .* calls=(\d+) ', out, re.M))
            check(
                code == 0
                and 'DecompiledCode = on' in ini.read_text()
                and (calls > 0) == runs
                and ('[ducktales] Decompiled code:' in out) == runs,
                f'DecompiledCode = on with NoSlowdown = {slowdown}: the decompiled routines '
                f'{"run" if runs else "do not run"} ({calls} calls)',
                out,
            )
            sessions = sorted(diag.glob('decomp-session-*.txt'))
            text = sessions[0].read_text() if sessions else ''
            frames = re.search(r'^frames (\d+), (\d+) of them with the decompiled code on$', text, re.M)
            listed = sum(int(c) for c in re.findall(r'^decomp: mode=replace .* calls=(\d+) ', text, re.M))
            check(
                (
                    len(sessions) == 1 and 'error none' in text and frames and int(frames[2]) > 0 and listed == calls
                    if runs
                    else not sessions
                ),
                f'NoSlowdown = {slowdown}: '
                + ('the session summary lists the frames and the calls' if runs else 'no session summary is written'),
                text or out,
            )

        ini = tmp / 'old.ini'
        ini.write_text('[Game]\nNoDamage = on\nNoSlowdown = off\nDecompiledCode = off\n')
        code, out = run(args.rom, '--hidden', '--exit-after', '120', '--config', str(ini))
        text = ini.read_text()
        check(
            code == 0
            and 'SettingsVersion = 2' in text
            and 'NoSlowdown = on' in text
            and 'DecompiledCode = on' in text
            and 'NoDamage = on' in text
            and ('[ducktales] Decompiled code:' in out) == native,
            'a config.ini without SettingsVersion gets No slowdown and Decompiled code on once and keeps its other keys',
            text + out,
        )
        ini.write_text(
            text.replace('NoSlowdown = on', 'NoSlowdown = off').replace('DecompiledCode = on', 'DecompiledCode = off')
        )
        code, out = run(args.rom, '--hidden', '--exit-after', '120', '--config', str(ini))
        text = ini.read_text()
        check(
            code == 0
            and 'SettingsVersion = 2' in text
            and 'NoSlowdown = off' in text
            and 'DecompiledCode = off' in text
            and 'No slowdown:' not in out
            and '[ducktales] Decompiled code:' not in out,
            'Original mode (NoSlowdown and DecompiledCode off, as the menu saves it) survives a restart',
            text + out,
        )

        def png_size(path):
            with open(path, 'rb') as f:
                head = f.read(24)
            return struct.unpack('>II', head[16:24])

        for name, game, display, vsync, size in (
            (
                'steady',
                'FramePacing = steady\nOverscan = all\nPixelAspect = 8:7\nPixels = sharp\n',
                'VSync = 1\n',
                0,
                (2 * round(240 * 8 / 7), 2 * 224),
            ),
            (
                'vsync',
                'FramePacing = vsync\nOverscan = top-bottom\nHudSplit = original\n',
                'VSync = 0\n',
                1,
                (512, 448),
            ),
        ):
            ini = tmp / f'p_{name}.ini'
            ini.write_text(f'[Display]\nWindowScale = 2\nIntegerScale = 1\n{display}[Game]\n{game}')
            out = tmp / f'p_{name}.png'
            code, log = run(
                args.rom,
                '--hidden',
                '--exit-after',
                '61',
                '--config',
                str(ini),
                '--present-out',
                str(out),
                '--present-every',
                '60',
            )
            text = ini.read_text()
            pics = sorted(tmp.glob(f'p_{name}_*.png'))
            got = png_size(pics[-1]) if pics else None
            keys = all(line in text for line in game.strip().split('\n'))
            check(
                code == 0 and keys and f'VSync = {vsync}' in text and got == size,
                f'presentation {name}: keys kept, VSync = {vsync}, picture {size[0]}x{size[1]} (got {got})',
                log + text,
            )

        shots = Path(args.shots).resolve() if args.shots else tmp / 'shots'
        shots.mkdir(parents=True, exist_ok=True)
        for old in shots.glob('*.png'):
            old.unlink()
        (tmp / 'e.ini').write_text('[Game]\nMenuPausesGame = on\n\n[Mods]\nOrder = disabled:off\nEnabled = on\n')
        mods = data / 'mods'
        fixture = mods / 'disabled'
        fixture.mkdir(parents=True, exist_ok=True)
        (fixture / 'mod.toml').write_text(
            'id = "disabled"\nname = "Disabled fixture"\nversion = "1"\ngame = "DuckTales (USA)"\n'
            'prg_sha256 = "8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013"\n'
        )
        (mods / 'broken.dtmod').write_bytes(b'not a zip')
        code, out = run(
            args.rom,
            '--hidden',
            '--exit-after',
            '400',
            '--config',
            str(tmp / 'e.ini'),
            extra_env={'DUCKTALES_UI_SELFTEST': str(shots)},
        )
        menu = sorted(p.name for p in shots.glob('menu_*.png'))
        want = [
            'menu_01_settings_general',
            'menu_02_settings_graphics',
            'menu_03_settings_audio',
            'menu_04_settings_controls',
            'menu_05_settings_controls_capture',
            'menu_06_settings_gameplay',
            'menu_07_enhancements_gameplay',
            'menu_08_enhancements_restore_defaults',
            'menu_09_about',
            'menu_10_settings_graphics_720p',
            'menu_11_settings_graphics_2160p',
            'menu_12_about_2160p',
            'menu_13_reset',
            'menu_14_quit',
            'menu_15_settings_mods',
        ]
        check(
            code == 0
            and menu == [w + '.png' for w in want]
            and '[ducktales] menu closed' in out
            and '(quit)' not in out
            and 'could not write' not in out,
            f'the menu opens, draws every tab and category ({len(menu)} PNGs) and closes; the run still ends',
            out,
        )
        sizes = {w: png_size(shots / f'{w}.png') for w in want if (shots / f'{w}.png').exists()}
        check(
            sizes.get('menu_10_settings_graphics_720p') == (1280, 720)
            and sizes.get('menu_11_settings_graphics_2160p') == (3840, 2160)
            and sizes.get('menu_12_about_2160p') == (3840, 2160),
            f'the off-screen pages have their sizes ({sizes})',
        )
        check(
            'menu closed, 0 game frames ran' in out, 'with MenuPausesGame = on no game frame runs behind the menu', out
        )
        mod_log = (data / 'mods.log').read_text() if (data / 'mods.log').exists() else ''
        check(
            'Mods: on, 2 found, 0 loaded' in mod_log
            and 'Mod disabled: Disabled fixture 1, not loaded, switched off' in mod_log
            and 'Mod broken.dtmod: Cannot be read: Not a zip archive' in mod_log,
            'the window reads Enabled after Order, retains a disabled mod and reports the broken archive',
            out + mod_log,
        )
        code, out = run(args.rom, '--hidden', '--exit-after', '10', '--config', str(tmp / 'e.ini'), '--mods', 'off')
        check(
            code == 0 and 'Mods: off, 2 found, 0 loaded' in out and 'Enabled = on' in (tmp / 'e.ini').read_text(),
            'the command line can turn mods off despite Enabled = on in the window config',
            out,
        )
        running = tmp / 'shots-running'
        running.mkdir()
        code, out = run(
            args.rom,
            '--hidden',
            '--exit-after',
            '400',
            '--config',
            str(tmp / 'g.ini'),
            extra_env={'DUCKTALES_UI_SELFTEST': str(running)},
        )
        ran = [
            int(line.split(', ')[1].split()[0])
            for line in out.splitlines()
            if line.startswith('[ducktales] menu closed, ')
        ]
        check(
            code == 0 and len(ran) == 1 and (ran[0] > 0 if native else ran[0] == 0),
            f'by default the game keeps running behind the menu on the native player ({ran} game frames ran)',
            out,
        )
        empty = tmp / 'empty-data'
        bad = tmp / 'bad.nes'
        shutil.copyfile(args.rom, bad)
        with open(bad, 'r+b') as f:
            f.seek(40000)
            b = f.read(1)
            f.seek(40000)
            f.write(bytes([b[0] ^ 1]))
        code, out = run(
            '--config',
            str(tmp / 'f.ini'),
            '--exit-after',
            '120',
            extra_env={
                'DUCKTALES_UI_SELFTEST': str(shots),
                'DUCKTALES_UI_SELFTEST_PICK': str(bad),
                'DUCKTALES_DATA_DIR': str(empty),
            },
        )
        first = sorted(p.name for p in shots.glob('first_run_*.png'))
        check(
            code == 0
            and len(first) == 2
            and 'not an unmodified' in out
            and not (empty / 'DuckTales (USA).nes').exists(),
            f'the first-run screen draws and refuses a modified ROM ({", ".join(first)})',
            out,
        )
        bad.unlink()
    print('all passed' if not failed else f'{len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
