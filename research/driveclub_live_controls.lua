-- The guest uses the same driver and HUD loop from both race entry points.
return function(cars, laps, say, stockHud, finish, nativeInput, cameraDown)
  local hud = assert(loadfile("splitscreen_hud.lua.bin"))()(cars, laps, say, stockHud, finish)
  say("local controls ready " .. Actor.GetActorId(cars[1]) .. " " .. Actor.GetActorId(cars[2]))
  say("local cars " .. Actor.GetVehicleName(cars[1]) .. " " .. Actor.GetVehicleName(cars[2]))

  local lastSequence, stale, frames = -1, 0, 0
  local neutral = {0, 0, 0, 0, 0}
  local steering = {0, 0}
  local reverse = {0, 0}
  local makePedals = assert(loadfile("splitscreen_pedals.lua.bin"))()
  local pedals = {makePedals(), makePedals()}
  cameraDown = cameraDown or {}
  local hudHidden = {false, false}
  local function readControls()
    if nativeInput then
      local seq, g1, b1, s1, h1, c1, u1, g2, b2, s2, h2, c2, u2 = print("DCSplitMenu.controls")
      if seq then return seq, {g1,b1,s1,h1,0,0,0,c1,0,u1}, {g2,b2,s2,h2,0,0,0,c2,0,u2} end
      return
    end
    local read = loadfile("splitscreen_controls.lua.bin")
    if read then return read() end
  end
  return function()
    local sequence, first, second = readControls()
    if sequence == lastSequence then stale = stale + 1 else stale = 0 end
    lastSequence = sequence
    local controls = {first or neutral, second or neutral}
    for seat, car in ipairs(cars) do
      -- The retail grid can reinstall its own driver during the start sequence
      -- even with UnderPlayerControl false. Apply pedals on every tick of GO.
      if frames <= 30 or car.UnderPlayerControl == true then
        car.UnderPlayerControl = false
        car.RTTEnabled = false
        Event.SendEvent(car, "Driver", "DIStart")
      end
      local c = stale < 30 and controls[seat] or neutral
      if stale < 30 and c[10] ~= nil then hudHidden[seat] = c[10] > 0 end
      if car.IsFinished then c = {0, 1, 0, 0, 0} end
      local gas, brake, backward = pedals[seat](c[1], c[2], car.LinearSpeedMPS, car.Gear, car.IsFinished)
      steering[seat] = steering[seat] + math.max(-0.08, math.min(0.08, c[3] - steering[seat]))
      Event.SendEvent(car, "Driver", "DISetForwardThrottle", gas)
      Event.SendEvent(car, "Driver", "DISetBrake", brake)
      Event.SendEvent(car, "Driver", "DISetSteering", steering[seat])
      Event.SendEvent(car, "Driver", "DISetHandbrake", c[4])
      Event.SendEvent(car, "Driver", "DISetReverseThrottle", backward)
      reverse[seat] = backward
      local cameraPressed = (c[8] or 0) > 0
      if cameraPressed and not cameraDown[seat] then
        Event.SendEvent(global, "SetCamera", car, {viewtype="nextvehiclecam"})
        say("local camera cycle " .. seat)
      end
      cameraDown[seat] = cameraPressed
    end
    frames = frames + 1
    hud(frames % 6 == 0, hudHidden)
    if frames % 60 == 0 then
      say(string.format("local controls frame=%d seq=%s stale=%d P1=%.2f,%.2f,%.2f speed=%.2f reverse=%.2f gear=%s P2=%.2f,%.2f,%.2f speed=%.2f reverse=%.2f gear=%s",
        frames, tostring(sequence), stale, controls[1][1], controls[1][2], steering[1], cars[1].LinearSpeedMPS,
        reverse[1], tostring(cars[1].Gear), controls[2][1], controls[2][2], steering[2], cars[2].LinearSpeedMPS,
        reverse[2], tostring(cars[2].Gear)))
    end
  end
end
