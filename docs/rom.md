# ROM identity

The port targets one cartridge revision. The user supplies it; it is never committed.

| Field | Value |
|---|---|
| File | `DuckTales (USA).nes` (131,088 bytes) |
| SHA-256 (file) | `97d472f7b2c1088448d1644155b2650a546bad887b3cba571e16d7986824c169` |
| SHA-1 (file) | `f83191749d019d75eb5ff95d247ef25882c2571a` |
| CRC32 (file) | `06B994B2` |
| SHA-256 (PRG, header excluded) | `8ba8baed01a9fbaf1e9ff29e0c9825db1963ac2aff211d6f1f3bcfd3839e2013` |
| SHA-1 (PRG, header excluded) | `6e700e8f72f767918b096f6a5f17f8cd1e2d0866` |
| CRC32 (PRG, header excluded) | `EFB09075` |
| Region | USA (NTSC) |

## Header

`4E 45 53 1A 08 00 21 00 00 00 00 4E 49 31 2E 33`

- iNES 1.0 (byte 7 bits 2-3 are `00`, so not NES 2.0).
- PRG-ROM: 8 x 16 KiB = 128 KiB. CHR-ROM: none (byte 5 = 0), so the board uses 8 KiB CHR-RAM.
- Flags 6 = `$21`: vertical mirroring, no battery, no trainer, mapper low nibble 2.
- Flags 7 = `$00`: mapper high nibble 0, so mapper 2 (UxROM).
- Bytes 11-15 hold the ASCII string `NI1.3` left by a dumping tool. Byte 7 is zero, so mapper
  decoding is unaffected; NESRecomp reads the header without complaint.

## Board behaviour observed in code

- Vectors: NMI `$C000`, RESET `$FEE0`, IRQ `$C000` (the game never enables IRQs; 0 IRQs in every
  route run).
- The last 16 KiB bank (bank 7) is fixed at `$C000-$FFFF`; banks 0-7 can be selected at `$8000`.
- The bank-select routine writes through a table in the fixed bank whose byte equals the value
  written (`$FFE5+n = n`), which avoids UxROM bus conflicts. See [functions.md](functions.md).

## Compatibility

NESRecomp's cycle backend identifies the image as "UxROM, 128 KB PRG in 32 banks of 4KB" and the
compiled program checks the ROM's SHA-256 before running. Any other dump or revision is refused.
