"""Decode DuckTales background screens from the user's ROM, check them against a frame log,
and render screens or whole maps to PNG (output under captures/, never committed).

Background only: the tool reads the screen, block, metatile, CHR-list and palette tables the
game itself uses (docs/rendering.md) and draws nametable tiles from the BG pattern table.

  map grid ($C7B0)      rows x 8 screen ids per map ($F6), $FF = none
  screen id (bank 4)    $8000 + id*72: 64 block ids (8x8 blocks of 32x32 px, row-major), then
                        8 bytes holding a 9th bit per block (byte = block row, bit 7 = column 0)
  block (bank $F7)      $B300 + block*4: metatile ids of the TL, TR, BL, BR 16x16 quadrants
  metatile (bank $F7)   tiles at $BB00/$BC00/$BD00/$BE00 + id (TL, TR, BL, BR);
                        $BF00 + id: bits 0-1 palette, bits 4-5 collision class
  CHR                   $C301 list per $F6 (land load) + $C42C list per land row
  palette               $DA34 + 16*$F6 (background), $DAC4 (sprites), $C6D9 (sprite palette 3 per row)

    py -3 -I tools/render_map.py ROM tables
    py -3 -I tools/render_map.py ROM check LOG [--frame N] [--picture]
    py -3 -I tools/render_map.py ROM screen MAP ROW PAGE OUT.png [--scale S]
    py -3 -I tools/render_map.py ROM land MAP OUT.png [--scale S]

'check' compares a --frame-log record with what the tables predict: nametable tiles and
attributes per 32-pixel column (HUD rows of nametable 0 excluded), the attribute shadow at
$0500, CHR RAM, the palette and, with --picture, the background pixels below the HUD.
PNG colours come from a generated NTSC approximation; compare colour indices with 'check'.
"""
import argparse
import colorsys
import struct
import zlib
from pathlib import Path

GRID_ROWS, GRID_STRIDE, GRID_LO, GRID_HI = 0xCE57, 0xCE61, 0xCF7F, 0xCF89
TILESET_BANK = 0xDA10
BG_PALETTES, SPR_PALETTE = 0xDA34, 0xDAC4
LOAD_LO, LOAD_HI = 0xC358, 0xC363
ROW_BASE, ROW_LIST, ROW_SPRPAL = 0xC508, 0xC511, 0xC53A
ROWLIST_LO, ROWLIST_HI, SPRPAL3 = 0xC563, 0xC583, 0xC6D9
HUD_TILE_ROWS = 6


def nes_rgb(v):
    """Rough NTSC colour for a 6-bit NES colour index (display only)."""
    hue, lum = v & 15, (v >> 4) & 3
    if hue >= 14:
        return (0, 0, 0)
    light = (0.35, 0.6, 0.85, 0.95)[lum]
    if hue == 0:
        g = int(255 * (0.4, 0.67, 1.0, 1.0)[lum])
        return (g, g, g)
    if hue == 13:
        g = int(255 * (0.0, 0.05, 0.3, 0.6)[lum])
        return (g, g, g)
    sat = (0.75, 0.75, 0.75, 0.35)[lum]
    r, g, b = colorsys.hls_to_rgb(((hue - 1) / 12.0 + 0.62) % 1.0, light * 0.6 + 0.05, sat)
    return (int(r * 255), int(g * 255), int(b * 255))


