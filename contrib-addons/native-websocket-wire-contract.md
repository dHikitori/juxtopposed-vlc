# Session connection wire format

`AddonWebSocketCodec` is a host-only protocol layer. It does not open a socket,
resolve a host, read a credential, approve an endpoint, or grant player control.
The connection owner must serialize calls on its network thread.

The wire rules follow [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html).
The host checks the HTTP upgrade response and the nonce response. It accepts only
an offered subprotocol. It does not negotiate compression or other extensions.
Server frames must be unmasked. Client frames use a fresh host mask. Fragmented
messages can contain control frames. Complete text and close reasons must be valid
UTF-8. A partial code point at the end is an error. A close response cannot reopen
the channel. An abrupt end reports the local abnormal-close status.

## Host limits

The request builder accepts WSS on port 443. It refuses user information and
fragments. It copies only the captured host, path, query, and optional protocols.
It sends no cookie, authorization header, or browser origin. Endpoint approval and
TLS checks remain separate requirements.

The protocol limits are:

- 16 KiB for the complete HTTP header and at most 64 response fields.
- Eight offered subprotocols, with at most 128 ASCII token characters each.
- 64 KiB for a complete text or binary message.
- 128 KiB for buffered wire data.
- 16 frames for one fragmented message.
- 32 frames per parser call, including control frames.
- 125 bytes for a control frame.

One returned batch has at most 64 KiB of message data and at most 32 control
replies. `more` tells the owner to schedule another bounded parser call. The owner
must also bound its event, write, and worker queues. It must apply an idle timeout,
a connection limit, and a retiring-work limit. This codec does not provide those
connection policies.

Malformed lengths, masks, reserved bits, opcodes, text, close values, duplicate
headers, unexpected protocols, or transfer framing fail the connection. No failure
permits HTTP fallback or a connection to a different endpoint.

## Connection and authority boundary

Before use, the host transport must validate the declared or approved WSS origin,
resolve and check all addresses, pin a validated address, and verify TLS against
the original hostname. It must not use an automatic redirect, ambient proxy,
cookie jar, or unapproved credential. A memory-only authority check must guard
network writes and result delivery. Saved approval checks belong on a background
admission path.

An opaque connection reference must bind the package, contribution, account,
configuration, and host session. A worker cannot select a native player origin or
lease. The separate native playback lease must arbitrate every accepted command
against local input and current media state.

## Verification

The focused suite has 19 cases. It covers the published nonce example, a partial
upgrade, combined header/frame input, subprotocol selection, client masks, extended
lengths, UTF-8, fragmentation, control frames, message and pump limits, close,
and abrupt end. Normal and ASAN/UBSAN runs pass with leak and stack-use-after-return
checks. Connection, worker-service, and two-player integration remain open.
