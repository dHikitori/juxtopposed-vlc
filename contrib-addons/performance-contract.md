# Add-on performance requirements

The user requires almost no add-on cost while the app is idle or during basic
UI use. Performance is a completion gate for this architecture. Current safety
and lifecycle checks do not establish that gate.

## Required behavior

- Inactive features have no recurring timers, package scans, network requests,
  or player-state polling. Their idle path must not acquire the player lock.
- Basic UI reads use bounded memory data. They must not open package files,
  hash content, read credentials, start processes, compile Wasm, or initialize
  a network backend. A stale view must not fall back to synchronous disk reads.
- Real package or settings changes cause one coalesced background update. A
  player capability change must not scan the package store again.
- Workers start when an add-on needs to execute. No worker or Wasm compilation
  is needed to draw the normal app UI. Report resident worker memory after use.
- Metadata requests, visualizers, sessions, installation, and other active work
  use bounded background execution. The basic UI must remain responsive while
  that work runs. Cancellation and retirement must retain their safety checks.
- Display data and execution authority remain separate. A cached UI row cannot
  authorize an invocation or a native write. Keep fresh release, grant, account,
  settings, source, and native admission checks at the execution boundary.
- Preserve the last safe visual data during refresh. Show loading or stale
  action state without an empty-list flash or a theme reset. A failed package
  can have a recovery row, but cannot supply executable contributions.
- Startup work is included in the budget. Moving a scan to a constructor or
  starting background work before a timed query does not remove its cost.

## Evidence required

Use the same machine, app configuration, package bytes, media, renderer, and
plug-in-cache settings for comparisons. Keep local Windows and WSL network
paths separate. Record first use and repeated use; do not call a warm file
cache a cold-disk result.

Measure first window and usable UI, first and repeated interactions, idle CPU,
resident memory, file I/O, process starts, and timer/native backend calls. For
workers, measure process-to-ready time, repeated no-network calls, idle CPU, and
memory. For a background catalogue, report loading and ready states separately,
including the full time to ready. Empty loading rows are not a faster valid
catalogue. Include constructors and bundled-package checks as separate phases.

Source checks and fake-backend call counts prove narrow structural properties.
Full app measurements must show that idle and basic UI overhead is within the
measurement noise of the comparable baseline. A console query benchmark alone
does not prove smooth app rendering or playback.

## Current findings and status

The published `fd3fed4173` Windows build still has these measured or verified
costs. It is not accepted against this contract.

- An earlier Windows synchronous package-list probe measured about 421 ms on a
  local path and 1,598 ms on a WSL network path. This is a host catalogue read,
  not a worker execution benchmark.
- Startup verifies seven bundled packages synchronously. Theme and UI queries
  then scan installed packages again. UI registry reload has five type queries;
  each query loads and checks all packages before it filters by type.
- Session UI listens to revision and capability signals. A revision also emits
  a capability signal, so that path repeats its catalogue work.
- The mounted session host starts a 50 ms timer without a session. Its tick
  reads completions and a player snapshot, which takes the native player lock.
- The mounted session UI starts a 250 ms review timer without a review. Source
  playback starts two 20 ms maintenance timers and a 250 ms consent timer when
  it becomes available, even with no source request. The three source timers
  schedule about 104 callbacks per second. This is a source-derived rate, not
  a measured CPU value.
- The default Artwork view in audio playback makes the visualizer surface
  visible. Its host starts a 34 ms refresh timer with no selected scene, then
  each refresh returns without work. The native visualizer clock listener is
  also installed before a visualizer is selected. Audio sample analysis itself
  is lazy.
- Opening the package summary view starts a 100 ms authority check timer. It
  remains active after that first use.
- Ordinary workers start on first use and reuse one compiled runtime per
  package. The worker waits for pipe input while idle. Used workers remain
  resident. A small offline worker test is recorded below; full host execution
  and active feature costs still need measurements.

The earlier 15–17 second app startup observations disabled the plug-in cache
and used software rendering. They are not a controlled normal-launch baseline.
See [the Windows record](windows-local-build.md).

