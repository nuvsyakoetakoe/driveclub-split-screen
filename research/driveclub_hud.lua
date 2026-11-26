-- Shared by live controls and the AI finish validation.
return function(cars, laps, say, updatePanels, finish)
  local started = GameState.CurrentGameTime()
  local finishedAt = {}
  local completed = false
  return function(report, hidden)
    if completed then return end
    local bothFinished = cars[1].IsFinished and cars[2].IsFinished
    report = report or bothFinished
    if not report and not updatePanels then return end
    local elapsed = math.max(0, GameState.CurrentGameTime() - started)
    for seat, car in ipairs(cars) do
      local finished = car.IsFinished and 1 or 0
      if finished == 1 and not finishedAt[seat] then finishedAt[seat] = elapsed end
      local lap = math.min(laps, (tonumber(car.CompletedLaps) or 0) + 1)
      local displayedTime = finishedAt[seat] or elapsed
      if updatePanels then updatePanels(seat, car, lap, displayedTime, report, hidden and hidden[seat]) end
      if report then
        say(string.format("split_hud %d %d %d %d %d %d %d %d", seat - 1,
          math.floor(math.abs(car.LinearSpeedMPS) * 3.6 + 0.5), car.Gear,
          car.RacePosition, lap, laps, finished, math.floor(displayedTime * 1000 + 0.5)))
      end
    end
    if bothFinished then
      completed = true
      say("local race complete")
      say("split_hud_hide")
      if finish then finish() else AutoTest:Transition("eResults") end
    end
  end
end
