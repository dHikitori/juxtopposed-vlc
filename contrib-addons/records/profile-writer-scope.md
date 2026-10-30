# Prepared profile writer

The host can now hold `store.lock` before it asks the coordinator for a write
ticket. `StoreProfileGeneration::prepareWriter` returns one `Writer` scope.
This closes the local lock-order gap in the profile mutation contract. The
coordinator channel and app mount remain open. Native metadata owner
activation stays off.

## Lock and lifetime rules

Acquire, use, and destroy the scope on the same joined background task. File
access belongs on that task. Ticket negotiation must use a separate control
channel that can run while the store lock is held.

Preparation takes `store.lock` without taking the exclusive native fence or
changing the saved generation. Existing peers can retain their shared fences,
and new peers can prepare a join. Other writers and coordinator recovery
cannot pass the held store lock.

The first `Writer::write` call consumes the scope. It acquires the exclusive
fence, checks the exact generation and revision, then uses the existing
writing/ready transaction. Each first attempt releases both locks before it
returns, including `Busy`, `Stale`, and `Error`. A second or reentrant call
returns `Error`; it cannot release the first call's locks. A fresh scope is
required for a retry.

Release keeps the established order: unlock the store, send the display
notice while the exclusive fence is still held, then release the fence.
A retained `Writer` object holds no lock after the first attempt returns.
Destroy a scope that did not get a ticket to release its store lock.

`current()` checks the preparation control and whether the scope is unused.
It does not use a timer, thread, or polling loop. Expiry does not destroy the
scope. Its owner must still cancel or finish joined work and destroy it.

Both controls must use one monotonic clock domain. A ticket cannot extend the
preparation deadline. Both controls must stay current through the write.
A delayed ticket cannot restart the total time budget.

A callback can retire its writer. The operation retains its internal state
and locks until that callback returns. The callback context then reports
false. A release notice can also close the writer or cancel its control.
The operation checks again after lock release and returns `NeedsRecovery`
rather than a false successful result. This result can require reconciliation
even when the final saved record is already ready.

A ready saved record is not local native authority. Peers must complete the
coordinator join confirmation before they can publish current native tokens.
The one-call static `write` helper is for synchronous use. Runtime ticket
negotiation must call `prepareWriter` before it sends the ticket request.
The [bounded message format and coordinator policy](../profile-control-protocol.md)
are now tested. Runtime connection binding, native cancellation
acknowledgements, complete profile recovery, and the local authority admission
gate remain required.
See [the complete protocol](../profile-authority-coordination.md).

## Verification

The new tests cover the actual store lock, shared peer leases during ticket
negotiation, duplicate and reentrant writes, refusal and cancellation paths,
fixed deadlines, callback retirement, cancellation during release, and a
real child process killed while it holds a prepared scope. Five package rows
use the retained scope for enable, disable, rollback, settings, and package
recovery. The existing policy and lifecycle suites remain in the cohort.

The initial four-suite cohort passes 184 normal, 184 ASAN/UBSAN, and 176
native Windows checks. All 13 real producer units are compiled. The proof is
`/tmp/hakone-profile-writer-scope/final-proof.json`, SHA-256
`0260fb0f6fb62fef0178607cc29e2c469cf351cdf5eb18e8c01bc3207126b605`.
A control that delays the store lock until `write` fails the preparation-lock
regression. Its source, object, program, and results are retained.

Further review found a control-callback retirement gap. A control check could
retire the writer while returning true. The callback context could then
report true, or the final result could report ready after release. Both new
rows fail before repair. The writer now rechecks its own open flag after the
control checks. The before-repair source, object, program, and failed results
are retained under `/tmp/hakone-profile-writer-retirement/before-repair/`.
The follow-up rebuild uses all 13 real producer units and repeats the complete
generation and guarded package suites:

| Suite | Normal | ASAN/UBSAN | Native Windows |
| --- | ---: | ---: | ---: |
| Generation and writer scope | 60 | 60 | 60 |
| Guarded package state | 62 | 62 | 60 |
| Total | 122 | 122 | 120 |

All checks pass with no failed or skipped cases, runtime warnings, or
sanitizer reports. Leak and vptr checks stay on. The follow-up proof is
`/tmp/hakone-profile-writer-retirement/final-proof.json`, SHA-256
`4cb8a11ec9856236084ea8c39c74cabdcb4ef74eeed92e875c131ea04a0029d8`.
It pins 117 source files, 628 normal inputs, 638 sanitizer inputs, and 776
Windows inputs. The initial four-suite cohort is a separate checkpoint;
its policy and lifecycle suites were not repeated after the final two-row
repair. The complete generation and package suites were repeated on each
platform. Windows uses the real cached Qt and MinGW closure.

Across both checkpoints, the minimum free space was 7,513,526,272 bytes.
The final check left 7,603,191,808 bytes free.

These component tests do not establish full app response time or the cause
of the Windows cursor report. They do not enable the native metadata owner
or change the current local Windows review package.

Completed output objects and programs are retained in verified archives;
raw outputs are removed after verification. Five older completed archives
were also compressed again without loss. Their complete uncompressed SHA-256
values match before and after. Replacement pointers remain beside the old
archive paths, and `/tmp/hakone-profile-writer-scope/archive-recompression.json`
records all five replacements. This recovered 49,559,339 bytes without
removing source, object, program, or result content. Protected Core and local
Windows package hashes match. No Docker cache or user profile was removed.
