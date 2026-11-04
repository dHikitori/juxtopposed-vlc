#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Windows MSI installer from the extracted portable package, on Windows.

The meson MSI rules run WiX through Wine on Linux. The release builds the MSI
natively instead, from the same files as the portable ZIP, with WiX 3 on the
Windows runner. The package metadata gains an "msi" entry.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from package import write_checksum
from version import ROOT, validate_tag

MSI_SOURCES = ROOT / 'extras/package/win32/msi'
# The heat fragments product.wxs references, by folder.
FOLDERS = ('Plugins', 'Visualization', 'Locale', 'Lua', 'Skins', 'Hrtfs', 'Share', 'Licenses')
REQUIRED = ('hakone.exe', 'hakone-addon-worker.exe', 'libvlc.dll', 'libvlccore.dll', 'hakone.ico', 'plugins', 'lua')


def config_wxi(package, info):
    values = dict(HAKONE_VERSION_MAJOR=info['major'], HAKONE_VERSION_MINOR=info['minor'],
                  HAKONE_VERSION_REVISION=info['patch'], WINDOWS_ARCH='x64',
                  PREFIX_PATH=str(package.resolve()).rstrip('\\/') + '\\',
                  HAVE_NLS_TRUE='' if (package / 'locale').is_dir() else '#',
                  BUILD_SKINS_TRUE='' if (package / 'skins').is_dir() else '#')
    text = (MSI_SOURCES / 'config.wxi.in').read_text()
    text = re.sub(r'@([A-Z0-9_]+)@', lambda match: str(values[match[1]]), text)
    if '@' in text:
        raise ValueError('config.wxi.in has an unknown substitution')
    return text


def heat(package, work, name, *extra):
    output = work / f'{name}.fragment.wxs'
    subprocess.run([sys.executable, str(MSI_SOURCES / 'msi-heat.py'), *extra, '-cg', f'Comp{name}Group',
                    '-dr', 'APPLICATIONFOLDER', '-out', str(output)],
                   check=True, stdout=subprocess.DEVNULL)
    return output


def build(package, output, wix, tag):
    info = validate_tag(tag)
    for name in REQUIRED:
        if not (package / name).exists():
            raise ValueError(f'Portable package missing {name}')
    if (package / 'portable').exists():
        raise ValueError('Build the MSI from a package without the portable folder')
    metadata_path = output / 'windows-build.json'
    metadata = json.loads(metadata_path.read_text())
    if metadata.get('version') != info['version']:
        raise ValueError(f'Package metadata is for {metadata.get("version")!r}, not {info["version"]!r}')
    msi = output / f'hakone-{info["version"]}-win64.msi'
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        include = work / 'extras/package/win32/msi'
        include.mkdir(parents=True)
        (include / 'config.wxi').write_text(config_wxi(package, info))
        sources = [MSI_SOURCES / 'product.wxs', MSI_SOURCES / 'extensions.wxs']
        sources += [heat(package, work, name, '--dir', str(package / name.lower())) for name in FOLDERS
                    if (package / name.lower()).is_dir()]
        sources.append(heat(package, work, 'TopLevel', '--dir', str(package), '--top-level',
                            '--skip-listed', str(MSI_SOURCES / 'product.wxs')))
        objects = []
        for source in sources:
            obj = work / (source.stem + '.wixobj')
            subprocess.run([str(wix / 'candle.exe'), '-nologo', '-arch', 'x64', f'-I{work}',
                            '-ext', 'WixUtilExtension', '-out', str(obj), str(source)], check=True)
            objects.append(str(obj))
        # Like the meson rules, skip ICE validation; the install test covers the result.
        subprocess.run([str(wix / 'light.exe'), '-nologo', '-sval', '-spdb', '-ext', 'WixUIExtension',
                        '-ext', 'WixUtilExtension', '-cultures:en-us', '-out', str(msi), *objects], check=True)
    metadata['msi'] = dict(asset_name=msi.name, size=msi.stat().st_size, sha256=write_checksum(msi),
                           package_type='msi-installer')
    metadata_path.write_text(json.dumps(metadata, indent=2) + '\n')
    return msi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, help='The extracted hakone-win64 folder, without portable')
    parser.add_argument('--output', type=Path, required=True,
                        help='The folder with windows-build.json, which receives the MSI')
    parser.add_argument('--wix', type=Path, default=Path(os.environ.get('WIX', '')) / 'bin',
                        help='The WiX 3 bin folder (default: %%WIX%%\\bin)')
    parser.add_argument('--tag', default=os.environ.get('RELEASE_TAG'))
    args = parser.parse_args()
    if not (args.wix / 'candle.exe').is_file():
        parser.error(f'WiX 3 candle.exe not found in {args.wix}')
    try:
        print(build(args.package, args.output, args.wix, args.tag))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
