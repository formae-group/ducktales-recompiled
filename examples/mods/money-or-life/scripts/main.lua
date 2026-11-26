-- Money or Life: an example script mod. Every beaten enemy may drop money, an HP dot or, rarely, a life.
-- Settings: DROP_CHANCE (one beaten enemy in DROP_CHANCE drops something) and the weights in DROPS.

local DROP_CHANCE = 3
local DROPS = {
    { item = 35, weight = 60 }, -- 2,000 money
    { item = 40, weight = 20 }, -- 10,000 money
    { item = 36, weight = 14 }, -- an HP dot back
    { item = 39, weight = 5 }, -- a life
    { item = 97, weight = 1 }, -- 50,000 money
}

local total = 0
for _, drop in ipairs(DROPS) do
    total = total + drop.weight
end

local function pick()
    local roll = dt.random(total)
    for _, drop in ipairs(DROPS) do
        if roll <= drop.weight then
            return drop.item
        end
        roll = roll - drop.weight
    end
end

dt.on("enemy-defeated", function(e)
    if dt.random(DROP_CHANCE) ~= 1 then
        return
    end
    local item = pick()
    if item == 36 and dt.state.hp >= dt.state.hp_max then
        item = 35
    end
    if item == 39 and dt.state.lives >= 9 then
        item = 40
    end
    local left = dt.camera().x
    local x = math.max(left, math.min(e.x, left + 255))
    local ok, made = pcall(dt.spawn_item, item, x, e.y)
    if ok and made and item == 39 then
        dt.toast("a life dropped!")
    end
end)
