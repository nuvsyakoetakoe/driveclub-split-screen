"""Defer the retail race's automatic results transition until both local cars finish."""
import hashlib
import struct
from driveclub_probe import test_assets


def read_race_state(game):
    _, _, original = next(test_assets(game, "stm/mission/race/race.smd"))
    if hashlib.sha256(original).hexdigest() != "4ac12cd635c5e6b715c0a7884114de35578adcdcc189e6dcc60361cda1943e85":
        raise ValueError("Unsupported race state machine")
    return bytearray(original)


def defer_results(data):
    # OnLapCompleted, Lua source lines 203..270. Keep Finished, performance,
    # finishTime and leaderboard bookkeeping. The local coordinator owns the
    # subsequent results transition and the end-of-race pause restriction.
    for offset, expected in ((0xdb15, 0x010041dc), (0xdb69, 0x0100419c)):
        instruction = struct.unpack_from("<I", data, offset)[0]
        if instruction != expected:
            raise ValueError(f"Unexpected race instruction at {offset:#x}")
        register = (instruction >> 6) & 255
        move_to_self = (register << 6) | (register << 23)
        struct.pack_into("<I", data, offset, move_to_self)


def prepare_session(game, mods, player_names=None):
    data = read_race_state(game)
    defer_results(data)
    if player_names is not None:
        from driveclub_participants import install
        install(data, mods, player_names)
    target = mods / "stm/mission/race/race.smd"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
