"""Decompilation checks (src/decomp/, docs/decompilation.md).

  check    every route runs with --decomp check: at every call of every
           decompiled routine (from a site or from another routine's C,
           decomp.c) the original runs in an isolated call and the C
           on the same state, and RAM (the dead stack excepted) and the
           routine's contract registers must agree; the first difference stops
           the run (exit 3). Prints how often each routine was called,
           compared, or given to the original routine.
  replace  (needs --dev-build) every route runs with --decomp replace
           against the check run (a stock run without check mode), both
           with --dt-ram-log: check mode keeps the original's result, so its
           RAM is the stock run's byte for byte, and the check then runs on
           the dev build. CPU RAM outside
           the stack ($0161-$01FF) must be equal after every frame. The stack
           is compared where
           both runs sit in the game's scheduler with every task yielded
           (S = $FF): there the main task's live stack (above its saved S up
           to its base $BF) must be equal. The rest of page 1 holds what
           interrupts pushed, which differs because an NMI the original takes
           inside a replaced routine is taken right after it.
  pokes    (needs --dev-build) states no route reaches, made with
           --dt-poke-at (and extra pad states, written into a copy of the
           route) from a point of re_amazon_moves (POKES below), started
           from the latest golden save state before the first poke where the
           scheduler is idle (power-on without one): each
           case runs in check mode, where the routines it targets must be
           compared at least once, and stock against replace as above.
  events   (needs --dev-build) enhancements whose event a decompiled routine
           raises itself (EVENTS below): check mode, and the same options
           with --decomp off against --decomp replace as above.

    py -3 -I tests/decomp_test.py --build build/headless [--dev-build build/headless-dev]
           [--routes tests/routes/X.txt ...] [--modes check replace pokes events] [--jobs 2]
           [--changed RANGE ... [--from-state [--span N]]] [--golden DIR]

--changed [BANK:]ADDR[-ADDR] ... keeps only the routes whose quick_verify coverage (the golden
directory, tests/quick_verify.py) executes those PRG addresses; --from-state starts each of them
from the latest golden save state at or before the first segment that does where the game's
scheduler is idle (S = $FF on the frame before it: a state inside a task would resume the original's
task while replace mode runs the C), and --span N stops N frames after that segment's start. That is the loop for one routine; the merge gate is every route from power-on.

Every case goes into one queue of --jobs workers, longest first.

DUCKTALES_DECOMP=check py -3 -I tests/verify_routes.py runs the oracle comparison with
the checks on.
"""

import argparse
import concurrent.futures
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import quick_verify  # noqa: E402
from romfile import default_rom  # noqa: E402

RAM = 0x800
IDLE_S = 0xFF
TASKS = 0x80
MAIN_TASK_BASE = 0xBF
STACK = 0x161  # page 1 below this is data: palette, consumed-record bits, HUD time (game-over box to $0160)
LINE = re.compile(
    r'^decomp: mode=\S+ routine=(\S+) addr=\$([0-9A-F]{4}) calls=(\d+) compared=(\d+) original=(\d+)$', re.M
)

SYMBOLS = {}
for _name, _addr in re.findall(
    r'^#define DT_ADDR_(\w+)\s+0x([0-9A-Fa-f]{4})', (ROOT / 'src' / 'game' / 'ducktales_symbols.h').read_text(), re.M
):
    SYMBOLS[int(_addr, 16)] = SYMBOLS[int(_addr, 16)] + '/' + _name if int(_addr, 16) in SYMBOLS else _name
NAMED = re.compile(r'\$([0-9A-F]{4})(?= original)')

