#!/usr/bin/python3
import json, os, sys, time
mode = os.path.basename(sys.argv[1])
if mode in ("--precompile", "--precompiled"):
    # This stand-in has no compiled form; the host then compiles from source.
    sys.exit(1)
def output(value):
    print(json.dumps(value), flush=True)
if mode == "invalid-protocol":
    output({"type":"ready","protocol":"unsafe-v0"})
    time.sleep(5)
    sys.exit(0)
if mode == "oversized-message":
    print("x" * (3 * 1024 * 1024), flush=True)
    time.sleep(5)
    sys.exit(0)
output({"type":"ready","protocol":"wasm-message-v1"})
quota = {}
for line in sys.stdin:
    request = json.loads(line)
    if request.get("type") == "invoke":
        invocation = request["id"]
        if mode in ("quota-complete", "quota-active"):
            count = min(4097, max(1, int(request.get("context", {}).get("count", 1))))
            quota[invocation] = [count, 0]
            for offset in range(0, count, 64):
                output({"type":"messages","messages":[{"type":"host-call","id":"call-" + str(number),
                    "invocation":invocation,"operation":"fixture.no-handler","request":{}}
                    for number in range(offset, min(offset + 64, count))]})
            continue
        if mode == "forged-invocation":
            messages = [{"type":"host-call","id":"call-1","invocation":"foreign",
                "operation":"network.http","request":{"url":"https://api.example.org/search"}}]
        elif mode == "duplicate-host-call":
            call = {"type":"host-call","id":"call-1","invocation":invocation,
                "operation":"network.http","request":{"url":"file:///etc/passwd"}}
            messages = [call, call]
        elif mode == "denied-capability":
            messages = [{"type":"host-call","id":"call-1","invocation":invocation,
                "operation":"shell.execute","request":{}}]
        elif mode == "credential-status":
            messages = [{"type":"result","id":invocation,"status":"match","data":{"credentials":request.get("credentials")}}]
        elif mode == "storage-host":
            messages = [{"type":"host-call","id":"call-1","invocation":invocation,
                "operation":"storage.set","request":{"key":"guest.value","value":{"saved":True}}}]
        elif mode == "cancel-late" and request.get("context", {}).get("fast"):
            messages = [{"type":"result","id":invocation,"status":"match","data":{"ok":True}}]
        elif mode in ("host-service", "cancel-late", "reuse-host-call"):
            messages = [{"type":"host-call","id":"call-1","invocation":invocation,
                "operation":"playback.observe","request":{"package":"org.attacker.forged"}}]
        else:
            if mode == "late-result":
                open(sys.argv[1] + ".received", "w").close()
                time.sleep(0.2)
            message = {"type":"result","id":invocation,"status":"match","data":{"ok":True}}
            messages = [message, message] if mode == "duplicate-result" else [message]
        output({"type":"messages","messages":messages})
    elif request.get("type") == "cancel" and mode == "cancel-late":
        open(sys.argv[1] + ".cancelled", "w").close()
        output({"type":"messages","messages":[{"type":"host-call","id":"late-call",
            "invocation":request["id"],"operation":"playback.observe","request":{}}]})
    elif request.get("type") == "host-result":
        if mode in ("quota-complete", "quota-active"):
            value = quota.get(request["invocation"])
            if value is not None:
                value[1] += 1
                if value[0] == value[1]:
                    if mode == "quota-complete":
                        output({"type":"messages","messages":[{"type":"result","id":request["invocation"],"status":"match","data":{"count":value[1]}}]})
                        del quota[request["invocation"]]
                    else:
                        open(sys.argv[1] + "." + request["invocation"] + ".drained", "w").close()
            continue
        output({"type":"messages","messages":[{"type":"result","id":request["invocation"],
            "status":"match","data":{"denied":request.get("status") == "error", "response":request.get("response", {})}}]})
        if mode == "reuse-host-call":
            output({"type":"messages","messages":[{"type":"host-call","id":"call-1","invocation":request["invocation"],
                "operation":"playback.observe","request":{}}]})
    elif request.get("type") == "cancel" and mode in ("quota-complete", "quota-active"):
        quota.pop(request["id"], None)
