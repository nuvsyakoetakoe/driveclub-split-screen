"""DRIVECLUB Split Screen 0.1.0 offline installer for Windows."""
# Copyright (C) 2026 nuvsyakoetakoe. SPDX-License-Identifier: GPL-2.0-only
import argparse
import contextlib
import ctypes
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from functools import lru_cache
from pathlib import Path
from zipfile import ZipFile

from driveclub_probe import inflate
from setup_text import translate

STOCK = '3411aa1b4943c40273b2d5cbb51e20c1656eb4dd44a1b4177eccda2048df00ce'
RUNTIME = 'fb2f88b6b016a91a8390a98250a86ace74aa2dd84d5e30031eb8be0fe076e400'
BOOT = 'cb2ae1d90e240bde81ff2bea048c4eb16a9287185afd05fa36fb32e4e7c88156'
BACKUP = 'mods/split-screen/DriveClub.stock.exe'
STAMP = 'mods/split-screen/installed.json'
VERSION = 'mods/split-screen/version.txt'
GRAPHICS = 'mods/global.rpk'
LAUNCHER = 'DriveClub.exe'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    if not path.exists():
        return None
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def unpack_asset(root, meta):
    with (root / meta['archive']).open('rb') as f:
        header = f.read(36)
        require(header[:4] == b'DATF' and header[32:] == b'CHNK', 'Unsupported game archive')
        count, block = struct.unpack_from('<II', header, 24)
        require(0 < block <= 16 << 20 and 0 < count < 4000000, 'Invalid archive table')
        sizes = struct.unpack('<%dI' % count, f.read(4 * count))
        require(f.read(4) == b'DATA', 'Missing archive data')
        offset, length = meta['offset'], meta['size']
        first, last = offset // block, (offset + length - 1) // block
        require(last < count, 'Different game archive revision')
        f.seek(sum(sizes[:first]), 1)
        blocks = []
        for size in sizes[first:last + 1]:
            require(size <= block + 32, 'Unsupported compressed block')
            blocks.append(inflate(f.read(size), block))
        start = offset % block
        return b''.join(blocks)[start:start + length]


def apply_recipe(base, recipe):
    f, out = io.BytesIO(recipe), io.BytesIO()
    while tag := f.read(1):
        if tag == b'C':
            offset, size = struct.unpack('<II', f.read(8))
            part = base[offset:offset + size]
        else:
            require(tag == b'L', 'Invalid package')
            size, = struct.unpack('<I', f.read(4))
            part = f.read(size)
        require(len(part) == size, 'Incomplete package or different game data')
        out.write(part)
    return out.getvalue()


def asset_roots(root):
    yield root
    ini = root / 'launcher.ini'
    if ini.is_file():
        raw = ini.read_bytes()
        encoding = 'utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'
        for line in raw.decode(encoding, errors='replace').splitlines():
            key, _, value = line.partition('=')
            if key.strip().lower() in ('base', 'update') and value.strip():
                yield root / value.strip().strip('"')


def owned_graphics(root, file_hash):
    """Only remove the exact graphics output produced by this build."""
    for base in ['data/graphics/global.rpk', 'mods/split-screen/default-mirrors.rpk']:
        path = root / base
        if not path.is_file():
            continue
        blob = bytearray(path.read_bytes())
        start, end = blob.find(b'<ViewportQuality>'), blob.find(b'</ViewportQuality>')
        for name, parent in [('TwoPlayer2D', 'OnePlayer2D'), ('TwoPlayer2DInCar', 'OnePlayer2DInCar'), ('TwoPlayer2DHQ', 'OnePlayer2DHQ')]:
            i = blob.find(('<Configuration name="%s"' % name).encode(), max(start, 0), end)
            j = blob.find(b'</Configuration>', i, end)
            if i < 0 or j < 0:
                break
            j += len(b'</Configuration>')
            profile = ('<Configuration name="%s" parent="%s"><General fieldOfViewScale="1.050000" '
                       'enableRearViewMirrorTextures="true"/><ScreenRush Rain2Enabled="false"/></Configuration>' % (name, parent)).encode()
            if len(profile) > j - i:
                break
            blob[i:j] = profile.ljust(j - i)
        else:
            if hashlib.sha256(blob).hexdigest() == file_hash:
                return True
    return False


def checked(root, relative):
    p = root / relative
    require(p.resolve() == p and p.is_relative_to(root), 'Linked or external path: ' + str(p))
    return p


def replace(path, blob, temporary):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary.write_bytes(blob)
    os.replace(temporary, path)


def recover(root, tx, allowed):
    if not tx.exists():
        return
    require(tx.is_dir() and (tx / 'owner').is_file() and
            (tx / 'owner').read_bytes() == b'DC Split Screen v0.1.0',
            'Unexpected setup directory: ' + str(tx))
    journal = tx / 'plan.json'
    if journal.exists() and not (tx / 'done').exists():
        for n, (rel, old, new) in reversed(list(enumerate(json.loads(journal.read_bytes())))):
            require(rel in allowed, 'Unrecognized recovery path')
            p = checked(root, rel)
            actual = digest(p)
            if actual == old:
                continue
            require(actual == new, 'A file changed during recovery: ' + rel)
            if old is None:
                p.unlink()
            else:
                backup = tx / ('old-%d' % n)
                require(digest(backup) == old, 'Invalid recovery copy: ' + rel)
                replace(p, backup.read_bytes(), tx / 'restore.tmp')
    shutil.rmtree(tx)