## Controlled Windows baseline

The baseline uses the published `fd3fed4173` host archive, matching source
headers, and seven real packages. Four separate console processes run in this
order: local Windows storage, WSL network storage, WSL network storage, local
Windows storage. Each process gets a byte-identical seed store. Package queries
return seven valid rows. The five UI contribution types return empty lists.

| Operation | Local Windows storage | WSL network storage |
| --- | --- | --- |
| First five-type UI query round | 1,194–1,226 ms | 4,774–4,818 ms |
| Median of later rounds in each process | 1,201–1,223 ms | 4,794–4,810 ms |
| UI registry construction | 1,217–1,257 ms | 4,784–4,872 ms |
| Seven already-installed bundle checks | 230–237 ms | 587–596 ms |

Each five-type round reads 14,535,845 bytes in 1,060 process read operations.
Later rounds repeat the same reads. UI registry construction repeats them
again. No guest process starts in any of these four runs. These results
establish a host UI cost when no add-on UI contribution is present.

The test records Manager construction, theme construction, raw catalogue reads,
bundle checks, and destruction as separate phases. No cache is warmed by a
hidden query before the first timed query. Seed copies and file hash checks do
warm the filesystem cache. Process read counts include cached requests; they
are not physical disk counters. These are console measurements, not app window
or frame measurements.

A separate test uses three real workers with offline French TV `no_match`
replies. Process-to-ready time is 309–496 ms. Across 60 warm calls, the median
round trip is 0.078 ms and the maximum is 2.55 ms. Each five-second idle window
has zero accounted CPU increase, within Windows process accounting precision.
Observed working sets are 11.2–14.9 MB and private bytes are 5.3–5.8 MB. The
first worker's working set grows during idle, so this test does not establish a
stable resident size. All workers exit normally. The test includes controller
framing and pipe I/O, but excludes Manager dispatch, metadata fetches, and HTTP.

The records are in `/tmp/hakone-addon-performance-baseline/`. The final proof
is `before-final-proof.json`, SHA-256
`c789735c3c19bf1c1b1a961aa68b7172172ffa17cfe049834d0850db1ba4748a`.
Sources, recipes, profiles, and the baseline archive are retained with hash
records. Some terminal build artifacts are stored as lossless gzip files.

The comparison must record attachment, loading, time to ready, and valid row
counts. It must include all background startup work. A faster empty or stale
view cannot pass as a faster valid catalogue. A new host cohort needs its own
matching headers, archive, and executable pins.

## Full app baseline and comparison

The retained pre-add-on app is `build-windows/hakone-win64`, from clean source
`619b57dd8597605500aed1845d182bd37742dc4e`. It includes the redesign and
precedes the new worker, host, and MainUI mount. A read-only audit records all
822 public files, with a total size of 281,421,367 bytes. It did not launch the
app or read a user profile. The manifest is
`/tmp/hakone-pre-addon-performance-baseline-audit/candidate-manifest.json`,
SHA-256 `9f0a47c5f519498b121fd362e7006668f9162bbf6733c71e8ca41f68c0a4da77`.

Preserve that complete build. The Windows build script defaults to that output
directory. Future local builds must explicitly select `build-windows/vlc-win64`
or an owned staging directory. Use independent copies of complete builds for
the comparison; do not mix an earlier Qt DLL with a later Core.

Compare the earlier app with the improved app. Also compare the same improved
binary with seven packages removed and suppressed, and with seven packages
enabled but unused. The latter comparison measures package-related cost; it
does not measure the host's fixed cost. Other Core and UI changes limit what
the earlier-app comparison can attribute to add-ons.

Use isolated, matching disposable profiles, normal rendering, the same cache
policy, and identical local media. Measure repeated earlier-app runs first to
record machine variation. Then use balanced run orders. Include startup to a
usable window, catalogue readiness, shared navigation, and at least 60 seconds
of idle Home, settings, and paused media. Record CPU, memory, file I/O, thread
and handle counts, and child processes. Keep WSL storage and network-file Close
tests separate; the older build lacks the later network-read cancellation fix.

