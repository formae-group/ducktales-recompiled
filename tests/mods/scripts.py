"""Step 8 of the mods test (tests/mods_test.py): the scripts' sandbox, limits and errors, dt.CANCEL,
dt.state, dt.set, dt.toast, dt.store, mods.log and hot reload, on the native player or else the
headless build."""

import os
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from .common import ROM, check, mod_toml, png_read, script_mod

SANDBOX_LUA = r'''
local open = {}
for _, name in ipairs({"io", "os", "package", "debug", "require", "dofile", "loadfile"}) do
    if _G[name] ~= nil then
        open[#open + 1] = name
    end
end
if string.dump ~= nil then
    open[#open + 1] = "string.dump"
end
local binary = load("\27Lua")
dt.log("sandbox", #open == 0 and "closed" or table.concat(open, ","), binary == nil and "binary-refused" or "binary-loaded",
    load("return 6 * 7")())
dt.on("new-game", function(e)
    dt.log("event", e.name)
end)
dt.on("frame", function(frame)
    if frame == 1000 or frame == 1400 then
        dt.log("state", frame, dt.state.land, dt.state.lives, dt.state.time, dt.state.hp)
    end
    if frame == 1200 then
        dt.toast("sandbox")
    end
    if frame == 1300 then
        dt.toast(string.rep("\u{E9}", 70))
    end
end)
'''

API_LUA = r'''
local ok, err = pcall(dt.set.lives, 3)
dt.log("outside", ok, err)
local runs = (dt.store.get("runs") or 0) + 1
dt.store.set("runs", runs)
dt.store.set("text", "a\nb\\c = d")
dt.store.set("half", 3.5)
dt.store.set("flag", true)
dt.log("store", runs, dt.store.get("text") == "a\nb\\c = d", dt.store.get("half"), dt.store.get("flag"))
local big_ok, big_err = pcall(dt.store.set, "big", string.rep("x", 70000))
dt.log("store-cap", big_ok, big_err)
local type_ok = pcall(dt.store.set, "table", {})
local key_ok = pcall(dt.store.set, "../escape", 1)
dt.log("store-refused", type_ok, key_ok)
dt.on("frame", function(frame)
    if frame == 900 then
        dt.log("toasts", dt.toast("Hello from api"), dt.toast("too soon"))
    end
    if frame == 1000 then
        dt.log("before", dt.state.lives, dt.state.time, dt.state.hp, dt.state.money)
        dt.set.lives(5)
        dt.set.time(123)
        dt.set.hp(1)
        dt.set.money(12340)
        dt.log("deferred", dt.state.lives)
        local range_ok, range_err = pcall(dt.set.lives, 10)
        dt.log("range", range_ok, range_err)
    end
    if frame == 1001 then
        dt.log("after", dt.state.lives, dt.state.time, dt.state.hp, dt.state.money)
    end
end)
'''

LIMIT_LUA = {
    'loop': 'dt.on("frame", function(f) if f == 100 then while true do end end end)',
    'pcall-loop': 'dt.on("frame", function(f) if f == 110 then while true do pcall(function() while true do end end) end end end)',
    'memory': (
        'dt.on("frame", function(f) if f == 120 then pcall(function() local t = {} for i = 1, 1000 do '
        't[i] = string.rep("x", 1000000) .. i end end) end end)'
    ),
    'error': 'dt.on("frame", function(f) if f == 130 then error("boom") end end)',
    'start-error': 'error("early")',
    'cancel-ignored': 'dt.on("new-game", function() return dt.CANCEL end)',
}

CANCEL_LUA = r'''
dt.on("timer-tick", function()
    return dt.CANCEL
end)
dt.on("frame", function(frame)
    if frame == 1000 or frame == 1400 then
        dt.log("state", frame, dt.state.land, dt.state.lives, dt.state.time, dt.state.hp)
    end
end)
'''


def states(log, mod_id):
    found = {}
    for line in log.splitlines():
        parts = line.split()
        if line.startswith(f'[ducktales] Mod {mod_id}: state') and len(parts) >= 9:
            found[int(parts[4])] = [None if p == 'nil' else int(p) for p in parts[5:9]]
    return found