class Rom:
    def __init__(self, path):
        data = Path(path).read_bytes()
        self.prg = data[16:16 + data[4] * 0x4000]

    def rd(self, addr, bank=7):
        if addr >= 0xC000:
            return self.prg[7 * 0x4000 + addr - 0xC000]
        return self.prg[bank * 0x4000 + addr - 0x8000]

    def blob(self, bank, addr, n):
        off = bank * 0x4000 + addr - 0x8000
        return self.prg[off:off + n]

    def grid(self, m):
        rows, stride = self.rd(GRID_ROWS + m), self.rd(GRID_STRIDE + m)
        ptr = self.rd(GRID_LO + m) | self.rd(GRID_HI + m) << 8
        return rows, stride, [[self.rd(ptr + r * stride + c) for c in range(stride)] for r in range(rows)]

    def chr_list(self, ptr):
        """3-byte records (bank << 4 | pages - 1, source page, PPU page); a byte >= $80 ends it."""
        out = []
        while self.rd(ptr) < 0x80:
            b = self.rd(ptr)
            out.append((b >> 4, (b & 15) + 1, self.rd(ptr + 1), self.rd(ptr + 2)))
            ptr += 3
        return out

    def load_list(self, x):
        return self.chr_list(self.rd(LOAD_LO + x) | self.rd(LOAD_HI + x) << 8)

    def row_index(self, land, row, page):
        """$C42C: list for a land row; bit 7 = two lists chosen by camera page ($F3)."""
        x = self.rd(ROW_LIST + self.rd(ROW_BASE + land) + row)
        if x & 0x80:
            x &= 0x7F
            if page >= (3 if land == 4 else 4):
                x += 1
        return x

    def row_list(self, land, row, page):
        x = self.row_index(land, row, page)
        return self.chr_list(self.rd(ROWLIST_LO + x) | self.rd(ROWLIST_HI + x) << 8)

    def sprite_pal3(self, land, row):
        k = self.rd(ROW_SPRPAL + self.rd(ROW_BASE + land) + row) * 4
        return [self.rd(SPRPAL3 + k + j) for j in range(4)]

    def build_chr(self, land, row=None, page=0):
        chr_ram = bytearray(0x2000)
        lists = self.load_list(land) + (self.row_list(land, row, page) if row is not None else [])
        for bank, pages, src, dst in lists:
            chr_ram[dst << 8:(dst + pages) << 8] = self.blob(bank, src << 8, pages * 256)
        return chr_ram

    def palette(self, land, row=None):
        pal = [self.rd(BG_PALETTES + 16 * land + i) for i in range(16)]
        pal += [self.rd(SPR_PALETTE + i) for i in range(16)]
        if row is not None:
            pal[28:32] = self.sprite_pal3(land, row)
        return pal

    def screen(self, sid, tbank):
        """(tiles[30][32], attributes[64], collision[64]) as $CCBD/$C830 build them."""
        raw = self.blob(4, 0x8000 + sid * 72, 72)
        tiles = [[0] * 32 for _ in range(30)]
        attr, coll = [0] * 64, [0] * 64
        for r in range(8):
            for c in range(8):
                blk = raw[r * 8 + c] | (0x100 if raw[64 + r] & (0x80 >> c) else 0)
                for q in range(4):
                    if r == 7 and q >= 2:
                        continue
                    m = self.rd(0xB300 + blk * 4 + q, tbank)
                    ty, tx = r * 4 + (q >> 1) * 2, c * 4 + (q & 1) * 2
                    tiles[ty][tx] = self.rd(0xBB00 + m, tbank)
                    tiles[ty][tx + 1] = self.rd(0xBC00 + m, tbank)
                    tiles[ty + 1][tx] = self.rd(0xBD00 + m, tbank)
                    tiles[ty + 1][tx + 1] = self.rd(0xBE00 + m, tbank)
                    a = self.rd(0xBF00 + m, tbank)
                    attr[r * 8 + c] |= (a & 3) << (2 * q)
                    coll[r * 8 + c] |= ((a >> 4) & 3) << (2 * q)
        return tiles, attr, coll


def read_log(path, want=None):
    with open(path, 'rb') as f:
        if f.read(8) != b'CYCFRAME':
            raise SystemExit('not a frame log')
        version, = struct.unpack('<I', f.read(4))
        nlen = 6 if version >= 2 else 5
        last = None
        while True:
            head = f.read(12 + 4 * nlen)
            if len(head) < 12 + 4 * nlen:
                return last
            frame, _, _, *lens = struct.unpack('<III%dI' % nlen, head)
            pal, oam = f.read(32), f.read(256)
            blobs = [f.read(n) for n in lens]
            last = dict(frame=frame, palette=pal, oam=oam, ram=blobs[0], ciram=blobs[1], chr=blobs[3],
                        pic=struct.unpack('<%dH' % (256 * 240), blobs[4]))
            if want is not None and frame == want:
                return last


def render_tiles(tiles, attr, chr_ram, pal):
    """256x240 colour indices from nametable tiles/attributes (BG pattern table $1000)."""
    out = [[pal[0]] * 256 for _ in range(240)]
    for ty in range(30):
        for tx in range(32):
            a = attr[(ty >> 2) * 8 + (tx >> 2)]
            p = (a >> (((ty >> 1) & 1) * 4 + ((tx >> 1) & 1) * 2)) & 3
            o = 0x1000 + tiles[ty][tx] * 16
            for y in range(8):
                lo, hi = chr_ram[o + y], chr_ram[o + y + 8]
                row = out[ty * 8 + y]
                for x in range(8):
                    v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                    row[tx * 8 + x] = pal[p * 4 + v] if v else pal[0]
    return out


