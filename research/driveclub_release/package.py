"""Assemble the public files from the frozen Windows installer."""
import hashlib
import argparse
import importlib.metadata
import json
from pathlib import Path
import sys
from zipfile import ZIP_DEFLATED, ZipFile
from source_archive import create_source


here = Path(__file__).resolve().parent
root = here.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--stock-launcher', action='store_true')
stock = parser.parse_args().stock_launcher
output = root / 'publish/driveclub-split-screen/0.1.0'
build = root / 'output/driveclub-release-010'
if stock:
    output = output / 'stock-launcher'
    build = build / 'stock-launcher'
output.mkdir(parents=True, exist_ok=True)
suffix = '-Stock-Launcher' if stock else ''
archive = output / f'DRIVECLUB-Split-Screen-0.1.0-Windows-x64{suffix}.zip'
readme = (here / ('README-stock-ru.txt' if stock else 'README-ru.txt')).read_text(encoding='utf-8').replace('\n', '\r\n').encode('utf-8-sig')
(output / 'README-ru.txt').write_bytes(readme)
if not stock:
    source = output / 'Source/DRIVECLUB-Split-Screen-0.1.0-source.zip'
    create_source(root, source)

with ZipFile(archive, 'w', ZIP_DEFLATED, compresslevel=9) as z:
    if not stock:
        english = (here / 'README-en.txt').read_text(encoding='utf-8').replace('\n', '\r\n').encode('utf-8-sig')
        (output / 'README-en.txt').write_bytes(english)
        z.writestr('README-en.txt', english)
    z.writestr('README-ru.txt', readme)
    if stock:
        for name, action in [('Install.cmd', 'install'), ('Uninstall.cmd', 'remove')]:
            z.writestr(name, '@echo off\r\nsetlocal\r\n"%~dp0SplitScreen-Setup.exe" ' + action +
                       ' %*\r\nset "result=%errorlevel%"\r\npause\r\nexit /b %result%\r\n')
    z.write(build / 'dist/SplitScreen-Setup.exe', 'SplitScreen-Setup.exe')
    if not stock:
        z.write(source, 'Source/' + source.name)
        z.write(here / 'COPYING.txt', 'COPYING.txt')
    python_license = next(Path(sys.base_prefix) / name for name in ('LICENSE_PYTHON.txt', 'LICENSE.txt')
                          if (Path(sys.base_prefix) / name).is_file())
    z.write(python_license, 'Notices/Python.txt')
    dist = importlib.metadata.distribution('PyInstaller')
    license_file = next(f for f in dist.files if str(f).endswith('/licenses/COPYING.txt'))
    z.write(dist.locate_file(license_file), 'Notices/PyInstaller.txt')
    for meta in (Path(sys.prefix) / 'conda-meta').glob('*.json'):
        info = json.loads(meta.read_bytes())
        if info['name'] in {'libffi', 'openssl', 'libzlib', 'zlib', 'libmpdec',
                            'xz', 'liblzma', 'bzip2', 'vc14_runtime', 'vs2015_runtime', 'ucrt'}:
            package = Path(sys.prefix) / 'pkgs' / meta.stem / 'info/licenses'
            for license_file in package.rglob('*'):
                if license_file.is_file():
                    z.write(license_file, f"Notices/{info['name']}/{license_file.relative_to(package).as_posix()}")
with ZipFile(archive) as z:
    assert z.testzip() is None
checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
(output / 'SHA256SUMS.txt').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
print(archive, archive.stat().st_size, checksum)
