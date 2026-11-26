-- Money Rain: an example script mod. Every so often diamonds rain from the top of the screen for a few
-- seconds. Settings: CALM (steps between showers), SHOWER (steps a shower lasts), DROP_EVERY (steps
-- between two diamonds) and the weights in DIAMONDS.

local CALM = 1200
local SHOWER = 480
local DROP_EVERY = 10
local DIAMONDS = {
    { item = 35, weight = 85 }, -- 2,000 money
    { item = 40, weight = 14 }, -- 10,000 money
    { item = 97, weight = 1 }, -- 50,000 money
}

local total = 0
for _, diamond in ipairs(DIAMONDS) do
    total = total + diamond.weight
end

local function pick()
    local roll = dt.random(total)
    for _, diamond in ipairs(DIAMONDS) do
        if roll <= diamond.weight then
            return diamond.item
        end
        roll = roll - diamond.weight
    end
end

local step = 0
local raining = 0
local made = 0

dt.on("land-start", function()
    step = 0
    raining = 0
end)

dt.on("update", function()
    if raining == 0 then
        step = step + 1
        if step >= CALM then
            step = 0
            raining = SHOWER
            made = 0
            dt.toast("money rain!")
        end
        return
    end
    raining = raining - 1
    if raining % DROP_EVERY == 0 then
        local ok, spawned = pcall(dt.spawn_item, pick(), dt.camera().x + dt.random(16, 240), 48)
        if ok and spawned then
            made = made + 1
        end
    end
    if raining == 0 then
        dt.log("shower over,", made, "diamonds")
    end
end)

dt.on("frame", function()
    if raining > 0 then
        dt.draw.text(196, 52, "MONEY RAIN " .. (raining // 60 + 1), 0xFFD700)
    end
end)
