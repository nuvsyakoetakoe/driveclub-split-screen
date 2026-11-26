"""Prepare the DRIVECLUB menu entry in a private mod directory."""
import hashlib
from pathlib import Path
import re
import subprocess

from dc_lua import embedded_chunks
from driveclub_menu_art import menu_art
from driveclub_menu_layout import prepare_setup
from driveclub_menu_strings import prepare_languages
from driveclub_probe import test_assets


def read_asset(game, path, expected):
    _, _, data = next(test_assets(game, path))
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"Unsupported DRIVECLUB asset {path}")
    return data


def write_asset(mods, path, data):
    target = mods / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def install_state_hook(data, mods, chunk, script, original):
    start, end, _ = next((start, end, proto) for start, end, proto in embedded_chunks(data)
                         if proto.source == chunk)
    write_asset(mods, original, data[start:end])
    source = Path(__file__).with_name(script)
    compiler = source.parent.parent / ".tools/luac51.exe"
    output = mods / (source.stem + ".lua.bin")
    subprocess.run([str(compiler), "-s", "-o", str(output), str(source)], check=True)
    hook = output.read_bytes()
    if len(hook) > end - start:
        raise ValueError(f"The {chunk} hook does not fit the original chunk")
    data[start:end] = hook.ljust(end - start, b"\0")


def prepare_menu(game, mods):
    path = "newui/panels/x_drive.txt"
    panel = read_asset(game, path, "d57f198017f579b14cbad2b4697d2eb1703360d0ea3c470fd451410dd7ea2329")
    single = re.search(rb'\t\tPANEL\r\n\t\t\tACTIVE TRUE\r\n\t\t\tNAME "tile_02".*?\t\tPANEL_END\r\n', panel, re.S)[0]
    random = re.search(rb'\t\tPANEL\r\n\t\t\tACTIVE TRUE\r\n\t\t\tNAME "tile_02b".*?\t\tPANEL_END\r\n', panel, re.S)[0]
    shorter = single.replace(b"HEIGHT 416.0", b"HEIGHT 200.0", 1).replace(b" -105.000 ", b" -207.000 ", 1)
    shorter = shorter.replace(b" -170.000", b" -64.000").replace(b" -172.500", b" -63.500")
    shorter = re.sub(rb'"patch-(?:laura|isaias):drive_menu_single"', b'"patch-laura:dc_single_wide"', shorter)
    middle = random.replace(b" 214.000 ", b" 1.000 ", 1)
    split = random.replace(b'NAME "tile_02b"', b'NAME "tile_split_screen"\r\n\t\t\tATTRIBUTE("LINK" "x_split_screen.setup")', 1)
    split = split.replace(b'"RANDOMEVENT_UPPERCASE"', b'"NOLAMS#SPLIT SCREEN"')
    split = split.replace(b'"Accept=Select$ Tip=RandomEventDescription$"', b'"Accept=Select$"')
    split = split.replace(b" 214.000 ", b" 209.000 ", 1)
    split = re.sub(rb'"patch-(?:laura:random_event|dcbikes:random_bikes_event)"', b'"patch-laura:dc_split_wide"', split)
    menu_art(game, lambda path, data: write_asset(mods, path, data))
    write_asset(mods, path, panel.replace(single, shorter, 1).replace(random, middle + split, 1))

    path = "newui/pages/new_front.ctl"
    pages = read_asset(game, path, "778e38dc46aa738b93fa477fe525dbc8c10543c1ef5ff558406f6bf62ca8cc5c")
    pages += prepare_setup(game, lambda path, data: write_asset(mods, path, data))
    prepare_languages(game, lambda path, data: write_asset(mods, path, data))
    write_asset(mods, path, pages)
    source = Path(__file__).with_name("driveclub_menu_settings.lua")
    write_asset(mods, "splitscreen_menu_settings.lua.bin", source.read_bytes())

    path = "stm/gamestate/gamestate.smd"
    data = bytearray(read_asset(game, path, "ab82db2545c5eefad175469eb735d44a6a3acd2fedb37e69543c3094e2e5fc99"))
    install_state_hook(data, mods, "Game.lua", "driveclub_menu_game.lua", "splitscreen_game_original.lua.bin")
    write_asset(mods, path, data)


def prepare_menu_race(game, mods, ai_test=False, stock_race=False):
    from driveclub_session import defer_results, read_race_state

    if not stock_race:
        data = read_race_state(game)
        local_race = bytearray(data)
        defer_results(local_race)
        start, _, racing = next((start, end, proto) for start, end, proto in embedded_chunks(local_race)
                                if proto.source == "Racing.lua")
        lap = next(proto for proto in racing.children if (proto.first_line, proto.last_line) == (203, 270))
        if lap.upvalues:
            raise ValueError("The local lap callback depends on another Lua function")
        write_asset(mods, "splitscreen_racing_local.lua.bin",
                    local_race[start:start + 12] + local_race[lap.start:lap.end])
        for chunk, script, original in (
            ("prerace.lua", "driveclub_menu_prerace.lua", "splitscreen_menu_prerace_original.lua.bin"),
            ("Countdown.lua", "driveclub_menu_countdown.lua", "splitscreen_menu_countdown_original.lua.bin"),
            ("Racing.lua", "driveclub_menu_racing.lua", "splitscreen_menu_racing_original.lua.bin"),
        ):
            install_state_hook(data, mods, chunk, script, original)
        write_asset(mods, "stm/mission/race/race.smd", data)

    for real, target in (
        ("menu_status", "menu_status"), ("menu_cameras", "menu_cameras"), ("live_controls", "live_controls"),
        ("hud", "hud"), ("pedals", "pedals"), ("stock_hud", "stock_hud"),
    ):
        source = Path(__file__).with_name(f"driveclub_{real}.lua")
        write_asset(mods, f"splitscreen_{target}.lua.bin", source.read_bytes())
    if ai_test:
        write_asset(mods, "splitscreen_menu_ai_test.lua.bin", b"return true\n")
