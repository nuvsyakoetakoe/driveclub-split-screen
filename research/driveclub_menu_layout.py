"""Build the native Split Screen setup pages from the installed catalog."""



SETUP = "x_split_screen.setup"
def panel_string(label):
    return label if label.startswith("DCSPLIT_") else "NOLAMS#" + label


def panel_text(label, x, y, name="", scale=1, font="font2_35", align="LEFT"):
    label = label.replace('"', "'")
    return f'''TEXT
ACTIVE TRUE
NAME "{name}"
STRING "{panel_string(label)}"
POSITION {x:.1f} {y:.1f}
SCALE {scale:.3f} {scale:.3f}
JUSTIFY "{align}"
RGBA "White"
FONT "{font}"
TEXT_END
'''


def panel(tiles):
    parts = ['''UI
version = 1.0
PANEL
ACTIVE TRUE
NAME "split_settings"
WIDTH 1920.0
HEIGHT 1080.0
PANEL
ACTIVE TRUE
NAME "tiles"
WIDTH 1680.0
HEIGHT 650.0
TRANSLATION 0.000 -10.000 0.000
''']
    for index, (label, link, x, y, width, height, value, *art) in enumerate(tiles, 1):
        tint = "0.38 0.38 0.38 1.0" if value else "1.0 1.0 1.0 1.0"
        background = (f'BG_SHADER "textured"\nBG_RGBA {tint}\nBG_TEXTURE "{art[0]}"'
                      if art else 'BG_SHADER "flat"\nBG_RGBA 0.09 0.09 0.09 0.94')
        parts.append(f'''PANEL
ACTIVE TRUE
NAME "tile_{index:02d}"
ATTRIBUTE("LINK" "{link}")
ATTRIBUTE("PROMPTS" "Accept=Select$")
WIDTH {width:.1f}
HEIGHT {height:.1f}
TRANSFORM_R 1.0000 0.0000 0.0000
TRANSFORM_U 0.0000 1.0000 0.0000
TRANSFORM_F 0.0000 0.0000 1.0000
TRANSLATION {x:.1f} {y:.1f} 0.000
{background}
''')
        left = -width / 2 + 26
        if value and height < 100:
            parts.append(panel_text(label,left,8,scale=0.96,font="font1_24"))
            parts.append(panel_text(" ",-left,8,f"dc_split_{value}",0.82,align="RIGHT"))
        elif value:
            parts.append(panel_text(label,left,-height/2+42,font="font1_24"))
            parts.append(panel_text(" ",left,height/2-30,f"dc_split_{value}",scale=0.96))
        else:
            parts.append(panel_text(label,left,-height/2+36 if art else 8,scale=0.92))
        parts.append("PANEL_END\n")
    parts.append("PANEL_END\nPANEL_END\n")
    return "".join(parts).replace("\n", "\r\n").encode("utf-8")


def prepare_setup(game, write):
    pages = []

    def page(key, header, filename, kind="GenericTiled", background="backgrounds"):
        pages.append(f'Page("{key}","{kind}","{filename}","{background}") '
                     f'HEADER("{panel_string(header)}") AUDIO_IN("PageIn") AUDIO_OUT("PageOut")')

    # Use the retail panel grammar, fonts, selection frame and controller prompts.
    def cards(key, header, rows):
        filename = key.replace(".", "_") + ".txt"
        write("newui/panels/"+filename, panel(rows))
        page(key, header, filename)

    cards(SETUP, "Split Screen", [
        ("DCSPLIT_RACE", "x_location.select", -555, 0, 550, 626, None, "shared_assets:single_event_race_background"),
        ("DCSPLIT_SETTINGS_TILE", "x_split_screen.settings", 0, -160, 548, 306, None, "shared_assets:eventsettings_bg"),
    ])
    rows = [
        ("DCSPLIT_NAME1", "edit_name1", "name1", "patch-laura:dc_single_wide"),
        ("DCSPLIT_NAME2", "edit_name2", "name2", "patch-laura:dc_split_wide"),
        ("DCSPLIT_DEVICES", "devices", "devices", "shared_assets:eventsettings_bg"),
        ("DCSPLIT_KEYBOARD", "keyboard", "keyboard", "shared_assets:eventsettings_bg"),
    ]
    cards("x_split_screen.settings", "DCSPLIT_SETTINGS", [
        (title, "x_split_screen."+target, (i%2-.5)*806, (i//2-.5)*228, 792, 214, field, art)
        for i,(title,target,field,art) in enumerate(rows)
    ])
    cards("x_split_screen.keyboard", "DCSPLIT_KEYBOARD", [
        ("DCSPLIT_KEYS1","x_split_screen.keys1",-403,-88,792,164,"keyswho1"),
        ("DCSPLIT_KEYS2","x_split_screen.keys2",403,-88,792,164,"keyswho2"),
        ("DCSPLIT_BASE_LAYOUT","x_split_screen.layout",0,100,1598,132,"layout"),
    ])
    actions = ["DCSPLIT_THROTTLE", "DCSPLIT_BRAKE", "DCSPLIT_LEFT", "DCSPLIT_RIGHT", "DCSPLIT_HANDBRAKE",
               "DCSPLIT_LOOK_LEFT", "DCSPLIT_LOOK_RIGHT", "DCSPLIT_LOOK_UP", "DCSPLIT_LOOK_DOWN", "DCSPLIT_CAMERA",
               "DCSPLIT_REAR", "DCSPLIT_HUD", "DCSPLIT_CAMERA_ALT"]
    for seat in (1, 2):
        rows = []
        for index, label in enumerate(actions):
            target = f"x_split_bind.p{seat}.{index}"
            page(target, label, "dc_split_capture.txt")
            rows.append((label,target,((index//7)-.5)*795,((index%7)-3)*84,
                         784,76,f"binding{index}"))
        cards(f"x_split_screen.keys{seat}", f"DCSPLIT_KEYS{seat}", rows)
    capture = panel([("DCSPLIT_PRESS_KEY","",0,0,1150,165,"capture")])
    write("newui/panels/dc_split_capture.txt", capture.replace(b'ATTRIBUTE("LINK" "")', b'ATTRIBUTE("VISITABLE" "CanVisit=No")'))
    choices = {
        "devices": ("DCSPLIT_DEVICES", ["DCSPLIT_AUTO", "DCSPLIT_KB_PAD",
                                             "DCSPLIT_PAD_KB", "DCSPLIT_PADS", "DCSPLIT_SHARED_KB"]),
        "layout": ("DCSPLIT_LAYOUT", ["WASD", "DCSPLIT_ARROWS"]),
    }
    write("newui/panels/dc_split_action.txt",panel(()))
    for field,(title,choices_for_field) in choices.items():
        tiles = []
        for i,label in enumerate(choices_for_field):
            action = f"x_split_pick.{field}.{i}"
            page(action, title, "dc_split_action.txt")
            tiles.append((label,action,0,(i-(len(choices_for_field)-1)/2)*105,1120,88,None))
        cards("x_split_screen."+field,title,tiles)
    for action in ("edit_name1","edit_name2","start"):
        page("x_split_screen."+action,"Split Screen","dc_split_action.txt")
    page("x_split_screen.garage1","DC_GARAGE_1","vehicleselect.txt",
         "FreeplayVehicleSelect","vehicle_select_background")
    page("x_split_screen.garage2","DC_GARAGE_2","vehicleselect.txt",
         "FreeplayVehicleSelect","vehicle_select_background")
    return ("\r\n"+"\r\n".join(pages)+"\r\n").encode("utf-8")
