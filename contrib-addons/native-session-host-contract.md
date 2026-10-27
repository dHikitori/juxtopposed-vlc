# Native session host

The host controls approval, Resume, and End session. A package cannot create
those choices through a guest call.
The app mount is described in [session app owners](native-session-app-contract.md).

## App owners and order

The app owns `AddonNativeSessionPlayer`, `AddonSessionHost`, and
`AddonSessionController` on its Qt thread. Close them before native player
destruction. Use the shared app `AddonStreamLifetime::Handle`.

The native adapter takes the native player pointer at construction. Its
`backend()` returns typed operations. The pointer remains inside a private
native gate. A native listener copies completion values into a bounded mailbox.
It does not call Qt or take the gate lock. Other operations take the gate lock
before the native player lock. No callback takes those locks in reverse order.

Construct the host with the player backend, an `AuthoritySource`, and the app
lifetime handle. `AuthoritySource::capture` runs on the app thread. It returns
`AddonManager::nativeAuthorityReader(contribution)`. The host runs that reader
and full saved-state checks in joined Qt work. Workers retain copied authority
values. They do not call the Manager or hold a raw Core or QObject pointer.

Call `AddonSessionHost::mount(manager.hostServices())` only after the required
components pass their checks. Mount returns false if a required operation is
already registered or cannot register. It removes a partial registration.
The closed operation map includes WebSocket open, send, receive, and close;
playback observation and commands; and session renewal and end.

The host UI calls `prepareApproval`, waits for `approvalReady`, then reads
`approval`. Present its captured name, relay, room, observation fields,
operations, and background statement. Pass only the user's chosen operations
to `approve`. An empty operation list permits observation only. These methods
are C++ only. No package view can approve a ticket. Each ticket has one use and
expires after 30 seconds. A changed native media/input stamp requires a new
ticket.

The controller takes a trusted backend with `invoke` and `cancel` callbacks.
The app routes matching Manager completion signals to `completed`. The
controller chooses only the declared `run` operation. It checks saved authority
before each slice. A slice has a 25-second deadline. Continuation waits at least
500 milliseconds. An invalid result or worker failure ends the session.

The controller calls C++ `bindInvocation(session, request, steadyDeadlineUs)`
after `invoke` returns and before it processes an early or queued result. The
host refuses calls before that binding. It checks the exact invocation and
deadline at entry and after each saved-state check. It also checks the binding
before upgrade/receive publication and native command admission. An active
binding cannot be replaced. An invocation ID has one use per session. The
retained ID limit is 4,096.

Call `releaseInvocation` on every terminal result, cancellation, and timeout.
It retires old receives, saved checks, and scheduled commands. An unfinished
open or admitted native command ends the session. The caller must treat false
as a refusal to continue. A later slice retains local-user suspension. It does
not grant or resume a native lease automatically. Old cancel closures bind the
old invocation and cannot remove a new slice's waiter.

Set the monotonic slice deadline before `invoke`. Refuse late returns and
results when the Qt event loop runs. A blocked Qt thread can delay controller
cleanup. Core's independent native lease deadline remains the authority bound
in that case. Do not claim a real-time 25-second native revoke. The worker's
30-second timer and instruction budget remain unchanged.

On exact `packageWithdrawn(package)`, call `host.revokePackage(package)` before
other package teardown. A worker failure between slices must use this path.
On Manager `retiring`, app retirement, or player owner retirement, close the
controller, then the host, then the native adapter. Use direct owner-thread
connections. Do not wait for `QObject::destroyed`: Worker shutdown can wait
before that signal. The host close revokes every exact native lease and closes
its post gate. App lifetime retirement then joins the WSS and Qt check workers.

The app mount must retain a native invalidation subscription through each
granted lease. Registration must atomically refuse an invalid or active-write
token, or cancel the lease before it returns. Each same-process authority
invalidation must cancel the lease before file publication and Worker stop.
The callback must use native state only. An epoch check denies new admissions,
but it does not by itself revoke a command already in Core's input FIFO. This
subscription is part of the component implementation. The destructor's early
`retiring` signal alone does not cover background writers or a settings commit
before Worker stop. The actual app mount must retain both paths.

Every native invalidation callback calls an immutable exact native revoker
directly. A swap-once host cancel can return while another revoke still waits
for the player lock. The direct call serializes those repeats. Native adapter
close also retains its gate through player-lock revoke and listener removal.
It reports closed only after those operations finish. A writer cannot pass an
early closed-pointer marker while native authority remains active.

## Native authority

Each command binds the native owner, media generation, input generation,
transport revision, local revision, lease, increasing sequence, and operation.
FIFO admission is not execution. The host returns `applied` only after a native
completion matches that full scope and lease epoch.

