# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("addon_runtime_check", SCRIPTS / "addon_runtime_check.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class AddonRuntimeCheckTests(unittest.TestCase):
    def test_layouts_locate_helpers_and_reference_modules_inside_release(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for executable, worker, module in (
                ("AppDir/AppRun", "AppDir/usr/bin/hakone-addon-worker", "AppDir/usr/share/vlc/addons/reference/thesubtitledb/dist/addon.wasm"),
                ("Hakone.app/Contents/MacOS/hakone", "Hakone.app/Contents/MacOS/hakone-addon-worker", "Hakone.app/Contents/Resources/share/addons/reference/thesubtitledb/dist/addon.wasm"),
                ("hakone-win64/hakone.exe", "hakone-win64/hakone-addon-worker.exe", "hakone-win64/share/vlc/addons/reference/thesubtitledb/dist/addon.wasm"),
                ("usr/bin/hakone", "usr/bin/hakone-addon-worker", "usr/share/vlc/addons/reference/thesubtitledb/dist/addon.wasm")
            ):
                self.assertEqual(check.installation_files(root / executable), (root / worker, root / module))

    def files(self, root):
        worker, module = root / "hakone-addon-worker", root / "addon.wasm"
        worker.write_bytes(b"worker-fixture")
        module.write_bytes(b"\0asm")
        return worker, module

    def response(self, status="auth_required", **message):
        return (json.dumps({"type": "ready", "protocol": "wasm-message-v1"}) + "\n" +
                json.dumps({"type": "messages", "messages": [
                    {"type": "result", "id": "release-worker-smoke", "status": status, "data": {}, **message}
                ]}) + "\n")

    def test_smoke_has_no_secrets_network_authority_or_loader_override(self):
        with tempfile.TemporaryDirectory() as temp:
            worker, module = self.files(Path(temp))
            response = subprocess.CompletedProcess([str(worker), str(module)], 0, self.response(), "")
            environment = dict(os.environ, THESUBTITLEDB_API_KEY="must-not-enter-worker",
                               LD_LIBRARY_PATH="/build-only/libraries", DYLD_LIBRARY_PATH="/build-only/frameworks")
            with mock.patch.dict(os.environ, environment), mock.patch.object(check.subprocess, "run", return_value=response) as run:
                self.assertIn("without an API key", check.check_worker(worker, module))
            arguments = run.call_args.kwargs
            self.assertNotIn("THESUBTITLEDB_API_KEY", arguments["env"])
            self.assertNotIn("LD_LIBRARY_PATH", arguments["env"])
            self.assertNotIn("DYLD_LIBRARY_PATH", arguments["env"])
            self.assertTrue(arguments["check"])
            self.assertEqual(arguments["timeout"], 20)
            invocation = json.loads(arguments["input"])
            self.assertEqual(invocation["credentials"], {"apiKey": {"present": False}})
            self.assertNotIn("must-not-enter-worker", arguments["input"])

    def test_missing_helper_or_module_fails_before_any_process(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(check.subprocess, "run") as run:
            with self.assertRaisesRegex(RuntimeError, "missing"):
                check.check_worker(Path(temp) / "missing", Path(temp) / "addon.wasm")
            run.assert_not_called()

    def test_protocol_host_effect_and_wrong_status_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            worker, module = self.files(Path(temp))
            responses = [
                "invalid-json",
                json.dumps({"type": "ready", "protocol": "other"}),
                self.response(status="match"),
                self.response(id="stale-invocation"),
                self.response(type="host-call"),
            ]
            for body in responses:
                with self.subTest(body=body), mock.patch.object(check.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, body, "")):
                    with self.assertRaisesRegex(RuntimeError, "protocol|credential"):
                        check.check_worker(worker, module)

    def test_worker_failure_or_timeout_is_not_success(self):
        with tempfile.TemporaryDirectory() as temp:
            worker, module = self.files(Path(temp))
            for error in (subprocess.TimeoutExpired([], 20),
                          subprocess.CalledProcessError(1, [], stderr="worker fixture failed")):
                with self.subTest(error=error), mock.patch.object(check.subprocess, "run", side_effect=error):
                    with self.assertRaisesRegex(RuntimeError, "did not complete"):
                        check.check_worker(worker, module)


if __name__ == "__main__":
    unittest.main()