def write_png(path, rows, scale=1):
    lut = [bytes(nes_rgb(v)) for v in range(64)]
    raw = bytearray()
    for row in rows:
        line = b''.join(lut[v & 0x3F] * scale for v in row)
        for _ in range(scale):
            raw += b'\x00' + line

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    h, w = len(rows) * scale, len(rows[0]) * scale
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def cmd_check(rom, a):
    log = read_log(a.log, a.frame)
    if log is None or (a.frame is not None and log['frame'] != a.frame):
        raise SystemExit('frame not in log')
    ram = log['ram']
    land, row, cx, page, tbank = ram[0xF6], ram[0xF1], ram[0xF2], ram[0xF3], ram[0xF7]
    print('frame %d: $F6=%d $F1=%d camera $F3:$F2=%d:%02X $2D=%02X $24=%02X $26=%02X $F7=%d (table: %d)'
          % (log['frame'], land, row, page, cx, ram[0x2D], ram[0x24], ram[0x26], tbank, rom.rd(TILESET_BANK + land)))
    _, stride, grid = rom.grid(land)
    cands = {p: grid[row][p] for p in range(stride) if grid[row][p] != 0xFF}
    total = [0, 0, 0, 0]
    for nt in (0, 1):
        ci = log['ciram'][nt * 0x400:(nt + 1) * 0x400]
        dec = {p: rom.screen(s, tbank) for p, s in cands.items() if p & 1 == nt}
        first = HUD_TILE_ROWS if nt == 0 else 0
        parts = []
        for c in range(8):
            best = None
            for p, (tiles, attr, _) in dec.items():
                ok = sum(ci[ty * 32 + tx] == tiles[ty][tx] for ty in range(first, 30) for tx in range(c * 4, c * 4 + 4))
                aok = sum(ci[0x3C0 + r * 8 + c] == attr[r * 8 + c] for r in range(2 if nt == 0 else 0, 8))
                if best is None or (ok, aok) > best[1:3]:
                    best = (p, ok, aok)
            n_t, n_a = (30 - first) * 4, 8 - (2 if nt == 0 else 0)
            if best:
                parts.append('p%d:%d/%d,%d/%d' % (best[0], best[1], n_t, best[2], n_a))
                total[0] += best[1]
                total[2] += best[2]
            total[1] += n_t
            total[3] += n_a
        print('nametable %d columns 0-7 (page: tiles, attributes): %s' % (nt, ' '.join(parts)))
    print('tiles %d/%d, attribute bytes %d/%d equal the best-matching screen of row %d'
          % (total[0], total[1], total[2], total[3], row))
    for nt in (0, 1):
        ci = log['ciram'][nt * 0x400:(nt + 1) * 0x400]
        same = sum(ram[0x500 + nt * 64 + i] == ci[0x3C0 + i] for i in range(64))
        print('attribute shadow $%04X-$%04X = nametable %d attributes: %d/64' % (0x500 + nt * 64, 0x53F + nt * 64, nt, same))
    exp, got = rom.build_chr(land, row, page), log['chr']
    for lo, hi, name in ((0x0000, 0x0800, 'sprite, common'), (0x0800, 0x0900, 'sprite, per land'),
                         (0x0900, 0x1000, 'sprite, per row'), (0x1000, 0x1300, 'background, common'),
                         (0x1300, 0x2000, 'background, per land')):
        same = sum(exp[i] == got[i] for i in range(lo, hi))
        print('CHR $%04X-$%04X (%s): %d/%d bytes equal the ROM lists' % (lo, hi - 1, name, same, hi - lo))
    pal = rom.palette(land, row)
    same = sum((log['palette'][i] & 0x3F) == pal[i] for i in range(32) if i & 3)
    print('palette RAM %s' % ' '.join('%02X' % v for v in log['palette']))
    print('ROM tables  %s  (%d/24 non-backdrop entries equal)' % (' '.join('%02X' % v for v in pal), same))
    if a.picture:
        ci = log['ciram']
        bgpal = [log['palette'][i] & 0x3F for i in range(16)]
        nts = []
        for nt in (0, 1):
            t = [[ci[nt * 0x400 + ty * 32 + tx] for tx in range(32)] for ty in range(30)]
            nts.append(render_tiles(t, list(ci[nt * 0x400 + 0x3C0:nt * 0x400 + 0x400]), got, bgpal))
        dec = {p: render_tiles(*rom.screen(s, tbank)[:2], got, bgpal) for p, s in cands.items() if p in (page, page + 1)}
        top = ram[0xFA] + 9
        ok_nt = ok_rom = tot = 0
        for y in range(top, 240):
            sy = (y + ram[0xF0]) % 240
            for x in range(256):
                wx = x + cx
                pix = log['pic'][y * 256 + x] & 0x3F
                tot += 1
                ok_nt += pix == nts[((page + (wx >> 8)) & 1)][sy][wx & 255]
                src = dec.get(page + (wx >> 8))
                ok_rom += src is not None and pix == src[sy][wx & 255]
        print('picture lines %d-239: %d/%d pixels equal the nametable render, %d/%d the ROM-only render'
              ' (the rest are sprites)' % (top, ok_nt, tot, ok_rom, tot))


