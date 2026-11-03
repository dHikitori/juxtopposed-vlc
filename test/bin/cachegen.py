#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the real cache generator without changing installed plugin caches."""
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

if os.name == "nt":
    print("This test uses POSIX plugin paths. Use an isolated package for Windows checks.")
    sys.exit(77)

HELPER = str(Path(sys.argv.pop(1)).resolve())
PLUGIN = None
if "--plugin" in sys.argv:
    index = sys.argv.index("--plugin")
    PLUGIN = Path(sys.argv[index + 1]).resolve()
    del sys.argv[index:index + 2]


class CacheGeneratorTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vlc-cache-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.plugins = self.root / "Plugin folder é"
        self.plugins.mkdir()
        self.env = dict(os.environ, VLC_LIB_PATH=str(self.root / "missing-install"),
                        VLC_PLUGIN_PATH=str(self.plugins),
                        XDG_CONFIG_HOME=str(self.root / "config"),
                        XDG_CACHE_HOME=str(self.root / "cache"))

    def run_helper(self, *args, success=True):
        result = subprocess.run([HELPER, *map(str, args)], env=self.env,
                                capture_output=True, timeout=15)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        else:
            self.assertNotEqual(result.returncode, 0)
        self.assertEqual(list(self.root.rglob(".vlc-cache-check-*")), [])
        return result

    def test_configured_paths(self):
        default = self.root / "runtime" / "plugins"
        default.mkdir(parents=True)
        self.env["VLC_LIB_PATH"] = str(default.parent)
        self.run_helper("--verify")
        self.assertGreater((default / "plugins.dat").stat().st_size, 0)
        self.assertGreater((self.plugins / "plugins.dat").stat().st_size, 0)

    def test_explicit_path(self):
        self.run_helper("--verify", self.plugins)
        self.assertGreater((self.plugins / "plugins.dat").stat().st_size, 0)

    def test_stale_cache_is_replaced(self):
        cache = self.plugins / "plugins.dat"
        cache.write_bytes(b"invalid old cache")
        self.run_helper("--verify")
        self.assertNotEqual(cache.read_bytes(), b"invalid old cache")

    @unittest.skipIf(PLUGIN is None, "Pass --plugin with the built dummy plugin")
    def test_rescan_finds_new_plugin_and_ignores_saved_scan_policy(self):
        self.run_helper("--verify")
        cache = self.plugins / "plugins.dat"
        empty_size = cache.stat().st_size
        shutil.copyfile(PLUGIN, self.plugins / PLUGIN.name)
        config = self.root / "config" / "hakone"
        config.mkdir(parents=True)
        (config / "vlcrc").write_text("[core]\nplugins-scan=0\n")
        self.run_helper("--verify")
        self.assertGreater(cache.stat().st_size, empty_size)
        self.assertIn(PLUGIN.name.encode(), cache.read_bytes())

    def test_missing_explicit_path_fails(self):
        self.run_helper("--verify", self.root / "not-present", success=False)

    def test_silent_cache_replace_failure_is_reported(self):
        cache = self.plugins / "plugins.dat"
        cache.mkdir()
        # The legacy helper returns zero when the cache cannot replace a directory.
        self.run_helper(self.plugins)
        self.run_helper("--verify", success=False)
        self.assertTrue(cache.is_dir())

    def test_missing_configured_path_fails(self):
        self.env["VLC_PLUGIN_PATH"] = str(self.root / "not-present")
        self.run_helper("--verify", success=False)

    def test_no_plugin_directories_fails(self):
        self.env.pop("VLC_PLUGIN_PATH")
        self.run_helper("--verify", success=False)

    @unittest.skipIf(os.name == "nt" or getattr(os, "geteuid", lambda: 0)() == 0,
                     "POSIX directory permissions need a non-root user")
    def test_read_only_directory_fails_and_retains_cache(self):
        cache = self.plugins / "plugins.dat"
        cache.write_bytes(b"existing cache")
        self.plugins.chmod(0o555)
        try:
            self.run_helper("--verify", success=False)
            self.assertEqual(cache.read_bytes(), b"existing cache")
        finally:
            self.plugins.chmod(0o755)

    def test_legacy_invocation_still_generates_cache(self):
        self.run_helper(self.plugins)
        self.assertGreater((self.plugins / "plugins.dat").stat().st_size, 0)


if __name__ == "__main__":
    unittest.main()
