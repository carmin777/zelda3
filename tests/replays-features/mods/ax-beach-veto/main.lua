-- Test helper for the AX beach feature replay. It carries the same beach
-- filter as mods-examples/islands-ganchos and, after the base replay ends,
-- puts Link at the right edge of screen $3F so a Right press exercises the
-- "return the current screen" veto in OverworldHandleTransitions.

local beach_step = 0
local beach_combo = { 0x37, 0x47, 0x40, 0x37 }

local function beach_screen(direction)
  if direction == 8 then return 0x37 end
  if direction == 4 then return 0x47 end
  if direction == 1 then return 0x40 end
  if direction == 2 then return 0x3E end
  return nil
end

local function set16(name, value)
  zelda.state.set(name, value & 0xFFFF)
end

local function beach_nudge(direction)
  if direction == 1 then
    set16("link_x_coord", 0xE10)
  elseif direction == 2 then
    set16("link_x_coord", 0xFF0)
  elseif direction == 4 then
    set16("link_y_coord", 0xE10)
  elseif direction == 8 then
    set16("link_y_coord", 0xFF0)
  end
end

zelda.filter("OverworldTransitionTarget", function(target, direction)
  local old = zelda.state.get("overworld_screen_index")
  if old ~= 0x3F then
    beach_step = 0
    return target
  end
  local screen = beach_screen(direction)
  if screen == 0x3E then
    beach_step = 0
    return target
  end
  if screen ~= nil and screen == beach_combo[beach_step + 1] then
    beach_step = beach_step + 1
  else
    beach_step = 0
  end
  if beach_step == #beach_combo then
    beach_step = 0
    zelda.state.set("sound_effect_2", 0x1B)
    return 0x37
  end
  beach_nudge(direction)
  return 0x3F
end)

zelda.on("FrameStart", function()
  if zelda.frame() == 400 then
    zelda.state.set("savegame_is_darkworld", 0)
    zelda.state.set("overworld_screen_index", 0x3F)
    zelda.state.set("overworld_area_index", 0x3F)
    zelda.state.set("overworld_screen_index_prev", 0x3F)
    zelda.state.set("link_x_coord", 0xFF0)
    zelda.state.set("link_y_coord", 0xFF0)
    zelda.state.set("link_x_vel", 0)
    zelda.state.set("link_y_vel", 0)
    zelda.state.set("link_direction", 0)
    zelda.state.set("link_direction_last", 0)
    zelda.state.set("link_state_bits", 0)
    zelda.state.set("link_position_mode", 0)
    zelda.state.set("player_is_indoors", 0)
    zelda.state.set("cheatWalkThroughWalls", 1)
  end
end)
