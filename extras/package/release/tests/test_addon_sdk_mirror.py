# SPDX-License-Identifier: GPL-2.0-or-later
"""The addon-sdk mirror: one sync commit per SDK change, release tags that never move."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parent.parent / "addon_sdk_mirror.py"
spec = importlib.util.spec_from_file_location("addon_sdk_mirror", SCRIPT)
mirror = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mirror)

ENV = {"GIT_AUTHOR_NAME": "Tests", "GIT_AUTHOR_EMAIL": "tests@example.org", "GIT_COMMITTER_NAME": "Tests",
       "GIT_COMMITTER_EMAIL": "tests@example.org", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.saved = {key: os.environ.get(key) for key in ENV}
        os.environ.update(ENV)
        self.directory = tempfile.TemporaryDirectory()
        base = Path(self.directory.name)
        self.source, self.remote = base / "hakone", base / "addon-sdk.git"
        self.source.mkdir()
        self.git("init", "-q", "-b", "master")
        self.git("init", "-q", "--bare", str(self.remote))
        self.previous = os.getcwd()
        os.chdir(self.source)
        self.write("README.md", "App\n")
        self.write("contrib-addons/sdk/VERSION", "1.0.0\n")
        self.write("contrib-addons/sdk/README.md", "SDK\n")
        self.commit("First")

    def tearDown(self):
        os.chdir(self.previous)
        self.directory.cleanup()
        for key, value in self.saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    def git(self, *args, cwd=None):
        return subprocess.run(["git", *args], cwd=cwd or self.source, check=True, capture_output=True, text=True).stdout.strip()

    def write(self, name, text):
        path = self.source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", message)
        return self.git("rev-parse", "HEAD")

    def run_mirror(self, *args):
        with contextlib.redirect_stdout(io.StringIO()):
            return mirror.main(["--remote", str(self.remote), "--push", *args])

    def mirrored(self, *args):
        return self.git(*args, cwd=self.remote)

    def test_master_has_the_sdk_folder_at_its_root_and_syncs_only_changes(self):
        self.run_mirror()
        self.assertEqual(self.mirrored("ls-tree", "--name-only", "master").split(), ["README.md", "VERSION"])
        first = self.mirrored("rev-parse", "master")
        self.assertIn("Sync contrib-addons/sdk", self.mirrored("log", "-1", "--format=%s", "master"))
        # A change outside the SDK: nothing new on the mirror.
        self.write("README.md", "App 2\n")
        self.commit("App only")
        self.run_mirror()
        self.assertEqual(self.mirrored("rev-parse", "master"), first)
        # An SDK change: one more commit, on top of the last.
        self.write("contrib-addons/sdk/README.md", "SDK 2\n")
        self.commit("SDK change")
        self.run_mirror()
        self.assertEqual(self.mirrored("rev-parse", "master^"), first)
        self.assertEqual(self.mirrored("show", "master:README.md"), "SDK 2")

    def test_a_release_tag_publishes_the_version_and_moves_the_major_tag(self):
        self.git("tag", "sdk-v1.0.0")
        self.run_mirror("--release-tag", "sdk-v1.0.0")
        self.assertEqual(self.mirrored("cat-file", "-t", "v1.0.0"), "tag")
        self.assertEqual(self.mirrored("rev-parse", "v1.0.0^{commit}"), self.mirrored("rev-parse", "master"))
        self.assertEqual(self.mirrored("rev-parse", "v1^{commit}"), self.mirrored("rev-parse", "master"))
        # A later release moves v1, and v1.0.0 stays.
        self.write("contrib-addons/sdk/VERSION", "1.1.0\n")
        self.write("contrib-addons/sdk/README.md", "SDK 1.1\n")
        self.commit("SDK 1.1")
        self.git("tag", "sdk-v1.1.0")
        self.run_mirror("--release-tag", "sdk-v1.1.0")
        self.assertEqual(self.mirrored("show", "v1:README.md"), "SDK 1.1")
        self.assertEqual(self.mirrored("show", "v1.0.0:README.md"), "SDK")
        # The same release again changes nothing.
        self.run_mirror("--release-tag", "sdk-v1.1.0")
        self.assertEqual(self.mirrored("show", "v1.1.0:README.md"), "SDK 1.1")

    def test_a_release_tag_must_agree_with_version_and_never_moves(self):
        self.git("tag", "sdk-v2.0.0")
        with self.assertRaises(SystemExit):
            self.run_mirror("--release-tag", "sdk-v2.0.0")
        self.git("tag", "sdk-v1.0.0")
        self.run_mirror("--release-tag", "sdk-v1.0.0")
        # The same version with other files: refused.
        self.write("contrib-addons/sdk/README.md", "Changed\n")
        self.commit("Change without a version")
        self.git("tag", "-f", "sdk-v1.0.0")
        with self.assertRaises(SystemExit):
            self.run_mirror("--release-tag", "sdk-v1.0.0")

    def test_an_older_release_does_not_move_master(self):
        self.git("tag", "sdk-v1.0.0")
        old = self.git("rev-parse", "HEAD")
        self.write("contrib-addons/sdk/README.md", "Newer\n")
        self.commit("Newer SDK")
        self.run_mirror()
        tip = self.mirrored("rev-parse", "master")
        self.run_mirror("--source", old, "--release-tag", "sdk-v1.0.0", "--no-master")
        self.assertEqual(self.mirrored("rev-parse", "master"), tip)
        self.assertEqual(self.mirrored("show", "v1.0.0:README.md"), "SDK")


if __name__ == "__main__":
    unittest.main(verbosity=2)