POKE_ROUTE = ROOT / 'tests' / 'routes' / 're_amazon_moves.txt'
POKE_FRAME = 1700  # Scrooge on the ground in the Amazon
POKE_FRAMES = 220
MESSAGE_FRAMES = 700
MESSAGE_PRESSES = {
    **{frame: '-' for frame in (1780, 1788, 1930, 2000, 2008)},
    **{POKE_FRAME + 120 + 30 * i: 'A' for i in range(19)},
    **{POKE_FRAME + 124 + 30 * i: '-' for i in range(19)},
}
# kind $38 in slot 3 at Scrooge's place in the Amazon at POKE_FRAME
SKY_ROPE = {
    0x703: 0xA0,
    0x603: 0xA0,
    0x623: 0x5A,
    0x643: 0xA9,
    0x723: 0x60,
    0x733: 0x00,
    0x713: 0x38,
    0x7D3: 0x00,
    0x633: 0x00,
    0x613: 0x00,
}
# kind $3A as the spawner makes it (hidden, action $84) in slot 3
STAND_IN = {
    0x703: 0xB0,
    0x603: 0xA0,
    0x623: 0x84,
    0x713: 0x3A,
    0x7D3: 0x00,
    0x693: 0x00,
    0x6B3: 0x00,
    0x6C3: 0x00,
    0x6D3: 0x00,
    0x6F3: 0x00,
    0x733: 0x00,
    0x723: 0x60,
    0x633: 0x00,
    0x613: 0x00,
}
# two kind $3C climbers: slot 3 on the map row's screen 5 just above a screen's top, slot 15 from its
# step 0; slot 15 is then put on screen 0 below Y $90 to stop them
CLIMBERS = {
    0x703: 0xA1,
    0x603: 0xA0,
    0x713: 0x3C,
    0x643: 0x05,
    0x6B3: 0x08,
    0x6C3: 0x05,
    0x6D3: 0x00,
    0x733: 0x00,
    0x723: 0x60,
}
CLIMBERS |= {0x70F: 0xA0, 0x60F: 0xA0, 0x71F: 0x3C, 0x64F: 0x40, 0x6BF: 0x00, 0x6CF: 0x00, 0x6DF: 0x00}
# kind $3D above Scrooge in slot 3 with message 6, hit at POKE_FRAME + 10
HIT_TALKER = {0x703: 0xA0, 0x603: 0xA0, 0x713: 0x3D, 0x623: 0x5A, 0x643: 0x80, 0x723: 0x60, 0x733: 0x00, 0x7D3: 0x00}
HIT_TALKER |= {0x6B3: 0x06, 0x6C3: 0x00, 0x693: 0x00, 0x633: 0x00, 0x613: 0x00}
# kind $40 in slot 3 (Y and world X added per case); slot 15's $06D0 is the climbers' arrival
FINAL_TREASURE = {
    0x703: 0xA0,
    0x603: 0xA0,
    0x713: 0x40,
    0x623: 0x5A,
    0x733: 0x00,
    0x7D3: 0x00,
    0x633: 0x00,
    0x613: 0x00,
    0x6DF: 0x00,
}
# Transylvania chest templates in slot 9. Keep each record's pose, Y and subtype,
# rebase its world X to page 0 and put Scrooge in cane contact.
CHEST_SWING = {0x39: 0x0E, 0x620: 0x09, 0x610: 3, 0x630: 0}
CHEST_RECORD = {
    0xF6: 0x01,
    0x709: 0xB0,
    0x609: 0xB0,
    0x719: 0x15,
    0x629: 0x28,
    0x739: 0x00,
    0x730: 0x00,
    0x7D9: 0x05,
    0x619: 0x00,
    0x639: 0x00,
    0x699: 0x00,
    0x6C9: 0x00,
} | CHEST_SWING
# name, {address: value} stored before POKE_FRAME (or {frame: {...}}), routines that must be compared,
# optionally {frame: buttons} pad states added to the route
ENDING_FRAMES = 3950  # to the frames after the last screen, about 3880 frames in
ENDING_WANT = [
    'Palette_LoadScene',
    'Ending_Waited',
    'EndingScreen_Streamed',
    'EndingScreen_Lit',
    'EndingObjects',
    'EndingObjects_Update',
    'EndingObjects_Exit',
    'EndingObjects_Done',
    'Ending_Split',
    'Ending_Second',
    'Ending_OverNext',
]
POKES = [
    (
        'chest-opening',
        CHEST_RECORD | {0x7F9: 0x53, 0x6B9: 0, 0x649: 0xB0, 0x640: 0xB0, 0x729: 0xF0, 0x720: 0xEA},
        ['Obj_Chest', 'Obj_ChestOpening'],
    ),
    (
        'chest-slider',
        CHEST_RECORD | {0x7F9: 0x34, 0x6B9: 2, 0x649: 0x70, 0x640: 0x70, 0x729: 0x70, 0x720: 0x6A},
        ['Obj_Chest', 'Obj_ChestSlider'],
    ),
    (
        'chest-health',
        {
            POKE_FRAME: CHEST_RECORD | {0x7F9: 0x35, 0x6B9: 3, 0x649: 0xB0, 0x640: 0xB0, 0x729: 0x70, 0x720: 0x6A},
            POKE_FRAME + 30: CHEST_SWING,
        },
        ['Obj_Chest', 'Obj_ChestOpening', 'Obj_ChestHealth', 'Pickup_Take'],
    ),
    ('hurt-wait', {0x39: 0x05, 0x7D: 0x20}, ['Player_Hurt']),
    ('hurt-now', {0x39: 0x05, 0x7D: 0x00}, ['Player_Hurt', 'Player_TakeDamage']),
    ('hurt-last-dot', {0x39: 0x05, 0x7D: 0x00, 0x346: 0x01, 0x34A: 0x01}, ['Player_Hurt', 'Player_Death']),
    ('pit-falling', {0x39: 0x08}, ['Player_Pit']),
    ('pit-death', {0x39: 0x08, 0x640: 0xD8}, ['Player_Pit', 'Player_PitDeath']),
    (
        'clear-keep',
        {POKE_FRAME: {0x39: 0x08, 0x640: 0xD8, 0xF6: 0x01}}
        | {POKE_FRAME + 20 * i: {0x7E: 0x89} for i in range(1, 16)}
        | {POKE_FRAME + 10 + 40 * i: {0x6BE: 0x10, 0x6CE: 0x01, 0x6BF: 0x50, 0x6CF: 0x02} for i in range(8)},
        ['Player_PitDeath', 'Obj_ClearSlots'],
        None,
        400,
    ),
    ('carried-10', {0x39: 0x0A}, ['Player_Carried']),
    ('carried-11', {0x39: 0x0B}, ['Player_Carried']),
    ('mode12', {0x39: 0x0C}, ['Player_Mode12']),
    ('mode12-page7', {0x39: 0x0C, 0x780: 0x00, 0x730: 0x07}, ['Player_Mode12']),
    ('invulnerable-held', {0x7D: 0x90, 0x75: 0x80}, ['Player_Update']),
    ('slippery', {POKE_FRAME + 10 * i: {0x71: 0x01} for i in range(20)}, ['Player_Walk']),
    (
        'select-refill',
        {0x34A: 0x01, 0x334: 0x01},
        ['Player_SelectRefill'],
        {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'},
    ),
    ('select-poor', {0x34A: 0x01}, ['Player_SelectRefill'], {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'}),
    ('select-full', {}, ['Player_SelectRefill'], {POKE_FRAME + 8: 'LEFT+SELECT', POKE_FRAME + 10: 'LEFT'}),
    ('hud-total', {0xAB: 0x01}, ['Hud_RefreshTotal']),
    ('scene-1', {0x78: 0x81}, ['Scene_Placed', 'Scene_Done']),
    ('scene-2', {0x78: 0x82}, ['Scene_Placed', 'Scene_Done']),
    ('sky-rope', SKY_ROPE, ['Obj_SkyRope']),
    ('sky-rope-dying', SKY_ROPE | {0x39: 0x0D}, ['Obj_SkyRope']),
    ('stand-in', STAND_IN, ['Obj_FinalStandIn'], None, 520),
    (
        'stand-in-beaten',
        {POKE_FRAME: STAND_IN | {0xB1: 0x02}, POKE_FRAME + 10: {0x693: 0x01}, POKE_FRAME + 200: {0x693: 0x01}},
        ['Obj_FinalStandIn', 'Boss_Hit', 'Obj_StandInBeaten'],
        None,
        420,
    ),
    (
        'climbers',
        {POKE_FRAME: CLIMBERS, POKE_FRAME + 100: {0x6CF: 0x00, 0x6BF: 0x92}},
        ['Obj_Climber'],
    ),
    ('hit-talker', {POKE_FRAME: HIT_TALKER, POKE_FRAME + 10: {0x693: 0x01}}, ['Obj_HitTalker'], None, 300),
    ('final-treasure', FINAL_TREASURE | {0x643: 0xA9, 0x723: 0x60}, ['Obj_FinalTreasure'], None, 300),
    ('final-treasure-lost', FINAL_TREASURE | {0x643: 0x40, 0x723: 0xA0, 0x6DF: 0x01}, ['Obj_FinalTreasure']),
    ('mode15-redraw', {0x39: 0x0F, 0x6E: 0x0B}, ['Mode15_Redraw', 'Mode15_ArenaChr', 'Mode15_Spawned']),
    ('final-trip', {100: {0x5E: 0x01}}, ['LandSelect_FinalScene', 'FinalTrip_Next', 'FinalTrip_Flashed']),
    ('all-treasures', {100: {0x5D: 0x01, 0xA0: 0x1F}}, ['LandSelect_Banked', 'LandSelect_FinalWait']),
    (
        'total-max',
        {
            100: {
                0x5D: 0x01,
                0x322: 0x07,
                0x323: 0x2C,
                0x324: 0x24,
                0x325: 0x24,
                0x326: 0x01,
                0x327: 0x05,
                0x328: 0x00,
                0x329: 0x00,
                0x32A: 0x00,
                **{0x334 + i: 0x09 for i in range(5)},
            }
        },
        ['BankTransfer_Next'],
    ),
    # slot 9 made a pickup on Scrooge (step 2, waiting): the extra HP dot; a big diamond with money at 999...
    (
        'extra-dot',
        {0x709: 0xA2, 0x719: 0x13, 0x629: 0x2B, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x00, 0x7D9: 0x01, 0x609: 0x80},
        ['Pickup_Take', 'Pickup_ExtraDot', 'Hud_RefillHp'],
    ),
    # the same pickup holding the extra life ($8BB0, a timed item, so its timer is set), and item $74, a treasure past the five lands that
    # shows message $0D instead of the treasure pose ($8C37)
    (
        'extra-life',
        {
            0x709: 0xA2,
            0x719: 0x13,
            0x629: 0x27,
            0x6B9: 0xFF,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x7D9: 0x01,
            0x609: 0x80,
        },
        ['Pickup_Take'],
    ),
    (
        'secret-treasure',
        {0x709: 0xA2, 0x719: 0x13, 0x629: 0x74, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x00, 0x7D9: 0x01, 0x609: 0x80},
        ['Pickup_Take', 'Pickup_Treasure'],
    ),
    # the camera at the right end of Amazon row 5 ($2D = $2F) with Scrooge walking right past X $F0:
    # the camera stays and his X is clamped to $F0 ($F08B)
    (
        'row-end-right',
        {0xF2: 0x00, 0xF3: 0x07, 0x2D: 0x2F, 0x720: 0xF8, 0x730: 0x07, 0x650: 0xF8},
        ['Camera_Update'],
        {POKE_FRAME + 1: 'RIGHT'},
    ),
    # slot 9 made a launched block (kind 9) at step 6 ($87D0) and step 8 ($87FF) above the floor
    (
        'thrown-6',
        {0x709: 0xA6, 0x719: 0x09, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x789: 0x00, 0x609: 0x80},
        ['Obj_Thrown'],
    ),
    (
        'thrown-8',
        {0x709: 0xA8, 0x719: 0x09, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x789: 0x00, 0x609: 0x80},
        ['Obj_Thrown'],
    ),
    # kind $19 in the air at step 4 ($98C8): it falls, lands and waits; at step 2 next to Scrooge it pounces ($98A8)
    (
        'pouncer-land',
        {0x709: 0xA4, 0x719: 0x19, 0x629: 0x3C, 0x639: 0x00, 0x649: 0x80, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Pouncer'],
    ),
    (
        'pouncer-near',
        {0x709: 0xA2, 0x719: 0x19, 0x629: 0x39, 0x6B9: 0x05, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Pouncer'],
    ),
    # kind 7 on Scrooge: it grabs him ($85DA) and lets go when $7D reaches 8
    (
        'snapper-grab',
        {0x709: 0xA0, 0x719: 0x07, 0x629: 0x12, 0x619: 0x00, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Snapper'],
    ),
    # a pickup of action $29 off screen: it frees its slot and clears its record's consumed bit ($8A6B)
    (
        'pickup-off-screen',
        {0x709: 0xA2, 0x719: 0x13, 0x629: 0x29, 0x7F9: 0x0A, 0x649: 0xA6, 0x729: 0x62, 0x739: 0x05, 0x609: 0x00},
        ['Obj_Pickup'],
    ),
    # a boss fight with every treasure: the camera window admits record $74, which becomes the stand-in ($B005)
    ('spawn-stand-in', {0xFB: 0x80, 0xA0: 0x1F, 0xF2: 0x40}, ['Obj_SpawnRecord'], {POKE_FRAME: 'RIGHT'}),
    # a dropper (kind $0A) off screen frees its slot ($88B6)
    (
        'dropper-off-screen',
        {0x709: 0xA0, 0x719: 0x0A, 0x649: 0x90, 0x729: 0x90, 0x739: 0x05, 0x609: 0x00},
        ['Obj_Dropper'],
    ),
    # a treat (kind $2D) and a rising shot (kind $27) moving left ($A2F5, $A5D1)
    (
        'treat-left',
        {0x709: 0xA0, 0x719: 0x2D, 0x7E9: 0x02, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Treat'],
    ),
    (
        'rising-shot-left',
        {0x709: 0xA0, 0x719: 0x27, 0x7E9: 0x00, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_RisingShot'],
    ),
    # the sky flyer leaves its rope behind at page 0, X below $70, then rises off screen ($F98C)
    (
        'sky-flyer-exit',
        {
            0x709: 0xA2,
            0x719: 0x37,
            0x6B9: 0x00,
            0x6C9: 0x02,
            0x649: 0x80,
            0x729: 0x60,
            0x739: 0x00,
            0x7C9: 0x02,
            0x609: 0x80,
        },
        ['Obj_SkyFlyer'],
    ),
    # a solid collision cell meets the thrower's wall probes and the kicker's floor and wall probes
    (
        'thrower-wall',
        {
            0x709: 0xA1,
            0x719: 0x03,
            0x649: 0xA6,
            0x729: 0x90,
            0x739: 0x00,
            0x7C9: 0x00,
            0x7B9: 0x00,
            0x7E9: 0x01,
            0x609: 0x80,
            0x5AC: 0x55,
        },
        ['Obj_Thrower'],
    ),
    (
        'block-kicker-wall',
        {
            0x709: 0xA1,
            0x719: 0x1D,
            0x6C9: 0x00,
            0x649: 0xA6,
            0x729: 0x90,
            0x739: 0x00,
            0x7C9: 0x00,
            0x7B9: 0x00,
            0x7E9: 0x01,
            0x609: 0x80,
            0x5AC: 0x55,
        },
        ['Obj_BlockKicker'],
    ),
    # a treat restores a lost HP dot ($8B96)
    (
        'treat-restore-dot',
        {
            0x709: 0xA0,
            0x719: 0x2D,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x7D9: 0x01,
            0x7E9: 0x00,
            0x609: 0x80,
            0x342: 0x01,
        },
        ['Obj_Treat', 'Hud_RestoreDot'],
    ),
    # an item emerging on Difficult changes pose from $25 to $28 ($F839)
    (
        'pickup-difficult',
        {
            0xF9: 0x02,
            0x709: 0xA1,
            0x719: 0x13,
            0x629: 0x2A,
            0x619: 0x01,
            0x639: 0x05,
            0x6B9: 0x25,
            0x649: 0x90,
            0x729: 0x90,
            0x739: 0x00,
            0x609: 0x80,
        },
        ['Obj_Pickup', 'Obj_DifficultAction'],
    ),
    # a flyer crossing left from page 0 despawns ($9212)
    (
        'flyer-off-left',
        {
            0x709: 0xA1,
            0x719: 0x0D,
            0x6B9: 0x02,
            0x649: 0x90,
            0x729: 0x00,
            0x739: 0x00,
            0x769: 0x00,
            0x779: 0x02,
            0x7E9: 0x02,
            0x609: 0x80,
        },
        ['Obj_Flyer'],
    ),
    # a left-facing thrower's probes extend left of page 0 ($D3CB)
    (
        'probe-off-left',
        {
            0x709: 0xA1,
            0x719: 0x03,
            0x649: 0x90,
            0x729: 0x04,
            0x739: 0x00,
            0x769: 0x00,
            0x779: 0x00,
            0x7E9: 0x02,
            0x609: 0x80,
        },
        ['Obj_Thrower', 'Bg_Probe'],
    ),
    # falling out the bottom of the last map row has no row below it ($E73D)
    ('air-last-row', {0x39: 0x01, 0x2A: 0x06, 0x640: 0xF0, 0x7C0: 0xFE}, ['Player_Air']),
    # the row below exists, but an even camera page rejects the crossing ($E8E2)
    (
        'air-row-blocked',
        {0x39: 0x01, 0x640: 0xF0, 0x7C0: 0xFE},
        ['Player_Air'],
    ),
    # opposite horizontal directions are cleared together ($C1F9)
    ('opposed-directions', {}, ['Input_ReadControllers'], {POKE_FRAME: 'LEFT+RIGHT', POKE_FRAME + 8: 'LEFT'}),
    (
        'money-max',
        {
            0x709: 0xA2,
            0x719: 0x13,
            0x629: 0x61,
            0x6B9: 0xFF,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x7D9: 0x01,
            0x609: 0x80,
            0x324: 0x09,
            0x325: 0x09,
            0x326: 0x09,
        },
        ['Money_Add', 'Money_Max'],
    ),
    # slot 9 made a lift (kind $0F) under Scrooge
    ('lift', {0x709: 0xA0, 0x719: 0x0F, 0x649: 0xB0, 0x729: 0x62, 0x739: 0x00, 0x609: 0x80}, ['Obj_Lift']),
    # slot 9 made a ledge patroller (kind $18) that a blow has hit ($0690): it is beaten
    (
        'patroller-hit',
        {0x709: 0xA1, 0x719: 0x18, 0x699: 0x01, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Patroller', 'Obj_Defeat'],
    ),
    # slot 9 made a charging enemy (kind $10, charging) on the floor that a blow has hit: it is beaten
    (
        'charger-hit',
        {0x709: 0xA2, 0x719: 0x10, 0x699: 0x01, 0x649: 0xA6, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Charger', 'Obj_Defeat'],
    ),
    # slot 9 the charging enemy running right at a hidden block in slot 10: it uncovers and knocks it
    (
        'charger-hidden-block',
        {
            0x709: 0xA2,
            0x719: 0x10,
            0x629: 0x2D,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x699: 0x00,
            0x6C9: 0x00,
            0x7E9: 0x01,
            0x779: 0x02,
            0x769: 0x00,
            0x70A: 0xB0,
            0x71A: 0x08,
            0x62A: 0x1A,
            0x64A: 0xB4,
            0x72A: 0x68,
            0x73A: 0x00,
            0x60A: 0x80,
            0x7DA: 0x80,
            0x69A: 0x00,
            0x6CA: 0x00,
            0x6BA: 0x00,
        },
        ['Obj_Charger', 'Obj_HitBlock', 'Tile_EditBySlot'],
    ),
    # slot 9 made a bobbing enemy (kind $20) that a blow has hit: it is beaten
    (
        'bobber-hit',
        {0x709: 0xA1, 0x719: 0x20, 0x699: 0x01, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Bobber', 'Obj_Defeat'],
    ),
    # slot 9 made a leaping enemy (kind $1C) that a blow has hit: it is beaten
    (
        'leaper-hit',
        {0x709: 0xA1, 0x719: 0x1C, 0x699: 0x01, 0x649: 0x90, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80},
        ['Obj_Leaper', 'Obj_Defeat'],
    ),
    # slot 9 made a leaping enemy (kind $1C) walking on the floor near Scrooge: it leaps after 5 frames
    (
        'leaper-walk',
        {0x709: 0xA1, 0x719: 0x1C, 0x6B9: 0x05, 0x649: 0xA6, 0x729: 0x90, 0x739: 0x00, 0x609: 0x80, 0x7E9: 0x01},
        ['Obj_Leaper', 'Obj_SpawnDust'],
    ),
    # kind $32 touches Scrooge with no money: the theft borrows out and blanks every digit
    (
        'pickpocket-broke',
        {0x709: 0xA1, 0x719: 0x32, 0x649: 0xB4, 0x729: 0x50, 0x739: 0x00, 0x609: 0x80, 0x6B9: 0x00, 0x699: 0x00},
        ['Obj_Pickpocket', 'Obj_StealMoney'],
    ),
    # kind $32 touches Scrooge holding 100,000: the theft borrows through two digits and blanks the leading zeros
    (
        'pickpocket-steal',
        {
            0x709: 0xA1,
            0x719: 0x32,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x6B9: 0x00,
            0x699: 0x00,
            0x324: 0x24,
            0x325: 0x01,
            0x326: 0x00,
            0x327: 0x00,
            0x328: 0x00,
            0x329: 0x00,
        },
        ['Obj_Pickpocket', 'Obj_StealMoney'],
    ),
    # kind $1A of variant 0 next to Scrooge: it jumps out and falls back with both splashes
    (
        'pit-splash',
        {0x709: 0xA0, 0x719: 0x1A, 0x729: 0x50, 0x739: 0x00, 0x609: 0x80, 0x6B9: 0x00, 0x699: 0x00},
        ['Obj_PitJumper', 'Obj_PitSplashOut', 'Obj_PitSplashIn'],
    ),
    # kind $24 by Scrooge: it rolls into a wall, walks once more and stops (kind 0)
    (
        'boulder-wall',
        {0x709: 0xA0, 0x719: 0x24, 0x649: 0xB4, 0x729: 0x60, 0x739: 0x00, 0x609: 0x80, 0x699: 0x00},
        ['Obj_Boulder', 'Obj_WalkRight'],
    ),
    # kind $2D dropped on Scrooge: taken, it gives an HP dot back
    (
        'treat-taken',
        {
            0x709: 0xA1,
            0x719: 0x2D,
            0x649: 0xB0,
            0x729: 0x58,
            0x739: 0x00,
            0x609: 0x80,
            0x7E9: 0x00,
            0x7C9: 0x00,
            0x7B9: 0x00,
        },
        ['Obj_Treat', 'Hud_RestoreDot'],
    ),
    # kind $30 pacing over Scrooge holding 5,000: a theft each frame of contact
    (
        'thief-steal',
        {
            0x709: 0xA0,
            0x719: 0x30,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x699: 0x00,
            0x324: 0x24,
            0x325: 0x24,
            0x326: 0x05,
            0x327: 0x00,
            0x328: 0x00,
            0x329: 0x00,
        },
        ['Obj_Thief', 'Obj_StealMoney'],
    ),
    # kind $31, the same handler with the shorter span
    (
        'short-thief-steal',
        {
            0x709: 0xA0,
            0x719: 0x31,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x699: 0x00,
            0x324: 0x24,
            0x325: 0x24,
            0x326: 0x05,
            0x327: 0x00,
            0x328: 0x00,
            0x329: 0x00,
        },
        ['Obj_Thief', 'Obj_StealMoney'],
    ),
    # the ending flag ($AC): the frame loop starts the ending, whose first scene loads its palettes ($E08F);
    # then the scene's objects, the credits under the sprite-0 split, and the frames after the last screen.
    # TOTAL ($0330 packet) and the treasures ($A0), poked before the variant is picked, give variants 0 and 2
    ('ending', {0xAC: 0x01}, ENDING_WANT + ['EndingObjects_Walk'], None, ENDING_FRAMES),
    (
        'ending-0',
        {POKE_FRAME: {0xAC: 0x01}, POKE_FRAME + 60: {0x33A: 0x01}},
        ENDING_WANT + ['EndingObjects_Hop'],
        None,
        ENDING_FRAMES,
    ),
    (
        'ending-2',
        {POKE_FRAME: {0xAC: 0x01}, POKE_FRAME + 60: {0xA0: 0x7F, 0x334: 0x01}},
        ENDING_WANT + ['EndingObjects_Wait'],
        None,
        ENDING_FRAMES,
    ),
    # kind $33 that a blow has hit: it is beaten
    (
        'target-hit',
        {0x709: 0xA1, 0x719: 0x33, 0x649: 0xB4, 0x729: 0x70, 0x739: 0x00, 0x609: 0x80, 0x699: 0x01},
        ['Obj_Target', 'Obj_Defeat'],
    ),
    # kind $34 under Scrooge: it launches him into mode 12
    (
        'launcher',
        {0x709: 0xA0, 0x719: 0x34, 0x649: 0xB4, 0x729: 0x60, 0x739: 0x00, 0x609: 0x80, 0x699: 0x00},
        ['Obj_Launcher', 'Player_Mode12'],
    ),
    # slot 9 a harmful solid pickup on Scrooge under the power-up, its contact count about to wrap:
    # the vertical contact test falls into the probe point $D350
    (
        'power-contact',
        {
            0x709: 0xA2,
            0x719: 0x13,
            0x629: 0x2B,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x7D9: 0x91,
            0x609: 0x80,
            0x699: 0xFF,
            0x75: 0x01,
            0x76: 0x10,
            0x77: 0x00,
        },
        ['Player_ContactV', 'Bg_ProbePoint'],
    ),
    # mode 15 with a message id: the box closes with A (pressed every 30 frames once the text is out), and
    # what the message does runs; slot 9 is the speaker
    (
        'message-pay',
        {0x39: 0x0F, 0x6E: 0x0A, 0x7A: 0x09, 0x324: 0x01, 0x325: 0x05},
        ['Message_Closed'],
        MESSAGE_PRESSES,
        MESSAGE_FRAMES,
    ),
    (
        'message-pay-poor',
        {0x39: 0x0F, 0x6E: 0x0A, 0x7A: 0x09, 0x324: 0x24, 0x325: 0x01},
        ['Message_Closed'],
        MESSAGE_PRESSES,
        MESSAGE_FRAMES,
    ),
    ('message-life', {0x39: 0x0F, 0x6E: 0x0C}, ['Message_Closed', 'Player_PitDeath'], MESSAGE_PRESSES, MESSAGE_FRAMES),
    ('message-block', {0x39: 0x0F, 0x6E: 0x06, 0x7A: 0x09}, ['Message_Closed'], MESSAGE_PRESSES, MESSAGE_FRAMES),
    ('message-launchpad', {0x39: 0x0F, 0x6E: 0x0F}, ['Message_Closed'], MESSAGE_PRESSES, MESSAGE_FRAMES),
    # Start pauses (the box and PAUSE a line a frame), and 30 frames later the pause ends
    (
        'pause',
        {},
        ['PauseText', 'PauseText_Framed', 'Text_LineNext', 'FrameLoop_Paused'],
        {1730: 'START', 1732: '-', 1760: 'START', 1762: '-'},
    ),
    ('message-scene', {0x39: 0x0F, 0x6E: 0x0B}, ['Message_Opened', 'Message_Closed'], MESSAGE_PRESSES, MESSAGE_FRAMES),
    # mode 16 past the music: one time unit left; the money's leading blanks, then its cap
    (
        'treasure-blanks',
        {0x39: 0x10, 0xC5: 0x80, 0x151: 0x00, 0x155: 0x01, 0x324: 0x24, 0x325: 0x24, 0x326: 0x09, 0x327: 0x09},
        ['Player_Treasure', 'Treasure_Done'],
    ),
    (
        'treasure-max',
        {0x39: 0x10, 0xC5: 0x80, 0x151: 0x00, 0x155: 0x01, 0x324: 0x09, 0x325: 0x09, 0x326: 0x09, 0x327: 0x09},
        ['Player_Treasure', 'Money_Max'],
    ),
    # slot 9 the timed enemy walking right at a block in slot 10: it kicks the block
    (
        'kicker-block',
        {
            0x709: 0xA1,
            0x719: 0x1D,
            0x629: 0x58,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x699: 0x00,
            0x7D9: 0x91,
            0x7E9: 0x01,
            0x779: 0x01,
            0x769: 0x00,
            0x6C9: 0x00,
            0x6B9: 0x00,
            0x7C9: 0x00,
            0x7B9: 0x00,
            0x70A: 0xA0,
            0x71A: 0x08,
            0x62A: 0x1A,
            0x64A: 0xB4,
            0x72A: 0x68,
            0x73A: 0x00,
            0x60A: 0x80,
            0x7DA: 0x80,
            0x69A: 0x00,
        },
        ['Obj_BlockKicker', 'Obj_KickBlock', 'Obj_LaunchBlock'],
    ),
    # the same with the block hidden: the kick uncovers its tile first
    (
        'kicker-hidden-block',
        {
            0x709: 0xA1,
            0x719: 0x1D,
            0x629: 0x58,
            0x649: 0xB4,
            0x729: 0x50,
            0x739: 0x00,
            0x609: 0x80,
            0x699: 0x00,
            0x7D9: 0x91,
            0x7E9: 0x01,
            0x779: 0x01,
            0x769: 0x00,
            0x6C9: 0x00,
            0x6B9: 0x00,
            0x7C9: 0x00,
            0x7B9: 0x00,
            0x70A: 0xB0,
            0x6CA: 0x00,
            0x6BA: 0x00,
            0x71A: 0x08,
            0x62A: 0x1A,
            0x64A: 0xB4,
            0x72A: 0x68,
            0x73A: 0x00,
            0x60A: 0x80,
            0x7DA: 0x80,
            0x69A: 0x00,
        },
        ['Obj_BlockKicker', 'Obj_KickBlock', 'Tile_EditBySlot', 'Obj_LaunchBlock'],
    ),
    # slot 9 a cart (kind $21) on Scrooge: he rides it (modes 10 and 11) as it rolls left
    (
        'cart-ride',
        {
            0x709: 0xA0,
            0x719: 0x21,
            0x629: 0x40,
            0x649: 0xA6,
            0x729: 0x62,
            0x739: 0x00,
            0x609: 0x80,
            0x7D9: 0x00,
            0x699: 0x00,
        },
        ['Obj_Cart'],
    ),
    # slot 9 a kind $22 near Scrooge: it hops, then walks
    (
        'hop-walker',
        {
            0x709: 0xA0,
            0x719: 0x22,
            0x629: 0x3D,
            0x649: 0xA6,
            0x729: 0x70,
            0x739: 0x00,
            0x609: 0x80,
            0x7D9: 0x11,
            0x699: 0x00,
        },
        ['Obj_HopWalker', 'Obj_HopWalkerWalk'],
    ),
    # slot 9 a kind $22 near Scrooge whose first step hits the floor: it turns each frame instead of hopping
    (
        'hop-walker-turn',
        {
            0x709: 0xA0,
            0x719: 0x22,
            0x629: 0x3D,
            0x649: 0xB4,
            0x729: 0x70,
            0x739: 0x00,
            0x609: 0x80,
            0x7D9: 0x11,
            0x699: 0x00,
        },
        ['Obj_HopWalker', 'Obj_HopWalkerWalk'],
    ),
]

# name, route, options, routines that must be compared
EVENTS = [
    ('no-damage', 'moon_intro', ['--no-damage', 'on'], ['Player_Knockback']),
    ('jump-scale-125', 're_amazon_moves', ['--jump-scale', '125'], ['Player_StartJump']),
]


def route_frames(path):
    last = 0
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            last = max(last, int(line.split()[0]))
    return last + 120


def idle_state(golden, route, frame):
    log = golden / f'{route.stem}.txt'
    if not log.exists():
        return 0, None
    idle = {int(m[1]) + 1 for m in re.finditer(r'^(\d+) .*\bS=FF\b', log.read_text(), re.M)}
    start = max((s for s in idle if s <= frame and quick_verify.state_path(golden, route, s).exists()), default=0)
    return start, quick_verify.state_path(golden, route, start) if start else None


def route_case(route, start=0, state=None, span=None):
    case = {'name': route.stem, 'route': route, 'frames': route_frames(route), 'extra': [], 'want': []}
    if span:
        case['frames'] = min(case['frames'], start + span)
    if state:
        case['extra'] = ['--load-state', str(state)]
    return case


def select_routes(routes, args, golden):
    ranges = quick_verify.parse_specs(args.changed, Path(args.rom).read_bytes()[4])
    selected = []
    for route in routes:
        cover = golden / f'{route.stem}.cover'
        if not cover.exists():
            sys.exit(f'{route.stem}: no coverage in {golden}; record it with tests/quick_verify.py --record')
        reach = [s for s, offs in quick_verify.read_cover(cover) if quick_verify.hits(offs, ranges)]
        if reach:
            start, state = idle_state(golden, route, reach[0]) if args.from_state else (0, None)
            selected.append(route_case(route, reach[0] if args.from_state else 0, state, args.span))
            print(
                f'{route.stem}: executes {" ".join(args.changed)} from the segment at frame {reach[0]}, run from frame {start}'
            )
    if not selected:
        sys.exit(f'no route executes {" ".join(args.changed)}; add a route that does')
    return selected


def poke_case(name, pokes, want, presses=None, frames=POKE_FRAMES):
    if not all(isinstance(v, dict) for v in pokes.values()):
        pokes = {POKE_FRAME: pokes}
    extra = [
        arg
        for frame, at in pokes.items()
        for addr, value in at.items()
        for arg in ('--dt-poke-at', f'{frame}:{addr:X}={value:02X}')
    ]
    first = min([*pokes, *(presses or {})], default=POKE_FRAME)
    case = {
        'name': name,
        'route': POKE_ROUTE,
        'frames': POKE_FRAME + frames,
        'extra': extra,
        'want': want,
        'first': first,
    }
    return case | {'presses': presses} if presses else case


def pressed_route(case, tmp):
    events = {}
    for line in POKE_ROUTE.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            frame, buttons = line.split(None, 1)
            events[int(frame)] = buttons.strip()
    events.update(case['presses'])
    path = Path(tmp) / f'{case["name"]}.txt'
    path.write_text(''.join(f'{frame} {buttons}\n' for frame, buttons in sorted(events.items())))
    return path


def event_case(name, route, options, want):
    path = ROOT / 'tests' / 'routes' / f'{route}.txt'
    return {'name': name, 'route': path, 'frames': route_frames(path), 'extra': options, 'want': want}


def run(exe, rom, case, extra):
    cmd = [str(exe), rom, '--frames', str(case['frames']), '--input', str(case['route']), *case['extra'], *extra]
    return subprocess.run(cmd, capture_output=True, text=True)


def check_result(p, case):
    stats = {(m[0], m[1]): tuple(map(int, m[2:])) for m in LINE.findall(p.stdout)}
    err = [
        NAMED.sub(lambda m: f'${m[1]} {SYMBOLS.get(int(m[1], 16), "")}'.rstrip(), l)
        for l in p.stderr.splitlines()
        if l.startswith('[decomp]')
    ]
    compared = {name for (name, _), s in stats.items() if s[1]}
    err += [f'{name} was not compared' for name in case['want'] if name not in compared]
    return p.returncode == 0 and bool(stats) and not err, stats, err


def ram_frames(path):
    with open(path, 'rb') as f:
        while True:
            rec = f.read(RAM + 1)
            if len(rec) < RAM + 1:
                return
            yield rec[:RAM], rec[RAM]


def ram_diff(logs):
    frames = idle = 0
    result = None
    for frame, ((a, sa), (b, sb)) in enumerate(zip(ram_frames(logs[0]), ram_frames(logs[1]))):
        frames += 1
        if result:
            continue
        live = range(0)
        if sa == sb == IDLE_S:
            idle += 1
            if a[TASKS]:
                live = range(0x101 + a[TASKS + 2], 0x100 + MAIN_TASK_BASE + 1)
        diff = [i for i in range(RAM) if a[i] != b[i] and (not STACK <= i < 0x200 or i in live)]
        if diff:
            shown = ' '.join(
                f'${i:04X}{"(" + SYMBOLS[i] + ")" if i in SYMBOLS else ""}:{a[i]:02X}/{b[i]:02X}' for i in diff[:12]
            )
            result = f'frame {frame}: {shown}'
    for log in logs:
        log.unlink()
    return result, f'{frames} idle={idle}'


def run_case(exe, dev, rom, case, modes, tmp, label):
    out = {}
    logs = [Path(tmp) / f'{label}.{case["name"]}.{mode}.ram' for mode in ('base', 'replace')]
    base = None
    if 'check' in modes:
        log = ['--dt-ram-log', str(logs[0])] if 'replace' in modes else []
        base = run(dev if log else exe, rom, case, ['--decomp', 'check', *log])
        out['check'] = check_result(base, case)
    if 'replace' not in modes:
        return out
    if base is None or base.returncode:
        base = run(dev, rom, case, ['--decomp', 'off', '--dt-ram-log', str(logs[0])])
        if base.returncode:
            out['replace'] = f'off run failed (exit {base.returncode}): {base.stderr[-500:]}', 0
            return out
    p = run(dev, rom, case, ['--decomp', 'replace', '--dt-ram-log', str(logs[1])])
    if p.returncode:
        out['replace'] = f'replace run failed (exit {p.returncode}): {p.stderr[-500:]}', 0
        return out
    out['replace'] = ram_diff(logs)
    return out


def print_checks(label, cases, results, failed):
    total = {}
    for case in cases:
        ok, stats, err = results[id(case)].result()['check']
        calls = sum(s[0] for s in stats.values())
        print(f'{label:8s} {case["name"]:24s} {"PASS" if ok else "FAIL"}  calls={calls}', flush=True)
        if not ok:
            failed.append(f'{label} {case["name"]}')
            print('\n'.join(err[:30]))
        for key, s in stats.items():
            t = total.setdefault(key, [0, 0, 0, set()])
            for i in range(3):
                t[i] += s[i]
            if s[1]:
                t[3].add(case['name'])
    print(f'{"routine":22s} {"addr":5s} {"calls":>9s} {"compared":>9s} {"original":>9s}  cases compared')
    for (name, addr), (calls, compared, orig, rs) in sorted(total.items(), key=lambda kv: kv[0][1]):
        print(f'{name:22s} ${addr} {calls:9d} {compared:9d} {orig:9d}  {len(rs)}')


def print_replaces(label, cases, results, failed):
    for case in cases:
        result, frames = results[id(case)].result()['replace']
        print(
            f'{label:8s} {case["name"]:24s} {"PASS" if result is None else "FAIL"}  frames={frames}'
            + ('' if result is None else f'  first difference: {result}'),
            flush=True,
        )
        if result is not None:
            failed.append(f'{label} {case["name"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default='build/headless')
    ap.add_argument('--dev-build', help='a -DDUCKTALES_DEV_DIAGNOSTICS=ON headless build (replace, pokes, events)')
    ap.add_argument('--config', default='Release')
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--routes', nargs='*')
    ap.add_argument('--modes', nargs='+', default=['check', 'replace', 'pokes', 'events'])
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--golden', default=str(ROOT / 'build' / 'golden'), help='quick_verify golden root')
    ap.add_argument('--changed', nargs='+', metavar='RANGE', help='only routes executing these PRG addresses')
    ap.add_argument('--from-state', action='store_true', help='with --changed: start from the golden state')
    ap.add_argument('--span', type=int, help='with --from-state: frames to run from the start')
    ap.add_argument('--pokes', nargs='+', metavar='NAME', help='only these poke cases')
    args = ap.parse_args()
    if (args.from_state and not args.changed) or (args.span and not args.from_state):
        ap.error('--from-state needs --changed, --span needs --from-state')
    golden = Path(args.golden) / quick_verify.file_sha256(Path(args.rom))
    routes = [Path(r) for r in args.routes] if args.routes else sorted((ROOT / 'tests' / 'routes').glob('*.txt'))
    cases = select_routes(routes, args, golden) if args.changed else [route_case(r) for r in routes]
    pokes = [poke_case(*p) for p in POKES if not args.pokes or p[0] in args.pokes]
    for case in pokes:
        start, state = idle_state(golden, POKE_ROUTE, case['first'])
        if state:
            case['extra'] = ['--load-state', str(state), *case['extra']]
    events = [event_case(*e) for e in EVENTS]
    exe = ROOT / args.build / args.config / 'ducktales.exe'
    dev = ROOT / args.dev_build / args.config / 'ducktales.exe' if args.dev_build else None
    groups = [
        ('check', 'replace', cases, [m for m in ('check', 'replace') if m in args.modes and (m == 'check' or dev)])
    ]
    if dev:
        groups += [
            (n, n, g, ['check', 'replace'] if n in args.modes else [])
            for n, g in (('pokes', pokes), ('events', events))
        ]
    failed = []
    scratch = ROOT / 'captures' if (ROOT / 'captures').is_dir() else None
    with tempfile.TemporaryDirectory(dir=scratch) as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for case in pokes:
            if 'presses' in case:
                case['route'] = pressed_route(case, tmp)
        jobs = sorted(
            ((case, modes, label) for label, _, g, modes in groups if modes for case in g),
            key=lambda j: -j[0]['frames'],
        )
        results = {
            id(case): pool.submit(run_case, exe, dev, args.rom, case, modes, tmp, label) for case, modes, label in jobs
        }
        for check, replace, g, modes in groups:
            if 'check' in modes:
                print_checks(check, g, results, failed)
            if 'replace' in modes:
                print_replaces(replace, g, results, failed)
    print('all passed' if not failed else 'failed: ' + ', '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
