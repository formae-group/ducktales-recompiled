"""Step 11 of the mods test (tests/mods_test.py): gameplay scripts. The "update" event once per step
of play, dt.set inside a game event's handler written at once and logged (repeats counted in one
line), on the headless build and the native player; and on the headless build the same writing
script with --decomp off, check and replace: the same updates and writes, no mismatch, and with the
developer build (--dt-ram-log) the same RAM outside the stack on every frame with the decompiled
code in place of the original (decomp_test.py's comparison)."""

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


def gameplay_checks(exe, native, dev):
    route = ROOT / 'tests' / 'routes' / 'amazon_intro.txt'
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        mods = tmp / 'mods'
        mods.mkdir()
        script_mod(mods, 'gameplay', UPDATE_LUA)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(host_exe, name, is_native, extra=()):
            cmd = [host_exe, ROM, '--frames', '1500', '--input', route, '--mods', 'on', '--mods-dir', mods, *extra]
            if is_native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            log = p.stdout + p.stderr
            if p.returncode:
                print(log[-3000:])
                raise SystemExit(f'gameplay {name}: exit {p.returncode}')
            return log

        def lines(log, what):
            return [
                line.split('\t')[1:]
                for line in log.splitlines()
                if line.startswith(f'[ducktales] Mod gameplay: {what}\t')
            ]

        steps = {}
        for host, host_exe, is_native in (('headless', exe, False), ('native player', native, True)):
            if not host_exe:
                continue
            log = run(host_exe, host.replace(' ', '_'), is_native)
            immediate = (lines(log, 'immediate') or [[]])[0]
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
            seen = (lines(log, 'steps') or [[]])[0]
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
        if not exe:
            return
        logs, ram = {}, []
        for mode in ('off', 'check', 'replace'):
            host_exe, extra = exe, ['--decomp', mode]
            if dev and mode != 'check':
                ram.append(tmp / f'{mode}.ram')
                host_exe, extra = dev, [*extra, '--dt-ram-log', ram[-1]]
            logs[mode] = run(host_exe, f'decomp_{mode}', False, extra)
        check(
            all('MISMATCH' not in log and 'TIMING' not in log for log in logs.values()),
            'headless: with the writing script, --decomp check finds no mismatch and no timing difference',
            logs['check'][-3000:],
        )
        written = {m: [line for line in log.splitlines() if 'Mod gameplay:' in line] for m, log in logs.items()}
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
            f'headless: with the writing script, --decomp replace keeps the RAM of --decomp off outside the stack ({info})',
            diff or '',
        )
