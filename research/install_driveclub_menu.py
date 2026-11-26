"""Install the local split-screen menu into the checked Windows port."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from driveclub_mirrors import prepare_mirrors
from driveclub_native_menu import prepare_menu, prepare_menu_race
from driveclub_probe import test_assets
from driveclub_stock_hud import prepare_stock_hud


REPO = Path(__file__).resolve().parents[1]
MENU = REPO / "research/dc_render/experiments/native-menu"
GAME = REPO / "DRIVECLUB MOD/DRIVECLUB"
BUILD = REPO / "output/driveclub-native-menu"
SUPPORTED = {
    "DriveClub.exe": "3411aa1b4943c40273b2d5cbb51e20c1656eb4dd44a1b4177eccda2048df00ce",
    "runtime/DriveClubGame.exe": "fb2f88b6b016a91a8390a98250a86ace74aa2dd84d5e30031eb8be0fe076e400",
    "data/prep/boot-linked.bin": "cb2ae1d90e240bde81ff2bea048c4eb16a9287185afd05fa36fb32e4e7c88156",
}
ADDON = Path("mods/split-screen")
MANIFEST = ADDON / "installed.json"
BACKUP = ADDON / "DriveClub.stock.exe"
GRAPHICS = Path("mods/global.rpk")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as pending:
        temporary = Path(pending.name)
        try:
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, pending)
            pending.close()
            os.replace(temporary, destination)
        finally:
            pending.close()
            temporary.unlink(missing_ok=True)


def prepare_payload(assets, stage, native_dir=BUILD):
    mods = stage / "mods"
    addon = stage / ADDON
    addon.mkdir(parents=True)
    prepare_menu(assets, mods)
    prepare_menu_race(assets, mods)
    prepare_stock_hud(mods, test_assets(assets, "newui/panels/osd*.txt"), inactive=True)
    prepare_mirrors(assets, addon)
    (addon / "global.rpk").rename(addon / "default-mirrors.rpk")
    shutil.copyfile(MENU / "driveclub_split.json", addon / "driveclub_split.json")
    shutil.copyfile(native_dir / "driveclub_split.dll", addon / "driveclub_split.dll")
    shutil.copyfile(native_dir / "DriveClub.exe", stage / "DriveClub.exe")


def install(game, assets):
    installed_path = game / MANIFEST
    previous = json.loads(installed_path.read_text()) if installed_path.is_file() else {}
    if not previous and (game / GRAPHICS).exists():
        raise ValueError(f"Already in use: {game / GRAPHICS}")
    if previous and digest(game / BACKUP) != SUPPORTED["DriveClub.exe"]:
        raise ValueError("The original launcher backup does not match the supported build")
    for relative, expected in SUPPORTED.items():
        if digest(game / relative) not in {expected, previous.get(relative)}:
            raise ValueError(f"Unsupported DRIVECLUB file: {game / relative}")

    with tempfile.TemporaryDirectory(prefix="split-menu-", dir=BUILD) as directory:
        stage = Path(directory)
        prepare_payload(assets, stage)
        paths = sorted(path.relative_to(stage) for path in stage.rglob("*") if path.is_file())
        hashes = {path.as_posix(): digest(stage / path) for path in paths}
        for path in paths:
            target = game / path
            if path != Path("DriveClub.exe") and target.exists() and digest(target) not in {
                previous.get(path.as_posix()), hashes[path.as_posix()]
            }:
                raise ValueError(f"Existing mod file conflicts with Split Screen: {target}")
        if not previous:
            copy_file(game / "DriveClub.exe", game / BACKUP)
        for path in paths:
            if path != Path("DriveClub.exe"):
                copy_file(stage / path, game / path)
        stamp = stage / "installed.json"
        stamp.write_text(json.dumps(hashes, indent=2), encoding="ascii")
        copy_file(stamp, installed_path)
        copy_file(stage / "DriveClub.exe", game / "DriveClub.exe")
    print(f"Split Screen installed in {game}")


def uninstall(game):
    stamp = game / MANIFEST
    installed = json.loads(stamp.read_text(encoding="ascii"))
    backup = game / BACKUP
    if digest(backup) != SUPPORTED["DriveClub.exe"]:
        raise ValueError("The original launcher backup does not match the supported build")
    paths = []
    for relative, expected in installed.items():
        path = (game / relative).resolve()
        if path != game / "DriveClub.exe" and not path.is_relative_to(game / "mods"):
            raise ValueError(f"Path outside the Split Screen installation: {path}")
        if path.exists() and digest(path) != expected:
            raise ValueError(f"Installed file was changed: {path}")
        paths.append(path)
    copy_file(backup, game / "DriveClub.exe")
    for path in paths:
        if path != game / "DriveClub.exe":
            path.unlink(missing_ok=True)
    (game / GRAPHICS).unlink(missing_ok=True)
    backup.unlink()
    stamp.unlink()
    print(f"Original launcher restored in {game}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=GAME, help="Launcher installation directory")
    parser.add_argument("--assets", type=Path, default=GAME, help="Matching game assets used to prepare the menu")
    parser.add_argument("--remove", action="store_true", help="Restore the original launcher and remove installed files")
    args = parser.parse_args()
    if args.remove:
        uninstall(args.game.resolve())
    else:
        install(args.game.resolve(), args.assets.resolve())


if __name__ == "__main__":
    main()
