-- Test helper for the SharedWorld different-items proof. Player 2 is seeded
-- on an early frame with the boomerang selected; after that the world HUD
-- selects the bow for player 1 only. The .script2 and the replay's own player
-- 1 input then press Y on the same frame, so each Link uses its own item.

zelda.on("FrameStart", function()
  local f = zelda.frame()
  if f <= 5 then
    zelda.state.set("hud_cur_item", 2)
    zelda.state.set("current_item_y", 2)
  elseif f == 6 then
    zelda.state.set("hud_cur_item", 1)
    zelda.state.set("current_item_y", 3)
  end
end)
