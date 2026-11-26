local settings = {devices=0, layout=0}
local russian
local function text(en,ru)
  return russian and ru or en
end
local deviceLabels,layouts
local settingsPage = "x_split_screen.settings"
local editing
local initialized, captureSeat, captureAction
local overrides = {{},{}}
local actionCount = 13

local function native(command, ...)
  return print("DCSplitMenu." .. command, ...)
end

local function loadSettings()
  if initialized then return end
  russian=native("russian")==true
  deviceLabels={text("Automatic","Автоматически"),text("P1 — keyboard / P2 — gamepad","P1 — клавиатура / P2 — геймпад"),
    text("P1 — gamepad / P2 — keyboard","P1 — геймпад / P2 — клавиатура"),text("Two gamepads","Два геймпада"),
    text("Shared keyboard","Одна клавиатура на двоих")}
  layouts={"WASD",text("Arrow keys","Стрелки")}
  for seat=1,2 do
    local field="name"..seat
    settings[field]=text("Player ","Игрок ")..seat
    local saved=native("read",field)
    if saved and string.sub(saved,1,1)=="@" then settings[field]=string.sub(saved,2) end
    local packed=native("read","keys"..seat) or ""
    local index=0
    for token in string.gmatch(packed,"[^,]+") do
      local code=tonumber(token)
      if index>=actionCount or not code or code< -1 or code>511 then break end
      overrides[seat][index]=code
      native("key_set",seat,index,code)
      index=index+1
    end
  end
  for field,limit in pairs({devices=4,layout=1}) do
    local value=tonumber((native("read",field)))
    if value and value==math.floor(value) and value>=0 and value<=limit then settings[field]=value end
  end
  native("bindings",settings.devices,settings.layout)
  initialized=true
end

local function refresh()
  native("label","name1",settings.name1)
  native("label","name2",settings.name2)
  native("label","devices",deviceLabels[settings.devices+1])
  native("label","layout",layouts[settings.layout+1])
  local keyboard=layouts[settings.layout+1]
  for _,row in ipairs(overrides) do
    for _,key in pairs(row) do
      if key~=0 then keyboard=text("Custom layout","Своя раскладка"); break end
    end
  end
  native("label","keyboard",keyboard)
  native("label","keyswho1",settings.name1)
  native("label","keyswho2",settings.name2)
end

local function refreshKeys(seat)
  for i=0,actionCount-1 do native("label","binding"..i,native("key_label",seat,i)) end
end

local function bindKey(seat,action,code)
  local previous=native("effective_key",seat,action)
  if previous==code then return true end
  local row={}
  for i=0,actionCount-1 do
    local key=overrides[seat][i] or 0
    if i==action then key=code
    elseif native("effective_key",seat,i)==code then key=previous>0 and previous or -1 end
    row[i+1]=key
  end
  if not native("save","keys"..seat,table.concat(row,",")) then return false end
  for i=0,actionCount-1 do
    overrides[seat][i]=row[i+1]
    native("key_set",seat,i,row[i+1])
  end
  return true
end

local function updateEditor()
  if captureSeat then
    local code=native("capture_poll")
    if not code then return end
    local seat=captureSeat
    if code>0 and not bindKey(seat,captureAction,code) then
      native("label","capture",text("Could not save settings. Esc to go back","Не удалось сохранить настройки. Esc — назад"))
      native("capture_begin")
      return
    end
    captureSeat=nil
    refresh()
    refreshKeys(seat)
    Selector.PopToPage("x_split_screen.keys"..seat)
    return
  end
  if not editing then return end
  local text = native("poll")
  if text == nil then return end
  if type(text) == "string" then
    text = string.match(text,"^%s*(.-)%s*$")
    local _,length = string.gsub(text,"[^\128-\191]","")
    if length<=24 and not string.find(text,"%c") then
      local name=text=="" and ((russian and "Игрок " or "Player ")..string.sub(editing,-1)) or text
      if native("save",editing,"@"..name) then settings[editing]=name end
    end
  end
  editing=nil
  refresh()
  Selector.PopToPage(settingsPage)
end

local function handlePage(page,status)
  loadSettings()
  if page=="x_split_screen.setup" then
    refresh()
    assert(native("freeplay"),"Split Screen menu unavailable for this build")
  elseif page==settingsPage or page=="x_split_screen.keyboard" then
    refresh()
  elseif page=="x_split_screen.garage1" or page=="x_split_screen.garage2" then
    native("garage",string.sub(page,-1))
  elseif page=="x_split_screen.start" then
    native("bindings",tostring(settings.devices),tostring(settings.layout))
    assert(native("start_mode"),"Split Screen initialization failed")
    rawset(_G,"DCSplitScreenRace",{names={settings.name1,settings.name2},laps=1,rain="",retail=true})
    native("name","name1",settings.name1)
    native("name","name2",settings.name2)
    assert(native("launch_event"),"Could not start the selected race")
  else
    local seat,action=string.match(page,"^x_split_bind%.p([12])%.(%d+)$")
    if seat then
      if native("capture_begin") then
        captureSeat,captureAction=tonumber(seat),tonumber(action)
        native("label","capture",text("Esc to cancel","Esc — отмена"))
      else Selector.PopToPage("x_split_screen.keys"..seat) end
      return
    end
    seat=string.match(page,"^x_split_screen%.keys([12])$")
    if seat then refreshKeys(tonumber(seat)); return end
    local key,choice=string.match(page,"^x_split_pick%.(%w+)%.(%d+)$")
    if key and (key=="devices" or key=="layout") then
      if native("save",key,choice) then settings[key]=tonumber(choice) end
      native("bindings",settings.devices,settings.layout)
      refresh()
      Selector.PopToPage(key=="layout" and "x_split_screen.keyboard" or settingsPage)
      return
    end
    key=string.match(page,"^x_split_screen%.edit_(name[12])$")
    if key then
      local n=string.sub(key,-1)
      if native("open",text("Player "..n.." name","Имя игрока "..n),settings[key]) then
        editing=key
      else
        Selector.PopToPage(settingsPage)
      end
    end
  end
end
local function leavePage(page)
  if captureSeat then
    native("capture_end")
    captureSeat=nil
  end
end
return handlePage,updateEditor,leavePage
