-- Test helper for the Pikit feature replay. The replay stops in the room
-- where a Pikit is already loaded; this moves Link next to it so the grab
-- branch runs deterministically. The decision itself is the hook under test.

zelda.on("FrameStart", function()
  if zelda.frame() == 350 then
    zelda.state.set("link_x_coord", 198)
    zelda.state.set("link_y_coord", 610)
  end
end)

zelda.decide("PikitStealShield", function(steal, slot)
  return false
end)
