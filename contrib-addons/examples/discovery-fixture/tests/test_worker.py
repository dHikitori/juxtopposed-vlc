#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify portable source operations through the actual worker; no network IO."""
import argparse
import json
from pathlib import Path
import selectors
import subprocess
import unittest

PACKAGE = Path(__file__).resolve().parent.parent
WORKER = None

class PortableDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.process = subprocess.Popen([str(WORKER), str(PACKAGE / "dist/addon.wasm")], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env={"LANG": "C"})
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.assertEqual(self.read(), {"type": "ready", "protocol": "wasm-message-v1"})

    def read(self):
        self.assertTrue(self.selector.select(timeout=10), "Worker response timed out")
        line = self.process.stdout.readline()
        self.assertTrue(line, "Worker stopped before returning a response")
        return json.loads(line)

    def invoke(self, operation, context=None):
        self.process.stdin.write(json.dumps({"type": "invoke", "id": "fixture-test", "contribution": "catalogue", "operation": operation, "context": context or {}}) + "\n")
        self.process.stdin.flush()
        envelope = self.read()
        self.assertEqual(envelope["type"], "messages")
        self.assertEqual(len(envelope["messages"]), 1)
        reply = envelope["messages"][0]
        self.assertEqual(reply["type"], "result")  # No host-call IO was requested.
        return reply

    def tearDown(self):
        self.selector.close()
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)
        self.process.stdout.close()
        self.process.stderr.close()

    def test_roots_browse_and_pagination(self):
        root = self.invoke("roots")["data"]["items"][0]["id"]
        first = self.invoke("browse", {"nodeRef": root})
        self.assertEqual(len(first["data"]["items"]), 2)
        second = self.invoke("browse", {"nodeRef": root, "cursor": first["data"]["cursor"]})
        self.assertEqual(second["data"]["items"][0]["id"], "sample-podcast")

    def test_search_and_suggestions(self):
        self.assertEqual(self.invoke("search", {"query": "jazz"})["data"]["items"][0]["id"], "sample-jazz")
        self.assertEqual(len(self.invoke("suggest", {"limit": 1})["data"]["items"]), 1)

    def test_playback_is_explicitly_unsupported(self):
        result = self.invoke("resolve", {"itemRef": "sample-jazz"})
        self.assertEqual(result["status"], "unsupported")
        self.assertNotIn("url", result["data"])

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    WORKER = parser.parse_args().worker.resolve()
    unittest.main(argv=[__file__], verbosity=2)
