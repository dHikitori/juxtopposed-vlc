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
  resident. Cold and repeated execution costs and memory still need benchmarks.

The earlier 15–17 second app startup observations disabled the plug-in cache
and used software rendering. They are not a controlled normal-launch baseline.
See [the Windows record](windows-local-build.md).

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