Message-loop probes can show responsiveness but do not measure painting or
frame time. Define the equivalence budget before judging results, and report
counter precision and run variation. The detailed plan is
`/tmp/hakone-pre-addon-performance-baseline-audit/PLAN.md`. These are prepared
comparisons, not passing full app measurements.

## Verified idle-session change

The first regression test fails against the previous host: one idle tick
increases player snapshot calls from one to two. The one initial call is the
constructor's health check.

The updated host starts no timer with no pending work. Idle ticks read neither
the player nor its clock or completions. A pending approval uses one expiry
wake and reads no player state. Active sessions retain their checks. Removing
the last approval or session stops the timer; later work can restart it.

All 38 session cases pass normally and with focused AddressSanitizer and
UndefinedBehaviorSanitizer checks, including leak and stack checks. The new
cases cover idle mount, approval expiry and removal, refusal, and repeated
session activation and end. Existing authority, cancellation, transport, and
callback-deletion checks still pass. There are no failed or skipped cases and
no runtime diagnostics. Existing source-format compiler warnings remain.

The proof compiles the current SessionHost and test fixture. Unchanged host
dependencies and meta-objects use the pinned fresh host archive; Qt remains a
normal shared dependency. The records are
`/tmp/hakone-session-idle-cost/{before,normal,san}/proof.json`. The initial
clock-polling intermediate sources and hashes are retained separately.

These checks prove the idle call and timer boundary. They are not a whole-app
performance result. The constructor health check, background display snapshots,
startup batching, worker costs, and full app measurements remain in the audit.
The published Windows build still uses `fd3fed4173` and does not contain this
idle-session change yet.

## Verified idle session UI change

The session UI now starts its 250 ms review timer only after it gets a review
ticket. It stops the timer on cancellation, acceptance, refusal, expiry,
invalidation, or close. An active session with no open review does not need
this timer. Current review checks remain in place while a review is open.

The idle regression fails against the previous UI implementation. All 22
cases pass normally and with focused AddressSanitizer and
UndefinedBehaviorSanitizer checks. There are no failed or skipped cases,
runtime diagnostics, or test warnings. Cases cover pending and inline reviews,
repeat activation, expiry, changed reviews, refusal, replacement during a
callback, and owner deletion. Existing host consent and media-change cases
also pass.

The changed UI and test fixture are compiled fresh. Other host dependencies and
matching-layout meta-objects come from the pinned normal host archive; Qt is
also uninstrumented. The separate current SessionHost proof above covers its
timer change. A combined full app measurement remains required. The records
are `/tmp/hakone-session-ui-idle-cost/{before,normal,san}/proof.json`; the final
sanitizer proof SHA-256 is
`ef06445b7577c6dcf5fa8f3f24fe2f695c9609e3ad9acac87509ccd26062b393`.
This change is not in the published Windows build yet.

## Verified idle source playback change

The mounted source owner and native resolver no longer run their 20 ms timers
without work. The consent UI no longer runs a 250 ms timer without a review.
These were about 104 callbacks per second while source playback was available
but unused. This is a source-derived rate, not a measured CPU saving.

Open reviews use one expiry wake. Active resolutions and issued transports
retain their required checks. Cached approval changes arrive through bounded
native notifications. Those display subscriptions use a separate quota from
native cancellation watches. A final notification follows the outer writer's
completion. Ordinary inactive source playback needs no recurring timer.

The initial six-suite normal run passes 147 cases. A 29-case follow-up checks
the final owner change, including an unapproved review notification. All 148
current sanitizer cases pass: consent 39, consent UI 23, resolver 44, source
owner 19, real owner/UI binding 10, and Store authority 13. The applied hashes
match the tested copies. Earlier-source controls fail their designated idle
checks. There are no failed or skipped cases or runtime diagnostics in the
accepted runs.

