-- Set the current sword tier before the progressive-sword chest. The regress
-- copies this file and changes the constant for the second receipt.
local frame = 0
local sword = 1

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 2 then
    zelda.state.set("link_sword_type", sword)
  end
end)
