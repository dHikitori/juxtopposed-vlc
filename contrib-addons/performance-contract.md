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
