"""Step 11 of the mods test (tests/mods_test.py): gameplay scripts.

The "update" event once per step of play, dt.set inside a game event's handler written at once and
logged (repeats counted in one line), on the headless build and the native player. Scrooge's
tunables on a short Amazon route: each changes what it should against a run without them, nil
gives the game's value back, the highest mod in the order wins, and the native player measures the
same. On the headless build, scripts that write and tune with --decomp off, check and replace: the
same lines, no mismatch, and with the developer build (--dev-build) the same RAM outside the stack
on every frame with the decompiled code in place of the original (decomp_test.py's comparison).
dt.set.invulnerable is refused where it would hold the knock-back forever.
"""

import os
import subprocess
import tempfile
from pathlib import Path

from decomp_test import ram_diff

from .common import ROM, ROOT, check, script_mod

UPDATE_LUA = r'''
local steps = 0
dt.on("update", function()
    steps = steps + 1
    if steps == 100 then
        local before = dt.state.lives
        dt.set.lives(7)
        dt.log("immediate", before, dt.state.lives)
    elseif steps > 100 and steps <= 160 then
        dt.set.lives(7)
    elseif steps == 161 then
        dt.set.lives(6)
    end
end)
dt.on("frame", function(frame)
    if frame == 1400 then
        dt.log("steps", steps, dt.state.lives)
    end
end)
'''

# Amazon from Land Select: Scrooge falls in, walks left, jumps straight up, lands on the pogo and
# bounces until 2100, then jumps again.
TUNE_ROUTE = '''0 -
200 START
206 -
400 START
406 -
1500 RIGHT
1560 -
1600 A
1630 -
1680 LEFT
1720 -
1780 A
1788 A+B+DOWN
2100 -
2140 A
2170 -
'''

# What each tunable changes, measured in steps of play and pixels: the fall into the land, the
# walk, the rise of the two jumps and of the first two pogo bounces. TUNED sets gravity for the
# fall, walk_speed, jump_velocity and pogo_velocity from the landing, and gives the second jump
# and bounce the game's values back with nil.
MEASURE_LUA = r'''
local TUNED = %s
local step, phase, last_x, last_mode = 0, "fall", 0, -1
local m = { fall = 0, walk = 0, jump1 = 0, jump2 = 0, pogo1 = 0, pogo2 = 0 }
local jump, pg, jumps = nil, nil, 0
if TUNED then
    dt.tune.gravity = 0x18
end
dt.on("jump-start", function()
    jumps = jumps + 1
    jump = { n = jumps, from = dt.state.y, min = dt.state.y }
    if TUNED and jumps == 1 then
        dt.tune.jump_velocity = nil
    end
end)
dt.on("update", function()
    step = step + 1
    local s = dt.state
    local mode, x, y = s.mode, s.x, s.y
    if phase == "fall" and mode == 0 then
        phase = "walk"
        m.fall = step
        if TUNED then
            dt.tune.gravity = nil
            dt.tune.walk_speed = 0x280
            dt.tune.jump_velocity = 0x600
            dt.tune.pogo_velocity = 0x700
        end
    end
    if phase == "walk" and mode == 0 and last_mode == 0 and s.pad & 3 ~= 0 then
        m.walk = m.walk + math.abs(x - last_x)
    end
    if jump then
        if mode == 1 then
            jump.min = math.min(jump.min, y)
        else
            m["jump" .. jump.n] = jump.from - jump.min
            jump = nil
            phase = "after"
        end
    end
    if mode == 2 then
        if not pg then
            pg = { from = y, min = y, prev = y, falling = false, n = 0 }
        else
            if y > pg.prev then
                if not pg.falling and pg.n == 0 and TUNED then
                    dt.tune.pogo_velocity = nil
                end
                pg.falling = true
            elseif y < pg.prev and pg.falling then
                pg.n = pg.n + 1
                if pg.n <= 2 then
                    m["pogo" .. pg.n] = pg.from - pg.min
                end
                pg.from, pg.min, pg.falling = pg.prev, y, false
            end
            pg.min = math.min(pg.min, y)
            pg.prev = y
        end
    end
    last_x, last_mode = x, mode
end)
dt.on("frame", function(frame)
    if frame == 2290 then
        dt.log("measured", m.fall, m.walk, m.jump1, m.jump2, m.pogo1, m.pogo2)
    end
end)
'''

