#!/usr/bin/env python3
"""Validate release tags against both build systems before building or packaging."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PATTERN = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(alpha|beta|rc)\.([1-9][0-9]*))?')


def parse_version(value):
    match = PATTERN.fullmatch(value)
    if not match:
        raise ValueError('Expected MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-beta.N (also alpha.N / rc.N)')
    major, minor, patch = map(int, match.group(1, 2, 3))
    stage, number = match.group(4, 5)
    return dict(version=value, major=major, minor=minor, patch=patch,
                channel=stage or 'stable', number=int(number) if number else None,
                prerelease=f'{stage}.{number}' if stage else '')


def source_version(root=ROOT):
    """Hakone's own version. LibVLC keeps the upstream project version."""
    meson = re.search(r"^hakone_version = '([^']+)'$", (root / 'meson.build').read_text(), re.MULTILINE)
    configure = (root / 'configure.ac').read_text()
    autoconf = re.search(r'^HAKONE_VERSION=(.*)$', configure, re.MULTILINE)
    if not meson or not autoconf or meson[1] != autoconf[1]:
        raise ValueError('Meson and Autoconf Hakone versions must agree')
    info = parse_version(meson[1])
    for name, expected in (('MAJOR', info['major']), ('MINOR', info['minor']),
                           ('REVISION', info['patch']), ('DEV', info['prerelease'])):
        value = re.search(rf'^HAKONE_VERSION_{name}=(.*)$', configure, re.MULTILINE)
        if not value or value[1].strip("\"'") != str(expected):
            raise ValueError(f'Autoconf HAKONE_VERSION_{name} must match {info["version"]}')
    info['generation'] = source_generation(root)
    return info


def source_generation(root=ROOT):
    meson = re.search(r'^vlc_update_generation = ([0-9]+)$', (root / 'meson.build').read_text(), re.MULTILINE)
    configure = re.search(r'^UPDATE_GENERATION=([0-9]+)$', (root / 'configure.ac').read_text(), re.MULTILINE)
    if not meson or not configure or meson[1] != configure[1] or int(meson[1]) > 2147483647:
        raise ValueError('Meson and Autoconf update generations must agree and be between 0 and 2147483647')
    return int(meson[1])


def validate_tag(tag, root=ROOT):
    info = source_version(root)
    if tag is not None and tag != info['version']:
        raise ValueError(f'Release tag {tag!r} does not match compiled version {info["version"]!r}')
    return info


def bundle_version(tag):
    info = parse_version(tag)
    core = f'{info["major"]}.{info["minor"]}.{info["patch"]}'
    if info['channel'] == 'stable':
        return core
    if info['number'] > 255:
        raise ValueError('macOS bundle prerelease numbers must be between 1 and 255')
    return core + {'alpha': 'a', 'beta': 'b', 'rc': 'fc'}[info['channel']] + str(info['number'])


def reported_version(output):
    # The CLI uses "Hakone version"; accept the product-name banner and the
    # pre-rebrand "VLC" banners too.
    # Match the application token, never a version in the parenthesized changeset.
    banner = re.search(r'^(?:Hakone|VLC) (?:version|[Mm]edia [Pp]layer) (\S+)(?=\s|$)', output, re.MULTILINE)
    if not banner:
        raise ValueError('Could not read the application version from Hakone --version output')
    return banner[1]


def probe_environment(env=None):
    # VLC translates the CLI banner. Force its language only for this probe.
    probe_env = dict(os.environ if env is None else env, LC_ALL='C', LANGUAGE='C')
    if os.name == 'nt':
        # VLC's CLI checks PWD before opening a console, redirecting stdout to
        # vlc-help.txt, and waiting for RETURN. Keep this subprocess capturable.
        probe_env.setdefault('PWD', os.getcwd())
    return probe_env


def verify_binary_version(executable, expected=None, env=None):
    result = subprocess.run([str(executable), '--version'], env=probe_environment(env),
                            timeout=30, check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    actual = reported_version(result.stdout)
    if expected is not None and actual != expected:
        raise ValueError(f'Installed application version does not match the release tag '
                         f'(expected {expected}; got {actual})')
    return result.stdout


def verify_required_modules(executable, required, env=None):
    result = subprocess.run([str(executable), '--ignore-config', '--list'],
                            env=probe_environment(env), timeout=60, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    loaded = {line.split()[0] for line in result.stdout.splitlines() if line.split()}
    missing = [name for name in required if name not in loaded]
    if missing:
        raise ValueError(f'Required modules missing: {", ".join(missing)}\n{result.stdout}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', nargs='?', default=os.environ.get('RELEASE_TAG') or None)
    parser.add_argument('--executable', type=Path, help='Also verify the installed Hakone executable')
    parser.add_argument('--require-module', action='append', default=[], help='Require a loadable module in --list output')
    args = parser.parse_args()
    try:
        expected = validate_tag(args.tag)['version']
        if args.executable:
            print(verify_binary_version(args.executable, expected), end='')
            if args.require_module:
                verify_required_modules(args.executable, args.require_module)
                print('Required modules loaded: ' + ', '.join(args.require_module))
        else:
            if args.require_module:
                parser.error('--require-module needs --executable')
            print(expected)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
