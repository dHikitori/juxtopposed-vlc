# Contained native media proxy

This component is not part of the production playback mount. The host must keep
the Play action off until the admission and shutdown gates below are complete.
The resolver, consent store, and transport also need normal application checks.

The normal Meson configuration accepts the registered source and test lists.
All nine transport/resolver production objects and the three new test objects
compile with the normal app flags. MOC receives the native playback test flag.
The standalone CMake configuration also accepts the native test targets.
The full app link and production shutdown connection remain open.

## Authority and lifetime

The native resolver accepts only an admitted `hakone-addon-source:v1:` reference.
It checks the saved source approval and the exact input generation. It retains
the stable reference on the queue item. It gives the input a new loopback policy
and lease for each open.

`Admission.current()` checks memory only. It checks the exact reference and
generation, the consent live token, and a captured configuration live predicate.
`Admission.authorize()` also checks the saved approval, current source, and full
configuration. The transport runs this full check on a background worker before
each new request and redirect. A file check does not run on the network thread.
The resolver runs `Backend.admit` on a background worker. At most eight admission
workers can run or retire across all resolver owners. Close and cancellation end
native waits immediately. The worker retains its permit through its thread join.
A late result cannot prepare a transport. The callback must not
call a QObject or retain a raw Core instance pointer.

`AddonStreamProxy::prepare()` binds a private loopback listener. It does not send
a remote request. The native adapter creates a proxy policy and lease. The
resolver attaches both before `startIo`. Each later remote request requires both
the live check and full admission check.

`cancel()` marks the proxy inactive and shuts down held native socket handles
before it returns. It does not wait for Qt events. The one-shot native cleanup
hook revokes the input policy and cancels its lease. This hook runs outside the
lifetime mutex. A late prepared result owns the same cleanup.

One network thread owns all Qt network objects for an input. Background admission
tasks retain a global session slot through their thread join. A closed proxy cannot
free its slot while one of those tasks remains. The host callbacks must terminate.
The proxy cannot interrupt an arbitrary file operation inside a supplied callback.
The Qt module descriptor already uses `cannot_unload_broken_library()`. The proxy
depends on that module pin until its tasks and network threads end. A canceled
native input does not provide an unload barrier. No isolated module unload is
supported. Every authorization closure must retain plain host state. It must not
capture a raw Core instance or QObject pointer.
`AddonStreamShutdown` supplies the app shutdown barrier. The host must construct
it on the app thread, after `QCoreApplication`. Every resolver backend and proxy
specification must use its retained handle. A missing or closed handle refuses
new work. The host must destroy this controller before it destroys the app.

For normal quit, close the handle and native resolver first. Call `request()`
while the GUI event loop can still run. Its completion reports `Drained` only
after all network and Qt work has joined. Only that result permits app exit.
`TimedOut` leaves the app alive. The host must not treat timeout as permission
to destroy Qt. The controller never terminates the process.

`finalGuard()` is an explicit last check before each path that destroys the app.
It waits at most five seconds and returns false if Qt work remains. A false result
requires a host decision that keeps Qt alive. This API does not fix direct calls
to `QCoreApplication::exit()` by itself. The production quit paths still need
this mount. Do not close the consent store on the GUI thread before joined work
ends. Its saved-file check can hold the consent mutex during a blocked file read.
The worker can retain the last consent reference until that read ends.
Call proxy preparation and startup on the app thread or within joined Qt work.
Each background task gets its own authority callback copy. Network teardown
clears the stored callbacks on its counted thread. A native lease can then retain
the stopped proxy without retaining a Qt-using consent or settings callback.

The process limits are eight network owners and 40 Qt work threads. A native
reaper sends cancellation outside the GUI thread. Each counted entry permits one
native collector and one cleanup thread. The cleanup thread joins the Qt worker
and destroys its callback captures. The collector then joins the cleanup thread.
Socket, TLS, callback, retained quota, and Qt thread-storage cleanup precede the
drained result. Failed helper-thread creation keeps the entry and quota closed.
A blocked file read keeps the barrier closed. Owned captures do not make Qt file
work native-only.

OS DNS uses a separate native/std work limit of 32. A native mailbox carries
its bounded address result. DNS never posts a Qt event. The network owner closes
the mailbox before its join. A blocked OS lookup can then retire after app exit
without a Qt call or Qt value. One native collector joins each native DNS callback
thread. It releases the input quota after that join, then the process quota.
The module pin must remain active for that native code too.

## Remote connection rules

The production path accepts public HTTPS on port 443. It refuses user information,
fragments, control characters, private addresses, and mixed public/private DNS
answers. It validates every DNS answer before it selects one address. It connects
to that numeric address. TLS verifies the original host name with the system
certificate authorities. The transport does not use a system proxy or a client
certificate. It does not ignore certificate errors.
The media proxy and document broker use the same public-address policy. This
policy also refuses documentation ranges and addresses with scope identifiers.

The transport follows at most five redirects. Each redirect repeats URL, source
authority, DNS, and TLS checks. One input can use at most 16 remote origins.

