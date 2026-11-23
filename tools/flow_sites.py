"""Writes the game-flow block of config/game.toml from src/decomp/flow.c.

The block holds a site for each flow routine that original code reaches
(SITE("XXXX") in the table) and one for each routine the flow hands off to
(STOP), where an isolated run of the original stops. Resume points have no
site: the RTS observer starts them. The content key is the CRC of the first
8 bytes, read from the local ROM (never committed).

    py -3 -I tools/flow_sites.py
"""
import re
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "roms" / "DuckTales (USA).nes"
TOML = ROOT / "config" / "game.toml"
HEAD = "# Game flow (src/decomp/flow.c"
TAIL = "# END game flow"


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
    src = (ROOT / "src/decomp/flow.c").read_text()
    out = [
        "",
        HEAD + ', docs/decompilation.md "Task code"): the',
        "# entries of the main task's decompiled code that original code reaches (its",
        "# resume points have no site: the RTS observer starts them), then the",
        "# routines it hands off to, where an isolated run of the original stops.",
        "",
    ]
    count = 0
    for m in re.finditer(r'(?:ROUTINE|TASK)\((\w+), SITE\("(\w+)"\), "(\w+)"', src):
        sym, site, name = m.groups()
        addr = syms[sym]
        out += [f"# {name}", "[[mod_function_hook]]", f'id = "ducktales.decomp.{site}"', f"addr = 0x{addr:04X}"]
        out += ["length = 8", f"crc32 = 0x{crc(7, addr):08X}", ""]
        count += 1
    for m in re.finditer(r'STOP\((\w+), "([\w.]+)"\)', src):
        sym, site = m.groups()
        addr = syms[sym]
        bank = int(site.split(".")[1]) if "." in site else 7
        out += ["[[mod_function_hook]]", f'id = "ducktales.decomp.stop.{site}"', f"addr = 0x{addr:04X}"]
        out += ["length = 8", f"crc32 = 0x{crc(bank, addr):08X}", ""]
        count += 1
    out.append(TAIL)
    text = TOML.read_bytes().decode().replace("\r\n", "\n")
    start = text.find("\n" + HEAD)
    if start >= 0:
        end = text.index(TAIL, start) + len(TAIL) + 1
        text = text[:start] + text[end:]
    text = text.rstrip("\n") + "\n" + "\n".join(out) + "\n"
    TOML.write_bytes(text.replace("\n", "\r\n").encode())
    print(f"{count} sites")


if __name__ == "__main__":
    main()
