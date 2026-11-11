-- Forge a Ganon hit and observe the seed goal gate. The regress test copies
-- this file and changes the crystal constant, so no seed data enters git.
local frame = 0
local crystals = 0x0F

zelda.on("FrameStart", function()
  frame = frame + 1
  if frame == 2 then
    zelda.state.set("byte_7E04C5", 2)
    zelda.state.set("sprite_type", 15, 0xd6)
    zelda.state.set("sprite_state", 15, 9)
    zelda.state.set("sprite_ai_state", 15, 1)
    zelda.state.set("sprite_health", 15, 208)
    zelda.state.set("sprite_flags4", 15, 8)
    zelda.state.set("sprite_ignore_projectile", 15, 0)
    zelda.state.set("sprite_hit_timer", 15, 0)
    zelda.state.set("sprite_z", 15, 0)
    zelda.state.set("sprite_floor", 15,
                    zelda.state.get("link_is_on_lower_level"))
    zelda.sprite.set(15, "x", zelda.state.get("link_x_coord"))
    zelda.sprite.set(15, "y", zelda.state.get("link_y_coord"))
    zelda.state.set("link_has_crystals", crystals)
  end
end)