`public-https-media@1` supports HTTPS on port 443 only. An HTTP item needs a
separate, explicitly approved public-HTTP media profile. Keep that extension point
available for radio and podcast sources. Do not advertise HTTP playback until that
profile and its transport exist. Do not downgrade this HTTPS profile to HTTP.

The app exposes only an opaque loopback token and route. It does not expose the
remote URL, redirect target, query token, cookie, or remote ETag. Local ETags bind
the route and remote tag through a digest. The remote URL stays in native memory.

The explicit C++ test entry point can supply a local TLS fixture and certificate
authority. No package setting or guest operation can select this entry point.

## Local requests and byte limits

The listener accepts only GET and HEAD with the exact captured loopback Host and
token path. It refuses CONNECT, uploads, pipelined requests, and client credentials.
It does not forward client cookies, authentication, Host, or User-Agent fields.

Single byte ranges and strong ETag conditions are supported. Suffix ranges,
multiple ranges, weak ETags, and date conditions are refused. Content-Range and
Content-Length must agree. Ambiguous framing, extra bytes, malformed chunks, and
short bodies fail. An unknown-length body requires a clean TLS close.
The returned strong ETag must match a captured condition. A changed or missing tag
fails the request. This profile also refuses a full-response If-Range fallback.

Each actor permits four clients and four background admission tasks. The global
limit is eight active or retiring actors. Remote read buffers are 64 KiB. Local
write buffers apply backpressure at 64 KiB for progressive media. Chunk output
adds at most 32 KiB per step. A complete rewritten manifest has a separate bounded
buffer. The transport does not buffer a complete progressive media object.

An input has a 64 GiB byte limit and a 24-hour lifetime. Header, DNS, and TLS work
has a ten-second deadline. Body progress has a 15-second idle deadline. Deliberate
downstream backpressure does not count as an idle failure.

## HLS and DASH

The proxy uses the host manifest converter. HLS and the declared static DASH
subset can reference only opaque local routes. Range coordinates remain absolute
within the captured remote object. All new routes stay in a temporary batch until
the complete rewritten manifest passes validation. A refused manifest leaves no
new route behind.

The rewritten manifest limit is 2 MiB. The route limit is 2048 with at most 4 MiB
of retained URLs. Depth is limited to
eight, with at most four nested playlist levels. Unused routes expire after 120
seconds. At most two prior manifest windows remain unless a request still uses
them. HLS AES-128 keys must contain exactly 16 bytes.

Dynamic DASH, SegmentTemplate, DRM, XLink, external clocks, low-latency HLS, and
generic media playlist expansion remain unsupported. A server-side source URL
change requires a new resolution. The proxy has no implicit raw-URL fallback.

## Verification limits

The focused HTTP suite passes 20 cases. The local TLS suite passes 25 cases.
These tests cover name verification, DNS refusal, redirects, ranges, truncation,
backpressure, staged manifest routes, close reentry, and socket cancellation
without network-thread progress. The fixture uses a test-only local dialer and
test certificate authority. It does not prove a public live stream or Windows
execution. The Windows socket guards still require a native Windows test.

The shutdown owner passes 19 cases. These include a real blocked consent-file
read and a blocked Qt thread-storage destructor during capture cleanup. The
resolver passes 30 cases. The native playback fixture passes six cases, including
its setup and cleanup. All these normal runs have no failed or skipped case.
The HTTP, TLS, shutdown, and resolver suites also pass address, undefined-behavior,
leak, and stack-use-after-return checks without diagnostics. The native lifetime
and Winsock DNS helpers compile and link with MinGW-posix. This is a Windows
compile check, not a Windows execution test.

The real Core WAV test passes. It decodes audio, stops, and replays the same stable
source item through a fresh admission. The real HLS fixture also decodes AAC audio
through the adaptive and transport-stream modules. These results use the fresh
native Core. They do not prove the app shutdown mount or public live playback.

An earlier native sanitizer run reports 20,918 bytes in 145 allocations per Core
instance. Its Core-only create/release control has the same
totals and allocation groups. These allocations come from protobuf, pixman, and
gomp initializers during plugin discovery. No leak suppression is used. This
native baseline prevents a clean leak-check claim for the full playback fixture.

The isolated recipe is
`modules/gui/qt/tests/stream-proxy/CMakeLists.txt`. Configure it with the Qt 6 SDK.
Set `VLC_ADDON_NATIVE_BUILD_ROOT` to a fresh native VLC build to include the Core
tests. Select that build's Core libraries and plugins in the test environment.
The native HLS test requires the adaptive and transport-stream modules. The TLS
fixture needs OpenSSL. The native HLS fixture also needs FFmpeg. These tools create
synthetic test data. No test needs a service key or public network access.

The filtered proof is `/tmp/hakone-contained-stream-validation.json`. It lists
the current logs, source hashes, checks, and open gates. Keep add-on playback
disabled until the quit mount and coherent application checks pass.
