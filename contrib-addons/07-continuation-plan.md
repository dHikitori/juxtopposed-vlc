# Continuation plan

Written 30 October 2026, against `addons` at `8ba81a1ebc` plus the
uncommitted profile socket work (`addon_store_profile_socket.*` and its test).
The plan reads the design set (`README.md`, `01`–`06`), the
[implementation status](IMPLEMENTATION.md), the
[completion backlog](completion-backlog.md), the profile authority records and
current source. It does not repeat the full backlog. It adds findings from
this review, recommends changes to approach, and gives an ordered way to
continue.

This document makes no new test claim except where it cites an existing
result file.

## 1. Where the work stands

The branch has 214 commits and about 125,000 added lines across 913 files,
in four days. Approximately 23,500 of those lines are in this directory. The
component breadth is large. Most architecture areas have a host
implementation with focused normal and sanitizer tests. The main remaining
work is **activation and integration**, not more component design.

| Phase (`06-delivery-plan.md`) | State | Main open items |
| --- | --- | --- |
| 0. Foundations | Mostly done on Linux and Windows | macOS has no build or runtime evidence. Worker startup, footprint and hostile-computation numbers are not recorded. |
| 1. Packages, themes, settings | Package lifecycle, grants, rollback, themes, safe mode and generated settings work | Offline self-test and health transaction; file/folder/account pickers; account recipe (OAuth/device code); settings layers; theme preview; crash backoff and quarantine (see F3) |
| 2. Metadata | Very deep component work: coordinator, prepared invocations, native Apply, artwork outcomes, provider families | Native owner activation is off. Identity dialog and accepted revision. Retention/expiry. Legacy bypass removal. Full app cutover. |
| 3. Discovery and UI | Browse, search, resolve, queue CAS, Radio Browser, Home replacement, typed modal shells | Subscriptions and continuous queue population; guest library/queue/fingerprint adapters; full app source playback check |
| 4. Visualization | Worker, renderer, clock and window lifecycle are mounted and tested | Physical GPU loss; supported rendering backends |
| 5. Sessions | Leases, WSS, host panel and actual-player assembly are tested | Drift/rate experiment; full app link |
| 6. Stabilize v1 | Not started | Normative schemas, package validator, generated SDK types, templates, preview tool, platform conformance |

Across all phases, these gates are still open: a **fresh full app build and
run on Linux**, the **Windows GUI checks**, and the **performance contract**.
Several records say "the actual MainUI object compiles". None of them shows a
linked app that runs with the new owners mounted.

## 2. In-progress work: the profile socket

The uncommitted socket layer adds `StoreProfileEndpoint`,
`StoreProfileSocket` and `StoreProfileListener`. It provides same-user local
transport (Unix socket with `SO_PEERCRED`/`getpeereid`, Windows named pipe
with an owner-only DACL and token SID comparison) for the existing 96-byte
protocol.

Existing results in `/tmp/hakone-profile-socket/`:

- Linux normal: 29 passed, 0 failed, 0 skipped.
- Linux ASAN/UBSAN: 29 passed, 0 failed, 0 skipped.
- Three negative controls (`ignore-user`, `direct-callback`,
  `unbounded-output`) fail their selected tests, as intended.
- **Windows build fails at link.** `win-build.log` shows that static Qt6
  Network needs `QNLMNIPlugin_init.cpp.obj` from `/work/local-qtnetwork/...`,
  and that file is not present. This is the first time this producer links
  `QLocalSocket`, so the static network plugin import is new.

No record document and no commit exist for this step yet.

Review notes on the socket code:

1. **Default Unix runtime directory.** `addon_store_profile_socket.cpp:114`
   uses `/tmp/hakone-ipc-<uid>` when no runtime directory is passed. Another
   local user can create that name first. `privateDirectory()` then refuses
   it, so the result is denial of service, not a compromise. Use
   `$XDG_RUNTIME_DIR` on Linux and `confstr(_CS_DARWIN_USER_TEMP_DIR)` on
   macOS. Keep `/tmp` only as the last fallback.
2. **Windows pipe-name squatting.** The pipe name hashes the user SID and the
   canonical path. Another user can compute both. That user can create the
   first instance, and then `FILE_FLAG_FIRST_PIPE_INSTANCE` fails for the real
   coordinator. The client's server-identity check prevents a compromise, so
   again this is only denial of service. To prevent it, mix a random secret
   into the hash. Store the secret in an owner-only file inside the profile.
