"""Install a local-player spawn hook in the race's own Lua environment."""
from pathlib import Path
import subprocess
from dc_lua import embedded_chunks


def install(data, mods, player_names):
    start, end, _ = next((start, end, proto) for start, end, proto in embedded_chunks(data)
                         if "SpawnGrid_Really" in proto.constants)
    original = mods / "splitscreen_prerace_original.lua.bin"
    original.write_bytes(data[start:end])
    # Decimal escapes preserve UTF-8 bytes in Lua 5.1, including quotes and backslashes.
    names = [
        '"' + "".join(f"\\{byte:03d}" for byte in name.encode("utf-8")) + '"'
        for name in player_names
    ]
    names_file = mods / "splitscreen_player_names.lua.bin"
    names_file.write_text("return {" + ", ".join(names) + "}\n", encoding="ascii")
    source = Path(__file__).with_name("driveclub_prerace.lua")
    compiler = source.parent.parent / ".tools/luac51.exe"
    compiled = mods / "splitscreen_prerace_hook.lua.bin"
    subprocess.run([str(compiler), "-o", str(compiled), str(source)], check=True)
    hook = compiled.read_bytes()
    if len(hook) > end - start:
        raise ValueError("Local-player hook does not fit the original Lua chunk")
    data[start:end] = hook.ljust(end - start, b"\0")
