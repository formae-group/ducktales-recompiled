"""Step 9 of the mods test (tests/mods_test.py): mod.toml and scripts/main.lua with a byte order mark,
names in any case, mod.toml errors, two files for one tile, palette or sound, --mods-order with a
refused package of the same id, --mods-dump with mods off and --mods-dir with a path that is not a
folder."""

import os
import subprocess
import tempfile
import zipfile
from pathlib import Path

from .common import ROM, check, make_mod, mod_toml, png_bytes, png_read, tone_wav


def catalogue_checks(exe, route, native):
    """mod.toml validation, names in any case, a byte order mark, two files for one asset and
    --mods-dir errors, from the log of one short run."""
    host = 'native player' if native else 'headless'
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        mods = tmp / 'mods'
        mods.mkdir()
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(*extra):
            cmd = [exe, ROM, '--frames', '30', '--input', route, *extra]
            if native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=600, env=env)

        def raw(name, files):
            with zipfile.ZipFile(mods / f'{name}.dtmod', 'w', zipfile.ZIP_DEFLATED) as z:
                for path, data in files.items():
                    z.writestr(path, data)

        toml = mod_toml('bom', 'Bom').encode()
        raw('bom', {'mod.toml': b'\xef\xbb\xbf' + toml, 'scripts/main.lua': b'\xef\xbb\xbfdt.log("bom", "ok")'})
        raw('case', {'MOD.TOML': mod_toml('case', 'Case') + '   ', 'Scripts/Main.lua': 'dt.log("case", "ok")'})
        raw('wrong-target', {'mod.toml': mod_toml('case', 'A different game', prg='0' * 64)})
        bad = {
            'missing': 'id = "missing"\nname = "Missing"\ngame = "DuckTales (USA)"\n',
            'unquoted': mod_toml('unquoted', 'Unquoted').replace('version = "1.0.0"', 'version = 1.0'),
            'twice': mod_toml('twice', 'Twice') + 'name = "Again"\n',
            'bad-sha': mod_toml('bad-sha', 'Bad Sha', prg='abc'),
            'long-id': mod_toml('a' * 64, 'Long Id'),
            'nul-byte': mod_toml('nul-byte', 'Nul Byte') + '\0name = "Hidden"',
        }
        for name, text in bad.items():
            raw(name, {'mod.toml': text})
        tile = png_bytes(8, 8, [0xFFFF00FF] * 64, tmp)
        wav = tone_wav(tmp / 'tone.wav')
        make_mod(mods, 'dup-tile', {'tiles/a/0123456789abcdef.png': tile, 'tiles/b/0123456789ABCDEF.png': tile})
        make_mod(mods, 'dup-pal', {'palettes/amazon.pal': bytes(192), 'palettes/x/Amazon.pal': bytes(192)})
        make_mod(mods, 'dup-sfx', {'sfx/13.wav': wav, 'sfx/13-swing.wav': wav})
        p = run('--mods', 'on', '--mods-dir', mods)
        log = p.stdout + p.stderr
        check(p.returncode == 0 and '12 found, 2 loaded' in log, f'{host}: two good mods load beside ten bad ones', log)
        check(
            'Mod bom: bom\tok' in log and 'Mod case: case\tok' in log,
            f'{host}: mod.toml and scripts/main.lua with a UTF-8 byte order mark, and MOD.TOML and '
            'Scripts/Main.lua, load',
            log,
        )
        reasons = {
            'missing': 'mod.toml is missing version, prg_sha256.',
            'unquoted': 'Cannot be read: mod.toml line 3: version must be a quoted string (version = "...")',
            'twice': 'Cannot be read: mod.toml line 8: name is set twice',
            'bad-sha': 'mod.toml: prg_sha256 must be the 64 hex digits of the PRG\'s SHA-256.',
            'dup-tile': 'Not loaded: tiles/b/0123456789ABCDEF.png: the mod has another file for this tile and palette.',
            'dup-pal': 'Not loaded: palettes/x/Amazon.pal: the mod has another amazon.pal.',
            'dup-sfx': 'Not loaded: sfx/13-swing.wav: the mod has another file for effect 13.',
            'long-id.dtmod': 'Cannot be read: mod.toml line 1: id is longer than 63 bytes',
            'nul-byte.dtmod': 'Cannot be read: mod.toml contains a NUL byte; save it as UTF-8 text.',
        }
        for mod_id, why in reasons.items():
            check(f'Mod {mod_id}: {why}' in log, f'{host}: {mod_id} is refused with "{why}"', log)
        p = run('--mods', 'on', '--mods-dir', mods, '--mods-order', 'case:off')
        log = p.stdout + p.stderr
        check(
            p.returncode == 0 and '12 found, 1 loaded' in log and 'Mod case: case\tok' not in log,
            f'{host}: a refused package with the same id does not steal the valid mod switch',
            log,
        )
        dump = tmp / 't\u00edles' / 'nested'
        hashes = () if native else ('--hash-out', tmp / 'dump.txt')
        p = run('--mods-dump', dump, *hashes)
        keys = list(dump.glob('*.png'))
        check(
            p.returncode == 0 and keys and png_read(keys[0])[:2] == (8, 8),
            f'{host}: dumping with mods off creates a nested UTF-8 path and writes {len(keys)} templates',
            p.stdout + p.stderr,
        )
        if not native:
            plain = run('--hash-out', tmp / 'plain.txt')
            expected = (tmp / 'plain.txt').read_bytes()
            check(
                plain.returncode == 0 and expected and (tmp / 'dump.txt').read_bytes() == expected,
                'headless: collecting templates leaves all 30 machine hash records identical',
                plain.stdout + plain.stderr,
            )
        p = run('--mods-dir', tmp / 'nowhere')
        check(
            p.returncode != 0 and 'not a folder' in p.stdout + p.stderr,
            f'{host}: --mods-dir with a path that is not a folder is refused (exit {p.returncode})',
            p.stdout + p.stderr,
        )
