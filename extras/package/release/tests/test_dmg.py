# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location('release_dmg', SCRIPTS / 'dmg.py')
dmg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dmg)


def png_size(path):
    header = path.read_bytes()[:24]
    assert header[:8] == b'\x89PNG\r\n\x1a\n' and header[12:16] == b'IHDR'
    return struct.unpack('>II', header[16:24])


class DiskImageTests(unittest.TestCase):
    def make_app(self, root):
        app = root / 'Hakone.app'
        (app / 'Contents/MacOS').mkdir(parents=True)
        (app / 'Contents/MacOS/hakone').write_bytes(b'binary')
        (app / 'Contents/MacOS/hakone').chmod(0o755)
        (app / 'Contents/Info.plist').write_text('plist')
        (app / 'Contents/Frameworks').mkdir()
        (app / 'Contents/Frameworks/Current').symlink_to('A')
        return app

    def make_volume(self, root, app_source):
        volume = root / 'volume'
        volume.mkdir()
        subprocess.run(['cp', '-R', str(app_source), str(volume / 'Hakone.app')], check=True)
        (volume / 'Applications').symlink_to('/Applications')
        for name in ('.DS_Store', '.VolumeIcon.icns', '.background.tiff'):
            (volume / name).write_bytes(b'layout')
        return volume

    def test_settings_lay_out_both_icons_inside_the_background(self):
        options = dict(defines=dict(app='/build/Hakone.app', background='/art/background.png',
                                    icon='/share/hakone.icns'))
        settings = dmg.ART / 'settings.py'
        exec(compile(settings.read_text(), str(settings), 'exec'), options, options)
        self.assertEqual(options['files'], ['/build/Hakone.app'])
        self.assertEqual(options['symlinks'], {'Applications': '/Applications'})
        # LZFSE images open on macOS 10.11 and later.
        self.assertEqual(options['format'], 'ULFO')
        self.assertEqual(options['icon'], '/share/hakone.icns')
        for flag in ('show_status_bar', 'show_toolbar', 'show_sidebar', 'show_pathbar', 'show_tab_view'):
            self.assertFalse(options[flag], flag)
        width, height = options['window_rect'][1]
        self.assertEqual(png_size(dmg.ART / 'background.png'), (width, height))
        self.assertEqual(png_size(dmg.ART / 'background@2x.png'), (width * 2, height * 2))
        self.assertEqual(set(options['icon_locations']), {'Hakone.app', 'Applications'})
        half = options['icon_size'] / 2
        for x, y in options['icon_locations'].values():
            # Leave room for a title bar and the label under the icon.
            self.assertTrue(half <= x <= width - half and half <= y <= height - half - 60)

    def test_build_runs_pinned_dmgbuild_with_volume_name_and_artwork(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = self.make_app(root)
            image = root / 'hakone-1.0.0-macos-arm64.dmg'
            image.write_bytes(b'stale')
            calls = []

            def execute(args, **kwargs):
                self.assertTrue(kwargs['check'])
                self.assertFalse(image.exists())  # A stale image is never reused.
                calls.append(args)
                image.write_bytes(b'image')

            with mock.patch.object(dmg, 'dmgbuild_command', return_value='/tools/dmgbuild'), \
                    mock.patch.object(dmg.subprocess, 'run', side_effect=execute):
                self.assertEqual(dmg.build(app, image), image)
            args = calls[0]
            self.assertEqual(args[:3], ['/tools/dmgbuild', '-s', str(dmg.ART / 'settings.py')])
            self.assertIn(f'app={app}', args)
            self.assertIn(f'background={dmg.ART / "background.png"}', args)
            self.assertIn(f'icon={dmg.ROOT / "share/icons/hakone.icns"}', args)
            self.assertEqual(args[-2:], ['Hakone', str(image)])
            with mock.patch.object(dmg, 'dmgbuild_command', return_value='/tools/dmgbuild'), \
                    mock.patch.object(dmg.subprocess, 'run'):
                with self.assertRaisesRegex(RuntimeError, 'did not create'):
                    dmg.build(app, image)
                with self.assertRaises(ValueError):
                    dmg.build(root / 'Other.app', image)

    def test_dmgbuild_lookup_prefers_explicit_tool_and_fails_clearly(self):
        with tempfile.TemporaryDirectory() as temp:
            tool = Path(temp) / 'dmgbuild'
            tool.write_text('#!/bin/sh\n')
            tool.chmod(0o755)
            with mock.patch.dict(os.environ, {'DMGBUILD': str(tool)}):
                self.assertEqual(dmg.dmgbuild_command(), str(tool))
            with mock.patch.dict(os.environ, {'DMGBUILD': '', 'PATH': temp}), \
                    mock.patch.object(dmg, 'ROOT', Path(temp) / 'missing'):
                self.assertEqual(dmg.dmgbuild_command(), str(tool))
            with mock.patch.dict(os.environ, {'DMGBUILD': '', 'PATH': ''}), \
                    mock.patch.object(dmg, 'ROOT', Path(temp) / 'missing'), \
                    self.assertRaisesRegex(RuntimeError, 'requirements.txt'):
                dmg.dmgbuild_command()

    def test_mounted_volume_must_hold_the_verified_app_and_applications_link(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference = self.make_app(root)
            volume = self.make_volume(root, reference)
            with mock.patch.object(dmg.subprocess, 'run') as codesign:
                self.assertEqual(dmg.check_volume(volume, reference), volume / 'Hakone.app')
            self.assertEqual(codesign.call_args.args[0][:2], ['codesign', '--verify'])
            self.assertTrue(codesign.call_args.kwargs['check'])

        def different_binary(volume):
            (volume / 'Hakone.app/Contents/MacOS/hakone').write_bytes(b'other')

        def not_executable(volume):
            (volume / 'Hakone.app/Contents/MacOS/hakone').chmod(0o644)

        def extra_file(volume):
            (volume / 'Hakone.app/Contents/extra').write_text('extra')

        def retargeted_link(volume):
            (volume / 'Hakone.app/Contents/Frameworks/Current').unlink()
            (volume / 'Hakone.app/Contents/Frameworks/Current').symlink_to('B')

        def wrong_applications(volume):
            (volume / 'Applications').unlink()
            (volume / 'Applications').symlink_to('/tmp')

        def visible_extra(volume):
            (volume / 'README.txt').write_text('read me')

        def missing_layout(volume):
            (volume / '.DS_Store').unlink()

        def missing_app(volume):
            subprocess.run(['rm', '-rf', str(volume / 'Hakone.app')], check=True)

        for change in (different_binary, not_executable, extra_file, retargeted_link,
                       wrong_applications, visible_extra, missing_layout, missing_app):
            with self.subTest(change=change.__name__), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                reference = self.make_app(root)
                volume = self.make_volume(root, reference)
                change(volume)
                with mock.patch.object(dmg.subprocess, 'run'), self.assertRaises(RuntimeError):
                    dmg.check_volume(volume, reference)


if __name__ == '__main__':
    unittest.main()
