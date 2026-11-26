local environment = getfenv(1)
local original = assert(loadfile("splitscreen_menu_racing_original.lua.bin"))
setfenv(original, environment)
original()

local enter, update, lap = OnEnterState, WhileInState, OnNewLap
local lapLocal = setfenv(assert(loadfile("splitscreen_racing_local.lua.bin")), environment)
local report = assert(loadfile("splitscreen_menu_status.lua.bin"))()
local updatePlayers

function OnNewLap(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if race and race.cars then return lapLocal(...) end
  return lap(...)
end

function OnEnterState(...)
  enter(...)
  updatePlayers = nil
  local race = rawget(_G, "DCSplitScreenRace")
  if not race or not race.cars then return end
  race.readCountdownCameras = nil
  if not race.retail and race.rain ~= "" then Event.SendEvent(global, "StartDrivingPrecipitation") end
  report("local racing ready")
  local function finish()
    RaiseEvent("eResults")
  end
  local nativeInput = print("DCSplitMenu.native_input")
  local stockHud = assert(loadfile("splitscreen_stock_hud.lua.bin"))()(race.laps, report, nativeInput)
  if loadfile("splitscreen_menu_ai_test.lua.bin") or print("DCSplitMenu.ai_test") then
    local hud = assert(loadfile("splitscreen_hud.lua.bin"))()(race.cars, race.laps, report, stockHud, finish)
    local frame = 0
    updatePlayers = function()
      frame = frame + 1
      -- The stock rules arm the driver after Racing starts. Let P1 get away
      -- first so the test can catch a premature results transition.
      if frame == 30 or frame == 1230 then
        local seat = frame == 30 and 1 or 2
        local car = race.cars[seat]
        car.UnderPlayerControl = false
        car.RTTEnabled = false
        Event.SendEvent(car, "Driver", "DIStop")
        report("AI released P" .. seat)
      end
      hud(frame % 6 == 0)
    end
  else
    updatePlayers = assert(loadfile("splitscreen_live_controls.lua.bin"))()(
      race.cars, race.laps, report, stockHud, finish, nativeInput, race.cameraDown)
  end
end

function WhileInState(...)
  update(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if updatePlayers and race and race.cars then updatePlayers() end
end

local exit = OnExitState
function OnExitState(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if race and race.cars then
    updatePlayers = nil
    print("DCSplitMenu.drop_actors")
  end
  if exit then return exit(...) end
end