3. **Handshake deadline.** Accepted sockets have no deadline. A same-user
   client that connects and sends nothing holds one of the 64 slots
   permanently. The protocol record assigns this to the owner. Implement it
   with the owner, as one single-shot timer per pending handshake.
4. **Docker in a unit test.** The two cross-user tests
   (`test_addon_store_profile_socket.cpp:223,232`) start `docker run` with a
   pinned registry image. On a host without Docker or that image, the tests
   fail. They do not skip. Use `QSKIP` when Docker or the image is not
   available, or move the tests to an opt-in label.
5. **Triple user check.** The listener checks peer identity in `queue()`,
   `accept()` and `opened()`. This is harmless. One check at `accept()`, plus
   the check in `opened()` for the client role, is sufficient.

Multi-window sharing is the baseline (§4.2), so this layer will be mounted.
Notes 1, 2 and 4 are required before the owners are built on it. Note 3
belongs to the coordinator owner in Step 2.

## 3. New findings from this review

The backlog does not list these items. Each one has a source reference.

### F1. A new worker process and a new Wasm compile for each prepared invocation

`addon_prepared_metadata_invocation.cpp:658` creates a new `AddonWorker` for
each prepared metadata or artwork invocation. The worker
(`worker/src/main.rs:40`) calls `Module::from_binary`, so Cranelift compiles
the whole module on each process start. No compiled-module cache is used.

The design reason is sound: "the child must reach clean EOF before it
transfers a receipt" gives a strong end-of-authority proof. However, this
makes each automatic artwork or metadata lookup cost one process start plus
one full compile. A playlist or library pass multiplies that cost. This
conflicts with the performance contract ("report worker cold/repeated cost").
It is also a likely contributor to F4.

Recommended change:

- At install time, precompile with `Engine::precompile_module`. Store the
  `.cwasm` file in the immutable release snapshot. Key it by the Wasmtime
  version, a hash of the engine configuration and the module digest. Load it
  with `Module::deserialize_file`. The host makes the file and checks its
  integrity, so the `unsafe` deserialize remains host-trusted. If the key
  does not match, compile from source again.
- Keep a warm process for each (package, release, account reference,
  settings revision). Use a fresh `Store` and instance for each invocation.
  Get the end-of-invocation proof from the instance and the invocation guard,
  not from process exit. Stop the warm process with one single-shot idle
  timer that runs only while the process is alive. Any authority change
  retires it, as now.
- Decided (30 October 2026): adopt both changes, in this order. First add
  the precompiled module and keep the process-exit proof, and measure. Then
  add the warm process. The warm process must hold no host-side state from
  one invocation to the next: credentials, HTTP state and host-call IDs are
  scoped to the invocation and wiped at its end. Its key includes the account
  reference and the settings revision, so two accounts never share a
  process. The worker sends an explicit "instance released" message for each
  invocation. The receipt requires that message, a current guard, and no
  outstanding host calls. A worker crash or a protocol error in this sequence
  retires the process and fails the invocation closed.
- Revised (30 October 2026, after measurement): the precompiled module alone
  brings a complete prepared invocation (process start, one call, clean EOF)
  from 140 ms to 2.1 ms (median; p90 2.4 ms). A warm process would save about
  2 ms more per lookup, but would replace the process-exit proof with the
  weaker instance-release proof. Prepared invocations therefore keep one
  precompiled process per call and the clean-EOF receipt. Revisit the warm
  process only if an in-app measurement exceeds the repeated-lookup budget.

### F2. Cancellation polling during each prepared invocation

`addon_prepared_metadata_invocation.cpp:692` starts a 25 ms `QTimer` for each
active invocation. The timer checks for cancellation and a stale guard. The
timer is limited to active work, so it does not break the idle rule. But it
wakes the event loop 40 times per second for each invocation, and
`addon_native_wake` / native notifications already exist. Use the
cancellation event to signal the worker owner directly. Keep the guard check
at each host-call and result boundary, where it already exists.

### F3. No crash backoff or quarantine

`01-architecture.md:136` requires bounded restart backoff, a visible error
card, and quarantine after three crashes in five minutes. A source search
finds no backoff or quarantine logic for workers. On `unavailable`,
`addon_manager.cpp:1249` withdraws the worker. The next invocation creates a
new one. A package that crashes at startup can therefore restart without a
limit, with one process start and one compile each time. Add a per-package
crash ledger in core-owned state, with exponential backoff, the quarantine
threshold, and a health row in Extensions. Clear it on update, rollback or an
explicit retry. This also partly provides the per-contribution health state in
`01-architecture.md:132`.

