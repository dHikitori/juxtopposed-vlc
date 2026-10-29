# Profile authority across processes

Multiple app processes must be able to use one local add-on profile. A change
to that profile must revoke old native write authority in every process.
The full gate remains open. The native fence below is implemented and tested.
The saved generation and guarded writer helper are now implemented. See
[guarded generations](profile-generation-transactions.md). Store policy,
package state and lifecycle methods now have tested guarded bindings. See
[guarded settings](profile-store-settings.md) and
[package state writes](profile-package-writes.md), plus
[lifecycle writes](profile-lifecycle-writes.md). A
[retained writer scope](profile-writer-scope.md) now holds the store lock
during ticket negotiation. The [message format and coordinator
policy](profile-control-protocol.md) are also implemented. Both socket roles,
same-user authentication, complete profile recovery and the production mount
are not implemented yet.
Native metadata owner activation stays off.

A peer is one app process. A fence lease is one held OS lock. An epoch is a
local authority revision. A nonce is a random identifier. A write ticket
identifies one proposed store transaction.

## Confirmed gap

`StoreAuthority` shares an epoch through a process-local registry. Its token
check reads memory only. `store.lock` serializes saved-state access. Neither
one sends native cancellation to another OS process.

A real two-process control publishes a change through the same mutation gate
used by the store. The first process still reports a current token, an armed
watch, and zero native cancellations. The record is under
`/tmp/hakone-store-process-fence/`. The control returns an error status. This
is evidence of a missing gate, not a passing completion check.

A file watcher alone cannot close the gap. A native result can commit before
the event arrives. A final saved-state read can also race with a later write.
The publication barrier must cover the final native commit.

## Implemented native fence

`StoreProcessFence` holds an OS lock on one stable, empty profile file.
It has two modes:

| Mode | Rule |
| --- | --- |
| Peer | Several processes can hold this shared lease. |
| Replacement | This exclusive lease requires all peer leases to end. |

Acquisition uses a nonblocking lock request. File open and validation still
use the filesystem. Run acquisition on joined background work. A refused lock
returns `Busy`; a bad file or unsupported system returns `Error`.

The retained object holds only a native handle. Native captures can keep it
after a Qt owner ends. The last capture closes the handle. The OS also
releases the lock when its process exits. No timer, thread, polling loop,
QObject, or saved-state reader is part of this object.

Windows uses a one-byte `LockFileEx` lock. The range can extend beyond the
empty file. Handles are not inheritable and do not share delete access.
POSIX uses `flock` on a close-on-exec descriptor. File open does not follow a
final symlink and does not block on a FIFO. Validation refuses special files,
nonempty files, and hard links. POSIX also requires the current user and
owner-only access. A profile must keep the same fence file for its lifetime.
It must not unlink, replace, or rename that file while any process uses it.

These are host-only interfaces. An add-on receives no file path or lease.
The fence does not grant package, account, network, or playback authority.
It does not replace `store.lock`.

## Coordinator and peer contract to implement

Each canonical profile has one coordinator. An owner lock selects it.
Each app process has one peer per profile, shared by its local store owners.
The coordinator and peers use an event-driven local control channel. This
channel must run independently of the GUI thread. Idle peers do no polling.
Use bounded startup or recovery retries only while that work is active.

A fresh coordinator generation has an unpredictable nonce. Each completed
mutation advances a revision. A small host-owned stamp stores the generation,
revision, and phase: `ready`, `writing`, or `recovering`. Its atomic writes
run under both `store.lock` and the exclusive fence. The stamp is a separate
file; the fence file stays empty and stable.

The control channel has a fixed version and strict message schemas. Assign
peer identity from the accepted connection. Bind each write ticket to that
connection, generation, revision, request, and deadline. A ticket alone does
not permit a saved-state write. Bound peer count, message size, pending
requests, and retry time. The local endpoint must admit only the profile's
user. This channel exposes no guest method or general file operation.

### Join

1. Start with local authority closed.
2. Receive a candidate ready generation and revision.
3. Acquire the shared peer fence on joined work.
4. Read the stamp while that lease is held. Require the same ready generation
   and revision. Refuse a writing or recovery phase.
5. Confirm the join through the control channel. Publish local ready authority
   only after the coordinator confirms the same revision.
6. Keep the shared fence until local native authority has been revoked and
   its cancellation callbacks have returned.

If a mutation starts during Join, the coordinator must revoke the joining
peer too. The writer cannot pass its exclusive fence until that peer has
closed its lease. A stale ready reply cannot reopen an old local epoch.

### Mutation

1. Acquire `store.lock`. Prepare the transaction without publishing authority
   changes. Data-only storage writes keep their existing separate policy.
2. Request a write ticket. The coordinator closes the ready phase and asks
   all joined and joining peers to revoke.
3. Each peer closes native admission, advances its local epoch, and runs the
   existing native cancellation callbacks. It then drops its shared fence
   and sends an acknowledgement.
4. The writer must acquire the exclusive fence itself. A disconnected peer
   is not proof that its native work has ended. Its retained fence still
   prevents unsafe publication.
