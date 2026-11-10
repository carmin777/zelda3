-- Overrides the green-armored knight (sprite type 0x4B, "4B-Knight" in the
-- sprite name table). It keeps the game routine for drawing, animation,
-- collision and contact damage, but replaces the movement decision:
--
--   * Link facing the knight: it backs away.
--   * Link with his back turned: it closes in and can hit him.
--
-- The original routine is still called every frame through original(slot).
-- We only force a non-zero delay_main and our own velocity so the game's
-- movement state machine does not overwrite the direction we chose.

local function facing_vector(facing)
  if facing == 0 then return 0, -1 end   -- up
  if facing == 2 then return 0, 1 end    -- down
  if facing == 4 then return -1, 0 end   -- left
  if facing == 6 then return 1, 0 end    -- right
  return 0, 0
end

local function head_dir_to(tx, ty)
  if math.abs(tx) >= math.abs(ty) then
    return tx >= 0 and 0 or 1            -- right / left
  end
  return ty >= 0 and 2 or 3              -- down / up
end

local function tick(slot, original)
  local link_x = zelda.state.get("link_x_coord")
  local link_y = zelda.state.get("link_y_coord")
  local x = zelda.sprite.get(slot, "x")
  local y = zelda.sprite.get(slot, "y")

  local fx, fy = facing_vector(zelda.state.get("link_direction_facing"))
  local to_link_x = link_x - x
  local to_link_y = link_y - y
  local behind = fx * (x - link_x) + fy * (y - link_y) < 0

  local vx, vy
  if behind then
    -- Link turned his back: approach and attack.
    if math.abs(to_link_x) >= math.abs(to_link_y) then
      vx = to_link_x >= 0 and 12 or -12
      vy = 0
    else
      vx = 0
      vy = to_link_y >= 0 and 12 or -12
    end
  else
    -- Link is facing us: run away along the largest distance.
    local away_x = x - link_x
    local away_y = y - link_y
    if math.abs(away_x) >= math.abs(away_y) then
      vx = away_x >= 0 and 14 or -14
      vy = 0
    else
      vx = 0
      vy = away_y >= 0 and 14 or -14
    end
  end

  -- Keep the original routine in its "moving" branch so it draws, checks
  -- collisions and contact damage, but does not replace our velocity.
  zelda.sprite.set(slot, "ai_state", 1)
  zelda.sprite.set(slot, "delay_main", 16)
  zelda.sprite.set(slot, "x_vel", vx)
  zelda.sprite.set(slot, "y_vel", vy)

  original(slot)

  -- A wall or the original movement code may have zeroed the velocity after
  -- drawing. Put it back for the next frame and keep the head facing Link.
  zelda.sprite.set(slot, "x_vel", vx)
  zelda.sprite.set(slot, "y_vel", vy)
  local hd = head_dir_to(to_link_x, to_link_y)
  zelda.sprite.set(slot, "head_dir", hd)
  zelda.sprite.set(slot, "D", hd)
end

zelda.override("Sprite:4B-Knight", { tick = tick })