### F4. Windows cursor report: a testable hypothesis

`records/windows-cursor-feedback.md` records a spinner cursor for the whole session,
**also outside the VLC window**. That detail is important:

- A Qt override cursor applies only over the application's own windows.
- The Windows startup-feedback cursor (`IDC_APPSTARTING`) is global.

So repeated process launches are the strongest hypothesis. The launch sites
are:

- Each prepared invocation starts a worker (F1).
- Workers that crash are started again without backoff (F3).
- Git staging and update checks (`addon_package_store.cpp:565`).
- The native `vlc_spawn`, which sets only `STARTF_USESTDHANDLES`
  (`src/win32/spawn.c:112`).

None of these launches sets `STARTF_FORCEOFFFEEDBACK`.

Test: add a launch counter, and set `STARTF_FORCEOFFFEEDBACK` through
`QProcess::setCreateProcessArgumentsModifier`. The plugin scan in
`redesign_preferences.cpp:742` already uses this hook. Set the same flag in
`spawn.c`. Then repeat the live check in the record with the launch counter
visible. Do not claim a fix without that live check. F1 and F3 reduce the
launch rate in any case.

### F5. Proof files in `/tmp` are not durable

Many records cite a `/tmp/hakone-*/final-proof.json` with a SHA-256. `/tmp`
is local to this machine, and a reboot or cleanup removes it. Other people
cannot reproduce or audit it. The repository documents point at evidence
that will disappear.

## 4. What should change in approach

### 4.1 Make integration the critical path

Through most of this branch, the component cohorts have grown and the full
app gate has stayed open. Each new component adds mount work that is not verified, and
each record ends with "app mount remains open". Without a running app, real
defects such as F1 and F4 stay hidden. The process inverts this:

- Keep a **walking skeleton**. Every checkpoint builds `build-redesign`
  completely and runs a scripted smoke test, headless with
  `QT_QPA_PLATFORM=offscreen` on Linux. The script starts the app, waits
  for the bundled imports, opens the Extensions model, runs one portable
  provider invocation against a local fixture, plays the Radio Browser
  fixture source, stops, and quits through the shutdown coordinator. It
  records wall time, process launches and idle CPU for 30 seconds.
- A component is "done" only when this smoke test passes with the component
  mounted.

### 4.2 Multi-process profile sharing is the baseline (decided)

Decision (30 October 2026): several app windows (processes) share one add-on
profile, and each one can use add-ons. A mutation in one window revokes old
native authority in every other window. The simpler "first window owns
add-ons" mode was considered and refused, because add-ons that work in one
window and not in another are unexpected for users.

Therefore the coordinator, peer, ticket and socket path in
`profile-authority-coordination.md` stays on the critical path, and it is a
gate before native metadata owner activation (Step 4). These rules follow
from the decision:

- **Do not block the other work on it.** The walking skeleton (Step 1) and
  the performance fixes (Step 3) do not need cross-process authority. Do
  them before the protocol owners are complete, or at the same time.
- **Revocation must be invisible during normal use.** In other windows, a
  mutation such as an install, a settings save or a provider order change
  cancels native work and closes admission for a short time. Automatic
  lookups that were cancelled for this reason must be queued again after
  rejoin. They must not show an error or record a negative result. Only an
  explicit user action that was cancelled reports a status, for example
  "Add-on settings changed. Try again."
- **Refresh the display after rejoin, without a flash.** Each window refreshes
  its display catalogue once, on joined work, and keeps the last safe rows
  until the new rows are ready (`performance-contract.md`).
- **Shared state that is not authority stays out of the revocation path.**
  Example: the crash ledger (F3). Keep each process's crash counts in memory.
  Write only a quarantine decision, through the guarded writer, so that every
  window applies it. A crash count update must not revoke other windows.
- **An idle window does nothing.** A ready peer has no timer and no
  heartbeat. Its socket is idle until a message arrives. Peer loss is
  detected from socket EOF, not from polling.
- **Every window does the same thing.** The coordinator is one of the app
  processes, chosen by the owner lock, and is not a separate service. When
  it exits, another window becomes the coordinator through recovery. The user
  does not see which window is the coordinator.

