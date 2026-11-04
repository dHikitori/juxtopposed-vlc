#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
spec = importlib.util.spec_from_file_location('build_msi', HERE.parent / 'build-msi.py')
build_msi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_msi)

TAG = build_msi.validate_tag(None)['version']


class MsiTests(unittest.TestCase):
    def package(self, root):
        package = root / 'hakone-win64'
        for name in ('hakone.exe', 'hakone-addon-worker.exe', 'libvlc.dll', 'libvlccore.dll', 'hakone.ico',
                     'vlc-cache-gen.exe', 'vlc-preparser.exe', 'libstdc++-6.dll', 'vulkan-1.dll',
                     'README.txt', 'SOURCE_COMMIT.txt', 'libvlc.dll.a', 'plugins/codec/libavcodec_plugin.dll',
                     'lua/intf/cli.luac', 'share/vlc/addons/reference/episodes.zip',
                     'licenses/vulkan-loader/LICENSE.txt'):
            (package / name).parent.mkdir(parents=True, exist_ok=True)
            (package / name).write_bytes(b'fixture')
        output = root / 'output'
        output.mkdir()
        (output / 'windows-build.json').write_text(json.dumps(dict(version=TAG, platform='windows')))
        return package, output

    def test_config_follows_the_package_and_version(self):
        with tempfile.TemporaryDirectory() as temp:
            package, _ = self.package(Path(temp))
            info = build_msi.validate_tag(TAG)
            text = build_msi.config_wxi(package, info)
            self.assertIn(f'<?define VerMajor = "{info["major"]}" ?>', text)
            self.assertIn(f'<?define VerPatch = "{info["patch"]}" ?>', text)
            self.assertIn('<?define Platform= "x64" ?>', text)
            prefix = re.search(r'VlcInstallPrefix = "([^"]*)"', text)[1]
            self.assertEqual(prefix, str(package.resolve()) + '\\')
            self.assertIn('<?if "#" = "" ?>', text)  # no locale or skins folders
            (package / 'locale').mkdir()
            self.assertIn('<?if "" = "" ?>', build_msi.config_wxi(package, info))
            self.assertNotIn('@', text)

    def test_top_level_fragment_lists_only_files_product_wxs_leaves_out(self):
        with tempfile.TemporaryDirectory() as temp:
            package, _ = self.package(Path(temp))
            fragment = Path(temp) / 'TopLevel.wxs'
            subprocess.run([sys.executable, str(build_msi.MSI_SOURCES / 'msi-heat.py'), '--dir', str(package),
                            '--top-level', '--skip-listed', str(build_msi.MSI_SOURCES / 'product.wxs'),
                            '-cg', 'CompTopLevelGroup', '-dr', 'APPLICATIONFOLDER', '-out', str(fragment)],
                           check=True, stdout=subprocess.DEVNULL)
            names = set(re.findall(r'<File Id="[^"]+" Name="([^"]+)"', fragment.read_text()))
            self.assertEqual(names, {'hakone-addon-worker.exe', 'libstdc++-6.dll', 'vulkan-1.dll', 'SOURCE_COMMIT.txt'})
            self.assertIn('<DirectoryRef Id="APPLICATIONFOLDER">', fragment.read_text())
            self.assertIn('<ComponentGroup Id="CompTopLevelGroup">', fragment.read_text())

    def test_build_compiles_every_fragment_and_records_the_msi(self):
        with tempfile.TemporaryDirectory() as temp:
            package, output = self.package(Path(temp))
            wix = Path(temp) / 'wix'
            calls = []
            real_run = subprocess.run

            def tool(command, **kwargs):
                if command[0] == sys.executable:
                    return real_run(command, **kwargs)
                calls.append(Path(command[0]).name)
                out = Path(command[command.index('-out') + 1])
                if Path(command[0]).name == 'candle.exe':
                    include = next(c for c in command if c.startswith('-I'))[2:]
                    self.assertTrue((Path(include) / 'extras/package/win32/msi/config.wxi').is_file())
                    out.write_text('object')
                else:
                    self.assertEqual(len([c for c in command if c.endswith('.wixobj')]), calls.count('candle.exe'))
                    out.write_bytes(b'msi')
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(build_msi.subprocess, 'run', side_effect=tool):
                msi = build_msi.build(package, output, wix, TAG)
            # product, extensions, four folders and the top level, then one link.
            self.assertEqual(calls, ['candle.exe'] * 7 + ['light.exe'])
            self.assertEqual(msi.name, f'hakone-{TAG}-win64.msi')
            digest = hashlib.sha256(b'msi').hexdigest()
            metadata = json.loads((output / 'windows-build.json').read_text())
            self.assertEqual(metadata['msi'], dict(asset_name=msi.name, size=3, sha256=digest,
                                                   package_type='msi-installer'))
            self.assertEqual((output / (msi.name + '.sha256')).read_text(), f'{digest}  {msi.name}\n')

    def test_build_refuses_a_portable_or_incomplete_package(self):
        with tempfile.TemporaryDirectory() as temp:
            package, output = self.package(Path(temp))
            (package / 'portable').mkdir()
            with self.assertRaisesRegex(ValueError, 'portable'):
                build_msi.build(package, output, Path(temp), TAG)
            (package / 'portable').rmdir()
            (package / 'hakone-addon-worker.exe').unlink()
            with self.assertRaisesRegex(ValueError, 'hakone-addon-worker.exe'):
                build_msi.build(package, output, Path(temp), TAG)


if __name__ == '__main__':
    unittest.main()