def cmd_tables(rom, _a):
    for x in range(11):
        print('$C301 list %d: %s' % (x, '; '.join('bank %d $%02X00 -> PPU $%04X x%d' % (b, s, d << 8, n)
                                                for b, n, s, d in rom.load_list(x))))
    for land in range(5):
        for row in range(rom.grid(land)[0]):
            x, x2 = rom.row_index(land, row, 0), rom.row_index(land, row, 7)
            lst = '; '.join('bank %d $%02X00 -> PPU $%04X x%d' % (b, s, d << 8, n) for b, n, s, d in rom.row_list(land, row, 0))
            print('land %d row %d: $C42C list %d%s, sprite palette 3 = %s: %s' % (
                land, row, x, ' (list %d from the split page on)' % x2 if x2 != x else '',
                ' '.join('%02X' % v for v in rom.sprite_pal3(land, row)), lst or 'nothing'))
    for land in range(9):
        print('$F6=%d: tileset bank $F7=%d, BG palette %s' % (land, rom.rd(TILESET_BANK + land), ' '.join(
            '%02X' % rom.rd(BG_PALETTES + 16 * land + i) for i in range(16))))
    print('sprite palette %s' % ' '.join('%02X' % rom.rd(SPR_PALETTE + i) for i in range(16)))


def render_map_screen(rom, m, sid, chr_ram, pal):
    return render_tiles(*rom.screen(sid, rom.rd(TILESET_BANK + m))[:2], chr_ram, pal)


def cmd_screen(rom, a):
    sid = rom.grid(a.map)[2][a.row][a.page]
    if sid == 0xFF:
        raise SystemExit('no screen there')
    img = render_map_screen(rom, a.map, sid, rom.build_chr(a.map), rom.palette(a.map))
    write_png(a.out, img, a.scale)
    print('map %d row %d page %d: screen id $%02X -> %s' % (a.map, a.row, a.page, sid, a.out))


def cmd_land(rom, a):
    rows, stride, grid = rom.grid(a.map)
    chr_ram, pal = rom.build_chr(a.map), rom.palette(a.map)
    out = [[0x0F] * (256 * stride) for _ in range(240 * rows)]
    for r in range(rows):
        for c in range(stride):
            if grid[r][c] != 0xFF:
                img = render_map_screen(rom, a.map, grid[r][c], chr_ram, pal)
                for y in range(240):
                    out[r * 240 + y][c * 256:(c + 1) * 256] = img[y]
    write_png(a.out, out, a.scale)
    print('map %d: %d rows x %d screens -> %s' % (a.map, rows, stride, a.out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('log')
    c.add_argument('--frame', type=int)
    c.add_argument('--picture', action='store_true')
    sub.add_parser('tables')
    s = sub.add_parser('screen')
    for k in ('map', 'row', 'page'):
        s.add_argument(k, type=int)
    s.add_argument('out')
    s.add_argument('--scale', type=int, default=1)
    l = sub.add_parser('land')
    l.add_argument('map', type=int)
    l.add_argument('out')
    l.add_argument('--scale', type=int, default=1)
    a = ap.parse_args()
    dict(check=cmd_check, tables=cmd_tables, screen=cmd_screen, land=cmd_land)[a.cmd](Rom(a.rom), a)


if __name__ == '__main__':
    main()