### 4.3 Reduce evidence overhead and make it reproducible (decided)

Decided (30 October 2026): adopt the changes below from the next commit.
Existing records keep their `/tmp` references as history, but new records
cite repository test targets.

The current method per step is: a proof archive, SHA pins, free-space logs,
archive recompression, three negative control producers, and three platform
builds. This is rigorous but expensive, and it is not durable (F5).

- Make each focused cohort a labelled `ctest`/meson test suite, for example
  `profile`, `metadata`, `source` or `ui`. Then one command reproduces it on
  any machine.
- Keep negative-control variants for security boundaries: same-user checks,
  fences, admission tokens and stream policy. For lifetime and UI changes, a
  failing regression test written before the fix is enough.
- Record results in the repository as a short table: commit, suite, counts
  and platform. Do not reference `/tmp` paths.
- Run the Windows component build at milestones (before a review package),
  not for every step.

### 4.4 Consolidate the documents (decided)

Decided (30 October 2026): do this as one mechanical commit, after Step 0
and before more records are added. Change only file locations and links in
that commit, so that the review is simple. Check that no relative link is
broken.

This directory has more than 90 files. `IMPLEMENTATION.md` is mostly
narration of test counts, and the same counts drift between files. Proposal:

- Keep `README.md`, `01`–`06`, this plan, and the contract documents that
  describe enduring rules (for example `native-*-contract.md` and
  `profile-authority-coordination.md`).
- Replace the narrative in `IMPLEMENTATION.md` with one matrix: requirement
  → maturity → evidence link. Maturity levels: *designed*, *component-tested*,
  *mounted*, *app-verified (Linux)*, *platform-verified*. The backlog becomes
  the rows that are not *platform-verified*.
- Move the dated Windows build records and per-step records to
  `contrib-addons/records/`.

### 4.5 Smaller code-quality items

- Much of the new code puts several statements on one line, for example in
  the socket and manager files. This is the local style, but it makes
  lifetime-critical code hard to review. Use one statement per line for new
  owner and lifetime code.
- Each test is registered in three build systems (Automake, meson, CMake).
  Add a check that compares the three lists, so a missed registration fails
  quickly.

## 5. How I would continue

This is the order of work. Each step has an exit condition.

### Step 0. Close the socket step

Done on 30 October 2026, except note 3, which moves to Step 2. See
[the socket record](records/profile-socket.md).

- Fix the Windows link. In the component harness, use
  `qt_import_plugins(<target> NO_DEFAULT)` or exclude the `networkinformation`
  plugin type for the test executables.
- Apply review notes 1, 2, 4 and 5 from §2. Note 2 (the random
  per-profile secret in the endpoint name) is required, because the socket
  will be mounted.
- Commit with a short record. **Exit:** Linux normal, sanitizer and Windows
  native runs pass, and the commit includes the record.

### Step 0b. Evidence and document housekeeping

- Add test labels (`profile`, `metadata`, `source`, `ui`, `session`,
  `visualizer`) in CMake and meson, and a check that the Automake, meson
  and CMake test lists agree (§4.5).
- Move the records to `records/` and replace the narrative in
  `IMPLEMENTATION.md` with the maturity matrix (§4.4).
- **Exit:** one command runs each label on Linux. All relative links in this
  directory resolve.

Done on 30 October 2026 in `81e29bc6d0` (labels), `992463f6f5` (move) and
the matrix commit after it. [`test/addon-tests.txt`](../test/addon-tests.txt)
holds the labels; meson and CMake read it. `check_addon_tests.py` passes for
128 tests. The `profile` label runs with one meson command: 21 tests and 720
cases pass. The other labels resolve to their tests in meson; they run with
the Step 1 build. Findings:

- 18 add-on tests are registered only in the standalone CMake project. The
  table records them. The three profile write tests are now also in
  Automake and meson.
- The episode and artwork consumer tests (`build_addon_episode_tests.py`)
  are in no build system.

### Step 1. Walking skeleton on Linux

This step does not depend on Step 2. Start it first, or run it at the same
time as Step 2.

- Build the full app in `build-redesign`. Fix link and compiled-QML errors
  until the app starts.
- Add the scripted smoke test from §4.1 as a test target. Include a
  two-window variant: start two app processes on one profile, and quit them
  in both orders.
- **Exit:** the smoke test passes, and the Phase 0 numbers are recorded
  (worker start, first call, repeated call, idle CPU and resident memory).
  Before Step 2 is mounted, the two-window variant only has to start and
  quit cleanly.

