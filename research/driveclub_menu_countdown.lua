local original = assert(loadfile("splitscreen_menu_countdown_original.lua.bin"))
setfenv(original, getfenv(1))
original()

local start = StartEventGUIFlow
local report = assert(loadfile("splitscreen_menu_status.lua.bin"))()
local attach = assert(loadfile("splitscreen_menu_cameras.lua.bin"))()

local function readCameras(race)
  local seq, _, _, _, _, first, _, _, _, _, _, second = print("DCSplitMenu.controls")
  if not seq then return end
  for seat, car in ipairs(race.cars) do
    local pressed = (seat == 1 and first or second) > 0
    if pressed and not race.cameraDown[seat] then
      Event.SendEvent(global, "SetCamera", car, {viewtype="nextvehiclecam"})
      report("countdown camera cycle " .. seat)
    end
    race.cameraDown[seat] = pressed
  end
end

function StartEventGUIFlow(...)
  start(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if not race then return end
  if race.retail then
    race.laps = GameConfig:GetNrLaps()
    race.rain = GameConfig:GetPrecipitationType() or ""
  elseif race.rain ~= "" then
    print("DCSplitMenu.rain", race.rain)
    Event.SendEvent(global, "SetPrecipitationFromGameConfig")
    Event.SendEvent(global, "SetInitialPrecipitationState")
  end
  race.cameraDown = {}
  attach(race, report)
  if race.cars then race.readCountdownCameras = readCameras end
  report("race environment laps=" .. tostring(GameConfig:GetNrLaps()) ..
    " precipitation=" .. tostring(GameConfig:GetPrecipitationType()))
  if race.cars then
    report("race models " .. Actor.GetVehicleName(race.cars[1]) .. " / " .. Actor.GetVehicleName(race.cars[2]))
    report("race names " .. tostring(race.cars[1].DisplayName) .. " / " .. tostring(race.cars[2].DisplayName))
  end
  report(race.cars and "local countdown ready" or "no local grid at countdown")
end

local exit = OnExitState
function OnExitState(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if race then race.readCountdownCameras = nil end
  return exit(...)
end
