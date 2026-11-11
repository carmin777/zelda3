-- Read the native HUD tile buffer after the menu forces a Hud_Rebuild.
-- ItemCounterHUD draws 077/216 at x=8..14, row 4; MapHUDMode draws 2/4 at
-- x=17..19, row 2.
local frame = 0

local function cell(x, y)
  return zelda.ram.read16(0xC700 + 2 * (x + y * 32)) & 0xff
end

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 2 then
    zelda.state.set("total_item_counter", 77)
    zelda.state.set("cur_palace_index_x2", 0)
    zelda.state.set("link_dungeon_map", 0x8000)
    zelda.state.set("link_num_keys", 2)
  end
end)

zelda.on("FrameEnd", function()
  if frame == 60 then
    local counter = ""
    for i = 0, 6 do
      counter = counter .. string.format("%02x ", cell(8 + i, 4))
    end
    local map = ""
    for i = 0, 2 do
      map = map .. string.format("%02x ", cell(17 + i, 2))
    end
    zelda.log("hud counter " .. counter .. "map " .. map)
  end
end)
