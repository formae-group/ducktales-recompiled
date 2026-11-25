"""Release package test (CONTRIBUTING.md "Releasing").

Builds the window build, packs it with CPack into a temporary folder and checks
DuckTalesRecompiled-<version>-win64.zip as a player gets it:

  1. the archive's name carries the version the executable reports on its
     About page (git describe when CMake configured);
  2. it holds exactly the executable (the native player: ducktales_cycle.exe,
     the cycle host, stays a maintainer build), SDL2.dll, README.txt,
     LICENSE.txt and the third-party licences under licenses/, in one top-level
     folder, and no ROM, generated source or test tool; and neither the archive
     nor any file in it holds a 32-byte run of the PRG (tools/rom_scan.py, the
     leak scan of the build with no ROM, docs/ROMLESS.md);
  3. neither ducktales.exe nor SDL2.dll imports a Visual C++ runtime DLL (the
     executable links the static /MT runtime), so players need no
     redistributable;
  4. unpacked into a temporary folder and run from there with PATH reduced to
     the Windows system folders, it imports a copy of the ROM (--import-rom)
     into a scratch data folder (DUCKTALES_DATA_DIR) and starts the game from
     the imported copy hidden (--hidden --exit-after 300), writing config.ini
     next to itself;
  5. the in-game menu self-test (DUCKTALES_UI_SELFTEST) draws every page from
     the unpacked folder.

    py -3 -I tests/package_test.py --build build/windows
    py -3 -I tests/package_test.py --build build/romless-windows   (-DDUCKTALES_ROMLESS=ON)

Nothing appears on screen. The ROM is never written into the package.
"""

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from romfile import default_rom  # noqa: E402

ROM_SHA256 = '97d472f7b2c1088448d1644155b2650a546bad887b3cba571e16d7986824c169'
FILES = [
    'ducktales.exe',
    'SDL2.dll',
    'README.txt',
    'LICENSE.txt',
    'licenses/NESRecomp-LICENSE.txt',
    'licenses/NESRecomp-PERMISSIONS.md',
    'licenses/emu2413-LICENSE.txt',
    'licenses/SDL2-COPYING.txt',
    'licenses/SDL2-README-SDL.txt',
    'licenses/imgui-LICENSE.txt',
    'licenses/formae-ui-LICENSE.txt',
    'licenses/stb-LICENSE.txt',
    'licenses/lua-LICENSE.txt',
]
RUNTIME_DLL = re.compile(r'^(vcruntime|msvcp|concrt|vccorlib|ucrtbase|api-ms-win-crt-)', re.I)


