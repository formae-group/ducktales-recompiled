"""Writes the game-flow block of config/game.toml from src/decomp/flow/flow_table.c.

The block holds a site for each flow routine that original code reaches
(SITE("XXXX") in the table). Resume points have no site: the RTS observer
starts them. The content key is the CRC of the first
8 bytes, read from the local ROM (never committed).

    py -3 -I tools/flow_sites.py
"""

import sys
import re
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfile import default_rom  # noqa: E402

ROM = Path(default_rom())
TOML = ROOT / "config" / "game.toml"
HEAD = "# Game flow (src/decomp/flow/"
TAIL = "# END game flow"
INTERNAL = {"FEEE", "FEF3"}  # these sites also fire inside a routine


def main():
    rom = ROM.read_bytes()
    banks = rom[4]
    prg = rom[16 : 16 + banks * 0x4000]

    def crc(bank, addr):
        bank = banks - 1 if addr >= 0xC000 else bank
        return zlib.crc32(bytes(prg[bank * 0x4000 + ((addr + i) & 0x3FFF)] for i in range(8)))

    syms = {}
    for m in re.finditer(r"#define\s+(DT_\w+)\s+(0x[0-9A-Fa-f]+)", (ROOT / "src/game/ducktales_symbols.h").read_text()):
        syms[m.group(1)] = int(m.group(2), 16)
    src = (ROOT / "src/decomp/flow/flow_table.c").read_text()
    out = [
        "",
        HEAD + ', docs/DECOMPILATION.md "Task code"): the',
        "# entries of the main task's decompiled code that original code reaches (its",
        "# resume points have no site: the RTS observer starts them).",
        "",
    ]
    count = 0
    for m in re.finditer(r'(?:ROUTINE|TASK)(?:_AT\(\w+, |_POLL\(|\()(\w+), SITE\("([\w.]+)"\), "(\w+)"', src):
        sym, site, name = m.groups()
        addr = syms[sym]
        bank = int(site.split(".")[0]) if "." in site else 7
        out += [f"# {name}", "[[mod_function_hook]]", f'id = "ducktales.decomp.{site}"', f"addr = 0x{addr:04X}"]
        out += ["length = 8", f"crc32 = 0x{crc(bank, addr):08X}"]
        out += ["include_internal = true"] if site in INTERNAL else []
        out += [""]
        count += 1
    out.append(TAIL)
    text = TOML.read_bytes().decode().replace("\r\n", "\n")
    block = "\n".join(out) + "\n"
    start = text.find("\n" + HEAD)
    if start >= 0:
        end = text.index(TAIL, start) + len(TAIL) + 1
        text = text[:start] + block + text[end:]
    else:
        text = text.rstrip("\n") + "\n" + block
    TOML.write_bytes(text.replace("\n", "\r\n").encode())
    print(f"{count} sites")


if __name__ == "__main__":
    main()
