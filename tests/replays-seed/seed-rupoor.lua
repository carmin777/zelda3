-- Put 100 rupees in the wallet before the Rupoor chest. The setting must
-- deduct 10 and leave the actual counter at 90.
local frame = 0

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 2 then
    zelda.state.set("link_rupees_actual", 100)
    zelda.state.set("link_rupees_goal", 100)
  elseif frame == 6 then
    zelda.state.set("link_rupees_goal", 90)
  end
end)

zelda.on("FrameEnd", function()
  if frame == 20 then
    zelda.log("rupoor rupees=" .. zelda.state.get("link_rupees_actual"))
  end
end)