def commit(root, tx, plan, allowed):
    if not plan:
        return
    tx.mkdir()
    (tx / 'owner').write_bytes(b'DC Split Screen v0.1.0')
    operations = []
    try:
        for n, (rel, old, blob) in enumerate(plan):
            p = checked(root, rel)
            require(digest(p) == old, 'File changed: ' + rel)
            if old is not None:
                shutil.copyfile(p, tx / ('old-%d' % n))
            new = None
            if blob is not None:
                (tx / ('new-%d' % n)).write_bytes(blob)
                new = hashlib.sha256(blob).hexdigest()
            operations.append((rel, old, new))
        replace(tx / 'plan.json', json.dumps(operations).encode(), tx / 'plan.tmp')
        for n, (rel, old, new) in enumerate(operations):
            p = checked(root, rel)
            require(digest(p) == old, 'File changed: ' + rel)
            if new is None:
                p.unlink()
            else:
                p.parent.mkdir(parents=True, exist_ok=True)
                os.replace(tx / ('new-%d' % n), p)
        (tx / 'done').touch()
    finally:
        recover(root, tx, allowed)


@contextlib.contextmanager
def exclusive(root):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    tag = hashlib.sha256(str(root).casefold().encode()).hexdigest()
    handle = kernel.CreateMutexW(None, False, 'Local\\DCSplitSetup' + tag)
    error = ctypes.get_last_error()
    require(handle, 'Cannot open setup lock')
    try:
        require(error != 183, 'Another installer is open for this folder')
        yield
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


