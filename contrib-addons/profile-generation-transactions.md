# Guarded profile generations and store writes

`StoreProfileGeneration` now owns coordinator election and the saved profile
generation. It prepares peer joins and runs guarded store callbacks. All
operations run on joined background work. The control channel and production
store bindings remain open. Native metadata cutover stays off.

A ready record proves that the saved transaction is coherent. It does not
grant local runtime authority. A prepared peer must still complete the
control-channel handshake for the same generation and revision.

## Ownership and locks

The coordinator holds `hakone-authority.owner.lock` for its lifetime. Its stale
lock time is zero. A live owner is not replaced because a fixed time has
passed. Qt can still detect a process that has exited. See the
[Qt lock-file contract](https://doc.qt.io/qt-6.8/qlockfile.html).

Recovery and writes acquire `store.lock`, then the exclusive native fence.
The actual writer holds both. Destruction releases the store lock before the
fence. A replacement cannot pass a retained peer fence or an active writer.
An old ticket must match the record again after exclusive acquisition.

A callback receives one `StoreProfileWriteContext`. Its directory is the
canonical profile directory. Its current-state check covers the operation's
native guard, monotonic deadline, and owned store lock. Recovery also covers
the coordinator owner. Do not retain the context or its references after the
callback. It is not a native cancellation capture.

The callback already owns the store lock. Production bindings must use
locked store entry points. Calling a public method that takes `store.lock`
again will fail. The remaining store bindings must also preserve local epoch
notifications and publish readiness after the outer lock has been released.

## Saved record

The private record is `hakone-authority.stamp`. It has 88 bytes:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Magic and version: `HAKONEA1` |
| 8 | 1 | Phase: ready 0, writing 1, recovering 2 |
| 9 | 7 | Reserved; all zero |
| 16 | 16 | Nonzero random coordinator generation |
| 32 | 8 | Unsigned revision, most significant byte first |
| 40 | 16 | Nonzero request ID while writing; otherwise all zero |
| 56 | 32 | SHA-256 of bytes 0 through 55 |

The fixed record has no JSON number conversion or duplicate-field rule.
The full 64-bit revision is retained. The checksum detects malformed records;
it is not an add-on permission. Reads refuse an unknown version, bad phase,
bad reserved bytes, zero generation, wrong request field, bad checksum,
truncation, or extra bytes. A malformed record is not silently replaced.

Writes use atomic replacement, with direct-write fallback disabled. The
writer requests owner read/write permissions. The native fence file remains empty and stable.
No credential, package path, media identity, or user metadata enters this
record.

## Recovery and publication

Recovery requires a host callback. It publishes a fresh recovering generation
before that callback runs. The callback must validate or restore the whole
store. A true result and a current guard permit a ready record at revision
zero. An interrupted or failed recovery leaves admission closed.

A writer ticket has an expected generation and revision, a nonzero request
ID, a current-state guard, and a deadline. The deadline has a 30-second bound.
The control channel must bind that ticket to the exact peer connection and
pending operation. This file layer does not replace that check.

Before it runs the store callback, the writer checks the ticket under both
locks. It advances the revision and publishes a writing record. A stale,
expired, malformed, or overflowed ticket cannot call the store callback.

The callback returns one of three outcomes:

- Applied: the ordinary store transaction committed.
- Rolled back: the store remains coherent. The caller's error text is retained.
- Needs recovery: no peer can join until host recovery succeeds.

A coherent result and a current guard permit the next ready revision. Both a
commit and a rollback advance it. Old tickets remain dead. Exceptions, an
unknown outcome, or cancellation after work leave the profile closed for
recovery. Remote effects already sent are outside this local transaction.

A prepared join acquires a shared fence and reads the record while it holds
that lease. It requires the exact ready generation and revision. The caller
must retain the fence until native cancellation has ended. It must confirm
the prepared join through the control channel before it opens local tokens.

## Coordinator retirement

A recovery callback can retire the coordinator. Retirement closes its guard
immediately. Retained state keeps the owner lock and file context valid until
the callback ends. The method does not access the destroyed owner afterward.
It returns NeedsRecovery and leaves the recovering record in place.

The old-code sanitizer control reports a use-after-free in this exact case.
The corrected path retains state and closes the guard. A replacement remains
blocked until that state has drained.

## Verification

The final cohort passes 70 normal and 70 ASAN/UBSAN checks on Linux, plus
66 native Windows checks. No case fails or skips. There are no runtime
warnings or sanitizer findings. Leak and vptr checks are on.

| Suite | Linux normal / sanitizer | Native Windows |
| --- | --- | --- |
| Guarded generation and writer | 34 / 34 | 34 |
| Native process fence | 23 / 23 | 19 |
| Existing Store authority | 13 / 13 | 13 |

The cases use real process replacement, a killed writer, saved records, and
OS fences. They cover rollback, bad tickets, corruption, coordinator
retirement, and a remote revision above the signed 64-bit midpoint. The
retirement case fails with a use-after-free against the initial code and
passes after the state-lifetime repair.

The final proof is `/tmp/hakone-store-profile-generation/final-proof.json`,
SHA-256 `5e28d2aebac9eb9b1159be91a32f62dbd5a37747e60d3e66338f825dbb7f6fce`.
The source cohort has 11 files. Source, object, program, recipe, dependency,
and result hashes are retained. All 54 completed program, object, and archive
files, including the old-code failure program, are in a verified compressed
archive. Raw completed outputs and superseded probes were removed. The
protected Core libraries and Windows player package remain unchanged.
Free space is above 7.5 GB.

The tests exercise this layer's actual callbacks and file publication. They
do not yet exercise a production `PackageStore` write inside the guarded
callback, the peer control channel, or native Core final Apply. The existing
cross-process Store-authority control still reports its missing invalidation.
No Windows player package is replaced by this component check.

## Remaining integration

- Add the strict control protocol, peer owner, and connection-bound ticket.
- Bind the prepared peer fence to local token readiness and native revocation.
- Connect locked package mutations and complete store recovery to these callbacks.
- Mount startup, reconnect, display refresh, and app drain without a GUI wait.
- Test control-channel loss and late replies at every publication boundary.
- Verify native final Apply, supported app targets, and full app idle performance.

See [profile coordination](profile-authority-coordination.md) for the complete
multi-process contract. This component does not close those remaining gates.