The sanitizer build compiles all 44 host producers and matching meta-objects.
The native source-owner run uses the documented HTTP/WAV modules and native
fallback resampler, with real playback assertions. A separate no-owner full
module scan reproduces the existing 20,918-byte leak in 145 allocations. That
failed run and its control remain recorded. No product codec choice or
sanitizer suppression changed. A build-space stop is also retained; resuming
with a thin archive kept all 45 compiled object hashes unchanged through the
last build command. Completed binaries and producer objects are retained in
verified lossless archives.

The records are in `/tmp/hakone-source-idle-timers/`. The final sanitizer proof
is `san-final-proof/proof.json`, SHA-256
`fdd5352de996ebb65f4a2b7585a0d3e2b8cd42c3151ac781ef902693e6531a8c`.
Qt and Core remain ordinary shared dependencies. An arbitrary external Core
close has no empty-owner notification; supported app retirement closes the
owner directly, and fresh actions still check Core state.

TLS support now has a separate idle boundary. Binding an empty source owner
does not initialize the TLS backend. Only an explicit review, source capture,
or primary playback request can run that probe, on joined work. Its memory-only
Unknown/Ready/Unavailable hint cannot grant or revoke source consent. A failed
probe keeps reviews, saved approvals, and stable queue references usable;
primary playback requires a successful fresh probe before an intent or provider
call. A later explicit attempt can retry without another consent review.

All 33 normal and 33 focused sanitizer cases pass, including the existing 19
owner and ten real owner/UI cases. Four new owner cases check zero startup
probes, review and approval while TLS is unavailable, primary refusal and retry,
and owner deletion during a blocked joined probe. The old startup control fails
its designated probe-count check. Positive probes use real Qt TLS support.
The changed owner, complete matching meta-objects, and fixtures are fresh;
unchanged dependencies use the recorded normal147 archive. This component
boundary uses its matching earlier repository-work declarations and does not
replace the full current host/app check.

The record is `/tmp/hakone-source-lazy-tls/san-proof/proof.json`, SHA-256
`a242b0ea25768eebde56c74be169f7f9562089723f3b3995ccf01e2c86e1aa43`.
All three applied files match the tested copies. Manager/MainUI transport-hint
wiring, shared catalogue integration, inactive visualizer integration, and full
app measurements remain open. The published Windows build does not contain
these source timer or lazy TLS fixes.

## HTTP transport creation

The default add-on network broker now creates its Qt network manager only
when an allowed request reaches transport dispatch. Construction, denied
addresses, stale requests, and cancellation before dispatch leave it absent.
An explicitly supplied transport stays owned and is reused. The public-address,
permission, proxy, TLS host, deadline, and response checks still apply.

All 73 broker cases pass on the shared fresh host, with no warnings, failed or
skipped cases. The three new cases check the idle path, refused and canceled
requests, and transport reuse through an injected reply. That reply performs
no network request. The applied three files match the tested source hashes.
The record is `/tmp/hakone-addon-display-host-proof/normal-resume-evidence/progress.json`,
SHA-256 `0f1ce8f95cb19fd837803edaf80b6e6fc5bd76cade6dd0589d13a87610172096`.
It also records a later artifact-retention utility failure. This result covers
the broker suite; the remaining integration and sanitizer checks are open.
The published Windows build does not contain this change.

## Artwork cache accounting

Creating the artwork adapter no longer lists its cache directory. Accounting
starts at the first valid cache miss, before the download quota check. A cached
hit can use its selected file without a directory scan. Invalid requests and
cancellation before the queued action leave accounting inactive.

All 13 artwork adapter cases pass on the same fresh shared host, with no
warnings, failed or skipped cases. Four new cases cover files added before
first use, cached hits before a miss, byte limits, and invalid or canceled
requests. The three applied files match those tested copies. The immutable
suite record is `/tmp/hakone-artwork-assets-lazy-cache/normal-shared-proof.json`.
The first real miss still scans the directory on the owner thread; active
artwork work and full app performance remain open checks. The published
Windows build does not contain this change.

## Stream handle expiry helper

