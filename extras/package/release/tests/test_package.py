# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location('release_package', SCRIPTS / 'package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def make_bundle(self, root, executable, framework=False):
        if framework:
            main = root / 'Versions/A' / executable
            plist = root / 'Versions/A/Resources/Info.plist'
        else:
            main = root / 'Contents/MacOS' / executable
            plist = root / 'Contents/Info.plist'
        main.parent.mkdir(parents=True, exist_ok=True)
        main.write_bytes(b'\xcf\xfa\xed\xfe')
        plist.parent.mkdir(parents=True, exist_ok=True)
        with plist.open('wb') as stream:
            plistlib.dump(dict(CFBundleExecutable=executable), stream)
        if framework:
            (root / 'Versions/Current').symlink_to('A')
            (root / executable).symlink_to('Versions/Current/' + executable)
            (root / 'Resources').symlink_to('Versions/Current/Resources')
        return main

    def test_macos_signs_nested_code_before_bundles_without_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'Hakone.app'
            launcher = self.make_bundle(app, 'hakone')
            framework = app / 'Contents/Frameworks/QtCore.framework'
            framework_main = self.make_bundle(framework, 'QtCore', framework=True)
            helper = framework / 'Helpers/Helper.app'
            helper_main = self.make_bundle(helper, 'Helper')
            library = app / 'Contents/Frameworks/libvlc.dylib'
            library.write_bytes(b'\xcf\xfa\xed\xfe')
            helper_library = helper / 'Contents/MacOS/helper.dylib'
            helper_library.write_bytes(b'\xcf\xfa\xed\xfe')
            with mock.patch.object(package.subprocess, 'run') as tool:
                # Intentionally start with the app launcher, as rglob can do.
                package.sign_macos_adhoc(app, [launcher, framework_main, helper_main,
                                             library, helper_library])
            calls = tool.call_args_list
            signed = [Path(call.args[0][-1]) for call in calls[:-1]]
            self.assertEqual(set(signed), {app, framework, helper, library, helper_library})
            self.assertLess(signed.index(helper_library), signed.index(helper))
            self.assertLess(signed.index(helper), signed.index(framework))
            self.assertEqual(signed[-1], app)
            for call in calls[:-1]:
                self.assertEqual(call.args[0][:6], ['codesign', '--force', '--sign', '-',
                                                  '--timestamp=none', '--verbose=2'])
                self.assertEqual(call.kwargs, dict(check=True))  # Keep diagnostics visible.
            self.assertEqual(calls[-1].args[0], ['codesign', '--verify', '--deep', '--strict',
                                               '--verbose=2', str(app)])

    def test_macos_invalid_adhoc_signature_stops_packaging(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'Hakone.app'
            launcher = self.make_bundle(app, 'hakone')
            error = subprocess.CalledProcessError(1, ['codesign'])
            with mock.patch.object(package.subprocess, 'run', side_effect=error) as tool:
                with self.assertRaises(subprocess.CalledProcessError):
                    package.sign_macos_adhoc(app, [launcher])
            self.assertEqual(tool.call_count, 1)

    def test_macos_final_metadata_is_sealed_and_verified_before_archive(self):
        for arch in ('x86_64', 'arm64'):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                stage, output, qt, sources = [root / p for p in ('stage', 'output', 'qt', 'sources')]
                output.mkdir()
                for name in ('bin/hakone', 'bin/vlc-addon-worker', 'libexec/vlc/vlc-preparser', 'libexec/vlc/vlc-cache-gen',
                             'libexec/vlc/vlc-qt-check', 'lib/libvlc.dylib',
                             'lib/vlc/plugins/gui/libqt_plugin.dylib'):
                    path = stage / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'\xcf\xfa\xed\xfe')
                for name in ('share/vlc', 'share/locale'):
                    (stage / name).mkdir(parents=True)
                for name in ('share/vlc512x512.png', 'share/icons/hakone.icns',
                             'modules/gui/qt/fonts/LICENSE.txt', 'COPYING', 'COPYING.LIB',
                             'AUTHORS', 'THANKS', 'README.md'):
                    path = sources / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'fixture')
                app = output / f'macos-{arch}/Hakone.app'
                verified = False
                sealed_info = None
                signed_paths = []
                zipped = False
                imaged = []

                def query(*args):
                    if args[0] == 'uname':
                        return arch
                    if args[0] == 'lipo':
                        return arch
                    if args[0] == 'otool':
                        return 'cmd LC_BUILD_VERSION\n minos 13.5'
                    if str(args[0]).endswith('/qmake'):
                        return '6.8.3'
                    raise AssertionError(args)

                def execute(args, **kwargs):
                    nonlocal verified, sealed_info, zipped
                    self.assertTrue(kwargs['check'])
                    if str(args[0]).endswith('/macdeployqt'):
                        worker = app / 'Contents/MacOS/vlc-addon-worker'
                        self.assertTrue(worker.is_file())
                        self.assertIn(f'-executable={worker}', args)
                    if args[0] == 'codesign':
                        info = (app / 'Contents/Info.plist').read_bytes()
                        self.assertEqual(plistlib.loads(info)['LSMinimumSystemVersion'], '13.5')
                        if '--verify' in args:
                            self.assertEqual(info, sealed_info)
                            verified = True
                        else:
                            self.assertFalse(verified)
                            sealed_info = info
                            signed_paths.append(Path(args[-1]))
                    if args[0] == 'ditto':
                        self.assertTrue(verified)
                        self.assertEqual((app / 'Contents/Info.plist').read_bytes(), sealed_info)
                        Path(args[-1]).touch()
                        zipped = True
                    if args[0] == '/fake/dmgbuild':
                        # The disk image is made from the same sealed app as the ZIP.
                        self.assertTrue(verified and zipped)
                        self.assertEqual((app / 'Contents/Info.plist').read_bytes(), sealed_info)
                        self.assertIn(f'app={app}', args)
                        self.assertEqual(args[-2], 'Hakone')
                        imaged.append(Path(args[-1]))
                        Path(args[-1]).write_bytes(b'image')

                with mock.patch.dict(package.os.environ, {'QT_ROOT_DIR': str(qt)}, clear=True), \
                        mock.patch.object(package, 'ROOT', sources), \
                        mock.patch.object(package, 'run', side_effect=query), \
                        mock.patch.object(package.dmg, 'dmgbuild_command', return_value='/fake/dmgbuild'), \
                        mock.patch.object(package.subprocess, 'run', side_effect=execute):
                    archive = package.macos(stage, output, package.validate_tag(None)['version'], 'a' * 40)
                self.assertTrue(archive.exists())
                self.assertEqual(imaged, [output / archive.name.replace('.zip', '.dmg')])
                self.assertIn(app / 'Contents/MacOS/vlc-addon-worker', signed_paths)
                self.assertLess(signed_paths.index(app / 'Contents/MacOS/vlc-addon-worker'), signed_paths.index(app))
                audit = json.loads((output / f'macos-{arch}-dependency-audit.json').read_text())
                self.assertIn('Contents/MacOS/vlc-addon-worker', audit)
                metadata = json.loads((output / f'macos-{arch}-build.json').read_text())
                self.assertEqual(metadata['signing'], 'ad-hoc; no Developer ID or notarization')

    def test_windows_archive_and_metadata(self):
        tag = package.validate_tag(None)['version']
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / 'stage'
            stage.mkdir()
            for name in ('hakone.exe', 'vlc-addon-worker.exe', 'libvlc.dll', 'libvlccore.dll'):
                (stage / name).write_bytes(b'fixture')
            (stage / 'plugins').mkdir()
            output = root / 'output'

            def tool(*args):
                if args[0] == 'git':
                    return 'a' * 40
                if args[0] == 'uname':
                    return 'x86_64'
                if args[0] == 'x86_64-w64-mingw32-objdump':
                    return ''  # Synthetic fixtures have no imported runtime DLLs.
                raise AssertionError(args)

            argv = ['package.py', 'windows', str(stage), '--output', str(output), '--tag', tag]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(package, 'run', side_effect=tool):
                package.main()
            archive = output / f'hakone-{tag}-win64.zip'
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as bundled:
                self.assertIn('hakone-win64/portable/', bundled.namelist())
                self.assertIn('hakone-win64/vlc-addon-worker.exe', bundled.namelist())
                self.assertEqual(bundled.read('hakone-win64/SOURCE_COMMIT.txt'), ('a' * 40 + '\n').encode())
            metadata = json.loads((output / 'windows-build.json').read_text())
            self.assertEqual(metadata['version'], tag)
            self.assertEqual(metadata['asset_name'], archive.name)
            self.assertEqual(metadata['size'], archive.stat().st_size)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertEqual(metadata['sha256'], digest)
            self.assertEqual(archive.with_suffix('.zip.sha256').read_text(), f'{digest}  {archive.name}\n')

    def test_macos_checksums_and_metadata_cover_zip_and_disk_image(self):
        tag = package.validate_tag(None)['version']
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'output'

            def fake_macos(stage, out, version, commit, reuse_qt):
                (out / 'macos-arm64-build.json').write_text(json.dumps(dict(tag=version, platform='macos')))
                archive = out / f'hakone-{version}-macos-arm64.zip'
                archive.write_bytes(b'zip archive')
                archive.with_suffix('.dmg').write_bytes(b'disk image')
                return archive

            def tool(*args):
                if args[0] == 'git':
                    return 'a' * 40
                if args[0] == 'uname':
                    return 'arm64'
                raise AssertionError(args)

            argv = ['package.py', 'macos', temp, '--output', str(output), '--tag', tag]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(package, 'run', side_effect=tool), \
                    mock.patch.object(package, 'macos', side_effect=fake_macos), \
                    mock.patch('sys.stdout'):
                package.main()
            metadata = json.loads((output / 'macos-arm64-build.json').read_text())
            archive, image = (output / f'hakone-{tag}-macos-arm64.{ext}' for ext in ('zip', 'dmg'))
            # The ZIP stays the primary asset that update metadata refers to.
            self.assertEqual(metadata['asset_name'], archive.name)
            self.assertEqual(metadata['package_type'], 'portable-zip')
            self.assertEqual(metadata['sha256'], hashlib.sha256(b'zip archive').hexdigest())
            digest = hashlib.sha256(b'disk image').hexdigest()
            self.assertEqual(metadata['disk_image'], dict(asset_name=image.name, size=len(b'disk image'),
                                                          sha256=digest, package_type='drag-install-dmg'))
            self.assertEqual((output / (image.name + '.sha256')).read_text(), f'{digest}  {image.name}\n')
            self.assertTrue((output / (archive.name + '.sha256')).is_file())

    def test_wrong_version_rejected_before_packaging(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'output'
            argv = ['package.py', 'windows', temp, '--output', str(output), '--tag', 'beta-4']
            with mock.patch.object(sys, 'argv', argv), mock.patch('sys.stderr'), self.assertRaises(SystemExit):
                package.main()
            self.assertFalse(output.exists())
