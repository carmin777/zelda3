-- Test helper for the SharedWorld player-2 sword replay. The base snapshot
-- has a Hopping Bulb Plant (sprite 13, type 0x22) far to the right; move it
-- next to player 1 so player 2 can reach it with a normal sword swing. The
-- enemy is already loaded, so this only changes its position and health.

zelda.on("FrameStart", function()
  if zelda.frame() == 2 then
    zelda.sprite.set(13, "x", 184)
    zelda.sprite.set(13, "y", 203)
    zelda.sprite.set(13, "health", 8)
  end
end)
