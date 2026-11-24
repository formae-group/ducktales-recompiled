"""Mods (src/mods, docs/MODS.md) on the headless build, with mods made on the fly.

Nothing derived from the game is kept: every mod is generated in a temporary folder, the tile
templates come from the player's own ROM through --mods-dump and are deleted with it.

  1. the machine is untouched: with a palette, a tile and a music replacement loaded the
     per-frame hashes (--hash-out: trace, RAM, picture, registers, cycles, hardware) equal a run
     without mods, frames 0-1500 of amazon_intro;
  2. palette swap (palettes/global.pal, every colour inverted): every pixel of the presented
     picture is the inverse of the picture without mods, at frames 300 (Land Select) and 1500
     (The Amazon); a per-land palette (palettes/amazon.pal) changes the Amazon frame and leaves
     Land Select alone;
  3. one tile replaced (tiles/<key>.png from --mods-dump): with the template itself as the art
     the picture is identical (placement, flips and palette key are right); with solid magenta
     every changed pixel is magenta and there is at least one;
  4. priority: two mods replacing the same tile (magenta, cyan) and two global palettes show the
     top mod's, and the other one after --mods-order swaps them; switching the top one off shows
     the other;
  5. a mod whose prg_sha256 is another game's is refused with "Made for a different version of
     the game" and changes nothing; a broken archive (random bytes) and a truncated one are
     listed as "Cannot be read: ..." and the other mods still load;
  6. music: a generated 440 Hz tone as music/07.wav (the title song): the sound is identical
     until the frame after the title requests it (frame 7), then the 440 Hz tone carries almost
     all of the energy (the driver's channels are muted), while without the mod it does not;
  7. with the mod loaded the machine's own audio hash is unaffected: the run's --hash-out equals
     the run without mods (step 1 covers it);
  8. scripts (the native player, else headless): the sandbox, the limits and errors with where they
     ran, a dt.CANCEL an event cannot take, dt.state, dt.set, toasts (cut at 64 UTF-8 characters),
     dt.store (no temporary file left), mods.log in the data folder and hot reload;
  9. the catalogue: mod.toml and main.lua with a byte order mark, names in any case, mod.toml
     errors (missing keys, unquoted value, key set twice, malformed prg_sha256), two files for one
     tile, palette or sound, and --mods-dir with a path that is not a folder;
 10. events, state and drawing on full_amazon (headless, and the native player when it is built):
     land-start, checkpoint, boss-hit and boss-defeated, land-clear once each with the land,
     enemy-defeated and item-collected with their slot, kind or item and position, money-changed
     deltas that add up to the money; dt.objects() and dt.camera() agree with dt.state; dt.draw
     shows its rectangle and text over the presented picture and stops at 64 a frame; with all of
     it running the per-frame machine hashes equal a run without mods, and the native player logs
     the same events in the same frames.

    py -3 -I tests/mods_test.py --build build/headless
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mods.assets import headless_checks, native_checks  # noqa: E402
from mods.catalogue import catalogue_checks  # noqa: E402
from mods.common import ROOT, failures  # noqa: E402
from mods.events import event_checks  # noqa: E402
from mods.scripts import script_checks  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--native-build', default='build/windows', help='the native player\'s build ("" to skip it)')
    ap.add_argument('--native-only', action='store_true', help='only the native player\'s checks')
    args = ap.parse_args()
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    route = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
    native = ROOT / args.native_build / args.config / 'ducktales.exe' if args.native_build else None
    if native and not native.exists():
        print(f'SKIP  the native player: {native} is not built')
        native = None
    if not args.native_only:
        headless_checks(exe, route)
    if native:
        native_checks(native, route)
    script_checks(native or exe, route, native is not None)
    catalogue_checks(native or exe, route, native is not None)
    event_checks(None if args.native_only else exe, native)
    print(f'\n{"FAILED: " + ", ".join(failures) if failures else "all mods checks passed"}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