Part done on 30 October 2026; see [the Linux smoke record](records/app-smoke-linux.md).
The full app builds and starts headless. `addon_app_smoke` and
`addon_app_smoke_two_windows` (label `app`) pass: start, bundled add-ons,
local playback, 30 s idle and quit, with two windows quit in both orders.
Idle CPU is about 0.2 s per 30 s and memory about 280 MB, with no add-on
worker running. All seven labels pass in the VLC build after fixes, including
a Core thumbnail status regression. The off-by-default test driver
(`-Daddon-test-driver=true`, decided by the user) adds the Extensions list
and a discovery source through the real worker: worker start and first call
144 ms, warm calls 3–4 ms, one worker for both opens, no worker at idle. It
also found and fixed a Discovery Back/repeat-search defect. Open: Radio
Browser playback in the app (no local fixture exists), the per-call cost of
prepared metadata invocations (F1), and a quit crash inside the WSL D3D12
GPU driver (the smoke test uses llvmpipe).

### Step 2. Profile peer protocol owners

Started 30 October 2026. 2.1, the coordinator owner, is done in `5fd715edaa`,
with the handshake deadline; see [the record](records/profile-coordinator.md).
2.2, the client owner, is done in `7a8c17ed81`; see
[the record](records/profile-client.md). 2.3, the write permit (`a61031e03a`),
and 2.4, write refusal (`8808838eee`), are done; see
[the record](records/profile-write-permit.md). 2.5 handover (`5760d62a9c`),
2.6 app mount (`09f47c44fa`) and 2.7 crash matrix (`3ad4ffc6a2`) are done; see
[the record](records/profile-mount.md). **Step 2's exit holds on Linux:** the
crash matrix passes, the two-process control now passes (a change in one
process cancels native work in the other), and the two-window smoke test
installs a package in one window and shows it in the other, with no error and
0.13–0.14 s idle CPU per window over 30 s. Windows and ASAN runs of the new
units remain for the Step 5 milestone.

Build on the tested protocol, arbiter, peer readiness, generation, writer
scope and socket. The order of work:

1. **Coordinator owner.** It takes the owner lock, then `store.lock`, then the
   exclusive fence. It recovers the profile and publishes a new generation
   before the first `Ready`. It runs the listener and applies all arbiter
   effects, including effects that come with `accepted == false`. It has one
   single-shot deadline, and only while a mutation or handshake is active.
   This includes the handshake deadline from §2 note 3.
2. **Client owner.** It owns the socket and the join preparation on joined
   work. On Revoke or loss it calls the `StoreAuthorityPeer` close handle at
   once. It drains, joins pending preparation and releases the shared fence
   before it sends `Revoked`. It never waits for the GUI thread.
3. **Connection-bound write permit.** Bind the writer scope to the live
   connection and the pending request, so that a lost connection invalidates
   the permit.
4. **Refuse unapproved writes.** In peer mode, every authority write entry
   point goes through the permit. Add a test that lists the entry points, so
   that a new unguarded write fails the test.
5. **Coordinator handover.** When the coordinator process exits, one of the
   remaining windows takes the owner lock, recovers and publishes a new
   generation. The other windows join it again.
6. **App mount.** Attach the peer before any native consumer starts, on
   joined startup work. Connect the retirement hooks to the shutdown
   coordinator. Requeue cancelled automatic lookups after rejoin (§4.2).
7. **Crash matrix.** Kill a process at each ticket, stamp, write,
   acknowledgement and join boundary. Also test late messages, wrong
   generations, queue limits and native final Apply.

**Exit:** the crash matrix passes. The existing two-process control in
`profile-authority-coordination.md` now passes: a change in one process
cancels native work in the other. The two-window smoke test from Step 1
installs a package in one window and shows it in the other window, with no
error and no idle cost in either window.

### Step 3. Performance fixes, measured with the skeleton

- F1: precompiled modules in the release snapshot, measured. Then the warm
  worker with a fresh instance per invocation and the receipt rules in F1.
- F2: replace the 25 ms poll with the cancellation event.
- F3: crash ledger, backoff, quarantine and a health row.
- F4: `STARTF_FORCEOFFFEEDBACK` on every helper launch, and a launch
  counter.
- **Exit:** idle CPU and basic UI timings are within noise of the
  pre-add-on baseline in `performance-contract.md`. The repeated-lookup cost
  is reported as a number.

