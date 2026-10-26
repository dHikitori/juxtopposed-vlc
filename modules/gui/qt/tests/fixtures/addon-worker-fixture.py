#!/usr/bin/python3
import json, os, sys, time
mode = os.path.basename(sys.argv[1])
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
for line in sys.stdin:
    request = json.loads(line)
    if request.get("type") == "invoke":
        invocation = request["id"]
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
        elif mode == "cancel-late" and request.get("context", {}).get("fast"):
            messages = [{"type":"result","id":invocation,"status":"match","data":{"ok":True}}]
        elif mode in ("host-service", "cancel-late"):
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
        output({"type":"messages","messages":[{"type":"result","id":request["invocation"],
            "status":"match","data":{"denied":request.get("status") == "error", "response":request.get("response", {})}}]})