def pe_imports(path):
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    opt = pe + 24
    import_rva = struct.unpack_from('<I', data, opt + 112 + 8)[0]
    table = [
        struct.unpack_from('<IIIII', data, pe + 24 + struct.unpack_from('<H', data, pe + 20)[0] + 40 * i + 8)
        for i in range(sections)
    ]

    def offset(rva):
        for vsize, va, rawsize, raw, _ in table:
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + raw
        raise ValueError(f'RVA {rva:#x} outside every section')

    names = []
    at = offset(import_rva)
    while True:
        name_rva = struct.unpack_from('<I', data, at + 12)[0]
        if not name_rva:
            return names
        start = offset(name_rva)
        names.append(data[start : data.index(b'\0', start)].decode('ascii'))
        at += 20


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/windows')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--jobs', type=int, default=4, help='parallel build jobs')
    args = ap.parse_args()
    build = (ROOT / args.build).resolve()
    cache = (build / 'CMakeCache.txt').read_text(errors='replace')
    cmake = Path(re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache, re.M).group(1).strip())
    cpack = cmake.with_name('cpack' + cmake.suffix)
    failed = []

    def check(ok, what, detail=''):
        print(f'{"PASS" if ok else "FAIL"}  {what}')
        if not ok:
            failed.append(what)
            if detail:
                print('      ' + str(detail)[-1500:].replace('\n', '\n      '))

    p = subprocess.run(
        [str(cmake), '--build', str(build), '--config', args.config, '--parallel', str(args.jobs)],
        capture_output=True,
        text=True,
    )
    check(p.returncode == 0, 'the window build is up to date', p.stdout + p.stderr)
    if p.returncode:
        return 1

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        out = tmp / 'out'
        p = subprocess.run(
            [str(cpack), '-G', 'ZIP', '-C', args.config, '-B', str(out)], cwd=build, capture_output=True, text=True
        )
        zips = sorted(out.glob('*.zip'))
        check(
            p.returncode == 0 and len(zips) == 1,
            f'cpack writes one zip ({", ".join(z.name for z in zips)})',
            p.stdout + p.stderr,
        )
        if not zips:
            return 1
        name = zips[0].name
        m = re.fullmatch(r'DuckTalesRecompiled-(.+)-win64\.zip', name)
        version = m.group(1) if m else ''
        top = name[: -len('.zip')]
        with zipfile.ZipFile(zips[0]) as z:
            listed = sorted(n for n in z.namelist() if not n.endswith('/'))
            z.extractall(tmp / 'unpacked')
        check(
            listed == sorted(f'{top}/{f}' for f in FILES),
            f'{name} holds exactly the {len(FILES)} release files in {top}/',
            '\n'.join(listed),
        )
        check(
            not any(
                re.search(r'\.(nes|c|h|cpp|pdb|py|lib)$', n, re.I) or 'oracle' in n.lower() or '_test' in n
                for n in listed
            ),
            'no ROM, generated source, symbols or test tool in the archive',
        )
        check(not any('ducktales_cycle' in n for n in listed), 'the archive holds the native player only')
        p = subprocess.run(
            [sys.executable, '-I', str(ROOT / 'tools' / 'rom_scan.py'), '--rom', args.rom, str(zips[0])],
            capture_output=True,
            text=True,
        )
        check(p.returncode == 0, 'no run of the PRG in the archive or any file in it', p.stdout + p.stderr)
        app = tmp / 'unpacked' / top
        exe = app / 'ducktales.exe'
        exe_bytes = exe.read_bytes() if exe.exists() else b''
        check(
            m is not None and version.encode() in exe_bytes,
            f'the archive name carries the version the executable reports ({version})',
        )
        readme = (app / 'README.txt').read_text(errors='replace') if (app / 'README.txt').exists() else ''
        check(
            ROM_SHA256 in readme and version in readme and '--import-rom' in readme and 'Controls' in readme,
            'README.txt gives the version, the ROM SHA-256, the import and the controls',
        )
        for dll in (exe, app / 'SDL2.dll'):
            imports = pe_imports(dll) if dll.exists() else []
            check(
                imports and not any(RUNTIME_DLL.match(d) for d in imports),
                f'{dll.name} needs no Visual C++ runtime DLL (imports: {", ".join(sorted(imports))})',
            )

        rom = tmp / 'rom' / 'any name.nes'
        rom.parent.mkdir()
        shutil.copyfile(args.rom, rom)
        data = tmp / 'data'
        system = Path(os.environ.get('SystemRoot', r'C:\Windows'))
        env = {k: v for k, v in os.environ.items() if not k.startswith('DUCKTALES_')}
        env.update(PATH=os.pathsep.join([str(system / 'System32'), str(system)]), DUCKTALES_DATA_DIR=str(data))

        def run(*argv, extra_env=None):
            p = subprocess.run(
                [str(exe), *argv],
                cwd=app,
                capture_output=True,
                text=True,
                env=dict(env, **(extra_env or {})),
                timeout=300,
            )
            return p.returncode, p.stdout + p.stderr

        code, log = run('--import-rom', str(rom))
        stored = data / 'DuckTales (USA).nes'
        check(
            code == 0 and stored.exists() and hashlib.sha256(stored.read_bytes()).hexdigest() == ROM_SHA256,
            'the unpacked executable imports a copy of the ROM into the data folder',
            log,
        )
        code, log = run('--hidden', '--exit-after', '300')
        check(
            code == 0 and (app / 'config.ini').exists(),
            'from the unpacked folder, the imported copy starts the game hidden for 300 frames (config.ini written)',
            log,
        )
        shots = tmp / 'shots'
        shots.mkdir()
        code, log = run(
            '--hidden',
            '--exit-after',
            '400',
            '--config',
            str(tmp / 'selftest.ini'),
            extra_env={'DUCKTALES_UI_SELFTEST': str(shots)},
        )
        menu = list(shots.glob('menu_*.png'))
        check(
            code == 0 and len(menu) == 15 and '[ducktales] menu closed' in log,
            f'the menu self-test draws every page from the unpacked folder ({len(menu)} PNGs)',
            log,
        )
        check(not any(f.suffix.lower() == '.nes' for f in app.rglob('*')), 'no ROM was written into the package folder')
    print('all passed' if not failed else f'{len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
