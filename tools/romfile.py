"""The default ROM of the scripts in tests/ and tools/: the file in roms/ whose
SHA-256 is the supported revision's (docs/ROM.md), whatever its name, or
roms/baserom.nes when none matches, so a missing-ROM message names a path.
--rom overrides it in every script.
"""

import hashlib
from pathlib import Path

SHA256 = '97d472f7b2c1088448d1644155b2650a546bad887b3cba571e16d7986824c169'
SIZE = 131088
ROMS = Path(__file__).resolve().parents[1] / 'roms'


def default_rom():
    for path in sorted(ROMS.iterdir()) if ROMS.is_dir() else []:
        if path.is_file() and path.stat().st_size == SIZE and hashlib.sha256(path.read_bytes()).hexdigest() == SHA256:
            return str(path)
    return str(ROMS / 'baserom.nes')
