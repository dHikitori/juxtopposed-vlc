# Guarded install, removal and bundled setup

Install, uninstall and bundled-package setup now have guarded host entry
points. They use the outer profile lock and exclusive fence. The ordinary
methods keep their existing interfaces. The app has not switched to the
guarded paths yet. Native metadata owner activation remains off.

## Install and update

Candidate and grant checks run before staging. The host checks retained data,
release parents and the target release for links or Windows junctions. A
linked release directory cannot redirect an update outside the profile.

The local mutation gate stays closed through private-data preparation,
resource staging and state commit. Native cancellation ends before writes
start. The context is checked during staging and before publication. Counter
and release-history limits are checked before staging. A failed staging or
state write leaves the profile in recovery. The active pointer is not reported
as ready while the result is uncertain.

The same-release shortcut now compares grants too. A request that removes
optional permission takes the normal update path, even if the release bytes
are unchanged. This repair applies to ordinary and guarded calls. A control
that restores the old shortcut fails both permission-revocation cases.

## Removal and retained data

Removal validates all affected paths before it changes files. The local gate
and package retirement stay active through the operation. A user can keep
settings and private data for a later install, or delete the exact package's
saved data. Cleanup also works after the active package has been removed.

When a guarded removal keeps data, a damaged primary with a valid backup is
still treated as damaged. The whole package directory is moved to retained
corrupt data. This preserves both state files and the release bytes. It does
not silently discard the damaged primary in favor of its backup.

An archive failure leaves the installed package in place. A failed withdrawal
or partial deletion returns NeedsRecovery. The guarded path does not make a
partly deleted package ready again. Complete profile recovery must inspect
staging and removal remnants before it admits peers.

## Bundled setup

Guarded setup writes the initial `enabled` value with the first package state.
It does not save a disabled package and then run a separate enable operation.
A stopped setup therefore cannot leave a new bundle disabled by an internal
half-step that a later startup mistakes for a user choice.

Existing disabled and suppressed choices are retained. An incomplete package
is refused even if it is suppressed. The package offer is checked again
before install. Only the outer Ready result permits publication of the output.
Cancellation after a real state commit leaves the profile in recovery; it
does not undo an already committed file or open old authority.

The ordinary startup method still uses its existing paired install and enable
under one lock. The application must adopt the guarded entry point with the
peer owner. No idle timer, polling loop, worker process or thread is added.

## Verification

| Suite | Linux normal | Linux ASAN/UBSAN | Native Windows |
| --- | --- | --- | --- |
| Lifecycle writes | 31 | 31 | 30 |
| Package state writes | 57 | 57 | 55 |
| Policy writes | 33 | 33 | 28 |
| Profile generation | 34 | 34 | 34 |
| Total | 155 | 155 | 147 |

All cases pass, with no skip, runtime warning or sanitizer finding. Leak and
vptr checks are on. The Windows suite creates and tests a real junction. The
file failure test makes a directory obstruct the first state commit, then
calls the real Qt commit. It confirms that staged bytes cannot open readiness
after publication fails. The bootstrap tests inspect its first real state
commit and exercise cancellation immediately after that commit.

The old-shortcut control removes only the grant comparison. Both the ordinary
and guarded permission-revocation rows then fail as expected. Control source,
objects, program bytes and results are retained.

The component uses all 13 production source units in the store dependency
closure. The Windows cache is mounted read-only. Existing compiler warnings
about the C++20 `requires` name and display-catalogue indentation remain.
These checks do not prove the full application or its performance.

The final record is `/tmp/hakone-profile-lifecycle/final-proof.json`, SHA-256
`6cce0001a0d5e97c8ae4b740f99da351b1e36f2cb394af35ae84117c522ebac1`.
The source snapshot has 117 files. Source, object, program, dependency, recipe
and result hashes are retained. Completed outputs are checked and compressed
before raw files are removed. The protected Core libraries and current Windows
review package are unchanged.

Lossless recompression recovered 106,014,339 bytes from older retained test
outputs. Every replacement has the same complete decompressed contents. The
records `archive-recompression.json`, `archive-recompression-extra.json` and
`native-program-recompression.json` in the proof directory replace the old
gzip paths. Each affected archive directory also has a
`RECOMPRESSED-2026-10-30.json` pointer. No source, object or program contents
were discarded by this cleanup.

The disk guard stopped one sanitizer link. After verified cleanup, the same
graph completed. The stopped and resumed records remain. This step's minimum
recorded free space is 7,513,391,104 bytes. After cleanup, free space is
7,628,152,832 bytes.

## Remaining integration

All planned local store mutation families now have guarded entry points.
This does not establish the peer protocol or the application mount. Complete
profile recovery still needs to validate policy, package state, private data
and transaction remnants.

The runtime writer must hold `store.lock` before it requests a ticket. The
file helper currently acquires that lock inside `write`. Add a retained writer
scope for ticket negotiation, then keep its exclusive fence through store
unlock and completion. Test coordinator loss and late grants at that boundary.

The peer channel must close local readiness, cancel native leases and retain
shared fences until cancellation ends. Mount startup, reconnect, refresh and
drain on joined work. Full app performance, Windows cursor behavior, supported
targets, preserved Core providers and native final Apply remain open. The
current Windows review package is unchanged.

See [profile coordination](profile-authority-coordination.md),
[package state writes](profile-package-writes.md) and
[guarded generations](profile-generation-transactions.md).
