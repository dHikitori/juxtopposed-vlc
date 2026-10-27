# Native media stream permission

A package can declare this required or optional permission:

```json
{"media.stream": {"profiles": ["public-https-media@1"]}}
```

Version 1 accepts exactly this profile list. An empty list, duplicate, unknown
profile, future version, or extra scope field is refused. A package cannot add
headers, ports, origins, local files, or private network access to this profile.
Visualizer packages cannot declare it, including as an optional permission.

The host approval screen describes public media playback and the separate
source approval. It refuses an unknown profile. Package approval and enablement
do not create a saved source approval, native playback intent, or media lease.

This permission is separate from `network.http` and `network.websocket`.
It does not give a guest an HTTP destination, a socket, a credential, or media
bytes. A provider can propose a media address only for the exact selected item.
The host must still validate source, account, consent, native intent, and current
package authority before it prepares a contained transport.

The profile permits public HTTPS media on port 443. The host transport checks
each redirect, manifest, segment, map, and key. A changed CDN host does not permit
a private address or an unsupported protocol. The transport does not use API
credentials. See [the native stream contract](native-discovery-stream-contract.md)
and [saved source consent](native-source-consent-contract.md).

`mediaStreamAvailable` reports the mounted host capability. It is false while
the production native source route remains incomplete. The install screen can
show that limit while it still permits package review. Adding this permission
does not enable Play or Enqueue.

WSS credential headers also have a closed host boundary. All header names with
the case-insensitive prefix `sec-websocket-` are reserved, including future
handshake fields. HTTPS credential binding rules remain the same.