Local mouse, keyboard, hotkey, and native API commands retain priority. Native
Core checks the command at admission and execution. Local input suspends remote
control. Guest renewal cannot resume it. Host Resume performs another saved
authority check and uses the current native stamp. A temporary rate correction
uses Core's saved user rate and restore ownership. The host never writes a
restore rate through a Qt timer.

Cancel before admission removes pending host work. Cancel after admission
revokes the exact lease. It does not promise rollback of an action that started.
Disconnect, account change, permission change, expiry, and End session revoke
the same authority. A native cancellation closure takes no Qt event to run.

Observations include approved scalar fields and opaque authority references.
They contain no media path, key, or media bytes. Unknown buffering, live-window,
and output-latency values remain unavailable. A seek-target correction between
two real players does not establish frame or sample synchronization.

## WSS transport

`AddonSessionWebSocket` accepts only explicit approved `wss://` origins on port
443. HTTPS credentials do not grant WSS use. The transport validates every DNS
answer, pins one public address, and verifies TLS against the original hostname.
It rejects redirects, ambient proxies, cookies, and insecure fallback.

The network owner runs on a counted joined thread. Saved authorization and
credential reads run in joined Qt work. Native DNS uses the bounded native
resolver. A blocked native DNS task retains its process connection slot through
the actual callback thread join. Cancellation closes native sockets and revokes
the lease before terminal delivery. Held stopped transports release Qt-using
authority and callback captures on the counted worker.

The app drain must complete before Qt global teardown. The Qt module remains
pinned as the existing descriptor requires. Module pinning alone does not prove
an app drain. External launcher signal deadlines can end the whole process;
they do not permit an early Qt destructor.

The guest request and IPC limits are in [the SDK contract](sdk/sessions.md).
The TLS fixture's address override and certificate authority injection are
native test APIs only. They grant no guest authority.

## Open checks

Keep production sessions unavailable until component tests, the actual app
mount, an app retirement test, and platform checks pass. No Windows execution
result exists for this new transport group yet.

## Validation

The isolated WSL build uses the normal strict Qt definitions. WSS passes 15
normal and 15 sanitizer cases. Host/controller passes 32 normal and 32 sanitizer
cases. Both sanitizer runs enable address, undefined-behavior, floating-cast,
leak, and stack-use-after-return checks. Their diagnostic files are empty.

Two registration rows destroy the host or dispatcher from the synchronous
availability observer. Mount returns false and removes a partial registration.
The host checks all owners after each registration before it uses another
member. A host that survives dispatcher loss can mount a new dispatcher.

The native adapter passes 11 normal and 11 component sanitizer cases with the
fresh Core library.
Its real input/demux fixture checks admission versus execution, local input
priority, queued cancellation without Qt delivery, rate restoration, a later
user rate, replay, and owner teardown. Two real players reduce a known seek
target offset from 18,000,000 microseconds to zero. This measures seek targets.
It does not establish output-clock synchronization.

The real-player watch case holds the actual player lock while native cancel
and store invalidation run on separate threads. The settings file cannot appear
until native revocation completes. A queued seek then has no effect. The close
case also holds the actual player lock. A writer cannot use the reported closed
state to pass an unfinished revoke. No Qt event runs to complete those native
barriers.

A temporary copy with the previous close order fails the close regression at
`!publishedWhileHeld`. The fixed source passes. The negative control changes no
production file. Its result is in the validation record.

The component sanitizer run uses the fixture's declared `araw` and `adummy`
modules. No suppression is used. The Core shared library is the existing
uninstrumented native build. An earlier full module scan passes nine behavior
checks but reports 146,426 leaked bytes at process exit. A
separate eight-instance native create/destroy baseline, without the adapter,
reports 167,344 leaked bytes. The same baseline has no sanitizer diagnostic with
the two required modules. Keep the independent full-scan startup leak limit
visible. Do not call the full-scan run leak-clean.

The JSON request schema passes full Draft 2020-12 validation with a pinned
temporary `jsonschema==4.23.0` tool. The SDK's `test-session-schema.py` passes
58 request and continuation examples. Native request checks have execution tests.
The proof record is `/tmp/hakone-session-validation.json`. It records exact
source hashes, binaries, logs, and disk checks. This checkpoint includes the
native invalidation subscription. It precedes the app mount.

The normal host build also links the registered components. The coherent
240-case host group passes, including all 32 session host and 15 WSS cases.
Worker retirement and dispatcher deletion pass their separate focused
sanitizer checks. These results still do not activate the app profile.

The separate component checkpoint preceded the app assembly. The app now
creates the adapter, Host, controller, and trusted panel through one explicit
owner. Its actual-player tests cover activation and teardown. Full app linking,
compiled UI, two-player correction, and supported-platform checks remain open.
