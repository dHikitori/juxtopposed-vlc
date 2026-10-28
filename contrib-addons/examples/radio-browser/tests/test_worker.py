#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test the package Wasm with the real worker and offline API records."""
import argparse
import json
from pathlib import Path
import queue
import subprocess
import threading
import unittest

PACKAGE = Path(__file__).resolve().parent.parent
WORKER = None
UUID = "550e8400-e29b-41d4-a716-000000000001"


class Worker:
    def __enter__(self):
        self.process = subprocess.Popen([str(WORKER), str(PACKAGE / "dist/addon.wasm")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.messages = queue.Queue()
        def read():
            try:
                for line in self.process.stdout:
                    self.messages.put(json.loads(line))
            except Exception as error:
                self.messages.put(error)
            self.messages.put(EOFError("The worker ended."))
        self.reader = threading.Thread(target=read)
        self.reader.start()
        if self.read() != {"type": "ready", "protocol": "wasm-message-v1"}:
            raise AssertionError("The worker did not accept the message ABI.")
        return self

    def read(self):
        value = self.messages.get(timeout=10)
        if isinstance(value, Exception):
            raise value
        return value

    def dispatch(self, value):
        self.process.stdin.write(json.dumps(value) + "\n")
        self.process.stdin.flush()
        result = self.read()
        if result.get("type") != "messages":
            raise AssertionError("The worker returned an incorrect packet.")
        return result["messages"]

    def __exit__(self, exc_type, exc, traceback):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)
        self.reader.join(timeout=3)
        self.process.stdout.close()
        error = self.process.stderr.read()
        self.stderr = error
        self.process.stderr.close()
        if exc_type is None and (error or self.process.returncode):
            raise AssertionError("The worker failed: " + error)


def invoke(operation, context=None, invocation="test"):
    return {"type": "invoke", "id": invocation, "contribution": "stations", "operation": operation,
            "context": context or {}, "settings": {}, "credentials": {}, "hostCapabilities": {"mediaStreamAvailable": True}}


def station(url="https://stream.example.org/live.mp3", uuid=UUID):
    return {"stationuuid": uuid, "name": "Example radio", "url_resolved": url,
            "url": "https://original.example.org/list.pls", "favicon": "https://image.example.org/cover.png",
            "lastcheckok": 1, "ssl_error": 0, "codec": "MP3", "hls": 0}


def complete(worker, call, rows, **response):
    return worker.dispatch({"type": "host-result", "id": call["id"], "invocation": call["invocation"], "status": "ok",
        "response": {"status": 200, "body": json.dumps(rows), "headers": {}, **response}})


class PortableRadioTests(unittest.TestCase):
    def test_roots_have_no_network_or_stream_address(self):
        with Worker() as worker:
            result, = worker.dispatch(invoke("roots"))
            self.assertEqual(result["status"], "match")
            self.assertEqual(len(result["data"]["items"]), 4)
            self.assertNotIn("https://", json.dumps(result))

    def test_browse_filters_and_deduplicates_without_url_snapshots(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("browse", {"nodeRef": "popular"}))
            self.assertIn("is_https=true", call["request"]["url"])
            bad = station("http://insecure.example.org/live")
            local = station("https://127.0.0.1/live")
            result, = complete(worker, call, [bad, local, station(), station()])
            self.assertEqual(result["data"]["items"], [{"id": UUID, "kind": "media", "title": "Example radio", "mediaKind": "audio"}])
            self.assertNotIn("https://", json.dumps(result))

    def test_search_encoding_and_page_mirror_are_bound(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("search", {"query": "Jazz &name=other", "filters": {}}))
            self.assertTrue(call["request"]["url"].endswith("&name=Jazz%20%26name%3Dother"))
            records = [station(uuid=f"550e8400-e29b-41d4-a716-{index:012x}") for index in range(10)]
            result, = complete(worker, call, records)
            cursor = result["data"]["cursor"]
            next_call, = worker.dispatch(invoke("search", {"query": "Jazz &name=other", "cursor": cursor}, "next"))
            self.assertIn("offset=10", next_call["request"]["url"])
            self.assertEqual(call["request"]["url"].split("/json/")[0], next_call["request"]["url"].split("/json/")[0])
            complete(worker, next_call, [])
            invalid, = worker.dispatch(invoke("search", {"query": "Other", "cursor": cursor}, "bad"))
            self.assertEqual(invalid["status"], "error")

    def test_each_playback_resolve_fetches_the_station_again(self):
        with Worker() as worker:
            a, = worker.dispatch(invoke("resolve", {"itemRef": UUID, "intent": "play"}, "first"))
            self.assertIn("/json/stations/byuuid?uuids=" + UUID, a["request"]["url"])
            first, = complete(worker, a, [station()])
            self.assertEqual(first["data"], {"url": "https://stream.example.org/live.mp3", "format": "progressive"})
            b, = worker.dispatch(invoke("resolve", {"itemRef": UUID, "intent": "play"}, "replay"))
            self.assertNotEqual(a["id"], b["id"])
            second, = complete(worker, b, [station("https://other.example.org/new.mp3")])
            self.assertEqual(second["data"]["url"], "https://other.example.org/new.mp3")

    def test_cancel_and_foreign_invocation_do_not_publish(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("browse", {"nodeRef": "popular"}))
            self.assertEqual(worker.dispatch({"type": "host-result", "id": call["id"], "invocation": "other", "status": "ok",
                "response": {"status": 200, "body": "[]"}}), [])
            self.assertEqual(worker.dispatch({"type": "cancel", "id": "test"}), [])
            self.assertEqual(complete(worker, call, [station()]), [])

    def test_retry_stays_in_declared_origins_and_stops_after_two(self):
        with Worker() as worker:
            first, = worker.dispatch(invoke("browse", {"nodeRef": "popular"}))
            second, = worker.dispatch({"type": "host-result", "id": first["id"], "invocation": "test", "status": "error", "error": "Offline"})
            self.assertTrue(first["request"]["url"].startswith("https://de1.api.radio-browser.info/json/"))
            self.assertTrue(second["request"]["url"].startswith("https://de1.api.radio-browser.info/json/"))
            end, = complete(worker, second, [], status=503)
            self.assertEqual(end["status"], "error")

    def test_resolve_denies_foreign_station_and_incomplete_reply(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("resolve", {"itemRef": UUID, "intent": "play"}))
            result, = complete(worker, call, [station(uuid="550e8400-e29b-41d4-a716-000000000002")])
            self.assertEqual(result["status"], "no_match")
            call, = worker.dispatch(invoke("browse", {"nodeRef": "popular"}, "page"))
            result, = complete(worker, call, [station()], truncated=True)
            self.assertEqual(result["status"], "error")
            self.assertNotIn("items", result["data"])

    def test_full_station_records_fit_the_unchanged_worker_budget(self):
        with Worker() as worker:
            call, = worker.dispatch(invoke("browse", {"nodeRef": "popular"}))
            records = []
            for index in range(10):
                value = station(uuid=f"550e8400-e29b-41d4-a716-{index:012x}")
                value.update(changeuuid=UUID, serveruuid=UUID, country="Example country", countrycode="GB", state="",
                    language="english", languagecodes="eng", tags="jazz,radio,music", votes=10, bitrate=128,
                    lastchangetime="2026-01-01 00:00:00", lastchangetime_iso8601="2026-01-01T00:00:00Z",
                    lastchecktime="2026-01-01 00:00:00", lastchecktime_iso8601="2026-01-01T00:00:00Z",
                    lastcheckoktime="2026-01-01 00:00:00", lastcheckoktime_iso8601="2026-01-01T00:00:00Z",
                    lastlocalchecktime="2026-01-01 00:00:00", lastlocalchecktime_iso8601="2026-01-01T00:00:00Z",
                    clicktimestamp="2026-01-01 00:00:00", clicktimestamp_iso8601="2026-01-01T00:00:00Z",
                    clickcount=10, clicktrend=1, geo_lat=0.0, geo_long=0.0, has_extended_info=False, iso_3166_2="")
                records.append(value)
            records[0]["ignored"] = "x" * (24 * 1024 - 256 - len(json.dumps(records).encode()))
            self.assertLess(len(json.dumps(records).encode()), 24 * 1024)
            result, = complete(worker, call, records)
            self.assertEqual(result["status"], "match")
            self.assertEqual(len(result["data"]["items"]), 10)

    def test_unavailable_native_playback_never_starts_a_resolve(self):
        with Worker() as worker:
            value = invoke("resolve", {"itemRef": UUID, "intent": "play"})
            value["hostCapabilities"]["mediaStreamAvailable"] = False
            result, = worker.dispatch(value)
            self.assertEqual(result["type"], "result")
            self.assertEqual(result["status"], "unsupported")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    args = parser.parse_args()
    WORKER = args.worker.resolve()
    unittest.main(argv=[__file__], verbosity=2)
