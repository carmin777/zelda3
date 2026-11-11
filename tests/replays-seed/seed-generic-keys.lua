-- A dungeon key for another dungeon must go to the shared counter when
-- GenericKeys is on, and to the per-dungeon slot otherwise.
local frame = 0

zelda.on("FrameEnd", function()
  frame = frame + 1
  if frame == 20 then
    zelda.log("generickeys keys=" .. zelda.state.get("link_num_keys") ..
              " slot7=" .. zelda.ram.read8(0xF37C + 7))
  end
end)