# Lower in the order than MEASURE_LUA: its gravity is not the one in force, and the limits.
LOW_LUA = r'''
local done = false
dt.on("update", function()
    if done then
        return
    end
    done = true
    dt.tune.gravity = 0x40
    dt.log("in force", dt.tune.gravity)
    dt.tune.gravity = nil
    local ok, err = pcall(function() dt.tune.gravity = 8 end)
    dt.log("range", ok, err)
    ok, err = pcall(function() dt.tune.speed = 1 end)
    dt.log("name", ok, err)
end)
'''

# Tunes on amazon_intro, and a write of the invulnerability at the first knock-back.
KNOCK_LUA = r'''
local step, knock, knocked = 0, 0, false
dt.tune.walk_speed = 0x200
dt.on("update", function()
    step = step + 1
    if dt.state.mode == 6 and not knocked then
        knock = knock + 1
        if knock == 1 then
            local ok, err = pcall(dt.set.invulnerable, 0x20)
            dt.log("knock-back write", ok, err)
            dt.set.invulnerable(0x60)
        end
    elseif knock > 0 then
        knocked = true
    end
    if step == 100 then
        dt.tune.gravity = 0x28
        dt.tune.jump_velocity = 0x500
        dt.tune.pogo_velocity = 0x680
    elseif step == 300 then
        dt.tune.gravity = nil
        dt.tune.walk_speed = nil
    elseif step == 400 then
        dt.tune.jump_velocity = nil
        dt.tune.pogo_velocity = nil
    end
end)
dt.on("frame", function(frame)
    if frame == 1400 then
        dt.log("knock-back steps", knock, step)
    end
end)
'''


