"""Download the official Mesen2 Windows release into build/tools/mesen2 (gitignored).

    py -3 -I tools/mesen/fetch_mesen2.py [--dest build/tools/mesen2]

Pinned to Mesen 2.1.1 (github.com/SourMesen/Mesen2 release 2.1.1) and checked
against the SHA-256 recorded below. An empty settings.json is written next to
Mesen.exe: Mesen2 then runs in portable mode from that folder (its native DLLs,
Saves/ and Debugger/ land there, not in Documents), and the test runner does
not stop at the first-run setup dialog.
"""

import argparse
import hashlib
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = '2.1.1'
URL = f'https://github.com/SourMesen/Mesen2/releases/download/{VERSION}/Mesen_{VERSION}_Windows.zip'
SHA256 = '23ccc2bc060b663c68dad3a8c5d6da7d23a50f872d04f135bafa2b04ff7d5cbe'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dest', default=str(ROOT / 'build' / 'tools' / 'mesen2'))
    args = ap.parse_args()
    dest = Path(args.dest)
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / Path(URL).name
    if not archive.exists():
        print(f'downloading {URL}')
        with urllib.request.urlopen(URL) as r:
            archive.write_bytes(r.read())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SHA256:
        archive.unlink()
        sys.exit(f'{archive.name}: SHA-256 {digest}, expected {SHA256}; deleted')
    with zipfile.ZipFile(archive) as z:
        if z.namelist() != ['Mesen.exe']:
            sys.exit(f'unexpected archive contents: {z.namelist()}')
        z.extract('Mesen.exe', dest)
    settings = dest / 'settings.json'
    if not settings.exists():
        settings.write_text('{}\n')
    print(f'Mesen {VERSION} ready: {dest / "Mesen.exe"}')


if __name__ == '__main__':
    main()
