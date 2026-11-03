# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'version.py'
spec = importlib.util.spec_from_file_location('release_version', SCRIPT)
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)


class VersionTests(unittest.TestCase):
    def test_reported_cli_version(self):
        output = ('Hakone version 4.0.0-beta.4 Otto Chriek (4.0.0-dev-40063-ge93a0c3478)\n'
                  'Compiled by [not implemented with meson] on [not implemented with meson]\n'
                  'Compiler: gcc 11.4.0\n')
        self.assertEqual(version.reported_version(output), '4.0.0-beta.4')

    def test_other_banners_and_line_endings(self):
        for output, expected in (
                ('Hakone Media Player 4.0.0-beta.4 Otto Chriek (abcdef)\r\n', '4.0.0-beta.4'),
                ('Hakone version 4.0.0 Otto Chriek\n', '4.0.0'),
                ('Hakone version 4.0.0-beta.10', '4.0.0-beta.10'),
                ('startup notice\nHakone version 4.1.0-rc.1 Otto Chriek\n', '4.1.0-rc.1'),
                # Builds from before the rebrand print VLC banners.
                ('VLC media player 4.0.0-beta.4 Otto Chriek (abcdef)\r\n', '4.0.0-beta.4'),
                ('VLC version 4.0.0 Otto Chriek\n', '4.0.0')):
            with self.subTest(output=output):
                self.assertEqual(version.reported_version(output), expected)

    def test_reject_unrecognized_banner(self):
        for output in ('', 'Build metadata: 4.0.0-beta.4', 'Application failed to start'):
            with self.subTest(output=output), self.assertRaises(ValueError):
                version.reported_version(output)

    def test_probe_matches_application_version(self):
        output = 'Hakone version 4.0.0-beta.4 Otto Chriek (4.0.0-dev-40063-ge93a0c3478)\n'
        result = subprocess.CompletedProcess([], 0, stdout=output)
        executable = Path('/path with spaces/Hakone.app/Contents/MacOS/hakone')
        with mock.patch.object(version.subprocess, 'run', return_value=result) as run:
            self.assertEqual(version.verify_binary_version(executable, '4.0.0-beta.4',
                             {'LC_ALL': 'fr_FR.UTF-8', 'LANGUAGE': 'fr', 'TEST_MARKER': 'kept'}), output)
        self.assertEqual(run.call_args.args[0], [str(executable), '--version'])
        self.assertEqual(run.call_args.kwargs['env'],
                         {'LC_ALL': 'C', 'LANGUAGE': 'C', 'TEST_MARKER': 'kept'})
        self.assertTrue(run.call_args.kwargs['check'])

    def test_probe_rejects_wrong_version_and_changeset_matches(self):
        for actual in ('4.0.0-beta.3', '4.0.0-beta.40', '4.0.0', '3.0.99'):
            with self.subTest(actual=actual):
                result = subprocess.CompletedProcess([], 0, stdout=
                    f'Hakone version {actual} Otto Chriek (4.0.0-beta.4-123-gabcdef)\n')
                with mock.patch.object(version.subprocess, 'run', return_value=result), self.assertRaises(ValueError):
                    version.verify_binary_version('hakone', '4.0.0-beta.4')

    def test_windows_probe_skips_interactive_help_console(self):
        result = subprocess.CompletedProcess([], 0, stdout='Hakone version 4.0.0-beta.4 Otto Chriek\n')
        with mock.patch.object(version.os, 'name', 'nt'), \
             mock.patch.object(version.subprocess, 'run', return_value=result) as run:
            version.verify_binary_version('hakone.exe', '4.0.0-beta.4', {})
        self.assertTrue(run.call_args.kwargs['env']['PWD'])

    def test_executable_cli_uses_shared_probe(self):
        current = version.source_version()['version']
        argv = ['version.py', current, '--executable', '/path with spaces/hakone']
        banner = f'Hakone version {current} Otto Chriek\n'
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(version, 'verify_binary_version', return_value=banner) as probe, \
             mock.patch('sys.stdout', new_callable=io.StringIO) as output:
            version.main()
        probe.assert_called_once_with(Path('/path with spaces/hakone'), current)
        self.assertEqual(output.getvalue(), banner)

    def test_required_modules_detect_loader_failures_before_gui_startup(self):
        executable = Path('/release/hakone.exe')
        output = '  qt Qt interface\n  avcodec Video decoder\n  medialibrary Media library\n'
        with mock.patch.object(version.subprocess, 'run', return_value=
                               subprocess.CompletedProcess([], 0, stdout=output)) as run:
            version.verify_required_modules(executable, ['qt', 'avcodec', 'medialibrary'])
        self.assertEqual(run.call_args.args[0], [str(executable), '--ignore-config', '--list'])
        self.assertEqual(run.call_args.kwargs['timeout'], 60)
        output = 'cannot load plug-in libqt_plugin.dll: error 126\n avcodec Video decoder\n'
        with mock.patch.object(version.subprocess, 'run', return_value=
                               subprocess.CompletedProcess([], 0, stdout=output)):
            with self.assertRaisesRegex(ValueError, 'Required modules missing: qt, medialibrary'):
                version.verify_required_modules(executable, ['qt', 'avcodec', 'medialibrary'])

    def test_windows_module_probe_captures_console_output(self):
        result = subprocess.CompletedProcess([], 0, stdout='qt Qt interface\n')
        with mock.patch.object(version.os, 'name', 'nt'), \
                mock.patch.object(version.subprocess, 'run', return_value=result) as run:
            version.verify_required_modules('hakone.exe', ['qt'], {})
        self.assertTrue(run.call_args.kwargs['env']['PWD'])

    def test_cli_can_require_loadable_modules(self):
        current = version.source_version()['version']
        argv = ['version.py', current, '--executable', 'hakone.exe', '--require-module', 'qt',
                '--require-module', 'medialibrary']
        with mock.patch.object(sys, 'argv', argv), \
                mock.patch.object(version, 'verify_binary_version', return_value='banner\n'), \
                mock.patch.object(version, 'verify_required_modules') as modules, \
                mock.patch('sys.stdout', new_callable=io.StringIO):
            version.main()
        modules.assert_called_once_with(Path('hakone.exe'), ['qt', 'medialibrary'])

    def test_source_matches_next_release(self):
        current = version.source_version()['version']
        self.assertEqual(version.validate_tag(current)['version'], current)

    def test_supported_tags(self):
        for tag in ('4.0.0', '4.0.0-beta.4', '4.0.0-beta.10', '4.1.0-rc.1', '5.0.0-alpha.2'):
            self.assertEqual(version.parse_version(tag)['version'], tag)
        for tag in ('beta-4', '4.0', '04.0.0', '4.0.0-beta-4', '4.0.0-beta.0', '4.0.0-beta.04', '4.0.0-dev'):
            with self.assertRaises(ValueError, msg=tag):
                version.parse_version(tag)

    def test_mismatched_tag(self):
        with self.assertRaises(ValueError):
            version.validate_tag('999999.0.0-beta.999999')

    def test_mismatched_build_definitions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'meson.build').write_text("project('VLC', version: '4.0.0-beta.4')")
            (root / 'configure.ac').write_text('AC_INIT([vlc], [4.0.0-beta.3])')
            with self.assertRaises(ValueError):
                version.source_version(root)

    def test_macos_bundle_versions(self):
        self.assertEqual(version.bundle_version('4.0.0-beta.4'), '4.0.0b4')
        self.assertEqual(version.bundle_version('4.0.0-rc.1'), '4.0.0fc1')
        self.assertEqual(version.bundle_version('4.0.0'), '4.0.0')
        with self.assertRaises(ValueError):
            version.bundle_version('4.0.0-beta.256')


if __name__ == '__main__':
    unittest.main()
