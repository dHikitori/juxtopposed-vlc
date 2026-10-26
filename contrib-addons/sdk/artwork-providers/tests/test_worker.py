#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test portable artwork packages with synthetic host responses."""
import argparse
import json
from pathlib import Path
import selectors
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
WORKER = None
RELEASE = "76df3287-6cda-33eb-8e9a-044b5e15ffdd"


class Worker:
    def __init__(self, package):
        self.package = package

    def __enter__(self):
        module = ROOT / "examples" / self.package / "dist/addon.wasm"
        self.process = subprocess.Popen([str(WORKER), str(module)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, env={"LANG": "C"})
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        if self.read() != {"type": "ready", "protocol": "wasm-message-v1"}:
            raise AssertionError("The worker did not accept the portable ABI.")
        return self

    def read(self):
        if not self.selector.select(timeout=10):
            raise AssertionError("The worker did not respond within ten seconds.")
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError("The worker stopped before its response.")
        return json.loads(line)

    def dispatch(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        reply = self.read()
        if reply.get("type") != "messages":
            raise AssertionError("The worker response is not correct.")
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


def invoke(identity, operation="enrich", id="lookup"):
    return {"type": "invoke", "id": id, "contribution": "artwork", "operation": operation,
            "context": {"identity": identity, "fields": ["artwork"]}}


def complete(worker, call, body, truncated=False):
    return worker.dispatch({"type": "host-result", "id": call["id"], "invocation": call["invocation"],
                            "status": "ok", "response": {"status": 200, "body": body,
                            "headers": {}, "truncated": truncated}})


class PortableArtworkTests(unittest.TestCase):
    def test_french_tv_search_and_issued_resolution(self):
        with Worker("french-tv") as worker:
            reply = worker.dispatch(invoke({"kind": "unknown", "title": "France Info HD"}, "searchAssets"))[0]
            self.assertEqual(reply["status"], "match")
            candidate = reply["data"]["candidates"][0]
            self.assertEqual(candidate["evidence"]["type"], "label")
            request = {"type": "invoke", "id": "resolve", "contribution": "artwork", "operation": "resolveAsset",
                       "context": {"candidate": candidate}}
            asset = worker.dispatch(request)[0]
            self.assertIn("500px-Franceinfo", asset["data"]["url"])
            request["id"] = "forged"
            request["context"]["candidate"]["id"] = "forged"
            self.assertEqual(worker.dispatch(request)[0]["status"], "error")

    def test_lastfm_page_uses_current_image_host(self):
        with Worker("lastfm") as worker:
            call = worker.dispatch(invoke({"kind": "musicTrack", "title": "Song", "artist": "Artist", "album": "Album"}))[0]
            self.assertEqual(call["request"]["responseBytes"], 65653)
            self.assertTrue(call["request"]["allowTruncated"])
            reply = complete(worker, call, "<meta content='https://lastfm-img.freetls.fastly.net/i/u/300x300/fixture.jpg' property='og:image'>", True)[0]
            self.assertEqual(reply["status"], "match")
            self.assertEqual(reply["data"]["records"][0]["evidence"]["type"], "label")

    def test_google_keeps_multiple_choices_and_omits_script_only_pages(self):
        with Worker("google-images") as worker:
            identity = {"kind": "unknown", "title": "Channel", "release": {"listingType": "tv"}}
            call = worker.dispatch(invoke(identity))[0]
            reply = complete(worker, call, "<img src='https://encrypted-tbn0.gstatic.com/images?q=one'><img src='https://encrypted-tbn1.gstatic.com/images?q=two'>", True)[0]
            self.assertEqual(len(reply["data"]["records"]), 2)
            call = worker.dispatch(invoke(identity, id="second"))[0]
            self.assertEqual(complete(worker, call, "<html><script>enablejs()</script></html>", True)[0]["status"], "no_match")

    def test_musicbrainz_resolves_scoped_release_and_amazon_fallback(self):
        for front in [True, False]:
            with Worker("musicbrainz") as worker:
                identity = {"kind": "musicTrack", "title": "Song", "artist": "Artist", "album": "Album",
                            "ids": {"musicBrainzAlbumId": RELEASE}}
                call = worker.dispatch(invoke(identity))[0]
                self.assertIn(RELEASE, call["request"]["url"])
                self.assertNotIn("allowTruncated", call["request"])
                data = {"id": RELEASE, "title": "Album", "artist-credit": [{"name": "Artist"}],
                        "cover-art-archive": {"front": front}, "asin": "B000001FI7"}
                reply = complete(worker, call, json.dumps(data))[0]
                self.assertEqual(reply["status"], "match")
                record = reply["data"]["records"][0]
                self.assertEqual(record["evidence"]["type"], "identifier")
                self.assertTrue(record["fields"]["artwork"]["url"].startswith(
                    "https://coverartarchive.org/" if front else "https://images-na.ssl-images-amazon.com/"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[__file__, *remaining])