def script_checks(exe, route, native):
    host = 'native player' if native else 'headless'
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)

        data = tmp / 'data'
        data.mkdir()
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(data))

        def run(name, mods, frames=1500, extra=()):
            cmd = [exe, ROM, '--frames', str(frames), '--input', route, *extra]
            if mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            if native:
                cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini']
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            if p.returncode:
                print(p.stdout + p.stderr)
                raise SystemExit(f'{host} scripts {name}: exit {p.returncode}')
            return p.stdout + p.stderr

        all_dir = tmp / 'mods_all'
        all_dir.mkdir()
        script_mod(all_dir, 'sandbox', SANDBOX_LUA)
        for mod_id, text in LIMIT_LUA.items():
            script_mod(all_dir, mod_id, text)
        log = run('all', all_dir)
        check('7 loaded' in log and '7 scripts' in log, f'{host}: seven script mods load', log)
        check(
            'Mod sandbox: sandbox\tclosed\tbinary-refused\t42' in log,
            f'{host}: io, os, package, debug, require, dofile, loadfile and string.dump are absent; '
            'load refuses a binary chunk and runs text',
            log,
        )
        check('Mod sandbox: event\tnew-game' in log, f'{host}: dt.on("new-game") runs at the title\'s set-up', log)
        reasons = {
            'loop': 'scripts/main.lua:1: ran more than 1000000 instructions in one callback (in a "frame" handler)',
            'pcall-loop': 'scripts/main.lua:1: ran more than 1000000 instructions in one callback (in a "frame" handler)',
            'memory': 'used more than 16 MiB of memory (in a "frame" handler)',
            'error': 'scripts/main.lua:1: boom (in a "frame" handler)',
            'start-error': 'scripts/main.lua:1: early (when the script started)',
        }
        for mod_id, why in reasons.items():
            check(f'Mod {mod_id}: script stopped: {why}' in log, f'{host}: {mod_id}: stopped with "{why}"', log)
        ignored = 'Mod cancel-ignored: a "new-game" handler returned dt.CANCEL, which that event ignores'
        check(
            log.count(ignored) == 1 and 'Mod cancel-ignored: script stopped' not in log,
            f'{host}: dt.CANCEL for an event that cannot be cancelled is reported once and the script runs on',
            log,
        )
        mods_log = (data / 'mods.log').read_bytes() if (data / 'mods.log').exists() else b''
        check(
            b'[ducktales] Mods: on, 7 found, 7 loaded' in mods_log
            and b'Mod sandbox: Sandbox 1.0.0, loaded' in mods_log
            and b'Mod error: script stopped: scripts/main.lua:1: boom' in mods_log
            and b'Mod sandbox: state\t1000' in mods_log,
            f'{host}: mods.log in the data folder holds the catalogue, dt.log lines and stopped scripts '
            f'({len(mods_log)} bytes)',
            mods_log.decode('utf-8', 'replace'),
        )
        cut = b'Mod sandbox: toast: ' + '\u00e9'.encode() * 64 + b'\r\n'
        check(
            cut in mods_log,
            f'{host}: dt.toast cuts 70 two-byte characters at 64 characters, not bytes',
            mods_log.decode('utf-8', 'replace')[-2000:],
        )
        seen = states(log, 'sandbox')
        check(
            sorted(seen) == [1000, 1400] and all(len(v) == 4 for v in seen.values()),
            f'{host}: the other mod still runs after them (dt.state at frames 1000 and 1400: {seen})',
            log,
        )
        check(
            len(seen) == 2 and seen[1000][2] != seen[1400][2], f'{host}: the HUD time runs without a cancel', str(seen)
        )

        cancel_dir = tmp / 'mods_cancel'
        cancel_dir.mkdir()
        script_mod(cancel_dir, 'stop-the-clock', CANCEL_LUA)
        log = run('cancel', cancel_dir)
        held = states(log, 'stop-the-clock')
        check(
            len(held) == 2 and held[1000][2] == held[1400][2] and seen and held[1000][2] != seen[1400][2],
            f'{host}: returning dt.CANCEL from "timer-tick" holds the HUD time ({held})',
            log,
        )

        api_dir = tmp / 'mods_api'
        api_dir.mkdir()
        script_mod(api_dir, 'api', API_LUA)
        shots = tmp / 'shots_api'
        shots.mkdir()
        log = run('api', api_dir, frames=1101, extra=('--present-out', shots / 'p.png', '--present-every', '900'))
        lines = {
            line.split(': ', 1)[1].split('\t')[0]: line.split(': ', 1)[1].split('\t')[1:]
            for line in log.splitlines()
            if line.startswith('[ducktales] Mod api: ') and '\t' in line
        }
        check(
            lines.get('outside', [''])[0] == 'false' and 'dt.set.lives is called in dt.on callbacks' in log,
            f'{host}: dt.set outside a callback is refused',
            log,
        )
        before, after = lines.get('before', []), lines.get('after', [])
        check(
            lines.get('deferred') == before[:1]
            and after[:1] == ['5']
            and after[1:2] in (['123'], ['122'])
            and after[2:3] == ['1']
            and after[3:4] == ['12340'],
            f'{host}: dt.set.lives/time/hp/money apply at the frame\'s end (before {before}, after {after})',
            log,
        )
        check(
            lines.get('range', [''])[0] == 'false' and 'dt.set.lives(10): out of range 0 to 9' in log,
            f'{host}: dt.set checks the range',
            log,
        )
        check(
            lines.get('toasts') == ['true', 'false'] and 'Mod api: toast: Hello from api' in log,
            f'{host}: dt.toast shows a notice and refuses a second one within a second',
            log,
        )
        check(
            lines.get('store') == ['1', 'true', '3.5', 'true'],
            f'{host}: dt.store keeps each type ({lines.get("store")})',
            log,
        )
        check(
            lines.get('store-cap', [''])[0] == 'false'
            and 'more than 65536 bytes' in log
            and lines.get('store-refused') == ['false', 'false'],
            f'{host}: dt.store refuses more than 64 KiB, tables and a key with a path in it',
            log,
        )
        base_dir = tmp / 'shots_base'
        base_dir.mkdir()
        run('plain', None, frames=901, extra=('--present-out', base_dir / 'p.png', '--present-every', '900'))
        shot, plain = png_read(shots / 'p_00900.png')[2], png_read(base_dir / 'p_00900.png')[2]
        rows = sorted({i // 256 for i, (x, y) in enumerate(zip(shot, plain)) if x != y})
        check(
            rows and rows[-1] < 120,
            f'{host}: the toast is drawn over the presented picture near the top (rows {rows[:1]}-{rows[-1:]})',
        )
        log = run('api-again', api_dir, frames=10)
        stored = (data / 'mods-data' / 'api.ini').read_text() if (data / 'mods-data' / 'api.ini').exists() else ''
        check(
            not (data / 'mods-data' / 'api.ini.tmp').exists(),
            f'{host}: dt.store leaves no temporary file after its write',
        )
        check(
            'Mod api: store\t2\ttrue\t3.5\ttrue' in log
            and 'runs = i:2' in stored
            and 'text = s:a\\nb\\\\c = d' in stored,
            f'{host}: dt.store comes back in the next run from mods-data/api.ini',
            log + stored,
        )

        reload_dir = tmp / 'mods_reload'
        script = reload_dir / 'reload' / 'scripts' / 'main.lua'
        script.parent.mkdir(parents=True)
        (reload_dir / 'reload' / 'mod.toml').write_text(mod_toml('reload', 'Reload'))
        versions = [
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version one") end end)',
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version two") end end)',
            'dt.on("frame", function(',
            'dt.on("frame", function(f) if f % 30 == 0 then dt.log("version three") end end)',
        ]
        stamp = (int(time.time()) - 1) * 10**9

        def save(version):
            pending = script.with_suffix('.tmp')
            pending.write_text(versions[version])
            os.utime(pending, ns=(stamp, stamp + version * 10**8))
            pending.replace(script)

        save(0)
        cmd = [exe, ROM, '--frames', '2000000', '--input', route, '--mods', 'on', '--mods-dir', reload_dir]
        if native:
            cmd += ['--config', tmp / 'x.ini']
        p = subprocess.Popen(
            [str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env
        )
        steps = [
            ('Mod reload: version one', 1),
            ('Mod reload: version two', 2),
            ('Mod reload: script stopped: scripts/main.lua:1:', 3),
            ('Mod reload: version three', None),
        ]
        got = []
        watchdog = threading.Timer(180, p.kill)
        watchdog.start()
        try:
            for want, write in steps:
                for line in p.stdout:
                    if want in line:
                        got.append(want)
                        break
                if write is not None:
                    save(write)
        finally:
            watchdog.cancel()
            p.kill()
            p.wait()
        check(
            len(got) == len(steps),
            f'{host}: hot reload: same-size edit in the same timestamp second runs, a broken script '
            f'stops and the fix runs ({len(got)} of {len(steps)} steps seen)',
        )
