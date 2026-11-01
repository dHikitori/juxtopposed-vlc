#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the shipped Wasm through the actual worker, with fixture-only IO."""
import argparse
import json
from pathlib import Path
import selectors
import subprocess
import unittest

PACKAGE = Path(__file__).resolve().parent.parent
WORKER = None


class Worker:
    def __enter__(self):
        self.process = subprocess.Popen(
            [str(WORKER), str(PACKAGE / "dist/addon.wasm")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env={"LANG": "C"},
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        ready = self.read()
        if ready != {"type": "ready", "protocol": "wasm-message-v1"}:
            raise AssertionError("Worker did not negotiate the expected portable ABI")
        return self

    def read(self):
        if not self.selector.select(timeout=10):
            raise AssertionError("Worker did not respond within ten seconds")
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError("Worker stopped before returning an ABI response")
        return json.loads(line)

    def dispatch(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        reply = self.read()
        if reply.get("type") != "messages":
            raise AssertionError("Worker returned an invalid envelope")
        return reply["messages"]

    def __exit__(self, exc_type, exc, traceback):
        self.selector.close()
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)
        self.process.stdout.close()
        self.process.stderr.close()


def invoke(id="lookup", key_present=True):
    return {
        "type": "invoke", "id": id, "contribution": "search", "operation": "searchAssets",
        "context": {"identity": {"kind": "movie", "title": "Example Movie", "ids": {"imdb": "tt1234567"}}, "language": "en"},
        "credentials": {"apiKey": {"present": key_present}},
    }


class PortableWorkerTests(unittest.TestCase):
    def test_missing_key_stops_before_a_host_call(self):
        with Worker() as worker:
            messages = worker.dispatch(invoke(key_present=False))
            self.assertEqual(len(messages), 1)
            self.assertEqual(messages[0]["type"], "result")
            self.assertEqual(messages[0]["status"], "auth_required")

    def test_actual_wasm_search_completion_and_resolution(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke())
            self.assertEqual(call["operation"], "network.http")
            self.assertEqual(call["request"]["credential"], "apiKey")
            self.assertNotIn("Authorization", call["request"]["headers"])
            result, = worker.dispatch({
                "type": "host-result", "id": call["id"], "invocation": "lookup", "status": "ok",
                "response": {"status": 200, "body": (PACKAGE / "tests/fixtures/movie.json").read_text(), "headers": {}},
            })
            self.assertEqual(result["status"], "match")
            self.assertEqual(len(result["data"]["candidates"]), 2)
            resolution = invoke(id="resolve")
            resolution["operation"] = "resolveAsset"
            resolution["context"] = {"candidate": result["data"]["candidates"][0]}
            resolved, = worker.dispatch(resolution)
            self.assertEqual(resolved["data"]["url"], "https://api.thesubtitledb.org/get/10001")
            self.assertEqual(resolved["data"]["credential"], "apiKey")
            self.assertNotIn("malicious.example", json.dumps(resolved))

    def test_cancel_rejects_late_completion(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke())
            self.assertEqual(worker.dispatch({"type": "cancel", "id": "lookup"}), [])
            messages = worker.dispatch({
                "type": "host-result", "id": call["id"], "invocation": "lookup", "status": "ok",
                "response": {"status": 200, "body": (PACKAGE / "tests/fixtures/movie.json").read_text(), "headers": {}},
            })
            self.assertEqual(messages, [])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    args = parser.parse_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[__file__], verbosity=2)
