#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the SDK fixture through the portable worker. No network or key is used."""
import json
import os
from pathlib import Path
import subprocess
import unittest


class WorkerSdk(unittest.TestCase):
    def setUp(self):
        worker = os.environ.get("HAKONE_ADDON_WORKER")
        module = os.environ.get("HAKONE_ADDON_SDK_FIXTURE")
        if not worker or not module:
            self.skipTest("Set the worker and SDK fixture paths.")
        self.process = subprocess.Popen(
            [str(Path(worker).resolve()), str(Path(module).resolve())],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, env={},
        )
        self.addCleanup(self.close)
        self.assertEqual(json.loads(self.process.stdout.readline()), {"type": "ready", "protocol": "wasm-message-v1"})

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)
        self.process.stdin.close()
        self.process.stdout.close()

    def send(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        response = json.loads(self.process.stdout.readline())
        self.assertEqual(response["type"], "messages")
        return response["messages"]

    def invoke(self, identifier, operation):
        return self.send({"type": "invoke", "id": identifier, "contribution": "fixture",
                          "operation": operation, "context": {}, "settings": {},
                          "credentials": {}, "hostCapabilities": {}})

    def test_repeated_allocation_and_dispatch(self):
        for number in range(100):
            identifier = f"job-{number}"
            self.assertEqual(self.invoke(identifier, "state"), [
                {"type": "result", "id": identifier, "status": "match", "data": {"pending": 0}}])

    def test_host_credential_status_capacity(self):
        response = self.send({"type": "invoke", "id": "many-fields", "contribution": "fixture",
                              "operation": "state", "context": {}, "settings": {},
                              "credentials": {f"key{number}": {"present": False} for number in range(64)},
                              "hostCapabilities": {}})
        self.assertEqual(response[0]["status"], "match")

    def test_cancel_removes_pending_call_and_late_result(self):
        call = self.invoke("job-1", "wait")[0]
        self.assertEqual(call["invocation"], "job-1")
        self.assertEqual(self.send({"type": "cancel", "id": "job-1"}), [])
        self.assertEqual(self.send({"type": "host-result", "id": call["id"], "invocation": "job-1",
                                    "status": "ok", "response": {}}), [])
        self.assertEqual(self.invoke("job-2", "state")[0]["data"], {"pending": 0})

    def test_host_result_cannot_take_another_invocations_call(self):
        call = self.invoke("job-1", "wait")[0]
        self.assertEqual(self.send({"type": "host-result", "id": call["id"], "invocation": "other",
                                    "status": "ok", "response": {}}), [])
        result = self.send({"type": "host-result", "id": call["id"], "invocation": "job-1",
                            "status": "ok", "response": {}})
        self.assertEqual(result[0]["id"], "job-1")
        self.assertEqual(result[0]["data"], {"ok": True})


if __name__ == "__main__":
    unittest.main()
