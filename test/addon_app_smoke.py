#!/usr/bin/env python3
"""Start the full app headless on a private profile, use it, and quit.

The run starts `vlc -I qt` with the offscreen Qt platform and a temporary
home, so it never reads or changes the user's profile. A `cli` control
socket drives playback and quit. The run checks that the bundled add-ons
are prepared, plays a local sample, stops, stays idle, and quits through
the normal interface shutdown.

It prints one JSON object with the measurements: start time, bundled
add-on preparation time, idle CPU time and resident memory of the app and
its helpers, quit time and, with --trace, the number of processes the app
starts. With --windows 2, a second app process shares the profile, and the
run quits the processes in both orders.

With --driver, the app must be built with meson -Daddon-test-driver=true.
The run then also loads the Extensions list, installs the synthetic
discovery fixture package, and opens, browses and searches it twice through
the add-on worker. The fixture has no network, credential or file access.
It then plays a short video in a series folder and waits for the bundled
Episodes panel's button (player.panels@1 and media.siblings).

Exit status 0 means every check passed, 77 means the build has no app.
"""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BUNDLED = ('org.videolan.tvmaze', 'org.videolan.musicbrainz', 'org.videolan.lastfm',
           'org.videolan.french-tv', 'org.videolan.google-images',
           'org.hakone.radio-browser', 'org.hakone.thesubtitledb', 'org.videolan.episodes')
CLOCK = os.sysconf('SC_CLK_TCK')
PAGE = os.sysconf('SC_PAGE_SIZE')


class Failure(Exception):
    pass


def wait_for(what, check, timeout):
    deadline = time.monotonic() + timeout
    while True:
        result = check()
        if result:
            return result
        if time.monotonic() > deadline:
            raise Failure(f'timed out after {timeout} s waiting for {what}')
        time.sleep(0.05)


def process_tree(pid):
    """Return pid and every live descendant."""
    children = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
        except OSError:
            continue
        children.setdefault(int(fields[1]), []).append(int(entry.name))
    tree, pending = [], [pid]
    while pending:
        current = pending.pop()
        tree.append(current)
        pending.extend(children.get(current, []))
    return tree


def cpu_seconds(pids):
    total = 0
    for pid in pids:
        try:
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        except OSError:
            continue
        total += int(fields[11]) + int(fields[12])
    return total / CLOCK


def resident_bytes(pids):
    total = 0
    for pid in pids:
        try:
            total += int(Path(f'/proc/{pid}/statm').read_text().split()[1]) * PAGE
        except OSError:
            continue
    return total


