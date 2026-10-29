# Portable artwork background work

The production portable image adapter now uses joined background work for
package checks, account and key preparation, TLS setup, cache reads, image
decode, and cache writes. It does not scan providers from the UI thread.
The old synchronous image-fetch API has been removed. Binary asset requests
keep their separate API.

## Prepared image authority

The Manager captures the reader and Store token in memory. The existing
preparation queue then checks the exact installed release, grants, settings,
account, and keys. The delivered value has copied permissions and secret
bytes. It has no saved-state reader. Its current-state check reads memory
only and covers the exact Manager, Store epoch, and application lifetime.
Another Manager cannot use it.

The Store token is local to the process. Store objects for the same profile
share it within one process. A write by a separate OS process does not send
an immediate invalidation. Background preparation and final saved-state
checks detect changes when they read the store. Profile ownership or a
cross-process revocation mechanism remains an activation gate. These tests
do not establish immediate revocation across processes.

Images share the existing preparation limits: 64 queued preparations per
Manager and two active repository jobs across the process. Secret snapshots
share the metadata recipe limits: 64 recipes, 16 KiB per field, 1 MiB per
recipe, and 16 MiB across the process. The image path starts no add-on worker.

The real network broker still controls public addresses, approved origins,
credential headers, redirects, pacing, and response size. Image requests use
GET and a 1 MiB response limit. An add-on receives no key value or local path.
The serial runner still repeats the full saved-state checks before it seals
the result for final Core Apply.

## Cache work

The adapter constructor does no file access. A cache hit is decoded and
checked on joined work. The first valid miss measures the managed cache.
The scan stops when the entry or byte limit has been reached. It does not
load an unbounded directory list.

The decoder accepts bounded static PNG, JPEG, and WebP images. Cache writes
use atomic replacement. Entry and byte limits keep existing selected images.
All owner-thread transfer and waiter maps remain on the owner thread. The
background cache holds copied input and shared accounting state, with no
QObject or owner access.

Cancellation removes the owner state before it calls external cancel
functions. Each cache stage has a distinct sequence. A late completion
cannot start a replacement request with the same cache key. Queue refusal
ends the request once. No idle timer or background job is added.

Manager retirement now cancels surviving image adapters and shuts down their
metadata services while Manager members are still live. That closes retained
Core backends and releases media references. Repeated shutdown has no effect.
The old-code control leaves the backend open. The repaired native path closes
it and refuses fresh native dispatch.

A blocked OS file read can still delay the app drain. Cancellation does not
wait for that read on the owner thread. The joined lifetime keeps its captures
until the read ends.

## Verification

The final normal cohort passes 271 checks, with no failures, skips, warnings,
or runtime messages:

| Suite | Cases |
| --- | --- |
| Prepared invocation and image authority | 55 |
| Portable image cache and cancellation | 15 |
| Typed aggregate, runner, and complete Core path | 91 |
| Shared metadata Service | 70 |
| Manager runtime | 40 |

The new Linux link-wrap checks hold the real authority reader before its
first file read, or hold the actual Qt image decoder. Request entry and
cancellation return within 100 ms on the test host. The owner event loop
continues to process a heartbeat while the read is blocked.

The TLS cases use the real Manager network broker and a trusted test route
after public-address admission. They check the per-user key header, a cold
image download, a cache hit, cancellation and disable during decode, Manager retirement during
decode, and retirement during a pending TLS response. A separate case changes the Store from another object in the
same process before the network request starts. Another Manager cannot use the prepared authority.

The complete native case starts with a controlled C process fixture result,
then uses the production Service, real TLS image broker, decoder, managed
cache, source seal, parent transaction, and final Core Apply. The second row
withdraws the provider after child completion and refuses the final write.
This test uses no direct URI approval or mock image adapter. It does not
execute a Wasm provider. Separate prepared-invocation cases exercise the real
Rust/Wasm worker.

Matching ASAN/UBSAN verification also passes all 271 checks. Leak detection
and vptr checks are on. No check is skipped. The complete host and Core
producer cohort is instrumented. Each mode uses 23 changed producer and test
objects, plus 36 verified unchanged producer objects. The final source has
530 files. Dependency records cover 1,044 normal and 1,055 sanitizer files.

The final record is
`/tmp/hakone-prepared-images-host/retirement-final-proof.json`, SHA-256
`a43ca84b9533dd5c93ee0e3f8614d9a316a83573234932ab743bdd850eadfb6c`.
Object, source, recipe, dependency, program, and result hashes are retained.
Completed outputs are in verified compressed archives. Unchanged suites keep
their exact passing results; the revised Core-independent pending-image test
passes again in both modes. Native Service closure and refused fresh Start
remain in the native aggregate suite. The separate old-code control fails
because the surviving Service backend remains open.

The real remote provider parsers and Windows GUI are outside these controlled
tests. The usual Windows package is unchanged.

## Disk recovery

A Git maintenance pack temporarily took free space below the required reserve.
The next build stopped before it wrote output. The multi-pack index verified
910,021 distinct objects across duplicate packs. Git's own expire and
prune-packed operations recovered the redundant copies. HEAD, refs, staged
changes, and working changes matched before and after cleanup. No history
was rewritten. The record is `git-redundant-pack-cleanup.json` in the proof
directory.

Superseded test executables were retired. Final successor programs or all
relink inputs remain available, with original reports and separate cleanup
records. A stopped static archive duplicated all 45 retained producer objects
byte for byte; its redundant container was removed. Later space guards also
stopped a link and a focused build. Both resumed from the same graph after
verified cleanup. Their stopped and resumed records remain available. The preserved Core libraries and Windows baseline were not replaced.

## Remaining gates

Native owner activation is still off. Preserved provider, local metadata,
subtitle, status, complete app, and supported-platform checks remain required.
The usual Windows package does not contain this change yet. Full app idle
CPU, startup, UI response, and quit measurements remain open.

The [Windows cursor report](windows-cursor-feedback.md) is a separate open
defect. These background-work checks do not establish its cause or its fix.
