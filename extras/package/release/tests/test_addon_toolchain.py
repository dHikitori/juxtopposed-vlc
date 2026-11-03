# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
WORKER_BUILD = SCRIPTS.parents[2] / "modules/gui/qt/scripts/build-addon-worker.py"
WORKER_MANIFEST = SCRIPTS.parents[2] / "modules/gui/qt/addons/worker/Cargo.toml"
spec = importlib.util.spec_from_file_location("addon_worker_build", WORKER_BUILD)
worker_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker_build)


class AddonToolchainTests(unittest.TestCase):
    def test_space_limit_is_optional_and_includes_build_reserve(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "missing/target"
            with mock.patch.object(worker_build.shutil, "disk_usage") as usage:
                worker_build.check_free_space([target], 0, 0)
                usage.assert_not_called()
                usage.return_value = type("Space", (), {"free": 8999})()
                with self.assertRaisesRegex(ValueError, "9000 free bytes"):
                    worker_build.check_free_space([target], 7500, 1500)
                usage.assert_called_once_with(Path(temp))
                usage.return_value = type("Space", (), {"free": 9000})()
                worker_build.check_free_space([target], 7500, 1500)
                with self.assertRaisesRegex(ValueError, "zero or larger"):
                    worker_build.check_free_space([target], -1, 0)

    def executable(self, path, source):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/usr/bin/env python3\n" + source)
        path.chmod(0o755)

    def setup_environment(self, root):
        commands = root / "commands"
        commands.mkdir()
        env = dict(os.environ, PATH=str(commands) + os.pathsep + "/usr/bin:/bin",
                   CARGO_HOME=str(root / "cargo"), TOOLCHAIN_TEST_LOG=str(root / "calls.jsonl"))
        env.pop("RUSTUP_TOOLCHAIN", None)
        return commands, env

    def test_release_setup_pins_toolchain_and_installs_explicit_cross_target(self):
        for override in (None, "nightly-2026-10-25"):
            with self.subTest(override=override), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                commands, env = self.setup_environment(root)
                recorder = (
                    "import json, os, sys\nfrom pathlib import Path\n"
                    "with Path(os.environ['TOOLCHAIN_TEST_LOG']).open('a') as stream:\n"
                    " stream.write(json.dumps([Path(sys.argv[0]).name,*sys.argv[1:]])+'\\n')\n"
                )
                self.executable(commands / "rustup", recorder)
                self.executable(commands / "cargo", recorder)
                if override:
                    env["RUSTUP_TOOLCHAIN"] = override
                command = ['bash', '-c',
                           'set -euo pipefail\nsource "$1"\nvlc_setup_addon_rust x86_64-pc-windows-gnu\n'
                           'python3 -c \'import json,os;print(json.dumps({k:os.environ.get(k) for k in '
                           '["RUSTUP_TOOLCHAIN","RUSTUP_AUTO_INSTALL"]}))\'',
                           'test', str(SCRIPTS / "setup-addon-rust.sh")]
                done = subprocess.run(command, env=env, text=True, capture_output=True, timeout=10)
                self.assertEqual(done.returncode, 0, done.stderr)
                pin = override or (SCRIPTS / "addon-rust-version.txt").read_text().strip()
                calls = [json.loads(line) for line in (root / "calls.jsonl").read_text().splitlines()]
                self.assertIn(["rustup", "toolchain", "install", pin, "--profile", "minimal", "--no-self-update"], calls)
                self.assertIn(["rustup", "target", "add", "--toolchain", pin, "x86_64-pc-windows-gnu"], calls)
                # Every platform builds the bundled add-on modules for wasm32.
                self.assertIn(["rustup", "target", "add", "--toolchain", pin, "wasm32-unknown-unknown"], calls)
                environment = json.loads(done.stdout.splitlines()[-1])
                self.assertEqual(environment, {"RUSTUP_TOOLCHAIN": pin, "RUSTUP_AUTO_INSTALL": "0"})

    def test_bootstrap_checksum_failure_never_executes_installer(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            commands, env = self.setup_environment(root)
            env["BOOTSTRAP_TEST_MARKER"] = str(root / "executed")
            installer = '#!/bin/sh\nprintf executed > "$BOOTSTRAP_TEST_MARKER"\nexit 97\n'
            # The fixture supplies an incorrect checksum without a network request.
            # The installer must not start.
            source = (
                "import sys\nfrom pathlib import Path\n"
                "destination=Path(sys.argv[sys.argv.index('-o')+1])\n"
                f"installer={installer!r}\n"
                "destination.write_text('0'*64 if str(destination).endswith('.sha256') else installer)\n"
            )
            self.executable(commands / "curl", source)
            done = subprocess.run(['bash', '-c', 'set -euo pipefail\nsource "$1"\nvlc_setup_addon_rust',
                                   'test', str(SCRIPTS / "setup-addon-rust.sh")],
                                  env=env, text=True, capture_output=True, timeout=10)
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("does not agree", done.stderr)
            self.assertFalse((root / "executed").exists())

    def test_fresh_bootstrap_verifies_then_provisions_the_pinned_toolchain(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            commands, env = self.setup_environment(root)
            recorder = (
                "#!/usr/bin/python3\nimport json, os, sys\nfrom pathlib import Path\n"
                "with Path(os.environ['TOOLCHAIN_TEST_LOG']).open('a') as stream:\n"
                " stream.write(json.dumps([Path(sys.argv[0]).name,*sys.argv[1:]])+'\\n')\n"
            )
            installer = (
                "#!/usr/bin/python3\nimport json, os, sys\nfrom pathlib import Path\n"
                "with Path(os.environ['TOOLCHAIN_TEST_LOG']).open('a') as stream:\n"
                " stream.write(json.dumps(['bootstrap',*sys.argv[1:]])+'\\n')\n"
                "binary=Path(os.environ['CARGO_HOME'])/'bin'\nbinary.mkdir(parents=True)\n"
                f"recorder={recorder!r}\n"
                "for name in ['rustup','cargo']:\n"
                " path=binary/name\n path.write_text(recorder)\n path.chmod(0o755)\n"
            )
            digest = hashlib.sha256(installer.encode()).hexdigest()
            transport = (
                "import sys\nfrom pathlib import Path\n"
                "destination=Path(sys.argv[sys.argv.index('-o')+1])\n"
                f"installer={installer!r}\ndigest={digest!r}\n"
                "destination.write_text(digest+' *rustup-init\\n' if str(destination).endswith('.sha256') else installer)\n"
            )
            self.executable(commands / "curl", transport)
            done = subprocess.run(
                ['bash', '-c', 'set -euo pipefail\nsource "$1"\nvlc_setup_addon_rust x86_64-pc-windows-gnu',
                 'test', str(SCRIPTS / "setup-addon-rust.sh")],
                env=env, text=True, capture_output=True, timeout=10)
            self.assertEqual(done.returncode, 0, done.stderr)
            calls = [json.loads(line) for line in (root / "calls.jsonl").read_text().splitlines()]
            self.assertEqual(calls[0], ["bootstrap", "-y", "--no-modify-path", "--profile", "minimal",
                                       "--default-toolchain", "none"])
            pin = (SCRIPTS / "addon-rust-version.txt").read_text().strip()
            self.assertIn(["rustup", "toolchain", "install", pin, "--profile", "minimal", "--no-self-update"], calls)
            self.assertIn(["rustup", "target", "add", "--toolchain", pin, "x86_64-pc-windows-gnu"], calls)
            # Every platform builds the bundled add-on modules for wasm32.
            self.assertIn(["rustup", "target", "add", "--toolchain", pin, "wasm32-unknown-unknown"], calls)

    def test_worker_builder_defaults_to_stable_and_respects_override(self):
        for override in (None, "nightly-2026-10-25"):
            with self.subTest(override=override), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                commands, env = self.setup_environment(root)
                source = (
                    "import json, os, sys\nfrom pathlib import Path\n"
                    "args=sys.argv[1:]\n"
                    "target=Path(args[args.index('--target-dir')+1])/'release'/'vlc-addon-worker'\n"
                    "target.parent.mkdir(parents=True,exist_ok=True)\n"
                    "target.write_bytes(b'worker-fixture')\n"
                    "Path(os.environ['TOOLCHAIN_TEST_LOG']).write_text(json.dumps({"
                    "'toolchain':os.environ.get('RUSTUP_TOOLCHAIN'),"
                    "'auto':os.environ.get('RUSTUP_AUTO_INSTALL'),'args':args}))\n"
                )
                self.executable(commands / "cargo", source)
                if override:
                    env["RUSTUP_TOOLCHAIN"] = override
                output = root / "output/worker"
                done = subprocess.run(
                    [sys.executable, str(WORKER_BUILD), "--cargo", str(commands / "cargo"),
                     "--manifest", str(WORKER_MANIFEST), "--output", str(output),
                     "--target-dir", str(root / "target")],
                    env=env, text=True, capture_output=True, timeout=10)
                self.assertEqual(done.returncode, 0, done.stderr)
                record = json.loads((root / "calls.jsonl").read_text())
                self.assertEqual(record["toolchain"], override or "stable")
                self.assertEqual(record["auto"], "0")
                self.assertIn("--locked", record["args"])
                self.assertIn("--release", record["args"])
                self.assertEqual(output.read_bytes(), b"worker-fixture")
                self.assertTrue(output.stat().st_mode & 0o100)


if __name__ == "__main__":
    unittest.main()
