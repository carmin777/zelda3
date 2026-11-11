-- Test fixture for the Ganon's Tower opening proof. It places a Waterfall
-- sprite on Link's position, raises the crystal count and clears the GT
-- overworld event bit. Used only by tools/regress.sh with a settings-only mod.
local frame = 0
local armed = false

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame ~= 2 or armed then
    return
  end

  local x = zelda.state.get("link_x_coord")
  local y = zelda.state.get("link_y_coord")
  zelda.sprite.set(15, "type", 0x37)
  zelda.sprite.set(15, "subtype2", 0)
  zelda.sprite.set(15, "state", 9)
  zelda.sprite.set(15, "x", x)
  zelda.sprite.set(15, "y", y)
  zelda.sprite.set(15, "floor", zelda.state.get("link_is_on_lower_level"))
  zelda.sprite.set(15, "graphics", 0)
  zelda.sprite.set(15, "ignore_projectile", 1)

  zelda.state.set("link_has_crystals", 0x0F)
  zelda.state.set("link_which_pendants", 7)
  local events = zelda.state.get("save_ow_event_info", 0x43)
  zelda.state.set("save_ow_event_info", 0x43, events & 0xDF)
  armed = true
end)
