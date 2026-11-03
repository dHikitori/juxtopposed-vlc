#!/usr/bin/env python3
"""Smoke-test an installed release with generated media and ignored VLC preferences."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
from version import verify_binary_version
from addon_runtime_check import verify_installation

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('--expected-version', help='Require the exact compiled release version')
parser.add_argument('--sample', type=Path, help='Use existing media instead of generating it with FFmpeg')
parser.add_argument('--gui', action='store_true', help='Also open the Qt interface for a short audio playback check')
parser.add_argument('--video', action='store_true', help='With --gui, also require video decoding and a Qt video window')
parser.add_argument('--gui-runs', type=int, default=2, help='Repeat GUI startup with the same cache (default: 2)')
parser.add_argument('--require-opengl', action='store_true', help='Require the Qt Quick OpenGL renderer, not the software scene graph')
parser.add_argument('--diagnostics-dir', type=Path, help='Keep process output, including failed starts, in this directory')
args = parser.parse_args()
if args.video and not args.gui:
    parser.error('--video requires --gui')
if args.gui_runs < 1:
    parser.error('--gui-runs must be positive')
if args.require_opengl and not (args.gui and args.video):
    parser.error('--require-opengl requires --gui --video')
if args.diagnostics_dir:
    args.diagnostics_dir.mkdir(parents=True, exist_ok=True)


def save_output(phase, output):
    # TimeoutExpired can contain bytes even when subprocess.run uses text=True.
    if isinstance(output, bytes):
        output = output.decode('utf-8', errors='replace')
    output = output or ''
    if args.diagnostics_dir:
        (args.diagnostics_dir / f'{phase}.log').write_text(output, encoding='utf-8')
    return output


def process_failed(phase, error):
    print(save_output(phase, error.stdout), flush=True)
    if isinstance(error, subprocess.TimeoutExpired):
        reason = f'timed out after {error.timeout} seconds'
    elif error.returncode < 0:
        reason = f'terminated by {signal.Signals(-error.returncode).name}'
    else:
        reason = f'exited with status {error.returncode}'
    raise SystemExit(f'{phase}: {reason}')


def checked_run(command, *, phase, **kwargs):
    try:
        result = subprocess.run(command, **kwargs)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        process_failed(phase, error)
    save_output(phase, result.stdout)
    return result


with tempfile.TemporaryDirectory(prefix='vlc-release-smoke-') as temp:
    state = Path(temp)
    env = dict(os.environ, XDG_CONFIG_HOME=str(state / 'config'),
               XDG_DATA_HOME=str(state / 'data'), XDG_CACHE_HOME=str(state / 'cache'),
               VLC_USERDATA_PATH=str(state / 'userdata'))
    if args.require_opengl:
        env.update(QT_QUICK_BACKEND='rhi', QSG_RHI_BACKEND='opengl',
                   QT_LOGGING_RULES=env.get('QT_LOGGING_RULES', '') +
                   ';qt.scenegraph.general=true;qt.rhi.general=true')
    executable = str(args.executable.resolve())
    try:
        print(save_output('version', verify_binary_version(executable, args.expected_version, env)))
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        process_failed('version', error)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
    modules = checked_run([executable, '--ignore-config', '--list'], phase='modules', env=env, timeout=60,
                             check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for required in ('qt', 'avcodec', 'medialibrary'):
        if not any(line.split() and line.split()[0] == required for line in modules.stdout.splitlines()):
            raise SystemExit(f'Required module missing: {required}')
    try:
        print(verify_installation(args.executable))
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
    sample = args.sample.resolve() if args.sample else state / 'sample.mkv'
    if not args.sample:
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=320x180:rate=24', '-f', 'lavfi', '-i',
                        'sine=frequency=440:sample_rate=48000', '-t', '2',
                        '-c:v', 'ffv1', '-c:a', 'pcm_s16le', str(sample)], check=True)
    # --no-video avoids the AppKit minimal-vout path during a headless Mac check.
    playback = checked_run([executable, '--ignore-config', '--no-media-library',
        '--no-metadata-network-access', '-I', 'dummy', '--no-video', '--aout=dummy',
        '--play-and-exit', '-vv', str(sample)], phase='audio', env=env, timeout=45, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(playback.stdout)
    if 'using audio decoder module' not in playback.stdout:
        raise SystemExit('No audio decoder engaged during playback')
    for attempt in range(args.gui_runs if args.gui else 0):
        print(f'GUI startup {attempt + 1}/{args.gui_runs}', flush=True)
        gui = checked_run([executable, '--ignore-config', '--no-media-library',
            '--no-metadata-network-access', '-I', 'qt', *([] if args.video else ['--no-video']), '--aout=dummy',
            '--no-qt-privacy-ask', '--no-qt-updates-notif', '--play-and-exit',
            *(['--qt-verbose'] if args.require_opengl else []), '-vv', str(sample)], phase=f'gui-{attempt + 1}', env=env,
            timeout=60, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(gui.stdout)
        if 'using interface module "qt"' not in gui.stdout:
            raise SystemExit('Qt interface did not start')
        if args.video:
            for required in ('using video decoder module', 'using vout window module "qt"', 'using vout display module'):
                if required not in gui.stdout:
                    raise SystemExit(f'Video playback missing: {required}')
            if 'using vout display module "vdummy"' in gui.stdout or 'video output display creation failed' in gui.stdout:
                raise SystemExit('Video output did not render to a working display')
        for failure in ('failed to load component', 'qqmlapplicationengine failed', 'is not installed', 'qml loading', 'cannot load the vlc interface', 'svg icon plugin is not found',
                        'failed to load client buffer integration', 'scene graph error:',
                        'failed to initialize graphics backend', 'failed to create opengl context',
                        'egl not available', 'failed to initialize egl display',
                        'egl implementation does not support the wayland platform'):
            if failure in gui.stdout.lower():
                raise SystemExit(f'Qt interface reported: {failure}')
        if args.require_opengl and not (re.search(r'Created OpenGL context|Using existing QOpenGLContext', gui.stdout, re.I)
                                       and re.search(r'Created QRhi\b', gui.stdout, re.I)):
            raise SystemExit('Qt Quick did not confirm a working OpenGL renderer')
print('Release version, required modules and playback smoke checks passed.')
