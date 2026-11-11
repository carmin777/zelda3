-- Queue a normal music request and report whether the NMI consumed it.
local frame = 0

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 10 then
    zelda.state.set("music_control", 0x13)
  end
end)

zelda.on("FrameEnd", function()
  if frame == 9 or frame == 10 then
    zelda.log("nobgm f" .. frame ..
              " control=" .. zelda.state.get("music_control") ..
              " unk1=" .. zelda.state.get("music_unk1"))
  end
end)