def gameplay_checks(exe, native, dev):
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))
        amazon = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
        tune_route = tmp / 'tune_route.txt'
        tune_route.write_text(TUNE_ROUTE)

        def mods(name, scripts):
            folder = tmp / name
            folder.mkdir()
            for mod_id, text in scripts.items():
                script_mod(folder, mod_id, text)
            return folder

        def run(host_exe, name, is_native, folder, route=amazon, frames=1500, extra=()):
            cmd = [host_exe, ROM, '--frames', str(frames), '--input', route, '--mods', 'on', '--mods-dir', folder]
            cmd += ['--mods-order', ','.join(f'{m.name}:on' for m in sorted(folder.iterdir())), *extra]
            if is_native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            log = p.stdout + p.stderr
            if p.returncode:
                print(log[-3000:])
                raise SystemExit(f'gameplay {name}: exit {p.returncode}')
            return log

        def lines(log, mod_id, what):
            return [
                line.split('\t')[1:]
                for line in log.splitlines()
                if line.startswith(f'[ducktales] Mod {mod_id}: {what}\t')
            ]

        hosts = [(h, e, n) for h, e, n in (('headless', exe, False), ('native player', native, True)) if e]
        update_mods = mods('update', {'gameplay': UPDATE_LUA})
        steps = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, host.replace(' ', '_'), is_native, update_mods)
            immediate = (lines(log, 'gameplay', 'immediate') or [[]])[0]
            check(
                len(immediate) == 2 and immediate[0] != '7' and immediate[1] == '7',
                f'{host}: dt.set in an "update" handler writes at once ({immediate})',
                log[-3000:],
            )
            written = [line for line in log.splitlines() if 'Mod gameplay: dt.set.lives(' in line]
            check(
                len(written) == 3
                and written[0].endswith(f'dt.set.lives(7), was {immediate[0] if immediate else "?"}')
                and written[1].endswith('dt.set.lives(7) repeated 60 more times')
                and written[2].endswith('dt.set.lives(6), was 7'),
                f'{host}: every write is logged, 60 identical repeats in one line ({len(written)} lines)',
                '\n'.join(written) or log[-3000:],
            )
            seen = (lines(log, 'gameplay', 'steps') or [[]])[0]
            steps[host] = seen
            check(
                len(seen) == 2 and int(seen[0]) > 161 and seen[1] == '6',
                f'{host}: "update" ran {seen[0] if seen else 0} steps by frame 1400 and the last write holds',
                log[-3000:],
            )
        if len(steps) == 2:
            check(
                steps['headless'][0] == steps['native player'][0],
                f'the native player runs as many "update" steps as the cycle host ({steps["native player"][:1]})',
            )

        names = ('fall', 'walk', 'jump', 'jump after nil', 'pogo', 'pogo after nil')
        tune_mods = mods('tune', {'tunes': MEASURE_LUA % 'true', 'tunes-low': LOW_LUA})
        measured = {}
        if exe:
            log = run(exe, 'untuned', False, mods('untuned', {'tunes': MEASURE_LUA % 'false'}), tune_route, 2300)
            measured['game'] = [int(v) for v in (lines(log, 'tunes', 'measured') or [[]])[0]]
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'tuned_{host.replace(" ", "_")}', is_native, tune_mods, tune_route, 2300)
            measured[host] = [int(v) for v in (lines(log, 'tunes', 'measured') or [[]])[0]]
            low = {what: (lines(log, 'tunes-low', what) or [[]])[0] for what in ('in force', 'range', 'name')}
            check(
                low['in force'] == ['24'],
                f'{host}: the higher mod\'s dt.tune.gravity is in force over a lower one\'s ({low["in force"]})',
                log[-3000:],
            )
            check(
                low['range'][:1] == ['false']
                and low['range'][1:2] == ['scripts/main.lua:11: dt.tune.gravity = 8: out of range 16 to 255']
                and low['name'][1:]
                == [
                    'scripts/main.lua:13: dt.tune has no tunable "speed" (gravity, walk_speed, pogo_velocity, jump_velocity)'
                ],
                f'{host}: dt.tune refuses a value out of range and an unknown name',
                f'{low}',
            )
            tuned = [line.split(': ', 1)[1] for line in log.splitlines() if 'Mod tunes: dt.tune.' in line]
            check(
                tuned
                == [
                    'dt.tune.gravity = 24',
                    'dt.tune.gravity = nil',
                    'dt.tune.walk_speed = 640',
                    'dt.tune.jump_velocity = 1536',
                    'dt.tune.pogo_velocity = 1792',
                    'dt.tune.jump_velocity = nil',
                    'dt.tune.pogo_velocity = nil',
                ],
                f'{host}: every change of a tunable is logged ({len(tuned)} lines)',
                '\n'.join(tuned),
            )
        game = measured.get('game')
        if game and len(game) == 6:
            got = measured.get('headless', [])
            detail = ', '.join(f'{n} {a} -> {b}' for n, a, b in zip(names, game, got))
            check(
                len(got) == 6
                and got[0] > game[0]
                and got[1] > game[1]
                and got[2] > game[2] + 10
                and abs(got[3] - game[3]) <= 2
                and got[4] > game[4] + 10
                and abs(got[5] - game[5]) <= 2,
                f'headless: lower gravity falls longer, walk_speed walks further, jump_velocity and '
                f'pogo_velocity rise higher, and nil gives the game\'s rise back ({detail})',
            )
        if 'native player' in measured:
            check(
                measured['native player'] == measured.get('headless', measured['native player']),
                f'the native player measures the same with the tunables ({measured["native player"]})',
            )

        if not exe:
            return
        trio_mods = mods('trio', {'gameplay': UPDATE_LUA, 'knock': KNOCK_LUA})
        logs, ram = {}, []
        for mode in ('off', 'check', 'replace'):
            host_exe, extra = exe, ['--decomp', mode]
            if dev and mode != 'check':
                ram.append(tmp / f'{mode}.ram')
                host_exe, extra = dev, [*extra, '--dt-ram-log', ram[-1]]
            logs[mode] = run(host_exe, f'decomp_{mode}', False, trio_mods, extra=extra)
        knock = (lines(logs['off'], 'knock', 'knock-back write') or [[]])[0]
        check(
            knock[:1] == ['false']
            and knock[1:2] == ['dt.set.invulnerable(32): during the knock-back it is 61 to 127 (it ends at 60)']
            and 'Mod knock: dt.set.invulnerable(96), was ' in logs['off'],
            'headless: dt.set.invulnerable is refused at or below the knock-back\'s end and written above it',
            f'{knock}',
        )
        held = (lines(logs['off'], 'knock', 'knock-back steps') or [[]])[0]
        check(
            held[:1] and int(held[0]) >= 30,
            f'headless: the knock-back lasts until the written invulnerability counts down to its end '
            f'({held[:1]} steps, the game\'s is 11)',
        )
        check(
            all('MISMATCH' not in log and 'TIMING' not in log for log in logs.values()),
            'headless: with the writing and tuning scripts, --decomp check finds no mismatch and no timing difference',
            logs['check'][-3000:],
        )
        written = {
            m: [line for line in log.splitlines() if 'Mod gameplay:' in line or 'Mod knock:' in line]
            for m, log in logs.items()
        }
        check(
            written['off'] == written['check'] == written['replace'],
            f'headless: --decomp off, check and replace log the same {len(written["off"])} lines',
        )
        if not dev:
            print('SKIP  the RAM of --decomp replace: no developer build (--dev-build)')
            return
        diff, info = ram_diff(ram)
        check(
            diff is None,
            f'headless: with the writing and tuning scripts, --decomp replace keeps the RAM of --decomp off '
            f'outside the stack ({info})',
            diff or '',
        )
