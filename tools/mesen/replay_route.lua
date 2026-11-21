-- replay_route.lua: replay a tests/routes/*.txt input schedule in Mesen2's
-- headless test runner and write a NESRecomp frame log (the "CYCFRAME" v2
-- format of runner/cyc/cyc_host.c write_frame_log, read by tools/framelog.py).
--
-- Run through tests/mesen_compare.py, or by hand:
--   Mesen.exe --testrunner replay_route.lua ROM.nes
--             --debug.scriptWindow.allowIoOsAccess=true --timeout=SECONDS
-- with these environment variables (Windows paths):
--   DT_MESEN_ROUTE   input schedule: "<frame> <BUTTONS joined by +, or ->"
--   DT_MESEN_OUT     frame log to write
--   DT_MESEN_FRAMES  frames to record (records 0 .. N-1)
--   DT_MESEN_CIRAM   "1": also store the 2 KB of nametable RAM per record
--   DT_MESEN_STATUS  optional: a text file that receives "ok N" or the error
--   DT_MESEN_TRACE_FRAME, DT_MESEN_TRACE_OUT  optional: write one line per
--                    instruction ("cycle PC A X Y S scanline dot") from the
--                    input poll that starts frame N to the one that ends it,
--                    the window of ducktales.exe --trace-frame N (see
--                    tools/mesen/trace_diff.py)
--
-- Timing (Mesen2 2.1.1 Core/NES/NesPpu.cpp): the PPU raises EndFrame at
-- scanline 240 dot 0 and polls the controllers at scanline 241 dot 0, then
-- raises InputPolled. Record k is taken at the k-th EndFrame (0 based); the
-- poll that follows it receives the schedule's buttons for frame k + 1, which
-- is what ducktales.exe --frame-log-at mesen numbers the same way: its frame
-- k + 1 begins at scanline 241 dot 1 with the buttons of line k + 1.

local route_path = os.getenv("DT_MESEN_ROUTE")
local out_path = os.getenv("DT_MESEN_OUT")
local frames = tonumber(os.getenv("DT_MESEN_FRAMES") or "")
local want_ciram = os.getenv("DT_MESEN_CIRAM") == "1"
local status_path = os.getenv("DT_MESEN_STATUS")
local trace_frame = tonumber(os.getenv("DT_MESEN_TRACE_FRAME") or "")
local trace_path = os.getenv("DT_MESEN_TRACE_OUT")

local stopped = false -- emu.stop takes effect a few events later; ignore those
local function finish(code, msg)
  stopped = true
  if status_path then
    local f = io.open(status_path, "w")
    if f then f:write(msg, "\n") f:close() end
  end
  emu.stop(code)
end

local BUTTONS = { A = "a", B = "b", SELECT = "select", START = "start",
                  UP = "up", DOWN = "down", LEFT = "left", RIGHT = "right" }

-- steps[port] = sorted list of { frame, buttons-table }
local function load_route(path)
  local steps = { [0] = {}, [1] = {} }
  local lineno = 0
  for line in io.lines(path) do
    lineno = lineno + 1
    local s = line:gsub("\r", ""):match("^%s*(.-)%s*$")
    if s ~= "" and s:sub(1, 1) ~= "#" then
      local frame, rest = s:match("^(%d+)%s*(.*)$")
      if not frame then error(path .. ":" .. lineno .. ": no frame number") end
      local port = 0
      local p = rest:match("^([12]):")
      if p then port = tonumber(p) - 1 rest = rest:sub(3) end
      local held = {}
      if rest ~= "-" and rest ~= "" then
        for name in rest:gmatch("[^+%s]+") do
          local key = BUTTONS[name:upper()]
          if not key then error(path .. ":" .. lineno .. ": unsupported in the Mesen replay: " .. name) end
          held[key] = true
        end
      end
      table.insert(steps[port], { tonumber(frame), held })
    end
  end
  return steps
end

local ok, steps = pcall(load_route, route_path or "")
if not ok or not out_path or not frames then
  finish(2, "error: " .. (ok and "DT_MESEN_OUT and DT_MESEN_FRAMES are required" or tostring(steps)))
  return
end

local out = io.open(out_path, "wb")
if not out then finish(2, "error: cannot write " .. out_path) return end
out:write("CYCFRAME", string.pack("<I4", 2))

local next_step = { [0] = 1, [1] = 1 }
local held = { [0] = {}, [1] = {} }
local recorded = 0
local mt = emu.memType

local function read_block(memtype, size)
  local t = {}
  for i = 0, size - 1 do t[i + 1] = emu.read(i, memtype) end
  local parts = {}
  for i = 1, size, 4096 do
    parts[#parts + 1] = string.char(table.unpack(t, i, math.min(i + 4095, size)))
  end
  return table.concat(parts)
end

local function on_end_frame()
  local cycles = emu.getState()["cpu.cycleCount"] or 0
  local ciram = want_ciram and read_block(mt.nesNametableRam, 0x800) or ""
  out:write(string.pack("<I4I4I4I4I4I4I4I4I4", recorded, cycles & 0xFFFFFFFF, (cycles >> 32) & 0xFFFFFFFF,
                        0x800, #ciram, 0, 0, 0, 0))
  out:write(read_block(mt.nesPaletteRam, 32), read_block(mt.nesSpriteRam, 256),
            read_block(mt.nesInternalRam, 0x800), ciram)
  recorded = recorded + 1
  if recorded >= frames then
    out:close()
    finish(0, "ok " .. recorded)
  end
end

local trace_f
local function on_exec(addr)
  if not trace_f then return end
  local s = emu.getState()
  trace_f:write(string.format("%d %04X %02X %02X %02X %02X %d %d\n", s["cpu.cycleCount"], addr, s["cpu.a"], s["cpu.x"],
                              s["cpu.y"], s["cpu.sp"], s["ppu.scanline"], s["ppu.cycle"]))
end

local function on_input_polled()
  -- this poll serves the frame that follows record (recorded - 1)
  local frame = recorded
  if trace_frame and trace_path then
    if frame == trace_frame then
      trace_f = io.open(trace_path, "w")
    elseif trace_f then
      trace_f:close()
      trace_f = nil
    end
  end
  for port = 0, 1 do
    local list = steps[port]
    while next_step[port] <= #list and list[next_step[port]][1] <= frame do
      held[port] = list[next_step[port]][2]
      next_step[port] = next_step[port] + 1
    end
    emu.setInput(held[port], port)
  end
end

local function guard(fn)
  return function()
    if stopped then return end
    local good, err = pcall(fn)
    if not good then
      pcall(function() out:close() end)
      finish(3, "error: " .. tostring(err))
    end
  end
end

emu.addEventCallback(guard(on_end_frame), emu.eventType.endFrame)
emu.addEventCallback(guard(on_input_polled), emu.eventType.inputPolled)
if trace_frame and trace_path then
  emu.addMemoryCallback(on_exec, emu.callbackType.exec, 0x0000, 0xFFFF)
end
