# Native discovery stream contract

This contract specifies lazy playback of an admitted discovery reference.
The current native queue stores references, but its playback capability remains unavailable.
Keep that capability unavailable until the bridge and transport pass the tests below.

The retained job and transport API now exists in `include/vlc_source_backend.h` and `src/input/source_backend.c`.
It copies the admission URI and input generation.
The host must attach the native policy and lease before transport I/O starts.
The core then owns cancellation of pending and issued transports.
Close revokes their policies and closes their leases without a Qt event.
The backend keeps a closed result unavailable.
Native lifetime tests cover cancellation, shutdown, late results, callback reentry, and 200 result races.
This API has no access-module binding or stream proxy yet.

## Stable item and per-open lease

Keep the queue item, item ID, and `vlc-addon-source:v1:` URI unchanged during playback.
The URI identifies the contribution, account reference, item, and host admission record.
Its encoded data is untrusted until `AddonSourceAdmissions::currentReference()` accepts it.
An admission proves a previous host selection.
It does not enable a source, select an account, or grant network access.

The persistent item holds a source-only native policy for its exact admitted URI.
That policy permits no connection.
Stop must not revoke or replace this persistent policy.
Otherwise the same queue item cannot play again.

Add a core `vlc-addon-source` access bridge with a retained backend and retained jobs.
Use the lifetime pattern of `vlc_metadata_backend`, with native cancellation and one terminal result.
The access open sends the copied URI and a fresh input generation to the backend.
It must not send an input or access pointer to Qt.
The core must verify `INPUT_TYPE_PLAYBACK` and the exact admitted primary item before it creates a job.
`b_preparsing=false` is insufficient: thumbnail inputs also have that value.
Reject thumbnail, preparse, background, and input-free opens.
Install the source-only policy before any fallback, including after an item copy through serialization loses its private policy.
The host validates the admission, current source, account, grants, consent, and intent before resolution.
It validates the same state before it accepts the result.

The native open waits with an interrupt hook and a ten-second deadline.
Stop, backend close, or interface shutdown completes that native wait without a Qt event.
Dispatch and cancel callbacks only schedule host work.
They must not wait for the Qt thread.
A late result releases its policy and transport lease without access to the old input.

The result contains a host-owned loopback URL, a held native policy, and a transport lease.
The native waiter installs the policy on both the input and access objects with `vlc_input_policy_ReplaceObject()`.
Each compare-and-swap uses the exact held bootstrap policy as its expected value.
A failed comparison revokes the new lease and fails the open.
Never replace an unrelated policy or change the item policy to complete this operation.

The access bridge can then redirect to its loopback resource.
The access stream receives the loopback URL as its effective base URL.
The queue and library retain the admitted URI.
HLS and DASH remain within this one input.
Reject generic playlist expansion until every child preserves the same admission and native policy.

## Consent and selected-item intent

Add a closed permission named `media.stream` with this profile:

```json
{"profiles": ["public-https-media@1"]}
```

This is a new contract; the current manifest validator does not accept it.
It grants native media GET and HEAD requests through the scoped transport.
It does not grant a Wasm HTTP request or return media bytes to an add-on.
The existing `network.http` permission keeps its exact API origins.

Save source consent in host storage outside add-on settings.
Bind it to the contribution ID, approved source kind and identity, account reference, and profile version.
Use the canonical source identity, not release bytes or a publisher display name.
The user must approve public stream playback from that source.
Add-on installation or enablement is insufficient.
A wider profile or a different account requires new approval.

Enqueue records the host selection and admission without resolving a media URL.
Each actual playback open creates a fresh one-use intent for that exact admitted item and input generation.
The host can create this intent for an approved queue advance or replay under the saved source consent.
Preparse, import, artwork fetch, and background browsing cannot create it.
An imported URI does not install or enable its source.
Missing consent leaves the item unavailable until the user approves playback.

## Public media and CDN origins

The public media profile permits changing station and CDN hosts for one selected media item.
Every initial, redirect, manifest, segment, map, and encryption-key request needs a verified dependency chain from that item.
The host must discover child URLs from supported media manifests; an add-on cannot insert arbitrary proxy routes.
Limit each open to sixteen distinct remote origins and five redirect hops per resource.
Use bounded manifests, request concurrency, buffers, and live refresh windows.
Do not buffer the complete media object.

Permit public HTTPS on port 443 only.
Reject URL credentials, fragments, local files, private addresses, multicast, and other schemes.
Validate all DNS answers, connect to a validated address, and verify TLS against the original hostname.
Do not resolve the hostname a second time through a connection helper.
Repeat these checks on redirects, reconnects, and refreshed URLs.
Disable implicit proxies, ambient cookies, keystore prompts, inherited referrers, and arbitrary request headers.
Do not send an API credential to a station or CDN.
Keep signed URL queries in memory and remove them from logs, queue data, and history.