The stream preparation helper no longer starts its 250 ms expiry timer at
construction. It starts that timer when an unconsumed handle exists, and stops
it after the last handle is consumed, released, canceled, or expires. Pending
preparation keeps its own deadline. Active stream leases keep their existing
cleanup and authority checks.

The new idle regression fails against the previous helper. All 16 cases pass
normally and with focused AddressSanitizer and UndefinedBehaviorSanitizer
checks, with no failed or skipped cases or runtime diagnostics. Tests include
two activation cycles, multiple handles, consumption, cancellation, expiry,
late completion, and callbacks that delete the helper. Proofs are in
`/tmp/hakone-playable-idle-cost/{before,normal,san}/proof.json`. An initial test
recipe include-path failure is retained separately.

The current MainUI uses `createPlayerDiscoveryWithSources`; it does not mount
this helper. This change hardens the alternate host path. It does not establish
a performance gain in the published app. Mounted session UI, source consent,
source resolver, catalogue, and visualizer idle costs are still under repair.

## Extensions settings display adapter

Extensions settings can now use the shared summary's memory-only safe-mode
value and retained rows. A refresh keeps the package rows visible while package
actions wait for a current summary. Recovery uses the same supplied values.
Standalone screens retain their earlier backend path after construction.
The global disable control remains available during other work.

All 62 Qt Quick cases pass: 47 installer/settings cases and 15 recovery cases.
There are no failed or skipped cases, warnings, or runtime diagnostics. Two new
cases check repeated display reads without synchronous package or safe-mode
reads, and retained rows with stale actions refused. These checks use injected
backends and the existing Qt Quick test executable. The applied source hashes
match those tested copies. The records are in
`/tmp/hakone-display-settings-ui/`; the normal proof SHA-256 is
`50e86782986e22deae54a2c41b91182481daf30d3bdf1e4e2411a613338a9ee8`.

The current MainUI still uses the earlier summary backend. The shared-view
mount, combined host checks, and full app measurements remain open. This
adapter alone does not remove the published app's synchronous reads.

A follow-up keeps the screen in display mode after its summary is removed.
It shows an unavailable list and refuses package actions instead of reading
the legacy backend. An unrelated summary refresh also keeps an open settings
draft. The form will check the selected package and settings before Save.
All 63 copied Qt Quick cases pass (48 installer and 15 recovery), with no
warnings, skipped cases, or runtime diagnostics. The test uses the existing
form; that form still makes legacy settings reads on a Manager revision.
The new attached form and its separate checks remain part of shared-view
integration. This result does not prove zero reads for the full app.

The record is `/tmp/hakone-management-display-lifetime/normal/proof.json`,
SHA-256 `fc96c0c66ef8032acc8ae04e14e0632149f26a2f915ec75de4969ab95e80bce4`.
The failed draft-property assertion and the old form's read-count failure are
kept in the same packet. The latter established the form boundary above.

An unknown configuration no longer shows a false `Setup required` state or
`Set up…` button. Those labels require a known missing setting. The screen can
show `Enabled` or `Disabled` and keep `Settings…` without a credential query.
All 64 copied Qt Quick cases pass (49 installer and 15 recovery), including
unknown-to-known state changes. The scope is unchanged. The record is
`/tmp/hakone-management-setup-status/normal/proof.json`, SHA-256
`6d2a98f9afae549731714cdf4681a9ea927db6ac5a325119dfefe6f139186983`.

## Native change notification foundation

The host now has a native wake with a Windows event or a nonblocking POSIX
pipe. It has no polling timer. Native callbacks retain only its native state;
the OS handle remains alive through an in-flight callback after the Qt owner
closes. The owner destroys its Qt notifier before it drops its handle owner.

Store display notifications use a separate bounded subscription quota. They
cannot grant authority or take a native cancellation watch slot. Notifications
can repeat; the wake and display view coalesce them. The store lock wrapper
sends a final display notification after it releases the actual file lock.
An early notification at mutation completion cannot replace that final wake.
The native invalidation and cancellation barriers remain in place.

