-- Hold APUI00 busy after a pendant receipt. FastFanfare must skip the wait;
-- without it link_receiveitem_index stays at the pendant id.
local frame = 0
local last = -1

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame >= 4339 then
    zelda.state.set("apui00", 1)
  end
end)

zelda.on("FrameEnd", function()
  local item = zelda.state.get("link_receiveitem_index")
  if item ~= last then
    zelda.log("fastfanfare f" .. frame .. " item=" .. item)
    last = item
  end
end)
