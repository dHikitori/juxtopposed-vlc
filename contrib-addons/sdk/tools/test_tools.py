#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for check-package.py and release.py, with throwaway Git repositories."""
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
SDK = TOOLS.parent
spec = importlib.util.spec_from_file_location("check_package", TOOLS / "check-package.py")
check_package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_package)

try:
    import jsonschema  # noqa: F401
    HAVE_JSONSCHEMA = True
except ImportError:
    HAVE_JSONSCHEMA = False

ENV = {**os.environ, "GIT_AUTHOR_NAME": "Tests", "GIT_AUTHOR_EMAIL": "tests@example.org",
       "GIT_COMMITTER_NAME": "Tests", "GIT_COMMITTER_EMAIL": "tests@example.org", "GIT_CONFIG_GLOBAL": os.devnull,
       "GIT_CONFIG_NOSYSTEM": "1"}
ICON = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M4 6h16" stroke="currentColor"/></svg>'


def run(*args, cwd):
    return subprocess.run(args, cwd=cwd, env=ENV, check=True, capture_output=True, text=True).stdout


def package(root, files=None):
    """A small panel package with a built module in an ignored dist/."""
    root = Path(root)
    manifest = {
        "formatVersion": 1, "id": "org.example.tools", "name": "Tools", "version": "1.2.0", "publisher": "Tests",
        "license": "GPL-2.0-or-later", "source": "https://example.org/tools.git",
        "api": {"addon": ">=1.0.0 <2.0.0", "contracts": {"ui": "^1.0.0"}},
        "runtime": {"kind": "wasm-message-v1", "entry": "dist/addon.wasm"},
        "files": files or ["addon.json", "LICENSE", "dist/addon.wasm", "views/main.json", "icons/main.svg", "locales/en.json"],
        "locales": {"default": "en", "files": {"en": "locales/en.json"}},
        "contributions": [
            {"id": "main", "type": "ui.panel", "contract": "ui@1", "hook": "player.panels@1", "name": "Tools",
             "label": "Show the tools", "icon": "icons/main.svg", "view": "views/main.json", "command": "context",
             "stateSchema": {"type": "object", "additionalProperties": False, "properties": {}}, "state": {}},
            {"id": "context", "type": "ui.command", "contract": "ui@1", "command": "context", "operations": ["context"]},
            {"id": "open", "type": "ui.command", "contract": "ui@1", "hook": "queue.actions@1", "label": "Open",
             "icon": "icons/main.svg", "command": "main"}],
        "permissions": {"required": {"ui.contribute": {"hooks": ["player.panels@1", "queue.actions@1"]}}}}
    view = {"contract": "ui@1", "id": "main", "root": {"type": "Page", "children": [
        {"type": "Text", "text": {"message": "hello"}}]}}
    for name, text in {"addon.json": json.dumps(manifest, indent=2), "LICENSE": "Test\n", "views/main.json": json.dumps(view),
                       "icons/main.svg": ICON, "locales/en.json": json.dumps({"hello": "Hello"}),
                       ".gitignore": "/dist/\n", "dist/addon.wasm": "\0asm"}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return manifest


def repository(root):
    run("git", "init", "-q", "-b", "master", cwd=root)
    run("git", "add", "-A", cwd=root)
    run("git", "commit", "-q", "-m", "Source", cwd=root)


def errors(root, **options):
    return check_package.check(root, **options).errors


