#!/usr/bin/env python3
"""Package a staged Meson installation; reject external Mach-O dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import dmg
from version import validate_tag, bundle_version

ROOT = Path(__file__).resolve().parents[3]


def run(*args, **kwargs):
    return subprocess.check_output([str(a) for a in args], text=True, **kwargs).strip()


def copy(source, dest):
    if source.is_dir():
        shutil.copytree(source, dest, symlinks=True, dirs_exist_ok=True)
    else:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe')


def dependencies(binary):
    # otool -L also lists a dylib's own LC_ID_DYLIB; it isn't a dependency.
    commands = run('otool', '-l', binary)
    return re.findall(r'cmd LC_(?:LOAD|LOAD_WEAK|REEXPORT|LOAD_UPWARD)_DYLIB\n\s*cmdsize \d+\n\s*name (.*?) \(offset', commands)


def sign_macos_adhoc(app, all_binaries):
    """Seal nested code before its parent, without certificates or a timestamp service."""
    bundles = {app}
    for path in app.rglob('*'):
        if not path.is_symlink() and path.is_dir() and path.suffix in (
                '.framework', '.app', '.xpc', '.appex', '.bundle', '.plugin'):
            bundles.add(path)
    main_executables = set()
    code_bundles = set()
    for bundle in bundles:
        for relative in ('Contents/Info.plist', 'Resources/Info.plist', 'Info.plist'):
            plist = bundle / relative
            if not plist.is_file():
                continue
            with plist.open('rb') as stream:
                executable = plistlib.load(stream).get('CFBundleExecutable')
            if executable:
                code_bundles.add(bundle)
                for directory in (bundle / 'Contents/MacOS', bundle):
                    main = directory / executable
                    if main.is_file():
                        main_executables.add(main.resolve())
                break
    # Signing Contents/MacOS/hakone directly can seal the app before its nested
    # libraries are signed. Framework executables must also be signed via their
    # bundle, including versioned frameworks whose top-level executable is a link.
    targets = code_bundles | {p for p in all_binaries if p.resolve() not in main_executables}
    targets.add(app)
    for target in sorted(targets, key=lambda p: (-len(p.parts), str(p))):
        subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none',
                        '--verbose=2', str(target)], check=True)
    # Diagnostics go to the build log. A malformed ad-hoc signature can prevent
    # execution on Apple Silicon, so it must still fail packaging.
    subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app)], check=True)


def macos(stage, output, tag, commit, reuse_qt=False):
    arch = run('uname', '-m')
    app = output / f'macos-{arch}' / 'Hakone.app'
    if app.exists() and not reuse_qt:
        raise SystemExit(f'Remove the old package before rebuilding: {app}')
    contents = app / 'Contents'
    frameworks = contents / 'Frameworks'
    binaries = contents / 'MacOS'
    resources = contents / 'Resources'
    for directory in (frameworks, binaries, resources):
        directory.mkdir(parents=True, exist_ok=True)
    qt = Path(os.environ.get('QT_ROOT_DIR') or run('brew', '--prefix', 'qt'))
    if reuse_qt:
        with (frameworks / 'QtCore.framework/Resources/Info.plist').open('rb') as stream:
            existing_qt = plistlib.load(stream)['CFBundleVersion']
        if existing_qt != run(qt / 'bin/qmake', '-query', 'QT_VERSION'):
            raise SystemExit('Cannot reuse a deployment from a different Qt version')
    copy(stage / 'bin/hakone', binaries / 'hakone')
    # Copy the worker before the dependency checks, Qt deployment and code signature.
    copy(stage / 'bin/vlc-addon-worker', binaries / 'vlc-addon-worker')
    for helper in ('vlc-preparser', 'vlc-cache-gen', 'vlc-qt-check'):
        copy(stage / 'libexec/vlc' / helper, binaries / helper)
    # Deploy Qt before adding hundreds of unrelated VLC modules: macdeployqt
    # otherwise repeatedly rewrites/scans every module for each Qt dependency.
    copy(stage / 'lib/vlc/plugins/gui/libqt_plugin.dylib', frameworks / 'plugins/gui/libqt_plugin.dylib')
    for library in (stage / 'lib').glob('libvlc*.dylib'):
        # Dereference aliases: install names are rewritten below.
        shutil.copy2(library, frameworks / library.name, follow_symlinks=True)
    copy(stage / 'share/vlc', resources / 'share')
    copy(stage / 'share/locale', resources / 'share/locale')
    copy(ROOT / 'share/vlc512x512.png', resources / 'hakone.png')
    copy(ROOT / 'share/icons/hakone.icns', resources / 'hakone.icns')
    for name in ('COPYING', 'COPYING.LIB', 'AUTHORS', 'THANKS', 'README.md'):
        copy(ROOT / name, resources / name)
    copy(ROOT / 'modules/gui/qt/fonts/LICENSE.txt', resources / 'Inter-LICENSE.txt')
    (resources / 'SOURCE_COMMIT.txt').write_text(commit + '\n')
    # The Qt plugin has greater interface priority than minimal_macosx.
    # Keep a native main executable so NSBundle, Finder file events and AppKit work.
    info = dict(CFBundleExecutable='hakone', CFBundleName='Hakone',
                CFBundleDisplayName='Hakone', CFBundlePackageType='APPL',
                CFBundleIdentifier='org.hakone.hakone', CFBundleIconFile='hakone.icns',
                CFBundleVersion=bundle_version(tag),
                CFBundleShortVersionString=tag.split('-')[0], NSHighResolutionCapable=True,
                NSMicrophoneUsageDescription='Hakone can play and record audio from input devices.',
                NSCameraUsageDescription='Hakone can play and record video from input devices.')
    with (contents / 'Info.plist').open('wb') as stream:
        plistlib.dump(info, stream)
    deploy = qt / 'bin/macdeployqt'
    if not deploy.exists():
        deploy = qt / 'libexec/macdeployqt'
    # Qt dependencies live in a dynamically loaded VLC module, not the launcher.
    plugins = [frameworks / 'plugins/gui/libqt_plugin.dylib']
    if not reuse_qt:
        subprocess.run([str(deploy), str(app), '-always-overwrite', '-verbose=1',
                        f'-libpath={qt / "lib"}',
                        f'-qmldir={ROOT / "modules/gui/qt"}',
                        *[f'-executable={p}' for p in plugins],
                        *[f'-executable={p}' for p in binaries.iterdir()]], check=True)
    # VLC's library uses SQLite directly. Optional Qt database drivers link to
    # build-machine ODBC/PostgreSQL/Mimer clients and aren't used by this app.
    for driver in (contents / 'PlugIns/sqldrivers').glob('*.dylib'):
        if driver.name != 'libqsqlite.dylib':
            driver.unlink()
    copy(stage / 'lib/vlc/plugins', frameworks / 'plugins')
    # macdeployqt handles Qt/QML; close the remaining codec/Homebrew dependencies.
    origins = {}
    processed = set()
    while True:
        pending = [p for p in contents.rglob('*') if macho(p) and p not in processed]
        if not pending:
            break
        for binary in pending:
            processed.add(binary)
            binary.chmod(binary.stat().st_mode | 0o200)
            changes = []
            for dep in dependencies(binary):
                staged_vlc = dep.startswith('/usr/lib/libvlc')
                if dep.startswith(('/System/Library/', '/usr/lib/')) and not staged_vlc:
                    continue
                if dep.startswith('@rpath/'):
                    relative = dep.removeprefix('@rpath/')
                    if (frameworks / relative).exists():
                        continue
                    commands = run('otool', '-l', binary)
                    identities = re.findall(r'cmd LC_ID_DYLIB\n\s*cmdsize \d+\n\s*name (.*?) \(offset', commands)
                    origin = origins.get(binary)
                    if origin is None and identities and identities[0].startswith('/'):
                        origin = Path(identities[0])
                    candidates = [qt / 'lib' / relative]
                    if origin:
                        candidates.append(origin.parent / relative)
                    rpaths = re.findall(r'cmd LC_RPATH\n\s*cmdsize \d+\n\s*path (.*?) \(offset', commands)
                    candidates.extend(Path(p) / relative for p in rpaths if p.startswith('/'))
                    source = next((p for p in candidates if p.exists()), None)
                    if source is None:
                        raise RuntimeError(f'Cannot resolve {dep} in {binary}')
                elif dep.startswith('@'):
                    continue  # Verified after all libraries have been copied.
                else:
                    source = stage / dep.removeprefix('/usr/') if staged_vlc else Path(dep)
                if str(source).startswith(str(contents)):
                    target = source
                elif '.framework/' in str(source):
                    prefix, suffix = str(source).split('.framework/', 1)
                    source_root = Path(prefix + '.framework')
                    target_root = frameworks / source_root.name
                    if not target_root.exists():
                        copy(source_root, target_root)
                    target = target_root / suffix
                else:
                    target = frameworks / source.name
                    previous = origins.get(target)
                    if previous and previous.resolve() != source.resolve():
                        raise RuntimeError(f'Library name collision: {previous}, {source}')
                    origins[target] = source
                    if not target.exists():
                        shutil.copy2(source, target, follow_symlinks=True)
                replacement = '@loader_path/' + os.path.relpath(target, binary.parent)
                changes.extend(['-change', dep, replacement])
            if changes:
                subprocess.run(['install_name_tool', *changes, str(binary)], check=True)
    # Remove build-machine RPATHs, set a bundle-local fallback for Qt frameworks.
    all_binaries = [p for p in contents.rglob('*') if macho(p)]
    for binary in all_binaries:
        commands = run('otool', '-l', binary)
        if 'cmd LC_ID_DYLIB' in commands:
            install_id = '@rpath/' + str(binary.relative_to(frameworks)) if binary.is_relative_to(frameworks) else '@rpath/' + binary.name
            subprocess.run(['install_name_tool', '-id', install_id, str(binary)], check=True)
        rpaths = re.findall(r'cmd LC_RPATH\n\s*cmdsize \d+\n\s*path (.*?) \(offset', commands)
        for rpath in set(rpaths):
            if rpath.startswith('/'):
                subprocess.run(['install_name_tool', '-delete_rpath', rpath, str(binary)], check=True)
        fallback = '@loader_path/' + os.path.relpath(frameworks, binary.parent)
        if fallback not in rpaths:
            subprocess.run(['install_name_tool', '-add_rpath', fallback, str(binary)], check=True)
    # Check every load command, including plugins/QML, before ad-hoc signing.
    audit = []
    minimum_versions = []
    for binary in all_binaries:
        arches = run('lipo', '-archs', binary).split()
        if arch not in arches:
            raise RuntimeError(f'Wrong architecture in {binary}: {arches}')
        commands = run('otool', '-l', binary)
        minimum_versions.extend(re.findall(r'\bminos (\d+(?:\.\d+)+)', commands))
        minimum_versions.extend(re.findall(r'cmd LC_VERSION_MIN_MACOSX\n\s*cmdsize \d+\n\s*version (\d+(?:\.\d+)+)', commands))
        for dep in dependencies(binary):
            if dep.startswith(('/System/Library/', '/usr/lib/')):
                continue
            if dep.startswith('@loader_path/'):
                resolved = binary.parent / dep[len('@loader_path/'):]
            elif dep.startswith('@executable_path/'):
                resolved = binaries / dep[len('@executable_path/'):]
            elif dep.startswith('@rpath/'):
                resolved = frameworks / dep[len('@rpath/'):]
            else:
                raise RuntimeError(f'External dependency in {binary}: {dep}')
            if not resolved.exists() or not resolved.resolve().is_relative_to(app.resolve()):
                raise RuntimeError(f'Unresolved dependency in {binary}: {dep}')
        audit.append(str(binary.relative_to(app)))
    (output / f'macos-{arch}-dependency-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    minimum = max(minimum_versions, key=lambda v: tuple(map(int, v.split('.'))))
    info['LSMinimumSystemVersion'] = minimum
    # Finish all bundle mutations before signing and verifying the final app.
    with (contents / 'Info.plist').open('wb') as stream:
        plistlib.dump(info, stream)
    sign_macos_adhoc(app, all_binaries)
    (output / f'macos-{arch}-build.json').write_text(json.dumps(dict(
        tag=tag, version=tag, channel=validate_tag(tag)['channel'], platform='macos', source_commit=commit,
        architecture=arch, minimum_macos=minimum, qt=run(qt / 'bin/qmake', '-query', 'QT_VERSION'),
        signing='ad-hoc; no Developer ID or notarization'), indent=2) + '\n')
    archive = output / f'hakone-{tag}-macos-{arch}.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)], check=True)
    # The drag-to-install image carries the same signed bundle as the ZIP.
    dmg.build(app, disk_image_path(archive))
    return archive


def disk_image_path(archive):
    return archive.with_suffix('.dmg')


def write_checksum(path):
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
        digest = digest.hexdigest()
    path.with_suffix(path.suffix + '.sha256').write_text(f'{digest}  {path.name}\n')
    return digest


# The installer scripts and helpers that -Dnsis=enabled installs beside the app.
NSIS_SUPPORT = ('vlc.win32.nsi', 'spad.nsi', 'NSIS', 'helpers', 'languages')


def top_level_ignore(root, ignored):
    """A copytree filter that leaves out names at the top of root only."""
    return lambda directory, names: [n for n in names if n in ignored] if Path(directory) == root else []


def windows(stage, output, tag, commit):
    target = output / 'hakone-win64'
    if target.exists():
        raise SystemExit(f'Remove the old package before rebuilding: {target}')
    shutil.copytree(stage, target, symlinks=True, ignore=top_level_ignore(stage, NSIS_SUPPORT))
    for required in ('hakone.exe', 'vlc-addon-worker.exe', 'libvlc.dll', 'libvlccore.dll', 'plugins'):
        if not (target / required).exists():
            raise RuntimeError(f'Windows installation missing {required}')
    # MinGW's compiler runtimes are not part of a stock Windows installation.
    # Follow imports recursively, resolving runtime DLLs through the same target
    # compiler that produced VLC. Windows system DLLs are not in its search path.
    bundled = {p.name.lower() for p in target.rglob('*.dll')}
    pending = [p for p in target.rglob('*') if p.suffix.lower() in ('.dll', '.exe')]
    runtime_libraries = set()
    imported = set()
    unresolved = set()
    while pending:
        binary = pending.pop()
        for name in re.findall(r'DLL Name:\s*(\S+)', run('x86_64-w64-mingw32-objdump', '-p', binary)):
            name = name.lower()
            imported.add(name)
            if name in bundled or name in unresolved:
                continue
            source = Path(run('x86_64-w64-mingw32-gcc', f'-print-file-name={name}'))
            if not source.is_absolute() or not source.is_file():
                if name == 'vulkan-1.dll' or name.startswith(('libgcc_', 'libstdc++-', 'libwinpthread-', 'libssp-', 'libgomp-', 'libatomic-', 'qt6')):
                    raise RuntimeError(f'Cannot locate required Windows runtime: {name}')
                unresolved.add(name)
                continue
            copy(source, target / name)
            bundled.add(name)
            runtime_libraries.add(name)
            pending.append(target / name)
    if runtime_libraries:
        copy(Path('/usr/share/doc/gcc-mingw-w64-base/copyright'), target / 'GCC-runtime-LICENSE.txt')
        copy(Path('/usr/share/doc/mingw-w64-common/copyright'), target / 'MinGW-runtime-LICENSE.txt')
    metadata_path = output / 'windows-build.json'
    metadata = json.loads(metadata_path.read_text())
    metadata['bundled_compiler_runtimes'] = sorted(runtime_libraries)
    metadata['external_imports'] = sorted(imported - bundled)
    metadata_path.write_text(json.dumps(metadata, indent=2) + '\n')
    (target / 'portable').mkdir(exist_ok=True)
    (target / 'SOURCE_COMMIT.txt').write_text(commit + '\n')
    copy(ROOT / 'modules/gui/qt/fonts/LICENSE.txt', target / 'Inter-LICENSE.txt')
    # Zip entries preserve the empty portable folder.
    archive = output / f'hakone-{tag}-win64.zip'
    shutil.make_archive(str(archive.with_suffix('')), 'zip', output, target.name)
    return archive


def windows_installer(stage, output, tag):
    """Build the NSIS installer from the portable package's files."""
    if not all((stage / name).exists() for name in NSIS_SUPPORT):
        raise RuntimeError('The installation has no NSIS scripts; configure with -Dnsis=enabled')
    package = output / 'hakone-win64'
    work = output / 'nsis'
    if work.exists():
        raise SystemExit(f'Remove the old installer stage before rebuilding: {work}')
    # The portable folder would switch an installed copy to portable mode.
    shutil.copytree(package, work, symlinks=True, ignore=top_level_ignore(package, ('portable', 'sdk')))
    for name in NSIS_SUPPORT:
        copy(stage / name, work / name)
    # spad-setup.exe handles Windows' "Set Program Access and Defaults" and is
    # installed with the other executables. The main script writes its
    # installer beside the stage, as hakone-<version>-win64.exe.
    for script in ('spad.nsi', 'vlc.win32.nsi'):
        subprocess.run(['makensis', '-V2', str(work / script)], check=True)
    installer = output / f'hakone-{tag}-win64.exe'
    if not installer.is_file():
        raise RuntimeError(f'makensis did not write {installer.name}')
    shutil.rmtree(work)
    return installer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=['macos', 'windows', 'linux'])
    parser.add_argument('stage', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tag', default=os.environ.get('RELEASE_TAG'))
    parser.add_argument('--reuse-qt', action='store_true', help='Restage a Mac package while reusing an existing deployment of the same Qt SDK')
    args = parser.parse_args()
    try:
        info = validate_tag(args.tag)
    except ValueError as error:
        parser.error(str(error))
    args.tag = info['version']
    args.output.mkdir(parents=True, exist_ok=True)
    commit = run('git', '-C', ROOT, 'rev-parse', 'HEAD')
    metadata = dict(tag=args.tag, version=args.tag, channel=info['channel'], source_commit=commit, platform=args.platform,
                    architecture=run('uname', '-m'))
    if args.platform != 'macos':
        (args.output / f'{args.platform}-build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    if args.platform == 'linux':
        subprocess.run(['bash', str(ROOT / 'extras/package/release/package-linux.sh'),
                        str(args.stage.resolve()), str(args.output.resolve()), args.tag], check=True)
        return
    if args.platform == 'macos':
        archive = macos(args.stage.resolve(), args.output.resolve(), args.tag, commit, args.reuse_qt)
    else:
        archive = windows(args.stage.resolve(), args.output.resolve(), args.tag, commit)
    digest = write_checksum(archive)
    metadata_path = args.output / (f"macos-{run('uname', '-m')}-build.json" if args.platform == 'macos' else 'windows-build.json')
    package_metadata = json.loads(metadata_path.read_text())
    # asset_name stays the ZIP: update metadata and older clients select it.
    package_metadata.update(asset_name=archive.name, size=archive.stat().st_size,
                            sha256=digest, package_type='portable-zip')
    if args.platform == 'macos':
        image = disk_image_path(archive)
        package_metadata['disk_image'] = dict(asset_name=image.name, size=image.stat().st_size,
                                              sha256=write_checksum(image), package_type='drag-install-dmg')
    else:
        # The MSI is added later, on Windows, by build-msi.py.
        installer = windows_installer(args.stage.resolve(), args.output.resolve(), args.tag)
        package_metadata['installer'] = dict(asset_name=installer.name, size=installer.stat().st_size,
                                             sha256=write_checksum(installer), package_type='nsis-installer')
    metadata_path.write_text(json.dumps(package_metadata, indent=2) + '\n')
    print(archive)
    if args.platform == 'macos':
        print(disk_image_path(archive))


if __name__ == '__main__':
    main()
