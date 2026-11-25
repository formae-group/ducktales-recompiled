"""Step 11 of the mods test (tests/mods_test.py): gameplay scripts.

dt.random: the same numbers after the same seed and on both hosts, its range, two mods' own
sequences, the refused calls, and the RAM of a run without mods on every frame while it draws.
The "update" event once per step of play, dt.set inside a game event's handler written at once and
logged (repeats counted in one line), on the headless build and the native player. Scrooge's
tunables on a short Amazon route: each changes what it should against a run without them, nil
gives the game's value back, the highest mod in the order wins, and the native player measures the
same. On the headless build, scripts that write and tune with --decomp off, check and replace: the
same lines, no mismatch, and with the developer build (--dev-build) the same RAM outside the stack
on every frame with the decompiled code in place of the original (decomp_test.py's comparison).
dt.set.invulnerable is refused where it would hold the knock-back forever. Object handles on
amazon_intro: a timed enemy moved and despawned, a stale handle, the player's slot, actions and
positions out of bounds, calls outside a game event's handler, and the four events seen at the
frame's end; dt.spawn on amazon_intro: a hopper made on the screen that the game runs as its own,
the arguments it refuses, spawns until 2 slots are left and a spawned enemy despawned; dt.level.set
puts a wall in front of Scrooge that stops him, logs its repeats in one line and refuses what it must;
dt.spawn_item on amazon_intro: the items it refuses, and a pickup of each item it makes that Scrooge
takes through the game's code (money, an HP dot, full HP, a life and none past 9, with the treasures
and key items unchanged); dt.ai on amazon_intro: a hopper skipped by dt.SKIP stays put, one given
speed 512 walks further and one given vy 1536 at its take-off jumps higher, what dt.ai and the
three fields refuse, the code taken back and given again, and the per-frame instruction bound;
dt.set.boss_hits on full_amazon's fight and outside it, and dt.spawn_item refused in the fight. The
dt.ai script, every hopper skipped in turn once dt.spawn filled the slots, and the item script are
also in the --decomp off, check and replace runs.
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

# dt.random: the same numbers again after the same seed, a range, a second mod's own sequence,
# and the refused calls; with only these scripts the RAM is the run without mods' on every frame.
RANDOM_LUA = r'''
local function draw(count, ...)
    local out = {}
    for i = 1, count do
        out[i] = dt.random(...)
    end
    return table.concat(out, ",")
end
local first = draw(8, 1000)
dt.random.seed(7)
local seven = draw(8, 1000)
dt.random.seed(7)
dt.log("seed", draw(8, 1000) == seven, seven ~= first)
local low, high = 99, 0
for _ = 1, 500 do
    local v = dt.random(3, 5)
    low, high = math.min(low, v), math.max(high, v)
end
dt.log("range", low, high, dt.random(-2, -2))
dt.log("first", first)
dt.log("empty", pcall(dt.random, 5, 4))
dt.log("no range", pcall(dt.random))
dt.log("write", pcall(function() dt.random.x = 1 end))
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

# Handles of dt.objects() on amazon_intro: the refusals at step 2, the first timed hopper moved up
# and despawned at step 30, its stale handle; the four frame-end events summed up at frame 1400.
OBJECTS_LUA = r'''
local step, moved, gone, held = 0, false, false, nil
local function find(kind)
    for _, o in ipairs(dt.objects()) do
        if o.kind == kind then
            return o
        end
    end
end
dt.on("update", function()
    step = step + 1
    local objects = dt.objects()
    if step == 2 then
        local ok, err = pcall(objects[1].set, objects[1], { y = 100 })
        dt.log("player", objects[1].slot, objects[1].player, ok, err)
        local block = find(8)
        ok, err = pcall(block.set, block, { action = 99 })
        dt.log("bad action", ok, err)
        dt.log("block action", block:set({ action = 26 }), block.action)
        local chest = find(21)
        ok, err = pcall(chest.set, chest, { action = 28 })
        dt.log("chest", ok, err)
        ok, err = pcall(chest.set, chest, { x = 0 })
        dt.log("off screen", ok, err)
    end
    local hopper = find(1)
    if hopper and not moved then
        moved = true
        local y = hopper.y
        hopper:set({ y = y - 24 })
        dt.log("moved", step, y, hopper.y, find(1).y)
        held = hopper
    elseif held and not gone and step == 30 then
        gone = true
        dt.log("despawn", step, held:despawn(), find(1) == nil)
        dt.log("stale", held:set({ y = 50 }))
    end
end)
dt.on("object-despawned", function(e)
    if e.kind == 1 and gone and step == 30 then
        dt.log("despawned event", e.slot, e.record)
    end
end)
local seen = { spawned = 0, despawned = 0, screens = {}, modes = {} }
dt.on("object-spawned", function(e)
    seen.spawned = seen.spawned + 1
    if e.kind == 1 and not seen.hopper then
        seen.hopper = e.slot .. "/" .. e.record
    end
end)
dt.on("object-despawned", function()
    seen.despawned = seen.despawned + 1
end)
dt.on("screen-changed", function(e)
    seen.screens[#seen.screens + 1] = e.row .. ":" .. e.screen
end)
dt.on("player-mode-changed", function(e)
    seen.modes[#seen.modes + 1] = e.previous .. ">" .. e.mode
end)
dt.on("frame", function(frame)
    if frame == 1400 then
        dt.log("events", seen.spawned, seen.despawned, seen.hopper, table.concat(seen.screens, ","),
            table.concat(seen.modes, ",", 1, math.min(#seen.modes, 6)))
    end
    if frame == 1000 then
        local o = dt.objects()[1]
        local ok, err = pcall(o.set, o, { y = 1 })
        dt.log("outside", ok, err)
    end
end)
'''

# dt.spawn on amazon_intro: a hopper (a timed enemy of the land) on the screen, refused arguments,
# the hopper walking and falling as the game's own, spawns until the 2 slots kept free, and the
# last one despawned.
SPAWN_LUA = r'''
local step, made, last, events = 0, nil, nil, {}
local function refused(...)
    local ok, err = pcall(dt.spawn, ...)
    return tostring(ok) .. " " .. err:gsub("^.-: ", "", 1)
end
local function live(slot)
    for _, o in ipairs(dt.objects()) do
        if o.slot == slot then
            return o
        end
    end
end
dt.on("update", function()
    step = step + 1
    local left = dt.camera().x
    if step == 40 then
        made = dt.spawn(1, left + 200, 100)
        local o = live(made.slot)
        dt.log("spawned", made.kind, made.action, made.x - left, made.y, made.record, o.kind, o.record)
        dt.log("not an enemy", refused(8, left + 200, 100))
        dt.log("not this land", refused(24, left + 200, 100))
        dt.log("action", refused(1, left + 200, 100, 99))
        dt.log("off screen", refused(1, left - 1, 100))
        dt.log("below", refused(1, left + 200, 240))
        dt.log("arguments", refused(1, left + 200))
    elseif step == 80 then
        local o = live(made.slot)
        dt.log("moved", o.kind, o.x < made.x, o.y > made.y)
        dt.log("fields", o.facing, o.speed, o.vy)
    elseif step == 150 then
        local n, why = 0, nil
        repeat
            local o
            o, why = dt.spawn(1, left + 100 + n, 80)
            last = o or last
            n = n + (o and 1 or 0)
        until not o
        dt.log("full", n, why, 16 - #dt.objects())
    elseif step == 151 then
        dt.log("despawn", last:despawn(), live(last.slot) == nil)
    end
end)
dt.on("object-spawned", function(e)
    if made and e.slot == made.slot and #events == 0 then
        events[1] = e.kind .. "/" .. e.record
        dt.log("spawned event", step, events[1])
    end
end)
dt.on("frame", function(frame)
    if frame == 1000 then
        local ok, err = pcall(dt.spawn, 1, 0, 0)
        dt.log("outside", ok, err)
    end
end)
'''

# dt.spawn_item on amazon_intro: the items it refuses, then each item it makes in turn, 60 lines up,
# moved onto Scrooge every step until he takes it (HP set to 1 before the HP items, lives to 3 and
# then 9 before the lives); each take logs its item and what changed.
ITEM_LUA = r'''
local step, n, took, wait = 0, 0, nil, 40
local plan = {
    {0x23}, {0x28}, {0x61}, {0x24, hp = 1}, {0x25, hp = 1}, {0x27, lives = 3}, {0x27, lives = 9},
}
local function try(...)
    local ok, err = pcall(dt.spawn_item, ...)
    return tostring(ok) .. " " .. (tostring(err):gsub("^.-: ", "", 1))
end
local function made()
    for _, o in ipairs(dt.objects()) do
        if o.kind == 19 and o.record == 255 then
            return o
        end
    end
end
dt.on("update", function()
    step = step + 1
    local s = dt.state
    if took then
        dt.log("took", took.item, s.money - took.money, s.hp - took.hp, s.lives - took.lives, s.lives, s.hp == s.hp_max,
            s.treasures == took.treasures and s.key_items == took.key_items)
        took = nil
    end
    if step == 30 then
        dt.log("power", try(0x26, s.x, s.y))
        dt.log("treasure", try(0x6F, s.x, s.y))
        dt.log("key", try(0x29, s.x, s.y))
        dt.log("extra dot", try(0x2B, s.x, s.y))
        dt.log("off screen", try(0x23, dt.camera().x - 1, s.y))
        dt.log("arguments", try(0x23, s.x))
    end
    local o = made()
    if o then
        o:set{x = s.x, y = s.y}
    elseif step >= wait and n < #plan then
        n = n + 1
        local p = plan[n]
        if p.hp then
            dt.set.hp(p.hp)
        end
        if p.lives then
            dt.set.lives(p.lives)
        end
        local ok = dt.spawn_item(p[1], s.x, 60)
        o = made()
        dt.log("spawned", p[1], ok, o.kind, o.action, o.record, o.state)
        wait = step + 10
    end
end)
dt.on("item-collected", function(e)
    local s = dt.state
    if made() and e.slot == made().slot then
        took = {
            item = e.item, money = s.money, hp = s.hp, lives = s.lives,
            treasures = s.treasures, key_items = s.key_items,
        }
    end
end)
dt.on("frame", function(frame)
    if frame == 1400 then
        dt.log("outside", pcall(dt.spawn_item, 0x23, 0, 0))
    end
end)
'''

# dt.ai on amazon_intro: three hoppers spawned at step 40, one skipped every turn, one walking at
# speed 512 and taking off at vy 1536, one left alone; the calls it refuses; the code taken back at
# step 100 and given again at 110; once the slots are full (SPAWN_LUA in the trio), every hopper
# skipped in turn.
AI_LUA = r'''
local step, frozen, fast, plain, base, thawed = 0, nil, nil, nil, {}, nil
local turns, afters, rise, heights = 0, 0, {}, {}
local function try(f, ...)
    local ok, err = pcall(f, ...)
    return tostring(ok) .. " " .. (tostring(err):gsub("^.-: ", "", 1))
end
local function live(slot)
    for _, o in ipairs(dt.objects()) do
        if o.slot == slot then
            return o
        end
    end
end
local hopper = {
    before = function(o)
        turns = turns + 1
        if frozen and o.slot == frozen.slot then
            return dt.SKIP
        end
        if fast and o.slot == fast.slot and o.speed ~= 512 then
            o:set{speed = 512}
        end
        if step > 150 and (dt.frame() + o.slot) % 4 == 0 then
            return dt.SKIP
        end
    end,
    after = function(o)
        afters = afters + 1
        local r = rise[o.slot]
        if not r and o.vy == 1024 then
            if fast and o.slot == fast.slot then
                o:set{vy = 1536}
            end
            rise[o.slot] = {o.y, o.y}
        elseif r then
            r[2] = math.min(r[2], o.y)
            if o.vy < 0 then
                heights[o.slot] = heights[o.slot] or (r[1] - r[2])
                rise[o.slot] = nil
            end
        end
        if fast and o.slot == fast.slot and step == 60 then
            dt.log("range", try(o.set, o, {speed = 2049}), try(o.set, o, {vy = -2049}), try(o.set, o, {facing = 2}))
        end
    end,
}
dt.log("not an enemy", try(dt.ai, 8, hopper))
dt.log("bad table", try(dt.ai, 1, {during = hopper.before}))
dt.log("empty", try(dt.ai, 1, {}))
dt.ai(1, hopper)
dt.on("update", function()
    step = step + 1
    local left = dt.camera().x
    if step == 40 then
        frozen = dt.spawn(1, left + 200, 100)
        fast = dt.spawn(1, left + 160, 100)
        plain = dt.spawn(1, left + 120, 100)
        base = {frozen.x, frozen.y, fast.x, plain.x}
        for _, o in ipairs(dt.objects()) do
            if o.kind == 8 then
                dt.log("block", try(o.set, o, {facing = 1}))
                break
            end
        end
    elseif step == 70 then
        local f, g, p = live(frozen.slot), live(fast.slot), live(plain.slot)
        dt.log("moved", f.x - base[1], f.y - base[2], g.x - base[3], p.x - base[4])
    elseif step == 100 then
        dt.log("heights", heights[plain.slot], heights[fast.slot])
        dt.ai(1, nil)
        thawed = live(frozen.slot)
    elseif step == 110 then
        local f = live(frozen.slot)
        dt.log("thawed", f == nil or f.y ~= thawed.y, turns, afters)
        dt.ai(1, hopper)
    end
end)
'''

# dt.ai callbacks of about 80,000 instructions each: one passes, several in one frame stop the mod.
AI_BUDGET_LUA = r'''
local calls = 0
dt.ai(1, {
    before = function(o)
        local n = 0
        for i = 1, 40000 do
            n = n + i
        end
        calls = calls + 1
        if calls == 1 then
            dt.log("first", n)
        end
    end,
})
'''

# dt.level.set on amazon_intro (metatile 102 is solid in the Amazon's tileset, 158 special): a wall
# put in front of Scrooge as he walks right, set again for 10 steps, and the calls it refuses.
LEVEL_LUA = r'''
local step, wall, other = 0, nil, false
local function refused(...)
    local ok, a, b = pcall(dt.level.set, ...)
    if ok then
        return tostring(a) .. " " .. tostring(b)
    end
    return "error " .. a:gsub("^.-: ", "", 1)
end
dt.on("update", function()
    step = step + 1
    local left = dt.camera().x
    if step == 70 then
        wall = { dt.state.x + 20, dt.state.y }
        dt.log("wall", dt.state.x, dt.level.set(wall[1], wall[2], 102))
        dt.log("second", refused(wall[1], wall[2] - 16, 102))
        dt.log("off screen", refused(left - 1, 100, 102))
        dt.log("hud", refused(left + 10, 40, 102))
        dt.log("special", refused(left + 10, 100, 158))
        dt.log("metatile", refused(left + 10, 100, 256))
        dt.log("arguments", refused(left + 10, 100))
    elseif wall and step < 70 + 10 then
        dt.level.set(wall[1], wall[2], 102)
    elseif step == 90 then
        dt.log("blocked", dt.state.x, wall[1])
    end
end)
dt.on("object-spawned", function()
    if wall and not other then
        other = true
        dt.log("other event", refused(dt.camera().x + 10, 100, 102))
    end
end)
dt.on("frame", function(frame)
    if frame == 1000 then
        local ok, err = pcall(dt.level.set, 0, 0, 0)
        dt.log("outside", ok, err)
    end
end)
'''

# dt.set.boss_hits outside the fight, out of range, and 5 at the second hit (the game's leaves 3).
BOSS_LUA = r'''
local hits = 0
dt.on("update", function()
    if dt.frame() == 1000 then
        local ok, err = pcall(dt.set.boss_hits, 3)
        dt.log("outside fight", ok, err)
    end
end)
dt.on("boss-hit", function()
    hits = hits + 1
    if hits == 2 then
        dt.log("item in fight", pcall(dt.spawn_item, 0x23, dt.state.x, dt.state.y))
        local ok, err = pcall(dt.set.boss_hits, 6)
        dt.log("range", ok, err)
        dt.set.boss_hits(5)
    end
end)
dt.on("frame", function(frame)
    if frame == 7300 then
        dt.log("hits left", hits, dt.state.boss_hits)
    end
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
        random_mods = mods('random', {'random': RANDOM_LUA, 'random-b': RANDOM_LUA})
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'random_{host.replace(" ", "_")}', is_native, random_mods, frames=60)
            got = {
                w: (lines(log, 'random', w) or [[]])[0]
                for w in ('seed', 'range', 'first', 'empty', 'no range', 'write')
            }
            other = (lines(log, 'random-b', 'first') or [[]])[0]
            check(
                got['seed'] == ['true', 'true']
                and got['range'] == ['3', '5', '-2']
                and got['first'][:1] != other[:1]
                and got['empty'] == ['false', 'dt.random(5, 4): the range is empty']
                and got['no range'] == ['false', 'dt.random(n) or dt.random(m, n)']
                and got['write'][:1] == ['false'],
                f'{host}: dt.random repeats after the same seed, keeps to its range, differs between two mods and '
                f'refuses an empty range',
                f'{got} {other}',
            )
            if host == 'headless':
                random_first = got['first']
            else:
                check(
                    got['first'] == random_first,
                    f'the native player draws the same dt.random numbers ({got["first"][0][:20]}...)',
                )
        if dev:
            ram = [tmp / 'random.ram', tmp / 'none.ram']
            run(dev, 'random_ram', False, random_mods, extra=['--dt-ram-log', ram[0]])
            subprocess.run(
                [
                    str(c)
                    for c in (dev, ROM, '--frames', 1500, '--input', amazon, '--mods', 'off', '--dt-ram-log', ram[1])
                ],
                capture_output=True,
                timeout=3600,
                env=env,
            )
            diff, info = ram_diff(ram)
            check(
                diff is None,
                f'headless: with dt.random drawing, the RAM is the run without mods\' on every frame ({info})',
                diff or '',
            )
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

        object_mods = mods('objects', {'objects': OBJECTS_LUA})
        object_lines = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'objects_{host.replace(" ", "_")}', is_native, object_mods)
            object_lines[host] = [line for line in log.splitlines() if 'Mod objects:' in line]
            got = {
                what: (lines(log, 'objects', what) or [[]])[0]
                for what in (
                    'player',
                    'bad action',
                    'block action',
                    'chest',
                    'off screen',
                    'moved',
                    'despawn',
                    'stale',
                    'despawned event',
                    'outside',
                    'events',
                )
            }
            refused = {what: got[what][-1:] for what in ('player', 'bad action', 'chest', 'off screen', 'outside')}
            check(
                got['player'][:3] == ['0', '1', 'false']
                and refused['player'][0].endswith("slot 0 is player 1's: read only")
                and got['bad action'][:1] == ['false']
                and refused['bad action'][0].endswith('this land spawns no kind 8 with that action')
                and got['chest'][:1] == ['false']
                and refused['chest'][0].endswith('the action of kind 21 is the item it gives')
                and got['off screen'][:1] == ['false']
                and refused['off screen'][0].endswith('(the screen)')
                and got['outside'][:1] == ['false']
                and refused['outside'][0].endswith("is called in a game event's handler"),
                f'{host}: a handle refuses the player\'s slot, an action the land does not spawn, a '
                f'chest\'s item, a position off the screen and a call outside a game event\'s handler',
                f'{got}',
            )
            moved = got['moved']
            check(
                got['block action'] == ['true', '26']
                and any(line.endswith(':set{action = 26}, was 28') for line in object_lines[host])
                and len(moved) == 4
                and int(moved[2]) == int(moved[1]) - 24 == int(moved[3]),
                f'{host}: o:set writes at once and logs the value it replaced (a block\'s action, a '
                f'hopper moved up 24 lines: {moved[1:]})',
                f'{got}',
            )
            check(
                got['despawn'] == ['30', 'true', 'true']
                and got['stale'] == ['false', 'gone']
                and any(line.endswith(':set() dropped: it is gone') for line in object_lines[host])
                and got['despawned event'][1:] == ['255'],
                f'{host}: o:despawn() frees the slot at once, object-despawned reports it, and the stale '
                f'handle changes nothing',
                f'{got}',
            )
            seen = got['events']
            check(
                len(seen) == 5
                and int(seen[0]) > 10
                and int(seen[1]) > 10
                and seen[2].endswith('/255')
                and '5:40' in seen[3].split(',')
                and seen[4].startswith('15>0,0>1'),
                f'{host}: object-spawned, object-despawned, screen-changed and player-mode-changed at the '
                f'frame\'s end ({seen})',
            )
        if len(object_lines) == 2:
            check(
                object_lines['headless'] == object_lines['native player'],
                f'the native player logs the same {len(object_lines["headless"])} lines with the handles',
            )

        spawn_mods = mods('spawn', {'spawn': SPAWN_LUA})
        spawn_lines = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'spawn_{host.replace(" ", "_")}', is_native, spawn_mods)
            spawn_lines[host] = [line for line in log.splitlines() if 'Mod spawn:' in line]
            what = ('spawned', 'not an enemy', 'not this land', 'action', 'off screen', 'below', 'arguments')
            got = {
                w: (lines(log, 'spawn', w) or [[]])[0]
                for w in (*what, 'spawned event', 'moved', 'fields', 'full', 'despawn')
            }
            got['outside'] = (lines(log, 'spawn', 'outside') or [[]])[0]
            check(
                got['spawned'] == ['1', '11', '200', '100', '255', '1', '255']
                and any(
                    line.endswith('Mod spawn: dt.spawn(1, 258, 100): object 8 (kind 1)') for line in spawn_lines[host]
                )
                and got['spawned event'] == ['40', '1/255'],
                f'{host}: dt.spawn makes a hopper on the screen with the land\'s template and record 255, logs it, '
                f'and object-spawned reports it ({got["spawned"]})',
                f'{got}',
            )
            check(
                got['not an enemy'] == ['false kind 8 is not one of the enemies (docs/MODS.md)']
                and got['not this land'] == ['false this land spawns no kind 24']
                and got['action'] == ['false this land spawns no kind 1 with that action']
                and got['off screen'][0].endswith('(the screen)')
                and got['below'] == ['false out of range 0 to 239']
                and got['arguments'] == ['false kind, x, y and an optional action are needed']
                and got['outside'][:1] == ['false']
                and got['outside'][-1].endswith("dt.spawn(1, 0, 0) is called in a game event's handler"),
                f'{host}: dt.spawn refuses a block, an enemy the land does not use, an action it does not spawn, a '
                f'position off the screen or below it, missing arguments and a call outside a game event\'s handler',
                f'{got}',
            )
            check(
                got['moved'] == ['1', 'true', 'true']
                and got['fields'] == ['0', '256', '0']
                and got['full'][1:] == ['2 free slots, 2 kept for the game', '2']
                and int(got['full'][0]) > 0
                and got['despawn'] == ['true', 'true'],
                f'{host}: the spawned hopper walks left at speed 256 (facing 0) and falls as the game\'s own, spawns stop with 2 slots free '
                f'({got["full"][:1]} more made, then nil and the reason), and a spawned enemy despawns',
                f'{got}',
            )
        if len(spawn_lines) == 2:
            check(
                spawn_lines['headless'] == spawn_lines['native player'],
                f'the native player logs the same {len(spawn_lines["headless"])} lines with dt.spawn',
            )

        item_mods = mods('item', {'item': ITEM_LUA})
        item_lines = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'item_{host.replace(" ", "_")}', is_native, item_mods)
            item_lines[host] = [line for line in log.splitlines() if 'Mod item:' in line]
            what = ('power', 'treasure', 'key', 'extra dot', 'off screen', 'arguments', 'outside')
            got = {w: (lines(log, 'item', w) or [[]])[0] for w in what}
            took = {}
            for t in lines(log, 'item', 'took'):
                took.setdefault(t[0], []).append(t[1:])
            check(
                all(
                    got[w] == [f'false item {i} is not one dt.spawn_item makes (docs/MODS.md)']
                    for w, i in (('power', 38), ('treasure', 111), ('key', 41), ('extra dot', 43))
                )
                and got['off screen'][0].endswith('(the screen)')
                and got['arguments'] == ['false an item, x and y are needed']
                and got['outside'][:1] == ['false']
                and got['outside'][-1].endswith("dt.spawn_item(35, 0, 0) is called in a game event's handler"),
                f'{host}: dt.spawn_item refuses the power-up, a treasure, a key item, the extra dot, a position off '
                f'the screen, missing arguments and a call outside a game event\'s handler',
                f'{got}',
            )
            spawned = [s for s in lines(log, 'item', 'spawned')]
            check(
                [s[:1] + s[2:] for s in spawned]
                == [[i, '19', i, '255', '161'] for i in ('35', '40', '97', '36', '37', '39', '39')]
                and [t[0] for t in took.get('35', [])] == ['2000']
                and [t[0] for t in took.get('40', [])] == ['10000']
                and [t[0] for t in took.get('97', [])] == ['50000']
                and [t[1] for t in took.get('36', [])] == ['1']
                and [t[4] for t in took.get('37', [])] == ['true']
                and [t[2:4] for t in took.get('39', [])] == [['1', '4'], ['0', '9']]
                and all(t[-1] == 'true' for ts in took.values() for t in ts)
                and any(line.endswith('Mod item: dt.spawn_item(35, 188, 60)') for line in item_lines[host]),
                f'{host}: dt.spawn_item makes a pickup with record 255 that Scrooge takes through the game\'s code: '
                f'2,000, 10,000 and 50,000 money, an HP dot, full HP and a life (none at 9), with the treasures and '
                f'key items unchanged',
                f'{spawned} {took}',
            )
        if len(item_lines) == 2:
            check(
                item_lines['headless'] == item_lines['native player'],
                f'the native player logs the same {len(item_lines["headless"])} lines with dt.spawn_item',
            )

        ai_mods = mods('ai', {'ai': AI_LUA, 'ai-budget': AI_BUDGET_LUA})
        ai_lines = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'ai_{host.replace(" ", "_")}', is_native, ai_mods)
            ai_lines[host] = [line for line in log.splitlines() if 'Mod ai' in line]
            what = ('not an enemy', 'bad table', 'empty', 'block', 'range', 'moved', 'heights', 'thawed')
            got = {w: (lines(log, 'ai', w) or [[]])[0] for w in what}
            check(
                got['not an enemy'] == ['false kind 8 is not one of the enemies (docs/MODS.md)']
                and got['bad table'] == ['false only before and after']
                and got['empty'] == ['false before, after or both are needed']
                and got['block'] == ['false facing is set on the enemies only (docs/MODS.md)']
                and got['range']
                == ['false out of range 0 to 2048', 'false out of range -2048 to 2048', 'false out of range 0 to 1'],
                f'{host}: dt.ai refuses a kind that is not an enemy and a table without before or after, and '
                f'facing, speed and vy are refused out of range and on a block',
                f'{got}',
            )
            moved, heights = got['moved'], got['heights']
            check(
                len(moved) == 4
                and moved[:2] == ['0', '0']
                and abs(int(moved[2])) > abs(int(moved[3])) + 10
                and len(heights) == 2
                and int(heights[1]) > int(heights[0]) + 20
                and got['thawed'][:1] == ['true']
                and any(line.endswith('Mod ai: dt.ai(1): object 8 (kind 1) skipped') for line in ai_lines[host])
                and any(line.endswith(':set{speed = 512}, was 0') for line in ai_lines[host])
                and any(line.endswith(':set{vy = 1536}, was 1024') for line in ai_lines[host]),
                f'{host}: a hopper skipped by dt.SKIP stays where it was, one at speed 512 walks further than the '
                f'game\'s ({moved[2:]}), vy 1536 at its take-off jumps higher ({heights}), the skip and the '
                f'writes are logged, and dt.ai(1, nil) gives the hopper its own code back',
                f'{got}',
            )
            budget = [line for line in ai_lines[host] if 'Mod ai-budget: script stopped: ' in line]
            check(
                (lines(log, 'ai-budget', 'first') or [[]])[0] == ['800020000']
                and len(budget) == 1
                and budget[0].endswith(
                    "ran more than 250000 instructions in dt.ai callbacks in one frame (in dt.ai(1)'s before callback)"
                ),
                f'{host}: one dt.ai callback of about 80,000 instructions runs, and several in one frame stop the mod',
                '\n'.join(budget),
            )
        if len(ai_lines) == 2:
            check(
                ai_lines['headless'] == ai_lines['native player'],
                f'the native player logs the same {len(ai_lines["headless"])} lines with dt.ai',
            )

        level_mods = mods('level', {'level': LEVEL_LUA})
        level_lines = {}
        for host, host_exe, is_native in hosts:
            log = run(host_exe, f'level_{host.replace(" ", "_")}', is_native, level_mods)
            level_lines[host] = [line for line in log.splitlines() if 'Mod level:' in line]
            what = ('wall', 'second', 'off screen', 'hud', 'special', 'metatile', 'arguments', 'other event')
            got = {w: (lines(log, 'level', w) or [[]])[0] for w in (*what, 'blocked', 'outside')}
            written = [line.split(': ', 1)[1] for line in level_lines[host] if ': dt.level.set(' in line]
            blocked = got['blocked']
            check(
                got['wall'][1:] == ['true']
                and len(blocked) == 2
                and int(blocked[0]) < int(blocked[1]) - 9
                and written == ['dt.level.set(214, 180, 102)', 'dt.level.set(214, 180, 102) repeated 9 more times'],
                f'{host}: dt.level.set puts a solid metatile in front of Scrooge and it stops him '
                f'(x {blocked[:1]}, the wall at {blocked[1:]}); set again each step, it is logged once with its '
                f'repeats',
                f'{got}\n' + '\n'.join(written),
            )
            check(
                got['second'] == ['false the tile edit queue is in use']
                and got['off screen'][0].endswith('(the screen)')
                and got['hud'] == ['error out of range 48 to 239 (below the HUD)']
                and got['special'] == ['error metatile 158 is a special one (collision class 3)']
                and got['metatile'] == ['error out of range 0 to 255']
                and got['arguments'] == ['error x, y and a metatile are needed']
                and got['other event'] == ['error only in an "update" handler']
                and got['outside'][:1] == ['false']
                and got['outside'][-1].endswith("dt.level.set(0, 0, 0) is called in a game event's handler"),
                f'{host}: dt.level.set refuses a second edit in the same step (false and the reason), a cell off '
                f'the screen or in the HUD, a special or unknown metatile, missing arguments, another event\'s '
                f'handler and a call outside a game event\'s handler',
                f'{got}',
            )
        if len(level_lines) == 2:
            check(
                level_lines['headless'] == level_lines['native player'],
                f'the native player logs the same {len(level_lines["headless"])} lines with dt.level.set',
            )

        if not exe:
            return
        log = run(
            exe, 'boss', False, mods('boss', {'boss': BOSS_LUA}), ROOT / 'tests' / 'routes' / 'full_amazon.txt', 7310
        )
        boss = {
            what: (lines(log, 'boss', what) or [[]])[0]
            for what in ('outside fight', 'range', 'hits left', 'item in fight')
        }
        check(
            boss['outside fight'] == ['false', 'dt.set.boss_hits(3): only during a boss fight']
            and boss['range'] == ['false', 'dt.set.boss_hits(6): out of range 1 to 5']
            and 'Mod boss: dt.set.boss_hits(5), was 4' in log
            and boss['hits left'] == ['2', '4']
            and boss['item in fight'][:1] == ['false']
            and boss['item in fight'][-1].endswith('not during a boss fight'),
            f'headless: dt.set.boss_hits works only during the fight, 1 to 5; written at the second hit, '
            f'the boss has {boss["hits left"][1:]} hits left where the game leaves 3; dt.spawn_item is refused in '
            f'the fight',
            f'{boss}',
        )

        knock_log = run(exe, 'knock', False, mods('knock', {'knock': KNOCK_LUA}))
        knock = (lines(knock_log, 'knock', 'knock-back write') or [[]])[0]
        check(
            knock[:1] == ['false']
            and knock[1:2] == ['dt.set.invulnerable(32): during the knock-back it is 61 to 127 (it ends at 60)']
            and 'Mod knock: dt.set.invulnerable(96), was ' in knock_log,
            'headless: dt.set.invulnerable is refused at or below the knock-back\'s end and written above it',
            f'{knock}',
        )
        held = (lines(knock_log, 'knock', 'knock-back steps') or [[]])[0]
        check(
            held[:1] and int(held[0]) >= 30,
            f'headless: the knock-back lasts until the written invulnerability counts down to its end '
            f'({held[:1]} steps, the game\'s is 11)',
        )

        scripts = {
            'gameplay': UPDATE_LUA,
            'knock': KNOCK_LUA,
            'objects': OBJECTS_LUA,
            'spawn': SPAWN_LUA,
            'level': LEVEL_LUA,
            'ai': AI_LUA,
            'item': ITEM_LUA,
        }
        trio_mods = mods('trio', scripts)
        logs, ram = {}, []
        for mode in ('off', 'check', 'replace'):
            host_exe, extra = exe, ['--decomp', mode]
            if dev and mode != 'check':
                ram.append(tmp / f'{mode}.ram')
                host_exe, extra = dev, [*extra, '--dt-ram-log', ram[-1]]
            logs[mode] = run(host_exe, f'decomp_{mode}', False, trio_mods, extra=extra)
        check(
            all('MISMATCH' not in log and 'TIMING' not in log for log in logs.values()),
            'headless: with the writing, tuning, object, spawn, level, dt.ai and item scripts, --decomp check finds no '
            'mismatch and no timing difference',
            logs['check'][-3000:],
        )
        written = {
            m: [line for line in log.splitlines() if any(f'Mod {i}:' in line for i in scripts)]
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
            f'headless: with the writing, tuning, object, spawn, level, dt.ai and item scripts, --decomp replace '
            f'keeps the RAM of --decomp off outside the stack ({info})',
            diff or '',
        )