All 14 native foundation cases pass normally and with focused sanitizers. They
cover idle silence, burst coalescing, close during a paused native signal,
callback deletion, quota cleanup, and actual lock release order. A deliberately
wrong lock wrapper fails the release-order assertion after cleanup. The record
is `/tmp/hakone-addon-catalogue-view/native-slice-final/final-validation.json`,
SHA-256 `98ebb9414a2ba22d2f69171ba4b3f4bfb5c8ea5d532c31b09d9c75a518f65331`.

A separate fresh 44-source host build includes the exact Store wrapper changes
and all matching meta-objects. Its six suites pass 147 cases, including all 13
existing Store authority cases. Three old-source controls each fail their
intended idle-timer assertion. Those source timer changes are now applied; the
final 148-case sanitizer result is recorded above. The initial normal record is
`/tmp/hakone-source-idle-timers/normal-build-result.json`. The Windows wake
branch and full app integration remain open. No shared display view or consumer
is enabled by this foundation alone.

The bounded native notification registry is now shared with repository work.
The existing submission API stays available; a new overload reports process
capacity, owner closure, busy state, and other refusal causes separately. The
actual process permit publishes capacity only after its retained work ends.
Clients can wait for that event without a polling timer. The Store subscription
keeps its opaque handle and owner lifetime. Sixteen native authority and helper
bodies remain unchanged.

The foundation passes 47 normal and 47 sanitizer cases: native notifications
seven, Store authority 13, native wake 14, and repository work 13. Its broader
copied view group passes 77 cases in each mode. Two old-behavior controls fail
at their designated readiness check. Those view cases cover real process-slot
release after GUI completion and an unlock notice received before a failed
reader completes. The view retry adapter is still copied work. The global
40-item Qt work limit has no capacity event in this slice; other refusal causes
must not trigger an automatic retry loop.

The record is `/tmp/hakone-addon-job-availability/final-validation.json`,
SHA-256 `9379efcdd60a683392d794ce86e338780e258f7dd7cb6a6b21258addf21e0eee`.
All ten applied foundation files match the reviewed packet. This does not
establish whole-app responsiveness.

An earlier test manifest write damaged a Linux LibVLC build output. The pin
check stopped that run before configuration or compilation. The library was
relinked from unchanged retained objects and matched its original SHA-256
before atomic restoration. Core, application source, and the Windows test
build were unchanged. The recovery record is
`/tmp/hakone-libvlc-recovery/proof.json`, SHA-256
`92955e4c186823a49e541093064aed86505ef1d350e35501b339b804757b7310`.
The replacement manifest writer checks fixed output names and resolved paths,
writes only inside its temporary packet, and treats native libraries as
read-only inputs.

## Shared theme document checks

Theme validation and token resolution now use one QtCore helper. The theme
service keeps its static API and translation context. The background display
reader can use the same rules without a theme service or Manager.

Peer comparison verifies 11 functions after the namespace and color-check
name changes. Bounds, defaults, contrast adjustment, and resolved tokens are
unchanged. The exact helper also passes the copied reader's 98 normal and
98 sanitizer cases. Its separate theme service prototype passes all 15 normal
cases, including the existing theme checks and four attached-view cases.
Those four cases cover loading without a file fallback, repeated reads and
core preference changes without a new package capture, retained palette with
stale action refusal, and disable/restore behavior. All 15 focused sanitizer
cases also pass with no failed or skipped cases or runtime diagnostics. That
record is `/tmp/hakone-display-theme-ui/san/proof.json`, SHA-256
`f4de45f6ac743db19a86cf394d790598a3a0548e8a4aa42ecac54600cb85bb6f`.

The live change in this step extracts the shared helper only. The attached
theme service, shared display reader, and other consumers remain separate
copied work. The prototype record is
`/tmp/hakone-display-theme-ui/normal/proof.json`, SHA-256
`7a7123a2202fedc35b0f64bdf6aba4039d643c7a26e4f0250565de59fd1ab249`.
The reader checkpoint is
`/tmp/hakone-addon-catalogue-view/reader-view-validation-v2.json`, SHA-256
`e01d7d6c544132b9dcde48e32c63bcffc69eeccf2ef43db0f7637c6ee7c83cde`.
Row parity, shared-view integration, startup work, and full app performance
remain open.