class App:
    def __init__(self, args, home, name, trace, driver=False):
        self.name = name
        # sockaddr_un limits the path length, so the socket gets a short directory.
        self.socket_path = Path(tempfile.mkdtemp(prefix='hk-')) / 'control'
        self.driver_path = self.socket_path.parent / 'driver' if driver else None
        self.driver = None
        self.log_path = home / f'{name}.log'
        self.trace_path = home / f'{name}.trace' if trace else None
        command = [str(args.build / 'bin' / 'vlc'), '-I', 'qt', '--no-one-instance',
                   '--extraintf', 'cli', '--rc-unix', str(self.socket_path),
                   '--aout', 'adummy', '--vout', 'vdummy', '--no-qt-privacy-ask',
                   '--no-metadata-network-access', '-vv', '--file-logging',
                   '--logfile', str(self.log_path), '--log-verbose', '2']
        if args.owner:
            command.append('--metadata-native-owner')
        if trace:
            command = ['strace', '-f', '-qq', '-e', 'trace=execve', '-o',
                       str(self.trace_path)] + command
        self.started = time.monotonic()
        env = dict(args.env)
        if self.driver_path:
            env['VLC_ADDON_TEST_DRIVER_SOCKET'] = str(self.driver_path)
        self.process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                        start_new_session=True)
        self.connection = None

    @property
    def pid(self):
        # Under strace the app is the tracer's child.
        if self.trace_path:
            tree = process_tree(self.process.pid)
            return tree[1] if len(tree) > 1 else self.process.pid
        return self.process.pid

    def log(self):
        try:
            return self.log_path.read_text(errors='replace')
        except OSError:
            return ''

    def connect(self, timeout):
        def attempt():
            if self.process.poll() is not None:
                raise Failure(f'{self.name} exited with {self.process.returncode} at start')
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                connection.connect(str(self.socket_path))
            except OSError:
                connection.close()
                return None
            return connection
        self.connection = wait_for(f'{self.name} control socket', attempt, timeout)
        self.connection.settimeout(10)

    def send(self, command):
        self.connection.sendall(command.encode() + b'\n')

    def ask(self, command, timeout=2.0):
        """Send a cli command and return the text it answers within the timeout."""
        self.connection.settimeout(0.05)
        try:
            while self.connection.recv(65536):
                pass
        except (socket.timeout, BlockingIOError):
            pass
        self.send(command)
        answer = b''
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = self.connection.recv(65536)
                if not chunk:
                    break
                answer += chunk
                if b'\n' in answer.strip(b'> \n'):
                    break
            except socket.timeout:
                if answer.strip(b'> \n'):
                    break
        self.connection.settimeout(10)
        return answer.decode(errors='replace')

    def playing(self):
        answer = self.ask('is_playing')
        return any(line.strip(' >') == '1' for line in answer.splitlines())

    def drive(self, command, timeout):
        """Send one driver command and return its JSON reply."""
        if self.driver is None:
            def attempt():
                connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                try:
                    connection.connect(str(self.driver_path))
                except OSError:
                    connection.close()
                    return None
                return connection
            try:
                self.driver = wait_for('the add-on test driver', attempt, 10)
            except Failure:
                raise Failure('the app has no add-on test driver; build with -Daddon-test-driver=true')
            self.driver.settimeout(timeout)
            self.driver_input = self.driver.makefile('rb')
        self.driver.sendall(command.encode() + b'\n')
        line = self.driver_input.readline()
        if not line:
            raise Failure(f'the add-on test driver closed during "{command}"')
        return json.loads(line)

    def quit(self, timeout):
        started = time.monotonic()
        self.send('quit')
        try:
            code = self.process.wait(timeout)
        except subprocess.TimeoutExpired:
            self.process.send_signal(signal.SIGABRT)
            self.process.wait(10)
            raise Failure(f'{self.name} did not quit within {timeout} s')
        if code != 0:
            raise Failure(f'{self.name} quit with exit status {code}')
        return time.monotonic() - started

    def launches(self):
        if not self.trace_path:
            return None
        programs = []
        for line in self.trace_path.read_text(errors='replace').splitlines():
            if 'execve(' in line and '= 0' in line:
                programs.append(line.split('execve("', 1)[1].split('"', 1)[0])
        return programs[2:]  # strace starts the app; the app may re-exec once.

    def stop(self):
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait()


def worker_process(pid):
    try:
        return Path(f'/proc/{pid}/exe').resolve().name.startswith('vlc-addon-worker')
    except OSError:
        return False


def bundled_ready(store):
    names = {entry.name for entry in store.rglob('*') if entry.is_dir()}
    return all(any(name.startswith(package) for name in names) for package in BUNDLED)


