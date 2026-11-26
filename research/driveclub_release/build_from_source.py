"""Build both native components and create the release payload in a staging directory."""
# Copyright (C) 2026 nuvsyakoetakoe. SPDX-License-Identifier: GPL-2.0-only
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from build_payload import ROOT, write_payload
from install_driveclub_menu import prepare_payload, SUPPORTED


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, required=True, help='Directory containing game.ndx')
    p.add_argument('--stock-launcher', type=Path, required=True, help='Original DriveClub.exe, port 0.4.2')
    p.add_argument('--vulkan-include', type=Path, required=True)
    p.add_argument('--zig', default='zig')
    p.add_argument('--output', type=Path, default=ROOT / 'output/driveclub-release-010')
    args = p.parse_args()
    if hashlib.sha256(args.stock_launcher.read_bytes()).hexdigest() != SUPPORTED['DriveClub.exe']:
        p.error('The original 0.4.2 launcher is required.')
    native_dir = args.output.resolve()
    native_dir.mkdir(parents=True, exist_ok=True)
    render = ROOT / 'research/dc_render'
    launcher = ROOT / 'research/upstream/driveclub-042/launcher/setup'
    target = ['-target', 'x86_64-windows-gnu']
    subprocess.run([args.zig, 'rc', '/:auto-includes', 'gnu', '/fo', native_dir / 'launcher.res',
                    'Setup.rc'], cwd=launcher, check=True)
    subprocess.run([args.zig, 'c++', *target, '-std=c++17', '-Os', '-municode', '-Wl,--subsystem,windows',
                    render / 'experiments/native-menu/launcher.cpp', native_dir / 'launcher.res',
                    '-lshell32', '-luser32', '-ladvapi32', '-o', native_dir / 'DriveClub.exe'], check=True)
    subprocess.run([args.zig, 'cc', *target, '-O2', '-g', '-DNDEBUG', '-DDC_NATIVE_MENU',
                    '-DDC_FEEDBACK_QA', '-shared', '-I', args.vulkan_include.resolve(), '-I', render,
                    render / 'capture.c', '-luser32', '-lgdi32', '-o', native_dir / 'driveclub_split.dll'], check=True)
    previous = json.loads(Path(__file__).with_name('upgrade-010.json').read_bytes())
    with tempfile.TemporaryDirectory(dir=native_dir) as directory:
        stage = Path(directory)
        prepare_payload(args.assets, stage, native_dir)
        paths = [p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()]
        write_payload(native_dir / 'payload.zip', paths, lambda rel: (stage / rel).read_bytes(),
                      previous, args.assets, args.stock_launcher)


if __name__ == '__main__':
    main()