## Shared integration checkpoint

The copied shared host now compiles all 66 producer CPP files and its MOCs.
The old reader cohort reached all 18 suites: 17 were clean with 509 passing
cases. Session UI had 34 passing cases and one failed cleanup case. The new
Manager facade returned an outer request ID, but Session UI released it as an
inner View ID. A separate correction uses the exact issuing Manager in both
cleanup paths. The record is
`/tmp/hakone-addon-display-host-proof/normal-baseline-terminal-report.json`.

Actual reader contention also exposed a missing wake after an owned reader
releases the store lock. An unconditional unlock wake alone can make a corrupt
reader retry itself. The corrected private reader reports lock contention
separately and waits for an actual unlock. Permanent errors wait for changed
inputs, a Store epoch change, or explicit refresh. Pending document work can
resume while the base screen stays current. All 55 focused normal cases pass;
five old or incomplete controls fail their designated checks. No timer or new
authority grant is added. This is copied work, with the private Snapshot header
changed. Its record is
`/tmp/hakone-display-reader-unlock/final-validation.json`, SHA-256
`ded549ffe59c3d5efdbf9c6d21509a80c159a547f143c790db6148b665ac4e09`.

The combined copied QML check passes 302 cases across 11 suites. Two exact
Track animation-driver warnings also occur with the old production controls
under both runners. They are kept as two known warnings; no warning is hidden.
The record is `/tmp/hakone-addon-final-qml-v2/final-validation.json`, SHA-256
`464224309536299820a02208664c15f4f3746e5902850255625e2eff69ba24ff`.
These checks use an existing test executable and injected backends. They do
not establish native ABI, app latency, or Windows performance.

The actual MainUI object and 21 native producer objects compiled with the
earlier matching headers. Their dependency records identify the objects that
must rebuild for the corrected private Snapshot header. The successor shared
cohort, native runtime cases, sanitizers, and full app measurements remain
open. The published Windows build still contains the earlier UI read path.

## Shared display foundation

The shared display reader, View, Manager facade, and joined startup batch are
now in the application source and mounted by MainUI. A fresh
matching normal host passes all 18 suites and 551 cases with no warnings or
skips. This includes the corrected reader unlock and Session request cleanup.
The producer contains 66 CPP files and its aggregate MOC object. The record is
`/tmp/hakone-addon-display-host-proof/normal-successor-terminal-report.json`,
SHA-256 `33d85e69c7c90d7cd3ebc9a68ee8dcdb862378604f79ea8a42e934dbd25e6d1d`.

Display getters read retained memory. File verification, settings documents,
and requested credential-presence checks run on joined work. Exact request
tickets bound retained documents and forms. Store unlock notifications retry
actual contention; permanent errors do not create a retry loop. Settings saves
still use fresh authority and an exact revision stamp.

The matching sanitizer run and new Windows package now pass their checks.
The native consumer checks are recorded below. These tests do not establish full
app responsiveness.

## Cached display consumers

Theme, visualizer, extension UI, settings, discovery, session, and subtitle
display adapters now use the shared View when it is attached. Each adapter
keeps that mode for its lifetime. Display refreshes retain open settings
drafts and requested resources. Explicit actions still check fresh authority;
settings saves also check the captured form revision. A retired View refuses
fresh actions.

The actual player analysis adapter attaches its native audio and player
observers only while an active visualizer accepts frames. The default image
and UI asset paths have no idle sweep timer. Exact native checks pass 103
cases: subtitle service 59, source owner 23, consent UI 10, and player analysis
11. All have zero warnings, skips, or stderr. The Subtitle fixture uses a
separate observer context so the service's own disconnect does not remove
the test trigger. No product repair was needed for that fixture failure.

