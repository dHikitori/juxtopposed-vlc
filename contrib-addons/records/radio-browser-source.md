# Radio Browser reference source

The package is in [examples/radio-browser](../examples/radio-browser/README.md).
Its ID is `org.hakone.radio-browser`. Version `0.1.0` uses `discovery@1`.
The repository address under `github.com/hakone-org/` is a proposed address.
This change does not create or publish that repository.

The app distribution includes the package and its compiled Wasm entry.
MainUI registers it through the existing distribution package path. A saved
package removal still takes effect. Enabling the add-on accepts its
`media.stream` permission, which approves source playback; there is no
separate approval step.

## Source behavior

The source supplies roots, folders, station search and fresh station resolution.
It has no user key or account setup. API pages contain display data and stable
station UUIDs. They do not expose stream URLs to the browser or queue.

Each playback open resolves the saved UUID again. The native source owner
checks the package, source approval and current media policy. The native
transport checks DNS addresses, redirects and child media requests.

The package permits one HTTPS API origin. It reads at most ten records per
page, 1,000 per query and 24 KiB per response. It has at most 16 active
requests. Requests use the host's one-second minimum interval and one bounded
retry. A page cursor binds its query, offset and API origin.

The source removes records with unsupported formats, broken playback, TLS
errors or disallowed addresses. Its syntax checks do not replace the native
transport checks. It does not call the API's station click update endpoint.

## Verification

All 16 Rust cases and nine actual Wasm-worker cases passed. The worker checks
cover full station records within the existing fuel and response limits.

A Linux live probe used a temporary profile and dummy audio output. It read
four roots and eight stations. It made no resolve request before Play and one
resolve request at Play. The selected HLS station decoded 30 audio blocks.
The queue count stayed fixed. Stop, owner retirement, joined work and Core
release all completed with exit code zero.

The app distribution registration passes its normal worker test. The current
MainUI object also compiles. These checks do not link the complete app.

The live probe used a fresh pure host archive. All affected sources, headers
and generated meta-object code were current. Source pins stayed fixed before
and after the run. The four native source producers were also fresh.

The probe found a native ID3 artwork callback with an optional output pointer.
That callback now checks the pointer. Its three normal and three focused
sanitizer cases passed. The exact old callback failed the null-output case.

Live station availability can change. This result proves one complete Linux
playback path. Full app linking, compiled QML and native playback on the other
supported platforms remain open.
