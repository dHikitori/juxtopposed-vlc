# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'dependency-cache.py'
spec = importlib.util.spec_from_file_location('dependency_cache', SCRIPT)
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)

IMAGE = 'registry.example.invalid/toolchain@sha256:' + 'a' * 64


class DependencyCacheTests(unittest.TestCase):
    def fixture_sdk(self, root):
        for relative in cache.REQUIRED:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('dependency fixture')
        binary = root / cache.WORK / 'contrib/bin/native-tool'
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_bytes(b'host tool')
        binary.chmod(0o755)
        (binary.parent / 'native-alias').symlink_to(binary.name)
        return binary

    def test_restore_rejects_missing_partial_modified_or_wrong_key_sdk(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertFalse(cache.check('key', root))
            binary = self.fixture_sdk(root)
            cache.seal('key', root)
            self.assertTrue(cache.check('key', root))
            self.assertFalse(cache.check('other', root))
            binary.write_bytes(b'corrupted host tool')
            self.assertFalse(cache.check('key', root))
            cache.seal('key', root)
            binary.chmod(0o644)
            self.assertFalse(cache.check('key', root))
            cache.seal('key', root)
            (root / cache.REQUIRED[0]).unlink()
            self.assertFalse(cache.check('key', root))

    def test_invalid_manifest_falls_back_to_a_cold_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture_sdk(root)
            marker = root / cache.MARKER
            marker.parent.mkdir()
            for data in ('{broken', '[]', 'null', '"not a manifest"', '{}'):
                marker.write_text(data)
                self.assertFalse(cache.check('key', root))

    def test_no_app_output_or_key_header_is_cached_and_clean_preserves_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture_sdk(root)
            generated = root / cache.WORK / 'win64-meson/subtitle-consumer-key.hpp'
            generated.parent.mkdir(parents=True)
            generated.write_text('dummy application API key')
            output = root / 'build-release-output/vlc.zip'
            output.parent.mkdir()
            output.write_bytes(b'packaged app')
            cache.seal('key', root)
            data = json.loads((root / cache.MARKER).read_text())
            self.assertNotIn(generated.relative_to(root).as_posix(), data['files'])
            self.assertNotIn(output.relative_to(root).as_posix(), data['files'])
            stamp = root / cache.WORK / 'contrib/contrib-win64/.qt'
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.touch()
            cache.clean(root)
            self.assertTrue(generated.exists())
            self.assertTrue(output.exists())
            self.assertFalse(stamp.exists())
            self.assertFalse(cache.check('key', root))

    def test_symlink_and_its_target_are_both_preserved_and_verified(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binary = self.fixture_sdk(root)
            cache.seal('key', root)
            alias = binary.parent / 'native-alias'
            alias.unlink()
            alias.symlink_to('another-tool')
            self.assertFalse(cache.check('key', root))

    def test_key_tracks_toolchain_recipes_patches_and_flags_but_not_app_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return subprocess.run(['git', '-C', str(root), *args], check=True,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            git('init')
            fixtures = {'contrib/src/qt/rules.mak': 'qt=6.8.3',
                        'contrib/src/qt/SHA512SUMS': 'qt source checksum',
                        'contrib/src/medialibrary/0001.patch': 'fork patch',
                        'extras/package/release/build-windows.sh': 'release flags',
                        'meson.build': "version: '4.0.0-beta.4'"}
            for name, content in fixtures.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            git('add', '.')
            original = cache.cache_key(IMAGE, root)
            (root / 'meson.build').write_text("version: '4.0.0-beta.5'")
            self.assertEqual(original, cache.cache_key(IMAGE, root))
            self.assertNotEqual(original, cache.cache_key(IMAGE.replace('a' * 64, 'b' * 64), root))
            # Generated files under contrib must not affect the fingerprint.
            generated = root / cache.WORK / 'generated'
            generated.parent.mkdir(parents=True)
            generated.write_text('not a build input')
            self.assertEqual(original, cache.cache_key(IMAGE, root))
            for name in fixtures:
                if name == 'meson.build':
                    continue
                path = root / name
                before = path.read_text()
                path.write_text(before + ' changed')
                self.assertNotEqual(original, cache.cache_key(IMAGE, root), name)
                path.write_text(before)

    def test_download_key_ignores_generated_checksums_and_tracks_source_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(['git', '-C', str(root), 'init'], check=True, capture_output=True)
            source = root / 'contrib/src/qt/SHA512SUMS'
            source.parent.mkdir(parents=True)
            source.write_text('original source checksum')
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            original = cache.download_key(root)
            generated = root / cache.WORK / 'contrib/contrib-win64/qt/thirdparty/SHA512SUMS'
            generated.parent.mkdir(parents=True)
            generated.write_text('generated third party checksum')
            self.assertEqual(original, cache.download_key(root))
            source.write_text('updated source checksum')
            self.assertNotEqual(original, cache.download_key(root))

    def test_wrapper_builds_dependencies_then_reuses_them_without_caching_app_key(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            release = root / 'extras/package/release'
            upstream = root / 'extras/package/win32/build.sh'
            release.mkdir(parents=True)
            upstream.parent.mkdir(parents=True)
            for name in ('dependency-cache.py', 'build-windows.sh', 'setup-addon-rust.sh', 'addon-rust-version.txt'):
                shutil.copy2(SCRIPT.parent / name, release / name)
            (release / 'version.py').write_text("print('4.0.0-beta.4')\n")
            (release / 'package.py').write_text('')
            upstream.write_text('''#!/usr/bin/env python3
import importlib.util, json, os
from pathlib import Path
root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cache', root / 'extras/package/release/dependency-cache.py')
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)
with (root / 'calls.jsonl').open('a') as stream:
    stream.write(json.dumps(dict(dependencies_only=os.getenv('VLC_BUILD_DEPENDENCIES_ONLY'),
                                cached=os.getenv('VLC_USE_CACHED_DEPENDENCIES'))) + '\\n')
if os.getenv('VLC_BUILD_DEPENDENCIES_ONLY') == '1':
    for relative in [*cache.REQUIRED, cache.PREFIX / 'bin/vulkan-1.dll',
                     cache.WORK / 'contrib/contrib-win64/vulkan-loader/LICENSES/license.txt']:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('dependency fixture')
else:
    (root / cache.WORK / 'install').mkdir(parents=True, exist_ok=True)
    key = root / cache.WORK / 'win64-meson/subtitle-consumer-key.hpp'
    key.parent.mkdir(parents=True, exist_ok=True)
    key.write_text(os.getenv('OPENSUBTITLES_API_KEY', ''))
''')
            upstream.chmod(0o755)
            commands = root / 'commands'
            commands.mkdir()
            for name in ('rustup', 'cargo', 'x86_64-w64-mingw32-gcc'):
                command_fixture = commands / name
                command_fixture.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$TOOLCHAIN_SETUP_LOG"\n')
                command_fixture.chmod(0o755)
            setup_log = root / 'toolchain.log'
            env = dict(os.environ, RELEASE_DEPS_CACHE_KEY='fixture-key',
                       CARGO_HOME=str(root / 'cargo'), TOOLCHAIN_SETUP_LOG=str(setup_log),
                       PATH=str(commands) + os.pathsep + os.environ['PATH'])
            env.pop('RUSTUP_TOOLCHAIN', None)
            env.pop('OPENSUBTITLES_API_KEY', None)
            command = ['bash', str(release / 'build-windows.sh')]
            subprocess.run([*command, '--dependencies-only'], env=env, check=True, capture_output=True)
            self.assertTrue(cache.check('fixture-key', root))
            # A second dependency phase must take the validated fast path.
            subprocess.run([*command, '--dependencies-only'], env=env, check=True, capture_output=True)
            self.assertFalse(setup_log.exists())  # No worker compiler in dependency-only cache phases.
            subprocess.run(command, env=dict(env, OPENSUBTITLES_API_KEY='dummy-app-key'),
                           check=True, capture_output=True)
            self.assertIn('x86_64-pc-windows-gnu', setup_log.read_text())
            calls = [json.loads(line) for line in (root / 'calls.jsonl').read_text().splitlines()]
            self.assertEqual(calls, [dict(dependencies_only='1', cached=None),
                                     dict(dependencies_only=None, cached='1')])
            self.assertTrue(cache.check('fixture-key', root))
            self.assertNotIn('dummy-app-key', (root / cache.MARKER).read_text())
            # A partial/corrupted SDK must cause a fresh dependency build.
            (root / cache.REQUIRED[0]).write_text('corrupted cached dependency')
            subprocess.run([*command, '--dependencies-only'], env=env, check=True, capture_output=True)
            self.assertTrue(cache.check('fixture-key', root))
            calls = [json.loads(line) for line in (root / 'calls.jsonl').read_text().splitlines()]
            self.assertEqual(calls[-1], dict(dependencies_only='1', cached=None))
            self.assertNotIn('dummy-app-key', (root / cache.MARKER).read_text())


if __name__ == '__main__':
    unittest.main()
