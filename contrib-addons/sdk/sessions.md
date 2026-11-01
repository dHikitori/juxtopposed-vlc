# Shared playback sessions

A session controller add-on keeps playback in step with other players
through its own room protocol and relay service. The user approves each
session in Extensions settings.

Declare `session@1` and a `session.controller` contribution with the `run`
operation. Use the normal message SDK. Each host call has a unique ID and its
current invocation ID. Keep its pending state in `Pending`. Cancel removes it.

## Host approval

Only host C++ can prepare or approve a session. A package cannot approve its
own room or resume control after local input. Installation and enablement do
not create a session.

The host shows the captured add-on name, relay origin, room label, observation
scope, background behavior, and playback operations. The user can join with
observation only. A separate choice permits playback control. This profile
requires the granted `session.background` scope for continued operation after
the session view closes. The host keeps Stop or End session outside package
views. A package cannot hide that control.

Approval uses a one-use ticket with a 30-second limit. It binds the package
release, contribution, account, settings, relay, and native media/input state.
A changed value needs a new ticket. The host also owns Resume synchronization
after local input.

## Explicit WSS credentials

WSS uses port 443. Declare a canonical origin with no final slash, explicit
port, user information, query, or fragment. For example:

```json
{
  "network.websocket": { "origins": ["wss://relay.example"] },
  "credentials.use": {
    "bindings": [{
      "field": "roomKey",
      "origin": "wss://relay.example",
      "header": "Authorization",
      "prefix": "Bearer "
    }]
  }
}
```

Declare `roomKey` as a string with `x-hakone-secret: true` in the settings
schema. The host inserts its value only for that exact approved WSS binding.
An HTTPS binding does not permit WSS use. A WSS binding does not permit HTTP
use. The host refuses cookie, origin, upgrade, framing, and WebSocket protocol
headers as credential targets. The guest receives presence flags only.

The transport checks every DNS address and pins one approved public address.
TLS verifies the original relay hostname. It uses no ambient proxy, cookie
store, redirect, or insecure fallback.

## Guest calls

The request schema is [session-requests.schema.json](session-requests.schema.json).
It does not grant permission; the host also checks each call.
`test-session-schema.py` checks the schema against example requests. It needs
Python 3.10 or later and `jsonschema` 4.23.0.

The host supplies an opaque session reference after approval. Send it through
the normal SDK host-call envelope:

```json
{
  "type": "host-call",
  "id": "receive-1",
  "invocation": "current-invocation",
  "operation": "network.websocket.receive",
  "request": { "session": "hakone-session:00000000000000000000000000000001" }
}
```

| Operation | Request and result |
| --- | --- |
| `network.websocket.open` | Supply the session. The host uses its captured relay and protocols. Success means that TLS and the WebSocket upgrade passed. |
| `network.websocket.send` | Supply the session, `kind` (`text` or `binary`), and `data`. Text uses UTF-8. Binary data uses canonical base64. `queued: true` means local queue admission. |
| `network.websocket.receive` | Supply the session. Only one receive can wait for a socket. It returns `messages`, with `kind` and `data` for each complete message. An empty batch ends a nine-second wait. |
| `network.websocket.close` | End the connection and its native session authority. |
| `playback.observe` | Return approved timeline fields and authority references. It returns no media path, credential, or media bytes. |
| `playback.command` | Supply the observed media epoch and transport revision. Status `applied` requires a matching native execution result. FIFO admission alone is insufficient. |
| `session.renew` | Extend a current session by at most 30 seconds. It cannot resume suspended control. |
| `session.end` | End the session and revoke its native control lease. |

An opaque reference belongs to one package, contribution, account, release, and
host session. Do not share it with another add-on. A disconnected or ended
reference cannot open a new connection.

The wire limit is 64 KiB per complete message. The guest IPC profile permits
64 KiB of UTF-8 text or 48 KiB of binary data. Base64 expands binary data before
it enters JSON. A receive batch has at most 32 messages, 64 KiB of encoded data,
and 256 KiB of complete serialized JSON. JSON escapes and field names count.
A complete message that cannot fit the IPC limit ends the connection.

The network owner permits 16 queued sends and 256 KiB of queued send data.
Eight active or retiring connections can exist in one process. Blocked native
DNS retains its connection slot. No API exposes a native socket or inbound
listener.

Each connection has a one-second rate budget. It permits 64 output frames and
128 input events or required replies. Each direction has a 1 MiB byte budget.
The connection ends if a budget is exceeded. These limits include protocol
traffic. This profile does not permit a media relay.

## Playback commands

Use `play`, `pause`, `seek`, or `setRate`. A command includes:

```json
{
  "session": "hakone-session:00000000000000000000000000000001",
  "operation": "seek",
  "commandId": "room-command-12",
  "sequence": "12",
  "mediaEpoch": "the-observed-opaque-epoch",
  "expectedRevision": "34",
  "parameters": { "positionUs": "20000000" }
}
```

Use decimal strings for 64-bit sequence, revision, and timestamp values. The
host also accepts exact nonnegative JSON integers up to 2^53 minus one. A
sequence must increase. A command ID has one use in a session.

Seek uses integer microseconds. A speed correction uses `rate` and
`durationMs`, with a maximum duration of 5,000 milliseconds. Native admission
limits it to five percent of the saved user rate and the current input limits.
The native input restores only the correction that it still owns. It never
overwrites a later user speed choice.

Optional `atUs` uses the same local monotonic clock as `observedAtUs`. It can
schedule at most five seconds ahead. A command more than 200 milliseconds late
is refused. Do not copy a monotonic timestamp from another machine.

Local keyboard, mouse, hotkey, and native API actions take priority. Native
admission and execution both check the media/input scope, lease, sequence,
and transport revisions. Local input suspends remote control. A guest renewal
does not resume it. Disconnect, expiry, withdrawal, or session end revokes
control. Cancel after native admission revokes the lease. It does not claim to
reverse an action that already began.

Unknown duration, buffering, live window, and output latency use explicit
unavailable values. Do not infer live playback from a missing duration. These
APIs do not promise frame-accurate or sample-accurate synchronization.

## Bounded guest work

The host starts `run` only after explicit session approval. Its context contains
the exact session reference. Each slice has a 25-second host limit. The guest
must finish before that limit. A successful `match` or `partial` result has
exactly one data field: `continue`, with a boolean value. A true value requests
another slice. The host waits at least 500 milliseconds before it starts one.
False ends the session. An error, timeout, malformed result, changed authority,
or worker failure ends native control and WSS.

Each new invocation checks the same contribution, account, release, and session
authority. Continuation creates no new permission. It cannot start another
guest entry point or continue after host End session.

The host binds calls to that exact active invocation ID. Another invocation of
the same contribution cannot use the reference. Each invocation ID has one use
per session, with a limit of 4,096 IDs. The host retires old receives and pending
callbacks before the next slice. An admitted native command that is still
pending ends the session. A new slice does not resume control after local input.

The 25-second deadline starts before the invocation. The host refuses a late
result.
