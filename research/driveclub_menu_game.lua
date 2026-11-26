-- Runs on the guest's normal game-state update, including the front end.
local original = assert(loadfile("splitscreen_game_original.lua.bin"))
setfenv(original, getfenv(1))
original()

local update = WhileInState
local pageListener, frontEndListener, pending, waiting
local popListener
local report = loadfile("splitscreen_menu_status.lua.bin")
if report then report = report() end
local status = report or function() end
local attach = assert(loadfile("splitscreen_menu_cameras.lua.bin"))()
local handlePage, updateEditor, leavePage = assert(loadfile("splitscreen_menu_settings.lua.bin"))()

local function clearRace()
  if rawget(_G, "DCSplitScreenRace") then
    print("DCSplitMenu.clear_scene")
    status("split_hud_hide")
  end
  rawset(_G, "DCSplitScreenRace", nil)
end

local wizardPages={["x_location.select"]=true,["x_track.select"]=true,["weather.preview"]=true,
  ["x_freeplay.details"]=true,["x_events.settings"]=true}
local function updateHeading(page)
  print("DCSplitMenu.wizard_heading",wizardPages[page] and "1" or "0",page)
end

local function tick()
  if not pageListener then
    popListener = Event.Listen(global, "GUIPopPage", function(page)
      status("return page " .. tostring(page))
      updateHeading(page)
      leavePage(page)
      if page == "x_split_screen.garage1" then print("DCSplitMenu.garage","1")
      elseif page == "x_split_screen.garage2" then print("DCSplitMenu.garage","2")
      elseif page == "x_drive.main" or page == "x_main.frontend" or page == "x_freeplay.main" then
        print("DCSplitMenu.exit_setup")
      end
    end)
    frontEndListener = Event.Listen(global, "IsFrontEnd", function(isFrontEnd)
      if isFrontEnd then
        clearRace()
        status("front end ready")
      end
    end)
    pageListener = Event.Listen(global, "GUIPushPage", function(page)
      updateHeading(page)
      status("page " .. tostring(page))
      if page == "vehicleselect.freeplay" then print("DCSplitMenu.garage","1") end
      if type(page) == "string" and string.match(page, "^x_split_") then pending = page end
      if page == "x_freeplay.main" and print("DCSplitMenu.return_route") then pending="return_to_split" end
      waiting = page == "x_prerace.waiting"
      if page == "x_main.frontend" or page == "x_drive.main" then
        clearRace()
      elseif page == "restarting" then
        local race = rawget(_G, "DCSplitScreenRace")
        if race and race.cars then
          print("DCSplitMenu.drop_actors")
          race.cars = nil
          status("split_hud_hide")
        end
      end
    end)
    status("menu hook ready")
  end
  updateEditor()
  local race = rawget(_G, "DCSplitScreenRace")
  if race and race.cars and race.readCountdownCameras then race.readCountdownCameras(race) end
  if waiting and race and not race.cars then attach(race, status, true) end
  if not pending then return end
  local page = pending
  pending = nil
  if page=="return_to_split" then
    Selector.PopToPage("x_drive.main")
    Selector.PushPage("x_split_screen.setup")
    return
  end
  local ok, err = pcall(handlePage, page, status)
  if not ok then
    clearRace()
    status("race error " .. tostring(err))
  end
end

function WhileInState(...)
  update(...)
  local ok, err = pcall(tick)
  if not ok then status("hook error " .. tostring(err)) end
end
