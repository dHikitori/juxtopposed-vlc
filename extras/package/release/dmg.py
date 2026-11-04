#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and check the drag-to-install macOS disk image."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ART = Path(__file__).resolve().parent / 'dmg'
VOLUME_NAME = 'Hakone'
APP_NAME = 'Hakone.app'


def dmgbuild_command():
    """Locate the pinned dmgbuild (dmg/requirements.txt) without a GUI session."""
    candidates = [os.environ.get('DMGBUILD'), ROOT / 'build-release-tools/bin/dmgbuild',
                  shutil.which('dmgbuild')]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise RuntimeError('dmgbuild not found: install extras/package/release/dmg/requirements.txt '
                       'into build-release-tools, or set DMGBUILD to its dmgbuild executable')


def build(app, image):
    """Create a compressed image with the signed app, an Applications link and Finder layout."""
    if app.name != APP_NAME:
        raise ValueError(f'Expected {APP_NAME}, got {app.name}')
    if image.exists():
        image.unlink()
    subprocess.run([dmgbuild_command(), '-s', str(ART / 'settings.py'),
                    '-D', f'app={app}', '-D', f'background={ART / "background.png"}',
                    '-D', f'icon={ROOT / "share/icons/hakone.icns"}',
                    VOLUME_NAME, str(image)], check=True)
    if not image.is_file() or image.stat().st_size == 0:
        raise RuntimeError(f'dmgbuild did not create {image}')
    return image


def tree(root):
    """Describe a bundle by path, type, executable bit, link target and content hash."""
    entries = {}
    for path in sorted(root.rglob('*')):
        relative = str(path.relative_to(root))
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            entries[relative] = ('link', os.readlink(path))
        elif stat.S_ISDIR(mode):
            entries[relative] = ('dir',)
        elif stat.S_ISREG(mode):
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
            entries[relative] = ('file', bool(mode & stat.S_IXUSR), digest.hexdigest())
        else:
            entries[relative] = ('other',)
    return entries


def check_volume(volume, reference=None):
    """Check a mounted image's layout, the app signature and (optionally) its identity."""
    app = volume / APP_NAME
    if app.is_symlink() or not app.is_dir():
        raise RuntimeError(f'{APP_NAME} missing from the disk image')
    applications = volume / 'Applications'
    if not applications.is_symlink() or os.readlink(applications) != '/Applications':
        raise RuntimeError('Applications link missing from the disk image')
    for name in ('.DS_Store', '.VolumeIcon.icns', '.background.tiff'):
        if not (volume / name).is_file():
            raise RuntimeError(f'Finder layout file missing from the disk image: {name}')
    visible = sorted(p.name for p in volume.iterdir() if not p.name.startswith('.'))
    if visible != ['Applications', APP_NAME]:
        raise RuntimeError(f'Unexpected disk image contents: {visible}')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    if reference is not None:
        expected, actual = tree(reference), tree(app)
        if expected != actual:
            differing = sorted(k for k in expected.keys() | actual.keys()
                               if expected.get(k) != actual.get(k))
            raise RuntimeError(f'Disk image app differs from {reference}: ' + ', '.join(differing[:10]))
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('build', help='Create IMAGE from APP')
    create.add_argument('app', type=Path)
    create.add_argument('image', type=Path)
    check = commands.add_parser('check', help='Check a disk image mounted at VOLUME')
    check.add_argument('volume', type=Path)
    check.add_argument('--reference', type=Path, help='Require the app to match this verified Hakone.app')
    args = parser.parse_args()
    try:
        if args.command == 'build':
            print(build(args.app.resolve(), args.image.resolve()))
        else:
            print(check_volume(args.volume, args.reference))
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
