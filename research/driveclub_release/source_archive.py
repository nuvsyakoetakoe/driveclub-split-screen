"""Collect the source and build inputs used by the Windows release."""
# Copyright (C) 2026 nuvsyakoetakoe. SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import re
from zipfile import ZIP_DEFLATED, ZipFile


def create_source(root, destination):
    render = root / 'research/dc_render'
    release = root / 'research/driveclub_release'
    menu = render / 'experiments/native-menu'
    files = set()
    def add_includes(path):
        path = path.resolve()
        if path in files:
            return
        files.add(path)
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding='utf-8'), re.M):
            included = path.parent / name
            add_includes(included if included.is_file() else render / name)

    for entry in (render / 'capture.c', menu / 'launcher.cpp'):
        add_includes(entry)
    upstream = root / 'research/upstream/driveclub-042'
    files.update(upstream / n for n in ('LICENSE', 'launcher/setup/Setup.rc',
                 'launcher/wpf/app.manifest', 'launcher/wpf/Assets/app.ico'))
    files.add(menu / 'driveclub_split.json')
    for stem in ('split-screen', 'single-event'):
        files.update(menu / 'art' / (stem + ext) for ext in ('.png', '.bc7'))
    for module in ('dc_lua', 'driveclub_probe', 'driveclub_session', 'driveclub_native_menu',
                   'driveclub_menu_layout', 'driveclub_menu_strings', 'driveclub_menu_art',
                   'driveclub_stock_hud', 'driveclub_mirrors', 'install_driveclub_menu',
                   'driveclub_participants'):
        files.add(root / 'research' / (module + '.py'))
    for name in ('menu_game', 'menu_countdown', 'menu_prerace', 'menu_racing', 'menu_settings',
                 'menu_status', 'menu_cameras', 'live_controls', 'hud', 'pedals', 'stock_hud'):
        files.add(root / 'research' / ('driveclub_' + name + '.lua'))
    for name in ('build_from_source.py', 'build_payload.py', 'package.py', 'source_archive.py',
                 'setup.py', 'setup-dialog.ps1', 'setup_text.py', 'upgrade-010.json',
                 'README-en.txt', 'README-ru.txt', 'COPYING.txt', 'BUILD.txt'):
        files.add(release / name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(destination, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(files):
            archive.write(path, path.relative_to(root).as_posix())
        for name in ('README-en.txt', 'README-ru.txt', 'BUILD.txt', 'COPYING.txt'):
            archive.write(release / name, name)
