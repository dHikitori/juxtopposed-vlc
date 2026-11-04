#!/usr/bin/env python3
"""Plan selective release builds and assemble packages with their original provenance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import urllib.error
import urllib.request

from version import ROOT, validate_tag
from release_metadata import append_metadata, release_metadata

MACOS_ARM_RUNNERS = ('macos-15', 'macos-14', 'macos-26')

# Extra downloads of the same app, as (metadata key, extension, label). The
# archive remains the package that update metadata and in-app checks select.
DISK_IMAGE = (('disk_image', 'dmg', 'disk image'),)
INSTALLERS = (('installer', 'exe', 'installer'), ('msi', 'msi', 'MSI'))

TARGETS = {
    # Macs also publish a drag-to-install disk image.
    'macos-arm64': dict(platform='macos', architecture='arm64', extension='zip', extras=DISK_IMAGE,
                        metadata='macos-arm64-build.json', job='macos (macos-15, arm64)'),
    'macos-x86_64': dict(platform='macos', architecture='x86_64', extension='zip', extras=DISK_IMAGE,
                         metadata='macos-x86_64-build.json', job='macos (macos-15-intel, x86_64)'),
    'linux-x86_64': dict(platform='linux', architecture='x86_64', extension='tar.gz', extras=(),
                         metadata='linux-build.json', job='linux'),
    # Windows also publishes NSIS and MSI installers. The windows-smoke job
    # builds the MSI, tests both installers and uploads the complete package.
    'win64': dict(platform='windows', architecture='x86_64', extension='zip', extras=INSTALLERS,
                  metadata='windows-build.json', job='windows-smoke'),
}


def git(*args, root=ROOT):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def api(repository, path, optional=False):
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', repository):
        raise ValueError('Expected an owner/repository name')
    request = urllib.request.Request(f'https://api.github.com/repos/{repository}/{path}', headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'vlc-release-recovery',
        **({'Authorization': f'Bearer {os.environ["GH_TOKEN"]}'} if os.environ.get('GH_TOKEN') else {}),
    })
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if optional and error.code == 404:
            return None
        raise


def pages(repository, path, field):
    result = []
    for page in range(1, 100):
        batch = api(repository, f'{path}?per_page=100&page={page}')[field]
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise ValueError(f'Too many pages in {path}')


def recovery_changes(source, current, root=ROOT, rebuild_targets=()):
    subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', source, current], check=True)
    paths = git('diff', '--no-ext-diff', '--no-renames', '--name-only', source, current, root=root).splitlines()
    # Reusing packages is only supported across release tooling/documentation
    # changes. Application, version and dependency changes require full builds.
    forbidden = [p for p in paths if not (
        p.startswith(('extras/package/release/', 'doc/')) or
        p in ('.github/workflows/beta-release.yml', 'extras/package/win32/build.sh', 'Makefile.am') or
        ('win64' in rebuild_targets and p in ('bin/winvlc.c', 'src/win32/plugin.c')))]
    if 'Makefile.am' in paths:
        diff = git('diff', '--no-ext-diff', '--unified=0', source, current, '--', 'Makefile.am', root=root)
        edits = [line[1:] for line in diff.splitlines()
                 if line.startswith(('+', '-')) and not line.startswith(('+++', '---'))]
        if any(not re.fullmatch(r'\s*extras/package/release/[\w./-]+\s*\\?', line) for line in edits):
            forbidden.append('Makefile.am (changes outside the release-script distribution list)')
    if forbidden:
        if any(p in ('bin/winvlc.c', 'src/win32/plugin.c') for p in forbidden):
            raise ValueError('Windows loader changed: rebuild win64 too (select failed to rebuild all failed targets)')
        raise ValueError('Application/build inputs changed; run a full build: ' + ', '.join(forbidden))
    return paths


def selected_targets(value):
    targets = set() if value == 'none' else set(TARGETS) if value == 'all' else set(value.split(','))
    if not targets.issubset(TARGETS):
        raise ValueError(f'Unknown rebuild target selection: {value}')
    return targets


def run_snapshot(repository, run_id):
    if not re.fullmatch(r'[1-9][0-9]*', run_id):
        raise ValueError('Run IDs must be positive integers')
    run = api(repository, f'actions/runs/{run_id}')
    if (run['head_repository']['full_name'] != repository or
            run['path'] != '.github/workflows/beta-release.yml' or
            run['event'] not in ('push', 'workflow_dispatch')):
        raise ValueError(f'Run {run_id} must be a Build release run from this repository')
    if run['status'] != 'completed':
        raise ValueError(f'Wait for run {run_id} to finish, including Windows startup checks')
    return dict(source_commit=run['head_sha'],
                jobs=pages(repository, f'actions/runs/{run_id}/jobs', 'jobs'),
                artifacts=pages(repository, f'actions/runs/{run_id}/artifacts', 'artifacts'))


def matching_artifacts(snapshot, version, name):
    return [a for a in snapshot['artifacts'] if a['name'] == f'{version}-{name}' and not a['expired']]


def verification_jobs(name):
    jobs = {TARGETS[name]['job']}
    if name == 'macos-arm64':
        jobs.update(f'macos ({runner}, arm64)' for runner in MACOS_ARM_RUNNERS)
    return jobs


def verified(snapshot, name):
    jobs = verification_jobs(name)
    return any(j['name'] in jobs and j['conclusion'] == 'success' for j in snapshot['jobs'])


def reuse_entry(snapshot, run_id, name, version):
    if not verified(snapshot, name):
        matches = [j for j in snapshot['jobs'] if j['name'] in verification_jobs(name)]
        job = ', '.join(j['name'] for j in matches) or TARGETS[name]['job']
        states = ', '.join(j.get('conclusion') or j.get('status', 'unknown') for j in matches) or 'missing'
        raise ValueError(f'Cannot reuse {name}: {job} in run {run_id} is {states}; '
                         'select failed to rebuild all failed targets')
    matches = matching_artifacts(snapshot, version, name)
    if len(matches) != 1:
        raise ValueError(f'Missing, expired or ambiguous artifact for {version}-{name} in run {run_id}')
    return dict(target=name, artifact_id=str(matches[0]['id']), run_id=run_id,
                source_commit=snapshot['source_commit'])


def build_plan(repository, previous_run, target, version, current, root=ROOT, completed_run='',
               macos_arm_runner='macos-15'):
    if macos_arm_runner not in MACOS_ARM_RUNNERS:
        raise ValueError('Unsupported macOS ARM runner: ' + macos_arm_runner)
    if bool(previous_run) != (target != 'all'):
        raise ValueError('Select rebuild targets and provide a previous run ID together, or use all with no run ID')
    if completed_run and not previous_run:
        raise ValueError('A completed build run requires an original previous run ID and target selection')
    selected = selected_targets(target) if target != 'failed' else set()
    reused = []
    source = current
    changes = []
    if previous_run:
        snapshot = run_snapshot(repository, previous_run)
        source = snapshot['source_commit']
        if target == 'failed':
            selected = {name for name in TARGETS if not verified(snapshot, name) or
                        len(matching_artifacts(snapshot, version, name)) != 1}
            if not selected:
                raise ValueError('All targets passed and have artifacts; select a specific target to rebuild')
        changes = recovery_changes(source, current, root, rebuild_targets=selected)
        for name in TARGETS:
            if name in selected:
                continue
            reused.append(reuse_entry(snapshot, previous_run, name, version))
        if completed_run:
            completed = run_snapshot(repository, completed_run)
            # Collected binaries must already include every application change.
            # Only release tooling/docs may differ from the packaging commit.
            recovery_changes(completed['source_commit'], current, root)
            for name in TARGETS:
                if name in selected:
                    reused.append(reuse_entry(completed, completed_run, name, version))
            selected = set()
    macs = [dict(runner=macos_arm_runner if name == 'macos-arm64' else 'macos-15-intel',
                 arch=TARGETS[name]['architecture']) for name in TARGETS
            if name.startswith('macos-') and name in selected]
    return dict(recovery=bool(previous_run), previous_run=previous_run, source_commit=source,
                reuse=bool(reused), rebuild_targets=','.join(name for name in TARGETS if name in selected) or 'none',
                macos_matrix=dict(include=macs), build_macos=bool(macs),
                build_linux='linux-x86_64' in selected,
                build_windows='win64' in selected,
                reuse_matrix=dict(include=reused), changed_paths=changes,
                reuse_origins={entry['target']: dict(source_commit=entry['source_commit'], run_id=entry['run_id'])
                               for entry in reused})


def validate_package(directory, target, version, source):
    spec = TARGETS[target]
    if (directory / spec['metadata']).is_symlink():
        raise ValueError(f'{target}: symbolic links are not valid package metadata')
    metadata = json.loads((directory / spec['metadata']).read_text())
    asset_name = f'hakone-{version}-{target}.{spec["extension"]}'
    for key, expected in dict(tag=version, version=version, platform=spec['platform'],
                              architecture=spec['architecture'], source_commit=source,
                              asset_name=asset_name).items():
        if metadata.get(key) != expected:
            raise ValueError(f'{target}: {key} must be {expected!r}, got {metadata.get(key)!r}')
    validate_asset(directory, f'{target}: archive', metadata, asset_name)
    for key, extension, label in spec['extras']:
        extra = metadata.get(key)
        extra_name = f'hakone-{version}-{target}.{extension}'
        if not isinstance(extra, dict) or extra.get('asset_name') != extra_name:
            # Packages built before this download existed cannot be reused.
            raise ValueError(f'{target}: {label} {extra_name!r} missing from package metadata; rebuild this target')
        validate_asset(directory, f'{target}: {label}', extra, extra_name)
    return metadata


def validate_asset(directory, label, metadata, asset_name):
    archive = directory / asset_name
    if archive.is_symlink() or not archive.is_file() or archive.stat().st_size == 0:
        raise ValueError(f'{label} missing or empty')
    if metadata.get('size') != archive.stat().st_size:
        raise ValueError(f'{label} size mismatch')
    digest = file_digest(archive)
    if metadata.get('sha256') != digest:
        raise ValueError(f'{label} checksum mismatch')
    checksum = archive.with_name(asset_name + '.sha256')
    if checksum.is_symlink() or not checksum.is_file() or checksum.read_text() != f'{digest}  {asset_name}\n':
        raise ValueError(f'{label} checksum file mismatch')


def file_digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def target_asset_names(version, target):
    spec = TARGETS[target]
    extensions = [spec['extension']] + [extension for _, extension, _ in spec['extras']]
    return [f'hakone-{version}-{target}.{extension}' for extension in extensions]


def package_asset_names(version):
    return sorted(name for target in TARGETS for name in target_asset_names(version, target))


def release_assets(directory, version):
    names = ['SHA256SUMS.txt']
    for name in package_asset_names(version):
        names.extend((name, name + '.sha256'))
    paths = [directory / name for name in names]
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Missing or invalid release asset: {path.name}')
    return paths


def assemble(artifacts, output, version, current, run_id, repository, previous_run='',
             target='all', source='', root=ROOT, origins=None):
    if output.exists():
        raise ValueError(f'Output already exists: {output}')
    update_metadata = release_metadata(version, root)
    notes = (root / f'doc/releases/{version}.md').read_text().rstrip()
    append_metadata(notes, update_metadata)  # Reject invalid/duplicate blocks before creating output.
    packages = []
    selected = selected_targets(target)
    # Validate the whole set before producing anything that looks complete.
    for name in TARGETS:
        reused = bool(previous_run) and name not in selected
        if not reused:
            origin = dict(source_commit=current, run_id=run_id)
        elif origins is None:
            origin = dict(source_commit=source, run_id=previous_run)
        else:
            origin = origins.get(name)
        if not origin:
            raise ValueError(f'Missing artifact origin for {name}')
        commit = origin['source_commit']
        if not re.fullmatch(r'[0-9a-f]{40}', commit):
            raise ValueError(f'{name}: expected a full source commit SHA')
        directory = artifacts / f'{version}-{name}'
        metadata = validate_package(directory, name, version, commit)
        packages.append(dict(target=name, source_commit=commit, reused=reused,
                             run_id=str(origin['run_id']),
                             asset_name=metadata['asset_name'], size=metadata['size'],
                             sha256=metadata['sha256']))
        for key, _, _ in TARGETS[name]['extras']:
            extra = metadata[key]
            packages[-1][key] = dict(asset_name=extra['asset_name'], size=extra['size'], sha256=extra['sha256'])
    for package in packages:
        expected = f'{update_metadata["package_prefix"]}-{version}-{package["target"]}.{TARGETS[package["target"]]["extension"]}'
        if package['asset_name'] != expected:
            raise ValueError(f'Update metadata does not match package {package["asset_name"]}')
    output.mkdir(parents=True)
    for package in packages:
        name = package['target']
        directory = artifacts / f'{version}-{name}'
        filenames = [TARGETS[name]['metadata']]
        for asset in [package, *(package[key] for key, _, _ in TARGETS[name]['extras'])]:
            filenames.extend((asset['asset_name'], asset['asset_name'] + '.sha256'))
        if name.startswith('macos-'):
            audit = f'{name}-dependency-audit.json'
            if (directory / audit).is_file():
                filenames.append(audit)
        for filename in filenames:
            path = directory / filename
            if path.is_symlink():
                raise ValueError(f'Artifact contains a symbolic link: {path}')
            shutil.copy2(path, output / filename)
    manifest = dict(version=version, packaging_commit=current, run_id=str(run_id),
                    previous_run_id=str(previous_run) if previous_run else None, packages=packages)
    (output / 'RELEASE_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SOURCE_COMMIT.txt').write_text(current + '\n')
    notes += '\n\n| Package | Binary source commit | Download |\n| --- | --- | --- |\n'
    for package in sorted(packages, key=lambda item: item['target'] != 'win64'):
        links = '[download](#)' + ''.join(f' · [{label}](#)' for _, _, label in TARGETS[package['target']]['extras'])
        notes += f'| {package["target"]} | `{package["source_commit"]}` | {links} |\n'
    (output / 'UPDATE_METADATA.json').write_text(json.dumps(update_metadata, indent=2) + '\n')
    (output / 'RELEASE_NOTES.md').write_text(append_metadata(notes, update_metadata))
    sums = ''.join(f'{file_digest(output / name)}  {name}\n' for name in package_asset_names(version))
    (output / 'SHA256SUMS.txt').write_text(sums)
    return manifest


def check_release_tag(repository, version, current):
    ref = api(repository, f'git/ref/tags/{version}', optional=True)
    if ref is None:
        return  # gh release create --target pins a new tag to this commit.
    obj = ref['object']
    for _ in range(10):
        if obj['type'] != 'tag':
            break
        obj = api(repository, f'git/tags/{obj["sha"]}')['object']
    if obj['type'] != 'commit' or obj['sha'] != current:
        raise ValueError(f'Tag {version} already points elsewhere; existing release tags are never moved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['plan', 'validate', 'assemble', 'publish'])
    parser.add_argument('--previous-run', default=os.environ.get('PREVIOUS_RUN_ID', ''))
    parser.add_argument('--completed-run', default=os.environ.get('COMPLETED_BUILD_RUN_ID', ''))
    parser.add_argument('--target', default=os.environ.get('REBUILD_TARGET') or 'all')
    parser.add_argument('--macos-arm-runner', choices=MACOS_ARM_RUNNERS,
                        default=os.environ.get('MACOS_ARM_RUNNER') or 'macos-15')
    parser.add_argument('--repository', default=os.environ.get('GITHUB_REPOSITORY', ''))
    parser.add_argument('--source', default=os.environ.get('REUSED_SOURCE_COMMIT', ''))
    parser.add_argument('--directory', type=Path, default=Path('release-artifacts'))
    parser.add_argument('--output', type=Path, default=Path('downloads'))
    args = parser.parse_args()
    try:
        version = validate_tag(os.environ.get('RELEASE_TAG') or None)['version']
        release_metadata(version)  # Validate release display fields before starting any build.
        current = git('rev-parse', 'HEAD')
        if args.command == 'plan':
            plan = build_plan(args.repository, args.previous_run, args.target, version, current,
                              completed_run=args.completed_run, macos_arm_runner=args.macos_arm_runner)
            print(json.dumps(plan, indent=2))
            if os.environ.get('CREATE_DRAFT') == 'true':
                check_release_tag(args.repository, version, current)
            with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
                for key, value in plan.items():
                    stream.write(f'{key}={json.dumps(value, separators=(",", ":")) if not isinstance(value, str) else value}\n')
        elif args.command == 'validate':
            validate_package(args.directory, args.target, version, args.source)
        elif args.command == 'assemble':
            assemble(args.directory, args.output, version, current, os.environ['GITHUB_RUN_ID'],
                     args.repository, args.previous_run, args.target, args.source,
                     origins=json.loads(os.environ['REUSE_ORIGINS']) if os.environ.get('REUSE_ORIGINS') else None)
        else:
            check_release_tag(args.repository, version, current)
            flags = ['--prerelease'] if validate_tag(version)['channel'] != 'stable' else []
            assets = [str(p) for p in release_assets(args.output, version)]
            subprocess.run(['gh', 'release', 'create', version, '--target', current, '--draft', *flags,
                            '--title', f'Hakone {version}',
                            '--notes-file', str(args.output / 'RELEASE_NOTES.md'), *assets], check=True)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
