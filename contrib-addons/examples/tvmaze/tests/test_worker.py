#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test the package Wasm with the host worker and synthetic HTTP data."""
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



def invoke(id="lookup"):
    return {"type": "invoke", "id": id, "contribution": "episodes", "operation": "enrich",
            "context": {"identity": {"kind": "episode", "title": "Example Series", "year": 2020,
                                      "season": 1, "episodes": [2]}, "fields": ["episodeTitle"]}}


def complete(worker, call, body):
    return worker.dispatch({"type": "host-result", "id": call["id"], "invocation": call["invocation"],
                            "status": "ok", "response": {"status": 200, "body": json.dumps(body), "headers": {}}})


class PortableWorkerTests(unittest.TestCase):
    def test_episode_lookup_and_source_credit(self):
        with Worker() as worker:
            search, = worker.dispatch(invoke())
            self.assertEqual(search["request"]["url"], "https://api.tvmaze.com/search/shows?q=Example%20Series")
            seasons, = complete(worker, search, [{"show": {"id": 111, "name": "Example Series", "premiered": "2020-01-01"}}])
            episodes, = complete(worker, seasons, [{"id": 444, "number": 1}])
            result, = complete(worker, episodes, [{"id": 333, "name": "Example episode", "season": 1, "number": 2,
                                                   "url": "https://www.tvmaze.com/episodes/333/example"}])
            self.assertEqual(result["status"], "match")
            self.assertEqual(result["data"]["fields"]["episodeTitle"], "Example episode")
            self.assertEqual(result["data"]["attribution"]["name"], "TVmaze")
            self.assertEqual(result["data"]["identity"]["episodes"], [2])

    def test_identify_offers_series_choices_for_the_episode(self):
        with Worker() as worker:
            request = invoke("identify")
            request["operation"] = "identify"
            request["context"]["fields"] = []
            search, = worker.dispatch(request)
            self.assertEqual(search["request"]["url"], "https://api.tvmaze.com/search/shows?q=Example%20Series")
            result, = complete(worker, search, [
                {"show": {"id": 111, "name": "Example Series", "premiered": "2020-01-01"}},
                {"show": {"id": 112, "name": "Example Series", "premiered": "2020-09-01",
                          "url": "https://www.tvmaze.com/shows/112/example-series"}}])
            self.assertEqual(result["status"], "match")
            identities = [row["identity"] for row in result["data"]["candidates"]]
            self.assertEqual([row["ids"]["tvmazeShow"] for row in identities], ["111", "112"])
            self.assertTrue(all(row["season"] == 1 and row["episodes"] == [2] for row in identities))
            # The chosen series is then looked up by its ID.
            chosen = invoke("details")
            chosen["context"]["identity"] = identities[1]
            show, = worker.dispatch(chosen)
            self.assertEqual(show["request"]["url"], "https://api.tvmaze.com/shows/112")

    def test_cancel_ignores_late_http_result(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke())
            self.assertEqual(worker.dispatch({"type": "cancel", "id": "lookup"}), [])
            self.assertEqual(complete(worker, call, []), [])

    def test_combined_episode_does_not_start_http(self):
        with Worker() as worker:
            request = invoke()
            request["context"]["identity"]["episodes"] = [1, 2]
            result, = worker.dispatch(request)
            self.assertEqual(result["status"], "unsupported")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    args = parser.parse_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[__file__], verbosity=2)
