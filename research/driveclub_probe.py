"""Read-only inspection of DRIVECLUB's DATX QA assets; no external dependencies."""
from functools import lru_cache
from pathlib import Path
import re
import struct
import sys
from io import BytesIO
from fnmatch import fnmatchcase


def lua_calls(data):
    """Summarize straight-line calls in this build's Lua 5.1 test function."""
    f = BytesIO(data)
    assert f.read(12) == bytes.fromhex("1b4c75615100010408040800")

    def uint(n=4):
        return int.from_bytes(f.read(n), "little")

    def string():
        return f.read(uint(8)).rstrip(b"\0").decode("ascii")

    def function():
        string()
        start, end = uint(), uint()
        f.read(4)
        code = [uint() for _ in range(uint())]
        const = []
        for _ in range(uint()):
            kind = uint(1)
            assert kind in (0, 1, 3, 4)
            const.append(string() if kind == 4 else struct.unpack("<d", f.read(8))[0]
                         if kind == 3 else bool(uint(1)) if kind == 1 else None)
        if "WorldLoad" in const:
            print("Lua lines", start, end)
            reg = {}
            def rk(v):
                return const[v-256] if v >= 256 else reg.get(v, "?")
            for ins in code:
                op, a, b, c = ins & 63, (ins >> 6) & 255, ins >> 23, (ins >> 14) & 511
                if op == 0: reg[a] = reg.get(b)
                elif op in (1, 5): reg[a] = const[ins >> 14]
                elif op == 2: reg[a] = bool(b)
                elif op == 6: reg[a] = f"{reg.get(b)}.{rk(c)}"
                elif op == 10: reg[a] = {}
                elif op == 9 and isinstance(reg.get(a), dict): reg[a][rk(b)] = rk(c)
                elif op == 9: print(" ", reg.get(a), rk(b), "=", rk(c))
                elif op == 11: reg[a+1], reg[a] = reg.get(b), f"{reg.get(b)}.{rk(c)}"
                elif op == 36: reg[a] = "<function>"
                elif op == 28:
                    args = [reg.get(r) for r in range(a+1, a+b)]
                    if reg.get(a) in ("Automation.Go", "Actor.GetActorByName", "Event.SendEvent"):
                        print(" ", reg[a], args)
                    if c == 2: reg[a] = f"{reg.get(a)}({args})"
        for _ in range(uint()):
            function()
        f.read(4 * uint())
        for _ in range(uint()):
            string()
            f.read(8)
        for _ in range(uint()):
            string()
    function()


def u32(buf, pos):
    return struct.unpack_from("<I", buf, pos)[0]


def inflate(raw, size):
    if not raw:
        return bytes(size)
    if len(raw) == size:
        return raw
    assert u32(raw, 0) == size
    i, out = 4, bytearray()

    def length(value):
        nonlocal i
        if value == 15:
            while True:
                v = raw[i]
                i += 1
                value += v
                if v < 255:
                    break
        return value

    while i < len(raw):
        token = raw[i]
        i += 1
        n = length(token >> 4)
        out.extend(raw[i:i+n])
        i += n
        if i == len(raw):
            break
        dist = int.from_bytes(raw[i:i+2], "little")
        i += 2
        n = length(token & 15) + 4
        assert 0 < dist <= len(out) and len(out) + n <= size
        out.extend((out[-dist:] * ((n + dist - 1) // dist))[:n])
    assert len(out) == size
    return out


def test_assets(folder, pattern="*test*.lua.bin"):
    idx = (folder / "game.ndx").read_bytes()
    assert idx[:4] == b"DATX"
    n = idx[60]
    pos = 61 + 2*n

    @lru_cache(None)
    def expand(c):
        return bytes([c]) if c < 128 else expand(idx[61+c-128]) + expand(idx[61+n+c-128])

    end = pos + 4 + u32(idx, pos)
    names = b"".join(map(expand, idx[pos+4:end])).split(b"\0")
    assert u32(idx, end) == 0x12345678
    count = u32(idx, 56)
    print("DATX records:", count)
    for j, name in enumerate(names[:count]):
        if b"split" in name.lower():
            print("Path:", name.decode())
        if not fnmatchcase(name.decode("ascii"), pattern):
            continue
        loc, size, hash_id = struct.unpack_from("<QII", idx, end+4+32*j)
        a, value = u32(idx, 24), 0
        for c in name.upper():
            a = ((a * u32(idx, 28)) & 0xffffffff) % 0x7ffffffe
            value = ((value*a+c) & 0xffffffff) % 0x7fffffff
        assert value == hash_id
        with (folder / ("game%03d.dat" % (loc & 65535))).open("rb") as f:
            header = f.read(36)
            assert header[:4] == b"DATF" and header[32:] == b"CHNK"
            chunks, block = struct.unpack_from("<II", header, 24)
            sizes = struct.unpack("<%dI" % chunks, f.read(4*chunks))
            assert f.read(4) == b"DATA"
            offset = loc >> 16
            first = offset // block
            f.seek(sum(sizes[:first]), 1)
            data = b"".join(inflate(f.read(sizes[c]), block)
                            for c in range(first, (offset+size-1)//block+1))
            data = data[offset%block:offset%block+size]
        yield name, loc, data


def main(folder):
    for name, loc, data in test_assets(folder):
        words = re.findall(rb"[ -~]{4,}", data)
        for i, word in enumerate(words):
            if re.search(rb"split.?screen", word, re.I):
                print(name.decode(), "volume", loc & 65535, "format", data[:12].hex())
                print("Nearby strings:", [w.decode() for w in words[max(0,i-12):i+16]])
                if name.endswith(b"/watchdogtest.lua.bin"):
                    lua_calls(data)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
