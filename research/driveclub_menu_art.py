"""Add named BC7 tiles to the retail UI texture dictionary without replacing its entries."""
from pathlib import Path
import struct

from driveclub_probe import test_assets


def block_offset(width, x, y):
    tile = (y // 8) * ((width + 31) // 32) + x // 8
    morton = sum(((x >> i & 1) + 2 * (y >> i & 1)) << (2*i) for i in range(3))
    return 16 * (64 * tile + morton)


def tile(template, uid, width, height, source):
    header = bytearray(template[:46])
    struct.pack_into("<Q", header, 8, uid)
    struct.pack_into("<HH", header, 20, width, height)
    payload = bytearray(((width+31)//32) * ((height+31)//32) * 1024)
    struct.pack_into("<I", header, 42, len(payload))
    for y in range((height+3)//4):
        for x in range((width+3)//4):
            pos = block_offset(width, x, y)
            payload[pos:pos+16] = source(x,y)
    return header + payload


def records(data):
    labels, paths = struct.unpack_from("<II", data, 74)
    pos = 98 + labels + paths
    rows = []
    for _ in range(struct.unpack_from("<I",data,62)[0]):
        uid, size, offset, count = struct.unpack_from("<QIIH", data, pos)
        pos += 18
        deps = list(struct.unpack_from(f"<{count}Q",data,pos))
        pos += count * 8
        names, label, files, path = struct.unpack_from("<HIHI",data,pos)
        if (names,files) != (1,1):
            raise ValueError("Unexpected UI resource directory")
        pos += 12
        rows.append((uid, deps, label, path, data[offset:offset+size]))
    if data[pos:pos+4] != b"\x99"*4:
        raise ValueError("Unexpected UI resource table terminator")
    return data[98:98+labels], data[98+labels:98+labels+paths], rows


def menu_art(game, write):
    path = "newui/art/patch-laura.rpk"
    data = next(test_assets(game,path))[2]
    labels, paths, rows = records(data)
    index = {labels[label:].split(b"\0",1)[0]: blob for _,_,label,_,blob in rows}
    single = index[b"drive_menu_single.tif"]
    base_uid = 0x5dc5100000000
    width,height = struct.unpack_from("<HH", single, 20)
    if (width,height) != (540,416):
        raise ValueError("Unsupported Single Event artwork")
    art = Path(__file__).parent / "dc_render/experiments/native-menu/art"
    def wide_tile(filename, uid):
        blocks = (art / filename).read_bytes()
        if len(blocks) != 560 * 200:
            raise ValueError(f"Invalid BC7 tile: {filename}")
        return tile(single, uid, 560, 200, lambda x,y: blocks[(140*y+x)*16:(140*y+x+1)*16])
    additions = [
        ("dc_single_wide", wide_tile("single-event.bc7",base_uid+1)),
        ("dc_split_wide", wide_tile("split-screen.bc7",base_uid+2)),
    ]
    package = rows.pop()
    for name, blob in additions:
        uid = struct.unpack_from("<Q",blob,8)[0]
        rows.append((uid, [], len(labels), len(paths), blob))
        labels += (name+".tif\0").encode()
        paths += ("assets/newui/art/patch-laura/"+name+".tif\0").encode()
        package[1].append(uid)
    rows.append(package)
    head = bytearray(data[:98])
    struct.pack_into("<5I",head,62,len(rows),len(rows),len(rows)-1,len(labels),len(paths))
    offset = 98+len(labels)+len(paths)+4+sum(30+8*len(r[1]) for r in rows)
    struct.pack_into("<I",head,30,offset-38)
    directory = bytearray()
    blobs = bytearray()
    for uid,deps,label,path_offset,blob in rows:
        directory += struct.pack("<QIIH",uid,len(blob),offset+len(blobs),len(deps))
        directory += struct.pack(f"<{len(deps)}Q",*deps)
        directory += struct.pack("<HIHI",1,label,1,path_offset)
        blobs.extend(blob)
    write(path,head+labels+paths+directory+b"\x99"*4+blobs)