5. While that exclusive lease is held, read the stamp again. Require the
   ticket's exact generation and revision. Check its deadline. This refuses
   an old ticket after coordinator replacement.
6. Mark the stamp as writing, then perform the ordinary store transaction.
   Keep both locks through publication, rollback, and recovery state.
7. Publish a ready stamp only for a coherent result. Otherwise leave the
   profile in recovery. Release `store.lock` before ending the exclusive
   lease and announcing completion.
8. Peers repeat Join and obtain fresh local epochs. Then refresh their cached
   display and runtime registrations on joined work.

The actual storage writer owns the exclusive lease. Thus a coordinator crash
cannot let a replacement pass an active writer. The writer's stamp check also
covers a crash between ticket delivery and exclusive acquisition.

The existing mutation RAII objects end inside an outer store-lock scope.
Production integration must retain the process lease until that outer lock
has been released. A premature ready notice can strand another process's
catalogue behind a locked store. Read-only catalogue locks must not request
a mutation or cancel active jobs.

### Loss, crash, and recovery

On control-channel loss, close local authority first. Cancel native leases
without a GUI wait. Drop the shared fence only after native cancellation has
returned. Old tokens must stay dead through reconnection.

A replacement coordinator uses this lock order:

1. Coordinator owner lock.
2. `store.lock`.
3. Exclusive profile fence.

It must recover the store and publish a new generation before it admits peers.
Use the same order for an ordinary writer's store lock and exclusive fence.
A peer must not need `store.lock` or the GUI thread to cancel native authority.

If a peer cannot acknowledge within the bound, refuse the mutation. Do not
infer cancellation from a timeout. A later writer must still acquire the
exclusive fence. It cannot skip a retained peer lease. If a writer exits,
recover its saved-state journal and stamp before opening ready authority.

Keep the ordinary player and trusted local readers available during profile
recovery. Report unavailable add-on authority in the host UI. Do not reopen
remote legacy paths to work around a failed gate.

## Verification

The focused normal run passes 23 native fence cases and all 13 existing
store-authority cases. It uses actual child processes, not two objects in one
process. It covers shared peers, exclusive refusal, a retained native capture,
process exit, process kill, a paused native cancellation, handle inheritance,
Unicode paths, bad files, links, and a FIFO. A refused lock returns within 100 ms on this host.
That timing includes a local test file open. It is not a filesystem latency
promise or a full app performance result.

The final results are:

| Host | Fence | Existing authority | Total |
| --- | --- | --- | --- |
| Linux normal | 23 | 13 | 36 |
| Linux ASAN/UBSAN | 23 | 13 | 36 |
| Native Windows | 19 | 13 | 32 |

There are no failures, skips, warnings, or runtime diagnostics in these
suites. Leak and vptr checks are on in the sanitizer build. The four POSIX
link, mode, and FIFO cases do not exist in the Windows test build. The Windows
run uses `LockFileEx` in actual Windows processes. Its Unicode path and helper
inheritance checks pass. The separate cross-process control reports the old
token gap on Linux and Windows, as expected.

Windows uses the existing GCC 14 and Qt 6.8.3 dependency cache read-only. Its
host MOC is 6.8.0, as in the app build. The component build supplies that tool
explicitly. It does not change the cached toolchain or bypass Qt's generated
code checks. The test programs import system DLLs only. They do not need the
player package or a separate GCC runtime DLL.

The final proof is `/tmp/hakone-store-process-fence/final-proof.json`, SHA-256
`6a74051febfd11f458da60dcec4cbaec17050a03dde16f0f61c57342131f5a93`.
Source, object, program, recipe, and dependency hashes are retained. All 41
completed program, object, and archive files are retained in a verified
compressed archive. Raw completed outputs and superseded probes were removed.
The protected Core libraries and current Windows player package are unchanged.
Free space is above 7.5 GB.

macOS runtime checks remain required. No profile peer is mounted in the app
yet. These tests do not establish cross-process Core publication or full app
performance.

## Remaining implementation gates

- Connect the tested control protocol to both authenticated socket owners.
- Use the implemented prepared writer to retain the store lock during ticket
  negotiation. Keep the control owner independent of that joined work.
- Bind the implemented generation and writer guard to an authenticated
  connection and pending request.
- Make mutation refusal explicit at every authority write entry point.
- Mount the writer lease that stays held through outer store-lock release.
- Attach peer readiness to the memory token and all native cancellation paths.
- Mount startup, display refresh, reconnect, and app drain without a GUI wait.
- Test crashes at each ticket, stamp, write, acknowledgement, and join boundary.
- Test late messages, wrong generations, queue limits, and native final Apply.
- Verify supported targets and full app idle CPU, response, and quit behavior.

Do not enable native metadata cutover on the strength of the fence alone.

## OS references

The implementation uses the documented shared, exclusive, nonblocking, and
process-exit semantics of
[Windows file locks](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
and [POSIX flock on Linux](https://man7.org/linux/man-pages/man2/flock.2.html).