def run(args):
    if not sys.platform.startswith('linux'):
        print('the measurements read /proc', file=sys.stderr)
        return 77
    if not (args.build / 'bin' / 'vlc').exists():
        print(f'{args.build}/bin/vlc does not exist', file=sys.stderr)
        return 77
    results = {}
    home = Path(tempfile.mkdtemp(prefix='hakone-smoke-', dir=args.work))
    os.chmod(home, 0o700)
    for name in ('config', 'data', 'cache', 'runtime'):
        (home / name).mkdir(mode=0o700)
    args.env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / 'config'),
                    XDG_DATA_HOME=str(home / 'data'), XDG_CACHE_HOME=str(home / 'cache'),
                    XDG_RUNTIME_DIR=str(home / 'runtime'), QT_QPA_PLATFORM='offscreen',
                    # Software GL: the same renderer on every host. The WSL D3D12
                    # driver crashes in its own code after the app quits.
                    LIBGL_ALWAYS_SOFTWARE='1',
                    VLC_PLUGIN_PATH=str(args.build / 'modules'),
                    VLC_DATA_PATH=str(args.build / 'share'),
                    VLC_LIBEXEC_PATH=str(args.build / 'bin'))
    args.env.pop('DBUS_SESSION_BUS_ADDRESS', None)
    # No window reaches the desktop: Qt is offscreen, and without a display
    # a video gets no native window either. (Builds without the offscreen
    # compositor fix also crash with a display.)
    args.env.pop('DISPLAY', None)
    args.env.pop('WAYLAND_DISPLAY', None)
    store = home / 'data' / 'vlc' / 'addons'
    apps = []
    try:
        first = App(args, home, 'first', args.trace, args.driver)
        apps.append(first)
        first.connect(args.timeout)
        results['start_seconds'] = round(time.monotonic() - first.started, 3)
        if not args.no_addons:
            wait_for('bundled add-ons', lambda: store.exists() and bundled_ready(store), args.timeout)
            results['bundled_ready_seconds'] = round(time.monotonic() - first.started, 3)

        if not args.no_playback:
            # Ask the player itself, so the check does not depend on log text.
            first.send(f'add file://{args.sample}')
            wait_for('playback', first.playing, args.timeout)
            first.send('stop')
            wait_for('stop', lambda: not first.playing(), args.timeout)
        # Let startup or the input tear down before anything is measured.
        time.sleep(2)

        second = None
        if args.windows == 2:
            second = App(args, home, 'second', False, args.driver)
            apps.append(second)
            second.connect(args.timeout)
            results['second_start_seconds'] = round(time.monotonic() - second.started, 3)

        if args.driver:
            reply = first.drive('extensions', args.timeout)
            if not reply.get('ok'):
                raise Failure(f'the Extensions list did not load: {reply}')
            results['extensions'] = reply
            if second:
                wait_for('the second window joins', lambda: second.drive('extensions', args.timeout).get('ok'),
                         args.timeout)
                rows_before = second.drive('extensions', args.timeout)['rows']
            installed_at = time.monotonic()
            reply = first.drive(f'install {args.fixture}', args.timeout)
            if not reply.get('ok'):
                raise Failure(f'the discovery fixture did not install: {reply}')
            results['fixture_install_ms'] = reply['ms']
            if second:
                # The other window sees the change after it rejoins the profile.
                wait_for('the install in the second window',
                         lambda: second.drive('extensions', args.timeout).get('rows') == rows_before + 1,
                         args.timeout)
                results['second_window_sees_install_ms'] = round((time.monotonic() - installed_at) * 1000)
            package = reply['id']
            # The first open includes the worker start. The second shows
            # whether the worker is reused.
            for key in ('discovery_first', 'discovery_second'):
                reply = first.drive(f'discover {package} {args.query}', args.timeout)
                if not reply.get('ok') or not reply.get('searchItems'):
                    raise Failure(f'the discovery fixture did not answer: {reply}')
                if reply.get('repeatSearchItems') != reply['searchItems']:
                    raise Failure(f'a repeated search showed different rows: {reply}')
                results[key] = {name: reply[name] for name in
                                ('rootsMs', 'searchMs', 'repeatSearchMs', 'rootsItems', 'searchItems')}
            results['workers_after_use'] = sum(
                1 for pid in process_tree(first.pid) if worker_process(pid))

        if args.driver:
            # Each window is its own process, so each one has its own owner.
            for app in [first] + ([second] if second else []):
                report = app.drive('metadata', args.timeout)
                results.setdefault('metadata', {})[app.name] = report
                expected = args.owner
                if not report.get('ok') or report.get('owned') != expected \
                        or report.get('ownerIsThisWindow') != expected or report.get('ownerActive') != expected:
                    raise Failure(f'{app.name}: the metadata owner is not as expected: {report}')
            if args.owner and not args.no_playback and results['metadata']['first']['coreRequests'] < 1:
                raise Failure('playback sent no metadata request to the add-on owner')
            if not args.no_playback and not args.no_addons:
                # The bundled Episodes panel: its button turns on for a file
                # in a series folder (player.panels@1 and media.siblings).
                season = home / 'Series' / 'Show' / 'Season 01'
                season.mkdir(parents=True)
                for episode in (1, 2):
                    shutil.copyfile(args.episode, season / f'Show - S01E0{episode}.mkv')
                first.send(f'add {(season / "Show - S01E01.mkv").as_uri()}')
                wait_for('series playback', first.playing, args.timeout)
                panel = 'org.videolan.episodes/episodes'

                def episodes_button():
                    report = first.drive('panels', args.timeout)
                    results['panels'] = report
                    return any(row.get('id') == panel and row.get('available') for row in report.get('panels', []))
                try:
                    wait_for('the Episodes button', episodes_button, args.timeout)
                except Failure as failure:
                    raise Failure(f'{failure}: {results.get("panels")}')
                first.send('stop')
                wait_for('stop', lambda: not first.playing(), args.timeout)
            results['helper_launches'] = first.drive('stats', args.timeout)['launches']
        windows = [first] + ([second] if second else [])
        trees = [process_tree(app.pid) for app in windows]
        before = [cpu_seconds(pids) for pids in trees]
        time.sleep(args.idle)
        trees = [process_tree(app.pid) for app in windows]
        results['idle_seconds'] = args.idle
        results['idle_cpu_seconds'] = round(cpu_seconds(trees[0]) - before[0], 3)
        results['idle_processes'] = len(trees[0])
        results['idle_workers'] = sum(1 for pid in trees[0] if worker_process(pid))
        results['resident_bytes'] = resident_bytes(trees[0])
        if args.driver:
            # An idle app starts no add-on helper.
            idle_launches = first.drive('stats', args.timeout)['launches'] - results['helper_launches']
            results['idle_helper_launches'] = idle_launches
            if idle_launches:
                raise Failure(f'{idle_launches} add-on helpers started while the app was idle')
        if second:
            results['second_idle_cpu_seconds'] = round(cpu_seconds(trees[1]) - before[1], 3)
            results['second_resident_bytes'] = resident_bytes(trees[1])

        if second:
            order = [second, first] if args.quit_second_first else [first, second]
            results['quit_order'] = [app.name for app in order]
            results['quit_seconds'] = [round(app.quit(args.timeout), 3) for app in order]
        else:
            results['quit_seconds'] = [round(first.quit(args.timeout), 3)]
        if args.trace:
            results['launches'] = first.launches()
            results['launch_count'] = len(results['launches'])
        for app in apps:
            for line in app.log().splitlines():
                if ' error: ' in line and 'addon' in line.lower():
                    raise Failure(f'{app.name} logged an add-on error: {line}')
        results['passed'] = True
        return_code = 0
    except Failure as error:
        results['passed'] = False
        results['failure'] = str(error)
        return_code = 1
    finally:
        for app in apps:
            app.stop()
            if app.driver:
                app.driver_input.close()
                app.driver.close()
            shutil.rmtree(app.socket_path.parent, ignore_errors=True)
            results.setdefault('stderr_tail', {})[app.name] = \
                app.process.stderr.read().decode(errors='replace')[-2000:]
        if args.keep or not results.get('passed'):
            results['home'] = str(home)
        else:
            shutil.rmtree(home, ignore_errors=True)
    if results.get('passed'):
        results.pop('stderr_tail', None)
    print(json.dumps(results, indent=2))
    return return_code


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build', type=Path, required=True, help='meson build directory')
    parser.add_argument('--sample', type=Path,
                        default=Path(__file__).resolve().parent / 'samples' / 'meta.mp3')
    parser.add_argument('--episode', type=Path,
                        default=Path(__file__).resolve().parent / 'samples' / 'episode.mkv',
                        help='a video that is copied into a series folder (one minute, 16x16 FFV1)')
    parser.add_argument('--windows', type=int, choices=(1, 2), default=1)
    parser.add_argument('--quit-second-first', action='store_true')
    parser.add_argument('--trace', action='store_true', help='count process launches with strace')
    parser.add_argument('--driver', action='store_true',
                        help='use the add-on test driver (needs -Daddon-test-driver=true)')
    parser.add_argument('--fixture', type=Path,
                        default=Path(__file__).resolve().parent.parent / 'contrib-addons' / 'examples' / 'discovery-fixture')
    parser.add_argument('--query', default='a', help='discovery fixture search text')
    parser.add_argument('--no-playback', action='store_true',
                        help='do not play the sample, for builds that cannot')
    parser.add_argument('--owner', action='store_true',
                        help='turn on the add-on metadata owner (needs --driver)')
    parser.add_argument('--no-addons', action='store_true',
                        help='do not wait for bundled add-ons, for a build without them')
    parser.add_argument('--no-display', action='store_true',
                        help='kept for older scripts; the app never gets DISPLAY or WAYLAND_DISPLAY')
    parser.add_argument('--idle', type=float, default=30.0, help='idle measurement in seconds')
    parser.add_argument('--timeout', type=float, default=60.0)
    parser.add_argument('--work', type=Path, default=None, help='parent of the temporary home')
    parser.add_argument('--keep', action='store_true', help='keep the temporary home')
    args = parser.parse_args()
    args.build = args.build.resolve()
    args.sample = args.sample.resolve()
    args.episode = args.episode.resolve()
    args.fixture = args.fixture.resolve()
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
