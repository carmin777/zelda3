-- Force one A press with the boots ability bit clear. FakeBoots must allow the
-- dash action; without the setting Link stays idle.
local frame = 0

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 10 then
    zelda.state.set("link_grabbing_wall", 0)
    zelda.state.set("link_state_bits", 0)
    zelda.state.set("link_item_in_hand", 0)
    zelda.state.set("link_position_mode", 0)
    zelda.state.set("byte_7E0379", 0)
    zelda.state.set("button_b_frames", 0)
    zelda.state.set("button_mask_b_y", 0)
    zelda.state.set("link_incapacitated_timer", 0)
    zelda.state.set("bitfield_for_a_button", 0)
    zelda.state.set("link_direction_facing", 2)
    zelda.state.set("filtered_joypad_L", 0x80)
  end
end)

zelda.on("FrameEnd", function()
  if frame == 10 then
    zelda.log("fakeboots run=" .. zelda.state.get("link_is_running") ..
              " dash=" .. zelda.state.get("link_dash_ctr"))
  end
end)
