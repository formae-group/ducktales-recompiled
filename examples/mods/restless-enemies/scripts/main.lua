-- Restless Enemies: an example script mod. The enemies move half again as fast.
-- Settings: BOOST (their speed in percent of the game's) and KINDS (the enemies it changes).

local KINDS = { 1, 2, 3, 5, 6, 12, 13, 16, 17, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34, 37, 48, 49, 50 }
local BOOST = 150
local MAX_SPEED = 2048

local written = {}

local function restless(o)
    if o.speed > 0 and o.speed ~= written[o.slot] then
        o:set{ speed = math.min(o.speed * BOOST // 100, MAX_SPEED) }
    end
    written[o.slot] = o.speed
end

for _, kind in ipairs(KINDS) do
    dt.ai(kind, { after = restless })
end
