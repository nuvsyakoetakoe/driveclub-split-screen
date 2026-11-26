"""Freeze the tested installation as patches against the player's retail assets."""
import contextlib
import hashlib
import io
import json
import re
import struct
import sys
from functools import lru_cache
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "research"))
from driveclub_probe import test_assets
from driveclub_stock_hud import PANELS
from driveclub_menu_layout import prepare_setup
from driveclub_menu_strings import prepare_languages

game = ROOT / "DRIVECLUB MOD/DRIVECLUB"
work = ROOT / "output/driveclub-release-010"
sha = lambda b: hashlib.sha256(b).hexdigest()


def recipe(original, target):
    """Use exact matches from the local asset, storing only new bytes."""
    if original and target.startswith(original):
        tail = target[len(original):]
        return b'C' + struct.pack('<II', 0, len(original)) + b'L' + struct.pack('<I', len(tail)) + tail
    index = {}
    block = 32
    for pos in range(0, len(original) - block + 1, block):
        index.setdefault(original[pos:pos + block], pos)
    out = io.BytesIO()
    cursor = literal = 0

    def flush():
        if literal < cursor:
            out.write(b'L' + struct.pack('<I', cursor - literal) + target[literal:cursor])

    while cursor + block <= len(target):
        match = index.get(target[cursor:cursor + block])
        if match is None:
            cursor += 1
            continue
        low, high = block, min(len(original) - match, len(target) - cursor)
        while low < high:
            n = (low + high + 1) // 2
            if original[match:match + n] == target[cursor:cursor + n]:
                low = n
            else:
                high = n - 1
        flush()
        out.write(b'C' + struct.pack('<II', match, low))
        cursor += low
        literal = cursor
    cursor = len(target)
    flush()
    return out.getvalue()


@lru_cache(None)
def stock(name, assets=game):
    with contextlib.redirect_stdout(io.StringIO()):
        _, loc, blob = next(test_assets(assets, name))
    return blob, dict(archive='game%03d.dat' % (loc & 65535), offset=loc >> 16,
                      size=len(blob), hash=sha(blob))


def source_for(path):
    if path.startswith('mods/gui/lams/'):
        return path[5:]
    if path == 'mods/split-screen/default-mirrors.rpk':
        return 'global.rpk'
    if path.endswith('game_original.lua.bin'):
        return 'stm/gamestate/gamestate.smd'
    if path.endswith('_original.lua.bin') or path.endswith('racing_local.lua.bin'):
        return 'stm/mission/race/race.smd'
    for _, role, asset, *_ in PANELS:
        if re.search(r'/osd_split_' + role.lower() + r'[12]\.txt$', path):
            return 'newui/panels/osd_' + asset + '.txt'
    if path in ('mods/newui/panels/x_drive.txt', 'mods/newui/panels/osd_layout_race.txt',
                'mods/newui/pages/new_front.ctl', 'mods/newui/art/patch-laura.rpk') or '/stm/' in path:
        return path[5:]
    return None


def main():
    work.mkdir(parents=True, exist_ok=True)
    expected = json.loads((game / 'mods/split-screen/installed.json').read_text())
    assert expected['mods/split-screen/driveclub_split.dll'] == '328af657287d2c3ab2900055c1c1d61903062d7cc707250e7d78714bb9428f8b'
    localized = {}
    def emit(name, blob):
        localized['mods/' + name] = blob
    pages = prepare_setup(game, emit)
    emit('newui/pages/new_front.ctl', stock('newui/pages/new_front.ctl')[0] + pages)
    prepare_languages(game, emit)
    emit('splitscreen_menu_settings.lua.bin', (ROOT / 'research/driveclub_menu_settings.lua').read_bytes())
    emit('split-screen/driveclub_split.dll', (work / 'bilingual/driveclub_split.dll').read_bytes())
    def read(path):
        if path in expected and sha((game / path).read_bytes()) != expected[path]:
            raise ValueError('Installed file differs from the tested MVP: ' + path)
        return localized[path] if path in localized else (game / path).read_bytes()
    write_payload(work / 'payload.zip', expected.keys() | localized.keys(), read,
                  expected, game, game / 'mods/split-screen/DriveClub.stock.exe')


def write_payload(output, paths, read, previous, assets, original_launcher):
    manifest = dict(version='0.1.0', files={}, assets={}, upgrade_from=previous)
    with ZipFile(output, 'w', ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(paths):
            target = read(path)
            asset = source_for(path)
            base = b''
            if asset:
                base, meta = stock(asset, assets)
                manifest['assets'][asset] = meta
            elif path == 'DriveClub.exe':
                asset = '@launcher'
                base = original_launcher.read_bytes()
            manifest['files'][path] = dict(hash=sha(target), asset=asset)
            z.writestr(path + '.patch', recipe(base, target) if base else b'L' + struct.pack('<I', len(target)) + target)
        z.writestr('release.json', json.dumps(manifest, indent=2))
    print(len(manifest['files']), 'files;', len(manifest['assets']), 'retail assets;', output.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
