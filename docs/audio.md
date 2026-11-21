# Audio

- The sound driver lives in bank 3. Every NMI maps bank 3 without touching `$E7`, then:
  1. drains the request ring `$C8-$CF` (read index `$C7`, empty marker `$88`), calling `$8003`
     with the request id in A for each entry and marking it empty;
  2. calls `$8000`, the per-frame update;
  3. restores the bank saved in `$E7`.
- Game code therefore requests sounds by writing ids into the ring; the writers are not yet
  identified.
- APU output comes from NESRecomp's cycle-level APU with the NES front-loader output stage
  (`console = "nes"` in `config/game.toml`).

Song and effect ids are not yet catalogued.