@unittest.skipUnless(HAVE_JSONSCHEMA, "jsonschema 4 is not installed")
class CheckPackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.manifest = package(self.root)

    def tearDown(self):
        self.directory.cleanup()

    def rewrite(self, change):
        change(self.manifest)
        (self.root / "addon.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_a_correct_package_passes(self):
        repository(self.root)
        self.assertEqual(errors(self.root, tag="v1.2.0"), [])

    def test_the_tag_agrees_with_the_version(self):
        self.assertTrue(any("v1.2.0" in error for error in errors(self.root, tag="v1.3.0")))

    def test_declared_files_must_exist_except_the_unbuilt_module(self):
        (self.root / "dist/addon.wasm").unlink()
        self.assertTrue(any("dist/addon.wasm" in error for error in errors(self.root)))
        self.assertEqual(errors(self.root, require_entry=False), [])
        self.rewrite(lambda manifest: manifest["files"].append("missing.json"))
        self.assertTrue(any("missing.json" in error for error in errors(self.root, require_entry=False)))

    def test_a_view_message_needs_the_default_locale(self):
        (self.root / "locales/en.json").write_text("{}", encoding="utf-8")
        self.assertTrue(any("'hello'" in error for error in errors(self.root)))

    def test_a_worker_command_lists_its_operation(self):
        self.rewrite(lambda manifest: manifest["contributions"][1].update(operations=[]))
        self.assertTrue(any("operations" in error for error in errors(self.root)))

    def test_icons_are_shapes_only(self):
        (self.root / "icons/main.svg").write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><text>A</text></svg>', encoding="utf-8")
        self.assertTrue(any("<text>" in error for error in errors(self.root)))

    def test_the_tree_has_no_links_and_no_case_collisions(self):
        if sys.platform == "win32":
            self.skipTest("symbolic links need privileges on Windows")
        (self.root / "link").symlink_to("LICENSE")
        (self.root / "license").write_text("Other\n", encoding="utf-8")
        repository(self.root)
        found = errors(self.root)
        self.assertTrue(any("links and submodules" in error for error in found))
        if not (self.root / "LICENSE").samefile(self.root / "license"):
            self.assertTrue(any("only by case" in error for error in found))

    def test_paths_follow_the_app_rules(self):
        self.assertTrue(check_package.portable("views/main.json"))
        for path in ("/abs", "a/../b", "a\\b", "c:x", "con.txt", "a/.git/b", "end.", "q?"):
            self.assertFalse(check_package.portable(path), path)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        base = Path(self.directory.name)
        self.remote = base / "remote.git"
        self.root = base / "addon"
        self.root.mkdir()
        run("git", "init", "-q", "--bare", str(self.remote), cwd=base)
        package(self.root)
        repository(self.root)
        run("git", "remote", "add", "origin", str(self.remote), cwd=self.root)
        run("git", "push", "-q", "origin", "master", cwd=self.root)

    def tearDown(self):
        self.directory.cleanup()

    def release(self, *args):
        return subprocess.run([sys.executable, "-B", str(TOOLS / "release.py"), str(self.root), *args],
                              cwd=self.root, env=ENV, capture_output=True, text=True)

    def test_the_tag_has_the_built_module_and_the_branch_does_not(self):
        output = self.root.parent / "github-output"
        result = self.release("--push", "--output", str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("released=true", output.read_text())
        # What an install reads: the tree of the tagged commit, on the remote.
        tree = run("git", "ls-tree", "-r", "--name-only", "v1.2.0^{commit}", cwd=self.remote)
        self.assertIn("dist/addon.wasm", tree.split())
        self.assertNotIn("dist/addon.wasm", run("git", "ls-tree", "-r", "--name-only", "master", cwd=self.remote).split())
        # The tag's commit sits on top of the branch head; the branch did not move.
        head = run("git", "rev-parse", "master", cwd=self.root).strip()
        self.assertEqual(run("git", "rev-parse", "v1.2.0^{commit}^", cwd=self.remote).strip(), head)
        self.assertEqual(run("git", "rev-parse", "master", cwd=self.remote).strip(), head)
        self.assertEqual(run("git", "cat-file", "-t", "v1.2.0", cwd=self.remote).strip(), "tag")
        self.assertEqual(run("git", "status", "--porcelain", cwd=self.root).strip(), "")
        # The same version again releases nothing.
        again = self.release("--push", "--output", str(output))
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertTrue(output.read_text().strip().endswith("released=false"))

    def test_a_missing_build_or_a_changed_checkout_is_refused(self):
        (self.root / "dist/addon.wasm").unlink()
        self.assertNotEqual(self.release().returncode, 0)
        (self.root / "dist/addon.wasm").write_text("\0asm", encoding="utf-8")
        (self.root / "LICENSE").write_text("Changed\n", encoding="utf-8")
        self.assertNotEqual(self.release().returncode, 0)


class SdkTests(unittest.TestCase):
    def test_the_workflow_pins_the_app_rust_version(self):
        pinned = (SDK / "rust-version.txt").read_text().strip()
        app = SDK.parent.parent / "extras/package/release/addon-rust-version.txt"
        if app.is_file():
            self.assertEqual(pinned, app.read_text().strip())
        self.assertRegex((SDK / "VERSION").read_text().strip(), r"^\d+\.\d+\.\d+$")

    def test_markdown_links_work_in_the_mirror(self):
        """hakone-org/addon-sdk has only this folder: a relative link must stay
        in it and name a file or folder that exists. Link to the rest of
        hakone-org/hakone with a full https://github.com/... address."""
        link = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)")
        problems = []
        for page in sorted(SDK.rglob("*.md")):
            if "target" in page.relative_to(SDK).parts:
                continue
            for match in link.finditer(page.read_text(encoding="utf-8")):
                target = match.group(1).split("#")[0]
                if not target or re.match(r"^[a-z][a-z0-9+.-]*:", target):
                    continue
                resolved = (page.parent / target).resolve()
                where = f"{page.relative_to(SDK)}: {match.group(1)}"
                if resolved != SDK and SDK not in resolved.parents:
                    problems.append(f"{where} leaves the SDK folder")
                elif not resolved.exists():
                    problems.append(f"{where} does not exist")
        self.assertEqual(problems, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
