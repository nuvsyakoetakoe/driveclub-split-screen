-- One brake/reverse pedal per player, with direction changes made at a standstill.
return function()
  local reversing = false
  local stoppedFrames = 0
  return function(gas, pedal, speed, gear, finished)
    if finished then return 0, 1, 0 end
    speed = math.abs(speed)
    if reversing then
      if gas > 0 then
        if speed > 0.5 then return 0, gas, 0 end
        reversing = false
        stoppedFrames = 0
        return gas, 0, 0
      end
      return 0, 0, pedal
    end
    if gas > 0 then
      stoppedFrames = 0
      return gas, pedal, 0
    end
    if pedal > 0 and speed < 0.5 then
      stoppedFrames = stoppedFrames + 1
      if stoppedFrames >= 12 then
        reversing = true
        return 0, 0, pedal
      end
    else
      stoppedFrames = 0
    end
    return 0, pedal, 0
  end
end
