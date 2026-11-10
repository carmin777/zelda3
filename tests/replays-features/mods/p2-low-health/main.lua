-- Test fixture for the SharedWorld respawn replay. Player 2 is seeded from
-- player 1's live state on the first normal shared-world frame, so lowering
-- P1's health once makes both Links start with one heart. P2 then takes the
-- cannon-trooper damage while P1 stays out of the way.
zelda.on("FrameStart", function()
  if zelda.frame() == 2 and zelda.link.max_health > 0 then
    zelda.link.health = 8
  end
end)
