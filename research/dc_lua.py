"""Read the Lua 5.1 prototypes embedded in the game's state-machine assets."""
from dataclasses import dataclass
from io import BytesIO
import re
import struct


@dataclass
class Prototype:
    source: str
    first_line: int
    last_line: int
    code_offset: int
    code: list
    constants: list
    children: list
    start: int
    end: int
    upvalues: int


def read_chunk(data, offset=0):
    stream = BytesIO(data)
    stream.seek(offset)
    if stream.read(12) != bytes.fromhex("1b4c75615100010408040800"):
        raise ValueError("Expected this build's Lua 5.1 bytecode")

    def uint(size=4):
        raw = stream.read(size)
        if len(raw) != size:
            raise ValueError("Truncated Lua chunk")
        return int.from_bytes(raw, "little")

    def string():
        size = uint(8)
        return stream.read(size).rstrip(b"\0").decode("utf-8", errors="replace")

    def function(parent_source=""):
        start = stream.tell()
        source = string() or parent_source
        first, last = uint(), uint()
        upvalues = uint(1)
        stream.read(3)
        count = uint()
        code_offset = stream.tell()
        code = [uint() for _ in range(count)]
        constants = []
        for _ in range(uint()):
            kind = uint(1)
            if kind == 0:
                value = None
            elif kind == 1:
                value = bool(uint(1))
            elif kind == 3:
                value = struct.unpack("<d", stream.read(8))[0]
            elif kind == 4:
                value = string()
            else:
                raise ValueError("Invalid Lua constant")
            constants.append(value)
        children = [function(source) for _ in range(uint())]
        stream.read(4 * uint())
        for _ in range(uint()):
            string()
            stream.read(8)
        for _ in range(uint()):
            string()
        return Prototype(source, first, last, code_offset, code, constants, children,
                         start, stream.tell(), upvalues)

    prototype = function()
    return prototype, stream.tell()


def embedded_chunks(data):
    for match in re.finditer(re.escape(b"\x1bLua"), data):
        prototype, end = read_chunk(data, match.start())
        yield match.start(), end, prototype
