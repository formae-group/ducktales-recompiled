"""EVENTS and SEQUENCES: the cases of tests/decomp_test.py --modes events, enhancements whose event
a decompiled routine raises, a warm reset, and the routes whose event logs must match."""

# name, route, options, routines that must be compared, optionally the frames to run
EVENTS = [
    ('no-damage', 'moon_intro', ['--no-damage', 'on'], ['Player_Knockback']),
    ('jump-scale-125', 're_amazon_moves', ['--jump-scale', '125'], ['Player_StartJump']),
    ('start-lives-5', 'amazon_intro', ['--start-lives', '5'], []),
    # the reset at frame 1200 comes with an NMI pending, which the original takes before its first store to $2000
    (
        'warm-reset',
        'amazon_intro',
        ['--dt-reset-at', '1200'],
        ['Reset_WaitVblank', 'Reset_WaitVblankEnd', 'Reset_SecondNametable', 'Reset_StartScheduler'],
        1301,
    ),
]

# Routes whose event logs must be equal with --decomp off, check and replace,
# and the routines whose calls each event counts in check mode.
SEQUENCES = ['full_moon', 'full_amazon']
EVENT_CALLS = {'enemy-defeated': 'Obj_Defeat', 'item-collected': 'Pickup_Take'}
