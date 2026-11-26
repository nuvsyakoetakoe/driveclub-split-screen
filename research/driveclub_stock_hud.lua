-- Send each actor's telemetry to the corresponding native race panels.
return function(laps, say, nativeMenu)
  local pointToPoint = Roads.IsPointToPoint()
  local previous = {{}, {}}
  local entrants = 0
  local actors = Actor.AllActors()
  for i = 1, Actor.CollectionSize(actors) do
    local car = Actor.GetActorFromColIdx(actors, i)
    local vehicle = Actor.GetVehicleName(car)
    if vehicle and vehicle ~= "" then entrants = entrants + 1 end
  end
  say("split stock hud: entrants=" .. entrants .. (pointToPoint and " point-to-point" or " circuit"))
  return function(seat, car, lap, elapsed, refresh, hidden)
    local speedPanel = "SplitSpeed" .. seat
    local settings = previous[seat]
    hidden = not not hidden
    if settings.hidden ~= hidden then
      local visibility = hidden and OSDPage.Hide or OSDPage.Show
      for _, role in ipairs({"Speed", "Pos", pointToPoint and "Progress" or "Lap", "Timer"}) do
        visibility("Split" .. role .. seat)
      end
      say("split hud P" .. seat .. (hidden and " hidden" or " shown"))
      OSDPage.Hide((pointToPoint and "SplitLap" or "SplitProgress") .. seat)
      OSDPage.Send((pointToPoint and "SplitProgress" or "SplitLap") .. seat, "SetVisible", not hidden)
      OSDPage.Hide("SplitWrong" .. seat)
      settings.wrong = nil
      settings.wrongSince = nil
      settings.hidden = hidden
    end
    if nativeMenu and seat == 1 and (refresh or not settings.configured) then
      for _, panel in ipairs({"Speedometer", "RacePos", "LapCount", "PercentageIcon", "WrongWay",
                              "Timer", "OpponentTracker", "Overdrives", "Difference", "Notification"}) do
        OSDPage.Hide(panel)
      end
    end
    if hidden then return end
    if refresh or not settings.configured then
      local metric = Settings.Display().units == "Metric"
      if settings.metric ~= metric then
        OSDPage.Send(speedPanel, "SetUnits", metric and "KPH" or "MPH")
        settings.metric = metric
      end
      OSDPage.Send(speedPanel, "SetVisible", not not car.IsFirstPersonVehicleCamera)
    end
    if not settings.configured then
      OSDPage.Send(speedPanel, "ConfigureKERSAndDRS", not not car.HasDRS,
        not not (car.HasKERS and not car.KERSAlwaysOn))
      settings.configured = true
    end
    if refresh or settings.wrong == nil then
      local wrong = car.GoingWrongWay
      -- At rest the direction flag oscillates with suspension movement.
      if (wrong == true or wrong == 1) and math.abs(car.LinearSpeedMPS) > 0.5 and not car.IsFinished then
        settings.wrongSince = settings.wrongSince or elapsed
      else
        settings.wrongSince = nil
      end
      wrong = settings.wrongSince ~= nil and elapsed - settings.wrongSince >= 0.5
      if settings.wrong ~= wrong then
        if wrong then OSDPage.Show("SplitWrong" .. seat) else OSDPage.Hide("SplitWrong" .. seat) end
        OSDPage.Send("SplitWrong" .. seat, "SetVisible", wrong)
        settings.wrong = wrong
        say("split direction P" .. seat .. " warning=" .. tostring(wrong))
      end
    end
    local revs = tonumber(car.Revs) or 0
    local maxRevs = tonumber(car.MaxRevs) or 0
    local shift = tonumber(car.ShiftUpRevs) or math.huge
    local speed = settings.metric and car.SpeedKPH or car.SpeedMPH
    OSDPage.Send(speedPanel, "Update", car.Gear, speed, car.KERSCharge, car.KERSActive,
      maxRevs > 0 and 100 * revs / maxRevs or 0, revs > shift, car.DRSActive,
      car.IsElectric, car.BrakeAssistState)
    if refresh then
      OSDPage.Send("SplitPos" .. seat, "SetPositionInfo", car.RacePosition, entrants)
      if pointToPoint then
        OSDPage.Send("SplitProgress" .. seat, "SetPercentageComplete", car.TrackPercentageCompleted)
      else
        OSDPage.Send("SplitLap" .. seat, "SetLapCount",
          math.max(1, math.min(laps, tonumber(car.CurrentLapForGUI) or lap)), laps)
      end
      OSDPage.Send("SplitTimer" .. seat, "SetTime",
        OSDController:Common_ClampedTimeToString(math.floor(elapsed * 1000 + 0.5)))
    end
  end
end
