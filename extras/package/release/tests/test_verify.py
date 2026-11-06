# SPDX-License-Identifier: GPL-2.0-or-later
import contextlib
import io
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import addon_runtime_check


class VerifyTests(unittest.TestCase):
    def setUp(self):
        # Worker protocol/staging has dedicated tests. These cases exercise
        # playback/diagnostic handling without pretending to launch a Mac app.
        check = mock.patch.object(addon_runtime_check, 'verify_installation', return_value='worker fixture checked')
        check.start()
        self.addCleanup(check.stop)

    def test_reported_banner_reaches_playback_checks(self):
        def command(args, **kwargs):
            if '--version' in args:
                output = 'Hakone version 4.0.0-beta.4 Otto Chriek (4.0.0-dev-40063-ge93a0c3478)\n'
            elif '--list' in args:
                output = 'qt Qt interface\navcodec FFmpeg decoder\nmedialibrary Media library\n'
            else:
                output = ('using audio decoder module "avcodec"\n'
                          'using interface module "qt"\n'
                          'using video decoder module "avcodec"\n'
                          'using vout window module "qt"\n'
                          'using vout display module "opengl"\n')
            return subprocess.CompletedProcess(args, 0, stdout=output)

        argv = ['verify.py', '/path with spaces/hakone', '--expected-version', '4.0.0-beta.4',
                '--sample', '/sample.mkv', '--gui', '--video']
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(subprocess, 'run', side_effect=command) as run, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        self.assertEqual(run.call_count, 5)
        gui_calls = [call for call in run.call_args_list if '-I' in call.args[0] and 'qt' in call.args[0]]
        self.assertEqual(len(gui_calls), 2)
        self.assertEqual(gui_calls[0].kwargs['env'], gui_calls[1].kwargs['env'])
        self.assertIn('playback smoke checks passed', output.getvalue())

    def test_update_notification_option_follows_the_build(self):
        gui = [args for args in self.run_gui(['', '']) if '-I' in args and 'qt' in args]
        self.assertTrue(all('--no-qt-updates-notif' in args for args in gui))
        gui = [args for args in self.run_gui(['', ''], without_update_check=True) if '-I' in args and 'qt' in args]
        self.assertEqual(len(gui), 2)
        self.assertFalse(any('--no-qt-updates-notif' in args for args in gui))

    def run_gui(self, outputs, require_opengl=False, diagnostics=None, capture=None, without_update_check=False):
        calls = []
        def command(args, **kwargs):
            calls.append(args)
            if '--version' in args:
                text = 'Hakone version 4.0.0-beta.4 Otto Chriek\n'
            elif '--list' in args:
                text = 'qt Qt\navcodec FFmpeg\nmedialibrary Library\n'
            elif 'qt' not in args:
                text = 'using audio decoder module "avcodec"\n'
            else:
                result = outputs.pop(0)
                if isinstance(result, Exception):
                    raise result
                text = ('using interface module "qt"\nusing video decoder module "avcodec"\n'
                        'using vout window module "qt"\nusing vout display module "gl"\n') + result
            return subprocess.CompletedProcess(args, 0, stdout=text)
        argv = ['verify.py', '/hakone', '--sample', '/sample.mkv', '--gui', '--video']
        if require_opengl:
            argv.append('--require-opengl')
        if diagnostics:
            argv.extend(['--diagnostics-dir', str(diagnostics)])
        if without_update_check:
            argv.append('--without-update-check')
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(subprocess, 'run', side_effect=command), \
             contextlib.redirect_stdout(capture if capture is not None else io.StringIO()):
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        return calls

    def test_second_start_abort_keeps_the_actual_crash_output(self):
        error = subprocess.CalledProcessError(-6, ['/hakone'], output='fatal graphics diagnostic\n')
        with tempfile.TemporaryDirectory() as temp:
            diagnostics = Path(temp) / 'logs'
            output = io.StringIO()
            with self.assertRaisesRegex(SystemExit, 'gui-2: terminated by SIGABRT'):
                self.run_gui(['', error], diagnostics=diagnostics, capture=output)
            self.assertIn('fatal graphics diagnostic', output.getvalue())
            self.assertIn('GUI startup 2/2', output.getvalue())
            self.assertIn('using interface module', (diagnostics / 'gui-1.log').read_text())
            self.assertEqual((diagnostics / 'gui-2.log').read_text(), error.stdout)
            self.assertTrue((diagnostics / 'modules.log').is_file())

    def test_timeout_keeps_partial_bytes_and_fails_verification(self):
        output = io.StringIO()
        error = subprocess.TimeoutExpired(['/hakone'], 60, output=b'last output before hang\n')
        with self.assertRaisesRegex(SystemExit, 'gui-1: timed out after 60 seconds'):
            self.run_gui([error], capture=output)
        self.assertIn('last output before hang', output.getvalue())

    def test_nonzero_exit_is_not_retried_or_reported_as_success(self):
        output = io.StringIO()
        error = subprocess.CalledProcessError(1, ['/hakone'], output='startup error\n')
        with self.assertRaisesRegex(SystemExit, 'gui-1: exited with status 1'):
            self.run_gui([error, ''], capture=output)
        self.assertNotIn('GUI startup 2/2', output.getvalue())
        self.assertNotIn('checks passed', output.getvalue())

    def test_second_start_rejects_reported_graphics_failures_even_with_exit_zero(self):
        for message in ('Failed to load client buffer integration: "wayland-egl"',
                        'Compositor: Scene Graph Error: QQuickWindow::ContextNotAvailable',
                        'Failed to initialize graphics backend for OpenGL.'):
            with self.subTest(message=message), self.assertRaisesRegex(SystemExit, 'Qt interface reported'):
                self.run_gui(['', message])

    def test_rejects_egl_initialization_failures_even_with_exit_zero(self):
        for message in ('EGL not available',
                        'Failed to initialize EGL display',
                        'EGL implementation does not support the Wayland platform'):
            for attempt in (1, 2):
                with self.subTest(message=message, attempt=attempt):
                    output = io.StringIO()
                    with self.assertRaisesRegex(SystemExit, 'Qt interface reported'):
                        self.run_gui([''] * (attempt - 1) + [message], capture=output)
                    self.assertIn(message, output.getvalue())
                    self.assertNotIn('checks passed', output.getvalue())

    def test_requesting_opengl_must_prove_a_working_context(self):
        with self.assertRaisesRegex(SystemExit, 'did not confirm a working OpenGL'):
            self.run_gui(['Creating QRhi with backend OpenGL'], True)
        self.run_gui(['Created OpenGL context QSurfaceFormat\nCreated QRhi 0x123\n'] * 2, True)
        self.run_gui(['Using existing QOpenGLContext 0x123\nCreated QRhi 0x456\n'] * 2, True)

    def test_wrong_version_stops_before_module_and_playback_checks(self):
        result = subprocess.CompletedProcess([], 0, stdout='Hakone version 4.0.0-beta.3 Otto Chriek\n')
        argv = ['verify.py', '/hakone', '--expected-version', '4.0.0-beta.4']
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(subprocess, 'run', return_value=result) as run, \
             self.assertRaisesRegex(SystemExit, 'expected 4.0.0-beta.4; got 4.0.0-beta.3'):
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        self.assertEqual(run.call_count, 1)
