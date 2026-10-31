#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test the package Wasm with the host worker and a synthetic episode list."""
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
            raise AssertionError("The worker did not accept the portable ABI.")
        return self

    def read(self):
        if not self.selector.select(timeout=10):
            raise AssertionError("The worker did not respond within ten seconds.")
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError("The worker stopped before its ABI response.")
        return json.loads(line)

    def dispatch(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        reply = self.read()
        if reply.get("type") != "messages":
            raise AssertionError("The worker response is invalid.")
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


def invoke(operation, context, id="call"):
    return {"type": "invoke", "id": id, "contribution": operation, "operation": operation, "context": context,
            "settings": {}, "credentials": {}, "hostCapabilities": {}}


def answer(worker, call, response):
    return worker.dispatch({"type": "host-result", "id": call["id"], "invocation": call["invocation"],
                            "status": "ok", "response": response})


def item(id, episode, version=0, **values):
    row = {"id": id, "kind": "folder", "name": f"Show.S01E{episode:02}.mkv", "title": "", "season": 1,
           "episodes": [episode], "version": version, "progress": 0, "played": False, "current": False, "video": True}
    row.update(values)
    return row


SEASON = {"kind": "folder", "show": "Show", "loading": False, "limited": False, "offset": 0, "total": 3, "currentSeason": 1,
          "seasons": [{"season": 1, "count": 2}],
          "items": [item("media:a", 1), item("media:b", 1, 1, current=True), item("media:c", 2, artwork="media:c")]}


def schema():
    manifest = json.loads((PACKAGE / "addon.json").read_text(encoding="utf-8"))
    return manifest["contributions"][0]["stateSchema"]


class PortableWorkerTests(unittest.TestCase):
    def test_self_test(self):
        with Worker() as worker:
            result, = worker.dispatch({"type": "self-test", "id": "check"})
            self.assertEqual(result["status"], "ok")

    def test_availability_needs_a_series(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("context", {"media": {"revision": 1, "kind": "video"}, "panel": "episodes"}))
            self.assertEqual((call["operation"], call["request"]), ("media.siblings", {"limit": 1}))
            result, = answer(worker, call, SEASON)
            self.assertEqual(result["data"], {"available": True})
            call, = worker.dispatch(invoke("context", {"media": {"revision": 2}}))
            result, = answer(worker, call, {"kind": "none", "seasons": [], "items": [], "total": 0})
            self.assertEqual(result["data"], {"available": False})

    def test_open_panel_state_agrees_with_the_manifest(self):
        with Worker() as worker:
            context = {"media": {"revision": 3}, "state": {"status": "loading", "revision": -1}}
            probe, = worker.dispatch(invoke("context", context))
            read, = answer(worker, probe, SEASON)
            self.assertEqual(read["request"]["season"], 1)
            artwork, = answer(worker, read, SEASON)
            self.assertEqual(artwork["request"]["artwork"], ["media:a"])
            result, = answer(worker, artwork, {})
            state = result["data"]["state"]
            self.assertEqual(state["status"], "ready")
            self.assertEqual([row["title"] for row in state["episodes"]], ["Episode 1", "Episode 2"])
            self.assertEqual(state["episodes"][0]["playKey"], "media:b")
            # The state schema check needs jsonschema 4 (Draft 2020-12).
            try:
                from importlib.metadata import version
                import jsonschema
                if int(version("jsonschema").split(".")[0]) < 4:
                    return
            except ImportError:
                return
            jsonschema.Draft202012Validator(schema()).validate(state)

    def test_clicks_are_effects(self):
        with Worker() as worker:
            result, = worker.dispatch(invoke("play", {"arguments": {"key": "media:c", "fromStart": True}}))
            self.assertEqual(result["data"], {"effect": {"play": {"id": "media:c", "fromStart": True}}})
            result, = worker.dispatch(invoke("refresh", {"arguments": {"folder": True}}))
            self.assertEqual(result["data"], {"effect": {"rescan": {"chooseFolder": True}}})

    def test_cancel_ignores_a_late_result(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("context", {"media": {"revision": 1}}, id="first"))
            self.assertEqual(worker.dispatch({"type": "cancel", "id": "first"}), [])
            self.assertEqual(answer(worker, call, SEASON), [])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    args = parser.parse_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[__file__], verbosity=2)
