local original = assert(loadfile("splitscreen_menu_prerace_original.lua.bin"))
setfenv(original, getfenv(1))
original()

local spawnGrid = SpawnGrid_Really
local report = assert(loadfile("splitscreen_menu_status.lua.bin"))()
function SpawnGrid_Really(...)
  local race = rawget(_G, "DCSplitScreenRace")
  if not race then return spawnGrid(...) end
  report("local grid entering")
  print("DCSplitMenu.drop_actors")
  race.cameraDown = {}
  race.cars = nil
  local spawn = Actor.SpawnActor
  Actor.SpawnActor = function(params)
    local seat = tonumber(params.controller)
    if params.vehiclename and params.type == "player" and (seat == 0 or seat == 1) then
      params.name = "Player " .. (seat + 1)
      params.displayname = race.names[seat + 1]
    end
    return spawn(params)
  end
  local ok, err = pcall(spawnGrid, ...)
  Actor.SpawnActor = spawn
  if not ok then error(err) end
  report("local grid ready")
end
