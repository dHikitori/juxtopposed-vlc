#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the session author contract with a Draft 2020-12 validator."""
import copy
import json
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError as error:
    raise SystemExit("Install jsonschema 4.23.0 to run this check.") from error


def main():
    path = Path(__file__).with_name("session-requests.schema.json")
    schema = json.loads(path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)
    continuation = Draft202012Validator(schema["$defs"]["continuationResult"])
    session = "hakone-session:" + "a" * 32
    cases = []

    def call(operation, request):
        return {"type": "host-call", "id": "call-1", "invocation": "slice-1",
                "operation": operation, "request": request}

    def case(name, value, valid=True, contract=validator):
        cases.append((name, value, valid, contract))

    for operation in ["network.websocket.open", "network.websocket.receive",
                      "network.websocket.close", "playback.observe", "session.renew", "session.end"]:
        case(operation, call(operation, {"session": session}))
        case(operation + "-extra-url", call(operation, {"session": session, "url": "wss://other.example"}), False)
    for kind in ["text", "binary"]:
        case("send-" + kind, call("network.websocket.send", {"session": session, "kind": kind, "data": "hello"}))
    for kind, data in [("other", "hello"), ("text", "x" * 65537), ("binary", 42)]:
        case("send-invalid-" + kind, call("network.websocket.send", {"session": session, "kind": kind, "data": data}), False)

    command = {"session": session, "commandId": "command-1", "sequence": "1",
               "mediaEpoch": "captured-media", "expectedRevision": "0"}
    parameters = {"play": {}, "pause": {}, "seek": {"positionUs": "1000000"},
                  "setRate": {"rate": 1.05, "durationMs": 5000}}
    for operation, values in parameters.items():
        request = dict(command, operation=operation, parameters=values)
        case(operation, call("playback.command", request))
        case(operation + "-foreign-parameter", call("playback.command", dict(request, parameters={"url": "file:///private"})), False)
    rate = dict(command, operation="setRate", parameters={"rate": 1.05, "durationMs": 5001})
    case("rate-duration-limit", call("playback.command", rate), False)
    for sequence in [0, "0", -1, "-1", 1.5, 9007199254740992, "01", "1" * 21]:
        case("invalid-sequence-" + str(sequence), call("playback.command", dict(command, operation="play", parameters={}, sequence=sequence)), False)
    case("safe-integer-sequence", call("playback.command", dict(command, operation="pause", parameters={}, sequence=9007199254740991)))
    case("native-integer-string", call("playback.command", dict(command, operation="pause", parameters={}, sequence="18446744073709551615")))

    base = call("playback.observe", {"session": session})
    for name in base:
        value = copy.deepcopy(base); del value[name]
        case("missing-" + name, value, False)
    for key, value in [("id", ""), ("id", "x" * 129), ("id", "bad\ncall"),
                       ("id", "bad\u0085call"), ("operation", "playback.play"),
                       ("type", "invoke"), ("hakone", {}), ("vlc", {})]:
        case("invalid-envelope-" + key + "-" + repr(value), dict(base, **{key: value}), False)
    for value in ["vlc-session:" + "a" * 32, "hakone-session:" + "A" * 32, "hakone-session:" + "a" * 31]:
        case("invalid-session-" + value, call("playback.observe", {"session": value}), False)
    for value in [{"continue": True}, {"continue": False}]:
        case("continuation-" + str(value), value, contract=continuation)
    for value in [{}, {"continue": 1}, {"continue": "true"}, {"continue": True, "session": session}]:
        case("invalid-continuation-" + str(value), value, False, continuation)

    failures = [name for name, value, valid, contract in cases if contract.is_valid(value) != valid]
    if failures:
        raise SystemExit("Session schema checks failed: " + ", ".join(failures))
    print(json.dumps({"draft": "2020-12", "cases": len(cases), "failed": 0}))


if __name__ == "__main__":
    main()
