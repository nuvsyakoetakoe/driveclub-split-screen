return function(race, report, preview)
  if not race.cars then
    local cars = {}
    local actors = Actor.AllActors()
    for index = 1, Actor.CollectionSize(actors) do
      local actor = Actor.GetActorFromColIdx(actors, index)
      if actor.ActorName == "Player 1" then cars[1] = actor end
      if actor.ActorName == "Player 2" then cars[2] = actor end
    end
    if not cars[1] or not cars[2] then return end
    race.cars = cars
    local first, second = Actor.GetActorId(cars[1]), Actor.GetActorId(cars[2])
    print("DCSplitMenu.actors", tostring(first), tostring(second))
    report("local render actors " .. first .. " " .. second)
  end
  -- Keep the retail preview in the upper half and give the lower half a
  -- valid camera of its own. At Countdown both seats switch to their cars.
  for seat = preview and 2 or 1, 2 do
    Event.SendEvent(global, "SetCamera", race.cars[seat], {viewtype="vehicle"})
  end
end
