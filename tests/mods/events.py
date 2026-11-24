"""Step 10 of the mods test (tests/mods_test.py): the events, dt.objects(), dt.camera() and dt.draw
on full_amazon, on the headless build and the native player, and the machine hashes with all of it
running."""

import os
import subprocess
import tempfile
from pathlib import Path

from .common import ROM, ROOT, check, png_read, script_mod

EVENTS_LUA = r'''
local function values(e)
    local t = {}
    for k, v in pairs(e) do
        if k ~= "name" then
            t[#t + 1] = k .. "=" .. v
        end
    end
    table.sort(t)
    return table.concat(t, " ")
end
for _, name in ipairs({"boss-hit", "boss-defeated", "enemy-defeated", "item-collected", "land-start", "land-clear",
                       "checkpoint", "money-changed"}) do
    dt.on(name, function(e)
        dt.log("event", dt.frame(), e.name, values(e))
    end)
end
dt.on("update", function()
    local _ = dt.state.x
end)
dt.on("frame", function(frame)
    dt.draw.rect(8, 40, 24, 16, 0xFF00FF)
    dt.draw.text(8, 60, "draw", 0x00FF00)
    if frame == 2000 then
        local objects, me, cam = dt.objects(), nil, dt.camera()
        for _, o in ipairs(objects) do
            if o.slot == 0 then
                me = o
            end
        end
        dt.log("query", #objects, me.x, me.y, dt.state.x, dt.state.y, cam.x, cam.row)
    end
    if frame == 2001 then
        local shown = 0
        for _ = 1, 70 do
            if dt.draw.rect(0, 0, 1, 1, 0) then
                shown = shown + 1
            end
        end
        dt.log("cap", shown)
    end
end)
'''


def event_checks(exe, native):
    """Step 10: the events, dt.objects(), dt.camera() and dt.draw on full_amazon."""
    route = ROOT / 'tests' / 'routes' / 'full_amazon.txt'
    frames = 9600
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        mods = tmp / 'mods'
        mods.mkdir()
        script_mod(mods, 'events', EVENTS_LUA)
        env = dict(os.environ, DUCKTALES_DATA_DIR=str(tmp / 'data'))

        def run(host_exe, name, with_mods, is_native):
            out = tmp / name
            out.mkdir()
            cmd = [host_exe, ROM, '--frames', frames, '--input', route]
            cmd += ['--present-out', out / 'p.png', '--present-every', '900']
            cmd += ['--lag', 'faithful', '--config', tmp / 'x.ini'] if is_native else ['--hash-out', out / 'hash.txt']
            if with_mods:
                cmd += ['--mods', 'on', '--mods-dir', mods]
            p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=3600, env=env)
            log = p.stdout + p.stderr
            if p.returncode:
                print(log[-3000:])
                raise SystemExit(f'events {name}: exit {p.returncode}')
            return out, log

        def lines(log, what):
            return [
                line.split('\t')[1:]
                for line in log.splitlines()
                if line.startswith(f'[ducktales] Mod events: {what}\t')
            ]

        logs = {}
        for host, host_exe, is_native in (('headless', exe, False), ('native player', native, True)):
            if not host_exe:
                continue
            out, log = run(host_exe, host.replace(' ', '_'), True, is_native)
            logs[host] = log
            events = lines(log, 'event')
            named = {}
            for frame, name, args in events:
                named.setdefault(name, []).append((int(frame), dict(a.split('=') for a in args.split())))
            once = {n: [v['land'] for _, v in named.get(n, [])] for n in ('land-start', 'checkpoint', 'land-clear')}
            check(
                all(v == ['0'] for v in once.values()),
                f'{host}: land-start, checkpoint and land-clear each fire once in the Amazon ({once})',
                log[-3000:],
            )
            hits, beaten = named.get('boss-hit', []), named.get('boss-defeated', [])
            check(
                len(beaten) == 1 and hits and beaten[0][0] == hits[-1][0] and beaten[0][1] == {'land': '0'},
                f'{host}: boss-defeated fires once, with the last of {len(hits)} boss hits',
                log[-3000:],
            )
            enemies, items = named.get('enemy-defeated', []), named.get('item-collected', [])
            check(
                enemies
                and items
                and all(set(v) == {'slot', 'kind', 'x', 'y'} for _, v in enemies)
                and all(set(v) == {'slot', 'item', 'x', 'y'} for _, v in items),
                f'{host}: {len(enemies)} enemy-defeated and {len(items)} item-collected events carry slot, '
                'kind or item and position',
                log[-3000:],
            )
            money = named.get('money-changed', [])
            total = sum(int(v['delta']) for _, v in money)
            check(
                money and total == int(money[-1][1]['money']),
                f'{host}: {len(money)} money-changed deltas add up to the money shown last ({total})',
                log[-3000:],
            )
            query = (lines(log, 'query') or [[]])[0]
            nums = [int(v) for v in query] if len(query) == 7 else []
            check(
                nums and nums[0] >= 1 and nums[1:3] == nums[3:5] and nums[5] <= nums[1] < nums[5] + 256,
                f'{host}: dt.objects() has Scrooge where dt.state has him, and dt.camera() shows him ({query})',
                log[-3000:],
            )
            check(
                [c[0] for c in lines(log, 'cap')] == ['62']
                and log.count('dt.draw: more than 64 drawings in one frame') == 1,
                f'{host}: dt.draw stops at 64 drawings a frame and says so once',
                log[-3000:],
            )
            pic = png_read(out / 'p_00900.png')[2]
            rect = [pic[y * 256 + x] for y in range(40, 56) for x in range(8, 32)]
            text = [pic[y * 256 + x] for y in range(60, 65) for x in range(8, 24)]
            check(
                all(p == 0xFFFF00FF for p in rect) and 0xFF00FF00 in text,
                f'{host}: dt.draw shows its rectangle and text over the presented picture (frame 900)',
                f'{rect.count(0xFFFF00FF)} of {len(rect)} rectangle pixels magenta, {text.count(0xFF00FF00)} text '
                f'pixels green; text area {sorted({hex(p) for p in text})}',
            )
        if exe:
            base, _ = run(exe, 'base', False, False)
            check(
                (tmp / 'headless' / 'hash.txt').read_bytes() == (base / 'hash.txt').read_bytes(),
                f'headless: with every event handled ("update" reading dt.state included), dt.objects(), dt.camera() and dt.draw the per-frame '
                f'machine hashes equal a run without mods (frames 0-{frames})',
            )
        if len(logs) == 2:
            a, b = (lines(logs[h], 'event') for h in ('headless', 'native player'))
            check(a == b, f'the native player logs the same {len(b)} events in the same frames as the cycle host')