def run(root, operation, bundle, language='en'):
    root = root.resolve()
    require((root / LAUNCHER).is_file(), 'Select the folder containing DriveClub.exe')
    z = bundle
    manifest = json.loads(z.read('release.json'))
    hashes = {p: v['hash'] for p, v in manifest['files'].items()}
    changes_launcher = LAUNCHER in hashes
    allowed = set(hashes) | {STAMP, VERSION, GRAPHICS}
    if changes_launcher:
        allowed.add(BACKUP)
    tx = checked(root, 'data/split-screen-setup-010.tmp')
    with exclusive(root):
        recover(root, tx, allowed)
        for rel in allowed:
            checked(root, rel)
        stamp = root / STAMP
        previous = json.loads(stamp.read_bytes()) if stamp.exists() else None
        if previous is not None:
            require(previous in (hashes, manifest.get('upgrade_from')), 'A different Split Screen build is installed. Remove it with its own installer first.')
            if changes_launcher:
                require(digest(root / BACKUP) == STOCK, 'Original launcher backup is missing or changed')
        if operation == 'remove' and previous is None:
            print('Split Screen 0.1.0 is not installed here.')
            return translate('Split Screen 0.1.0 is not installed here.', language)
        # The overlay is live while a game is running. Check this installation only.
        game_env = dict(os.environ, DC_SETUP_FOLDER=str(root))
        status = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
            "$r=$env:DC_SETUP_FOLDER+'\\'; foreach($p in Get-Process -Name DriveClub,DriveClubGame,DriveClub.Launcher,DriveClub.Backend,DriveClub.Native -ErrorAction SilentlyContinue){if($p.Path -and $p.Path.StartsWith($r,[StringComparison]::OrdinalIgnoreCase)){exit 1}}; exit 0"],
            env=game_env, check=False, creationflags=subprocess.CREATE_NO_WINDOW)
        require(status.returncode == 0, 'Close DRIVECLUB and its launcher before continuing.')
        plan = []
        def put(rel, blob):
            old = digest(root / rel)
            new = hashlib.sha256(blob).hexdigest() if blob is not None else None
            if old != new:
                plan.append((rel, old, blob))
        tag = b'0.1.0\n'
        require(not (root / VERSION).exists() or (previous and (root / VERSION).read_bytes() == tag), 'Version marker conflict')
        for rel, expected in hashes.items():
            actual = digest(root / rel)
            if rel == LAUNCHER:
                require(actual == (hashes[rel] if previous else STOCK), 'Unsupported or modified DriveClub.exe')
            elif previous is None:
                require(actual is None, 'Existing mod file: ' + rel)
            else:
                require(actual in (None, previous.get(rel)), 'Installed file has external changes: ' + rel)
        if operation == 'remove':
            if changes_launcher:
                put(LAUNCHER, (root / BACKUP).read_bytes())
                put(BACKUP, None)
            gh = digest(root / GRAPHICS)
            if gh and owned_graphics(root, gh):
                put(GRAPHICS, None)
            elif gh:
                print('Retained externally changed mods/global.rpk')
            for rel in hashes:
                if rel != LAUNCHER:
                    put(rel, None)
            for rel in (VERSION, STAMP):
                put(rel, None)
        else:
            if not changes_launcher:
                require(digest(root / LAUNCHER) == STOCK, 'Requires original DriveClub.exe from port 0.4.2')
            require((root / 'version.txt').read_text(encoding='utf-8-sig').strip() == '0.4.2', 'Requires Windows port 0.4.2')
            require(digest(root / 'runtime/DriveClubGame.exe') == RUNTIME, 'Unsupported game runtime')
            require(digest(root / 'data/prep/boot-linked.bin') == BOOT, 'Start a normal race in port 0.4.2 once, then close the game.')
            if previous is None:
                require(not (root / BACKUP).exists(), 'Existing launcher backup')
                require(not (root / GRAPHICS).exists(), 'Another mod owns mods/global.rpk')
            if previous is not None and digest(root / GRAPHICS):
                require(owned_graphics(root, digest(root / GRAPHICS)), 'Existing graphics overlay has changed')
            if changes_launcher and not previous:
                put(BACKUP, (root / LAUNCHER).read_bytes())

            @lru_cache(None)
            def asset(name):
                if name == '@launcher':
                    require(changes_launcher, 'Unexpected launcher patch')
                    return (root / (BACKUP if previous else LAUNCHER)).read_bytes()
                if name is None:
                    return b''
                meta = manifest['assets'][name]
                for source in dict.fromkeys(asset_roots(root)):
                    try:
                        blob = unpack_asset(source, meta)
                    except (OSError, ValueError, struct.error, AssertionError, IndexError):
                        continue
                    if hashlib.sha256(blob).hexdigest() == meta['hash']:
                        return blob
                raise ValueError('Matching game data missing: ' + name + ' (' + meta['archive'] + ')')
            for rel in sorted(hashes, key=lambda r: r == LAUNCHER):
                blob = apply_recipe(asset(manifest['files'][rel]['asset']), z.read(rel + '.patch'))
                require(hashlib.sha256(blob).hexdigest() == hashes[rel], 'Verification failed: ' + rel)
                put(rel, blob)
            put(STAMP, json.dumps(hashes, indent=2).encode())
            put(VERSION, tag)
        commit(root, tx, plan, allowed)
        if operation == 'remove':
            print('Split Screen 0.1.0 removed.' + (' Original launcher restored.' if changes_launcher else ''))
            message = translate('Split Screen removed.', language)
            if changes_launcher:
                message += ' ' + translate('Original launcher restored.', language)
            if gh and (root / GRAPHICS).is_file():
                message += '\n' + translate('Kept the modified mods/global.rpk file.', language)
            return message
        else:
            print('Split Screen 0.1.0 installed and verified.')
            return translate('Installation complete. Open DriveClub.exe and select Drive → Split Screen.', language)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', nargs='?', choices=['install', 'remove'])
    parser.add_argument('--game', type=Path)
    args = parser.parse_args()
    root = args.game
    operation = args.operation
    interactive = operation is None
    language = 'en'
    if interactive:
        script = Path(__file__).with_name('setup-dialog.ps1').read_text(encoding='utf-8')
        dialog = subprocess.run(['powershell.exe', '-NoProfile', '-STA', '-Command', script],
                                capture_output=True, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if not dialog.stdout:
            return
        selected = json.loads(dialog.stdout.decode('utf-8-sig'))
        operation, game = selected['operation'], Path(selected['file'])
        language = selected['language']
        root = game.parent
    elif root is None:
        dialog = subprocess.run(['powershell.exe', '-NoProfile', '-STA', '-Command',
            "Add-Type -AssemblyName System.Windows.Forms; $f=New-Object System.Windows.Forms.OpenFileDialog; $f.Title='DRIVECLUB 0.4.2 - Select DriveClub.exe'; $f.Filter='DriveClub.exe|DriveClub.exe'; if($f.ShowDialog() -eq 'OK'){[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);[Console]::Write($f.FileName)}"],
            capture_output=True, check=True)
        if not dialog.stdout:
            return
        root = Path(dialog.stdout.decode('utf-8-sig')).parent
    try:
        if interactive:
            require(game.name.casefold() == LAUNCHER.casefold(), 'Select DriveClub.exe.')
        with ZipFile(Path(__file__).with_name('payload.zip')) as z:
            message = run(root, operation, z, language)
    except Exception as exc:
        if not interactive:
            raise
        title = 'Ошибка установки' if language == 'ru' else 'Setup error'
        ctypes.windll.user32.MessageBoxW(None, translate(str(exc), language), 'DRIVECLUB Split Screen — ' + title, 0x10)
        sys.exit(1)
    if interactive:
        ctypes.windll.user32.MessageBoxW(None, message, 'DRIVECLUB Split Screen 0.1.0', 0x40)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        if len(sys.argv) == 1:
            ctypes.windll.user32.MessageBoxW(None, str(exc), 'DRIVECLUB Split Screen — Setup error', 0x10)
        sys.exit('Setup: ' + str(exc))
