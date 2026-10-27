# Native source resolver owner

`AddonNativeSourceResolver` binds one `vlc_source_backend` to a VLC instance.
The class is a C++ host owner. It has no QML or worker operation. It is not yet
mounted in the app and does not enable Play or Enqueue.

## Host backend

The constructor takes the native instance, a `Backend`, and an optional parent.
`bound()` reports a successful native owner binding. It does not report a
complete stream capability. The real contained media transport is still needed.

`Backend::admit(uri, generation)` runs only after the registered native primary
playback request reaches the host. It validates the current host admission,
source, account, media grant, and saved source consent. It mints and consumes a
fresh exact `PrimaryPlayback` intent. This request does not identify a new UI
click or distinguish replay from queue advance.

The returned `Admission` owns the immutable consent authorization and a plain
captured configuration check. That check must not call a `QObject`. It must fail
after a release, account, grant, settings, or enablement change.

The host can instead supply `Backend::captureAdmission(uri, generation)`. The
resolver calls this factory on its Qt owner thread. The factory must read memory
only and return an exact owned `AdmissionReader`. The resolver invokes that
reader on joined background work. This permits the host to prepare a manager
authority snapshot without a background call to the manager. An empty reader
refuses the request. It cannot fall back to the general `admit` callback. Closing
or destroying the owner during capture prevents worker submission.

`Backend::prepare(admission, current, completion)` can resolve trusted source
metadata and allocate a private listener. It must check `current` before each
request and after each asynchronous step. It cannot start outbound media I/O.
Its completion transfers one dormant `Prepared` transport.
Copy the admission if preparation continues after the backend call returns.

`Prepared::adopt()` takes ownership of the native policy and lease references.
Use the core proxy policy with `source=NULL` and `attachments=false`. The result
contains the exact private loopback URI, a plain atomic active flag, and
`startIo(current, completion)`. The native lease must clear the active flag,
close sockets, and wake reads before its cancellation callback returns.
Late prepared values can be destroyed on a background thread. Their native
cleanup and captured state must not require a Qt owner or GUI delivery.

The resolver checks the native job and captured authority, then calls
`vlc_source_job_AttachTransport()` before `startIo()`. It checks authority again
before it completes the native request. A late or refused transport revokes its
policy and cancels its lease. There is no unrestricted fallback.

## Cancellation and ownership

Native dispatch posts bounded host work. Native cancellation records only a
job identity and input generation in a bounded native mailbox. It posts no Qt
event. A plain mutex protects the dispatch owner pointer. Destruction clears
that pointer before it closes the native backend. Native Close and Cancel end
input waits and close attached transports without a GUI event. See
[native source retirement](native-source-foundation.md) for the watch and
concurrent cancellation checks.

Concurrent lease cancellation waits for the native transport callback to
finish. Same-thread callback reentry does not wait for itself. See
[native lease retirement](native-source-lease-retirement.md) for the reference
lifetime rule and the controlled concurrent-caller checks.

The default limits are 16 pending host jobs, 64 issued transports, and 256
queued host deliveries. A prepare or startup completion has one use. Stopped
transports leave the issued set at the next host admission. The guard uses weak
host references, so a native result and its transport cannot retain each other
through a callback cycle.

`withdraw(binding)` revokes pending and issued work for that exact source and
account. The host must call it on disable, uninstall, account change, permission
withdrawal, and old executable withdrawal. `close()` revokes all work. Call both
methods on the owner's Qt thread. Native lease callbacks remain independent of
Qt and can run on other threads.

The input owns the successful lease through Stop and playback end. An individual
manifest, key, or segment EOF must not cancel it. A replay uses a fresh native
generation and a new transport. Saved queue and library items keep their exact
admitted source URI.

## Admission reader checks

The current focused suite passes 33 normal and 33 sanitizer cases. It proves
that exact reader preparation runs on the Qt owner thread and its saved-state
read runs on joined work. A refused factory cannot use the general admission
callback. A factory can destroy its owner without starting the returned reader.
The existing cancellation, consent, quota, and native transport checks also pass.

The sanitizer build instruments the resolver and its focused host dependencies.
Address, undefined-behavior, leak, and stack checks report no diagnostic. These
checks do not activate the production playback mount or prove the full app.
