# Rendering

Facts below come from the NMI handler at `$C000` (static reading) and from route runs.

- The NMI saves P, A, X and Y, then (when `$EA` is set) rewrites sprite 0 from `$FA` for the HUD split.
- Unless `$E8` is set it disables rendering, sends sprite DMA from `$0200` (skipped when `$98` is
  set), runs two optional VRAM update routines (`$C139` when `$19` is set, `$C15E` when `$1A` is
  set), uploads the palette from `$0300` when `$18` is set, and writes the scroll from `$FD`/`$FC`.
- With `$EA` set it polls `$2002` for the sprite-0 hit and then writes a second scroll and
  nametable (`$F0`, `$F2`, `$F3`) for the playfield under the HUD.
- CHR is RAM, so tiles are uploaded by the game; mirroring is fixed vertical.

The PPU itself is NESRecomp's cycle-level PPU (`runner/cyc/hw_ppu.c`); route runs match the
TriCNES reference picture on every frame (see [validation.md](validation.md)).