Step 3 status, 30 October 2026; see [the record](records/performance-step3.md).
F1 (precompiled modules), F2, F3 and F4 are done; the warm worker is not built
(revised decision in F1). Idle CPU is within the Linux pre-add-on baseline,
and the repeated-lookup cost is 2.1 ms per prepared invocation. Open: the
Extensions health row, basic UI timings, a 0.35 s slower startup on this
machine (partly explained), and the Windows cursor check.

### Step 4. Activate the native metadata owner

- Mount the factory behind a core setting that is off by default. Run the
  preserved-provider parity matrix: local tags, sidecars, attachments, local
  artwork, user corrections, and each core online provider against its
  add-on replacement.
- Remove the legacy online bypass only for each family that passes. Keep
  "old and new remote paths must not run concurrently" as a test.
- Requires Step 2. Do not activate the owner while cross-process
  revocation is missing.
- **Exit:** the smoke test passes with the owner on, in one window and in
  two windows. Player, preparser,
  library and artwork requests go through the one owner. Subtitle53 and the
  native source checks pass in the full app.

### Step 5. Windows review package and live checks

- Build the package from the Step 4 commit. Run the Extensions click,
  cached start, network-file Close, and the cursor check with the launch
  counter.
- **Exit:** the user confirms the GUI checks, or the failing check has a
  specific defect with a repro.

### Step 6. Remaining feature rows, in this order

1. **Identity choice dialog** and persistent accepted revision. The Service
   already exists, so this has high user value for little work.
2. **Offline self-test and health transaction** around activation. This
   builds on F3 and the precompiled module, because compiling at install is
   the natural self-test point.
3. **Retention and expiry** in the metadata schema, to replace the fixed
   15 s / 60 s TTLs.
4. **Settings pickers and one account recipe.** Start with device-code
   OAuth, because it needs no local redirect listener. Then add profile and
   session layers.
5. **Guest library, queue and fingerprint adapters.** Start with read-only
   projections. Do not advertise an operation until its handler exists.
6. **Discovery subscriptions** and continuous queue population with a
   session lease.

Each row reaches *mounted* and passes the smoke test before the next row
starts.

### Step 7. Stabilize v1

- Normative schemas (manifest, UI, metadata, discovery, visualizer, optional
  contracts), with a shared positive/negative corpus checked by the host and
  by the schema validator.
- A package validator that does not execute code, generated Rust types,
  author templates, and a theme/UI preview harness.
- **macOS:** the first build, credential store, fence, socket (if mounted),
  worker sandbox and app run. Until now there is no macOS evidence.
- The two-player drift experiment, if v1 claims coordinated playback.
- An ASD-STE100 audit of new app English, then a final comparison against
  every requirement in `01`–`05`.

## 6. Decisions

All four decisions were made on 30 October 2026:

1. **Profile scope (§4.2):** several windows share one add-on profile, with
   live revocation. User decision.
2. **Worker proof model (F1):** a warm process with a fresh instance per
   invocation replaces the process-exit receipt, after the precompiled
   module is measured, with the receipt rules in F1. Delegated by the user.
3. **Evidence method (§4.3):** labelled test suites in the repository
   replace per-step `/tmp` proof archives. Negative controls stay for
   security boundaries. Delegated by the user.
4. **Document consolidation (§4.4):** one mechanical commit moves the records
   to `records/`, and `IMPLEMENTATION.md` becomes a maturity matrix.
   Delegated by the user.

## 7. Risks

- **Integration surprises.** Many owners that are tested only as components
  will be mounted together for the first time in Step 2. Expect lifetime and
  order defects at app shutdown. The shutdown coordinator and retirement
  hooks are the right place to fix them.
- **The performance gate can force design changes.** F1 suggests that the
  current prepared-invocation model cannot meet the repeated-lookup budget
  without a change.
- **The peer protocol is the longest gate before activation.** Its owners
  have many lifetime edges. Step 1 and Step 3 can continue while Step 2 is
  in progress, so that it does not hold everything else.
- **macOS is unknown.** Platform-specific code exists (Keychain,
  `getpeereid`, `flock`), but it has never been built or run.
- **Scope pressure.** The deferred items in the backlog (plain HTTP, LAN,
  WebRTC, shaders, a watch-party service, sidecar write-back) are correctly
  out of v1. Keep them out.