The native proxy listens only on literal `127.0.0.1` with an unpredictable per-open path token.
Its resource paths contain opaque IDs, not remote URLs.
Reject unknown tokens, foreign Host values, browser Origin headers, uploads, and unsupported methods.
The native input policy permits only that exact port and path prefix.
Use `attachments=false` unless a separate validated use requires embedded attachments.

Rewrite every supported HLS or DASH URI into an opaque proxy resource.
Resolve relative URLs against the remote resource's final URL.
Constrain segment templates to their validated numeric or time range.
Reject an unsupported URI field, manifest feature, or nested protocol.
Do not pass it to an unrestricted fallback.

## Replay, revocation, and account changes

| Event | Required result |
| --- | --- |
| Stop or input replacement | Cancel pending resolution; revoke only the per-open policy; close its proxy and upstream sockets |
| Replay or queue advance | Validate the saved admission again; create a new intent and lease; resolve a fresh URL |
| URL expiry | Refresh within the same live lease and account; reject a late result from an old lease |
| Key replacement, deletion, or account change | Reject old account references; revoke active leases; keep the saved row for explicit user action |
| Display or language setting change | Keep the account locator; use the current validated request configuration |
| Code-only provider update | Keep the stable locator; revoke the old executable generation; resolve again with current grants |
| Disable, uninstall, safe mode, or permission removal | Reject new opens and revoke active leases; do not activate a fallback source |
| Interface shutdown | Close the native backend and leases immediately; do not wait for Qt cancellation work |

An explicit host action can rebind a saved item to another account through a queue transaction.
Do not silently retarget the old reference.
`vlc_input_policy_Revoke()` alone does not close an existing socket.
The lease must also close native sockets and wake blocked reads.
Already delivered decoder data cannot be recalled; do not claim immediate removal of all buffered media.

## Native enforcement gaps

| Source boundary | Current state and required work |
| --- | --- |
| `src/input/access.c:96` and `:120` | Initial and redirected URLs have policy checks. Preserve them around the lazy redirect. |
| `modules/access/http/connmgr.c:267` | Each manager request checks URL and connection scope. Keep reuse, retries, and redirects behind this gate. |
| `modules/access/http/access.c:194` and `:217` | Ambient credentials and prompts still run. Disable them for scoped input. |
| `modules/access/http/resource.c:236` | ICY and MMS redirects can change scheme. Reject unsupported redirects before a new access opens. |
| `modules/access/http.c:220` and `:713` | The legacy HTTP access still has proxy, credential, and referrer paths. Contain or deny this fallback for scoped input. |
| `modules/demux/adaptive/http/HTTPConnection.cpp:252`, `:423`, and `:527` | Native adaptive HTTP, authentication, and access fallback need the same scope. Remove ambient credentials for scoped input. |
| `modules/demux/adaptive/http/HTTPConnectionManager.cpp:136` | Validate policy before connection reuse or factory selection. `localAllowed` is not a security grant. |
| `modules/demux/adaptive/http/Chunk.cpp:294` | Each redirect loop must remain within the proxy mapping and lease. |
| `modules/demux/hls/playlist/Parser.cpp:247` | HLS key URLs, maps, variants, and refreshes need verified proxy resources. |
| `modules/demux/dash/mpd/IsoffMainParser.cpp:84` | DASH BaseURL and segment templates need verified proxy resources. |
| `modules/demux/avformat/demux.c:396` and `:433` | AVFMT_NOFILE and secondary `io_open` are denied. Test this with real scoped inputs. |
| `modules/access/avio.c:85` | A new scoped-input guard denies `OpenAvio` before its direct `avio_open2` call. Test the denial. |
| `modules/access/live555.cpp:344` | A new scoped-input guard denies Live555 before SDP can open third-party RTP sockets. Test the denial. |
| `src/network/io.c:119` | A new connection guard runs before DNS and socket creation. Test direct `net_Connect` denial. |
| `src/network/stream.c:409` and `modules/access/http/h1conn.c:390` | The low-level address helper has no policy object. Its caller must validate the scope before use. |

Also redact scoped URLs in access, demux, and adaptive diagnostic messages.
The current source admission and queue code does not attach the persistent source-only policy yet.
This attachment and the native backend are required before playback becomes available.

## Acceptance tests

Test a real stable queue item through Play, Stop, replay, and queue advance.
Its URI and queue item ID must stay unchanged.
Close the interface with resolution and reads pending; native shutdown must finish without Qt events.
Test late results, policy replacement conflicts, source removal, key rotation, and signed URL refresh.
Test a dynamic station host and a CDN redirect with public DNS and TLS validation.
Reject a private redirect, DNS rebinding, forged proxy route, ambient credential, and unsupported nested scheme.
Test HLS keys, variants, and refreshes, plus DASH BaseURL and segment templates.
Test an SDP response and FFmpeg secondary I/O; neither can create an external connection.
Do not enable the playback capability until these tests pass.