The native records are
`/tmp/hakone-addon-core-display-proof/successor-native-tail-retry-finish-evidence/progress.json`
and `/tmp/hakone-subtitle-observer-fixture/normal-proof/progress.json`.
The 551-case shared normal run and 302-case QML run cover the matching
consumer source. Matching sanitizers also pass all 18 suites and 551 cases
with no warnings or skips. The final sanitizer report is
`/tmp/hakone-addon-display-host-proof/san-current-terminal-report.json`,
SHA-256 `07f0d25cb52afaa96c0c2b09e30ab614fd02d1b49829d78c9e358d12699468fe`.

MainUI starts the joined distribution batch, then attaches the display View
and summary before it constructs the consumers. The View holds its first
capture until the startup batch ends. All seven bundled IDs are registered
as one batch. Source-owner signals publish retained transport hints for the
display path. The actual configured MainUI object compiles with the corrected
private Snapshot header; its proof is
`/tmp/hakone-mainui-display-successor-object/runs/1793223997174226768/proof.json`.
The final integration audit found no source or registration drift. The local
Windows build is now published in the usual local folder. Its 416 executable
and library checks, seven bundles, and native packaged-worker ABI check pass.
See [the 29 October build record](windows-performance-build-2026-10-29.md).
GUI automation could not initialize from the WSL workspace. Actual UI
responsiveness, Extensions navigation, and the full app comparison remain
open; the unit and package results do not establish a performance budget.


## Prepared portable artwork

The production image adapter now runs package, account, key, TLS, cache, and
decode work on joined execution. Entry and cancellation checks return within
100 ms on the controlled host while the actual authority reader or image
decoder is blocked. The owner event loop still processes its heartbeat.
The image path starts no worker, timer, or scan while idle. Manager retirement
closes surviving services and cancels pending image work.

See [the image evidence](portable-artwork-background.md) for the exact test
cohort and limitations. These component checks do not measure the whole app.
The current Windows package predates this change. Startup, idle CPU, ordinary
UI response, and quit comparisons remain open. The persistent
[Windows cursor report](windows-cursor-feedback.md) also remains open; no
CPU or helper-launch sample establishes its cause yet.


## Profile fence foundation

The native profile fence has no QObject, timer, thread, or polling loop.
Acquire it on joined work because file open and validation use the filesystem.
A retained lease holds only an OS handle. Native cancellation and helper
inheritance checks pass on Linux and Windows.

See [profile coordination](profile-authority-coordination.md) for the exact
checks and the real cross-process token gap. The coordinator transport,
startup mount, and app drain remain open. The stamped writer is implemented. This
component does not establish full app idle CPU or UI response. The Windows
player package is unchanged.


The [generation and writer layer](profile-generation-transactions.md) now
keeps profile election, saved stamps, and transaction callbacks on joined
work. Its guard checks native/memory state and a monotonic deadline. A
callback can retire the owner without leaving a live guard or freed state.
The actual peer control channel and production store mount remain open.
There is no new idle timer or polling loop in this layer. Its component
checks do not establish full app performance.

Four policy writes now use the guarded store context without a second lock.
Native cancellation ends before publication. A display notice runs after the
store lock is released and before the exclusive fence ends. The previous
lock code fails that refresh regression. The current cohort passes 103
normal, 103 ASAN/UBSAN and 94 native Windows cases. No idle timer, polling
loop, thread or worker process is added. See
[guarded settings](profile-store-settings.md). The peer channel and remaining
store bindings are still required before application activation. These checks
do not measure app startup, idle CPU, cursor behavior or UI response.

The package enable, rollback, settings and recovery methods now have guarded
entry points. They share the ordinary state commit path. Package, schema and
state reads stay inside explicit joined work. The cohort passes 124 normal,
124 ASAN/UBSAN and 117 native Windows checks. It includes cancellation after
the real backup commit and before primary publication. No new idle timer,
polling loop, thread or worker process is added. See
[package state writes](profile-package-writes.md). The peer channel and
application mount remain open. Full app performance is not established by
these component checks.
