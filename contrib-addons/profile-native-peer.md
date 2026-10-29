# Native peer readiness

`StoreAuthorityPeer` now binds native store tokens to local peer readiness.
It is an opt-in host helper. The app does not attach it yet. Socket owners,
connection-bound write permits, complete profile recovery, and app cutover
remain open. Native metadata owner activation stays off.

## Admission and cancellation

The helper separates two operations:

- `close()` closes admission at once. It uses memory and atomics only. It does
  not wait for a native callback, touch a file, or call a QObject.
- `drain()` runs on joined background work. It waits for native cancellation
  and local callback-capture cleanup. It releases the shared process fence
  only after that work ends.

All `StoreAuthority` handles for one canonical profile share the readiness
stamp. Zero is the current ungated mode before peer attachment. Odd stamps
close admission. Later even stamps admit a confirmed peer. Attachment closes
old tokens. Once attached, that retained store state never returns to zero.
Old tokens keep the store state alive, so owner loss or reconnect cannot make
those tokens current again. A fresh profile open must still attach its peer
before any native consumer starts.

`capture()`, `StoreAuthorityToken::current()`, and watch checks include the
exact peer stamp. They use memory only. A delayed background cancellation
cannot leave old tokens current after immediate closure. After a new join,
only fresh captures can become current.

Closing an already closed peer advances the stamp again. Each join attempt
therefore has a separate local guard. Admission uses an exact atomic compare
against the drained stamp; a stale ready completion cannot open a later
attempt. Counters do not wrap. A counter limit retires the authority.

## Host sequence

Use one peer owner per canonical profile per process. Other store handles
share its gate. `attachPeer()` refuses a second owner. Create it on joined
startup work, before native consumers. This is not a live migration protocol
for consumers that were started without a process fence.

1. Call `close()` for the new join attempt and retain its nonzero stamp.
2. Run `drain(stamp)` on joined work. It must succeed before admission.
3. Use `pending(stamp)` as the memory-only guard for background shared-fence
   preparation. Prepare the exact epoch with `StoreProfileGeneration`.
4. Confirm the candidate through the live control connection. Require the
   same connection, request, generation, and revision in the server's Ready.
5. Move that profile's checked shared fence into `admit(stamp, fence)` on
   joined work. A null fence, stale stamp, missing drain, active local writer,
   retired store, or closed owner refuses admission.
6. Only then capture native authority and arm native cancellation before
   starting a native lease.

The helper does not authenticate a socket, check the saved epoch, or validate
that a caller supplied the correct profile's fence. Those are required
responsibilities of the host peer owner. Do not call `admit` from an add-on
or directly from a decoded message.

On Revoke, recovery, timeout, or socket loss, call the native weak close handle
at once. Bind its use to the live connection. Then drain on separate joined
work. Also cancel and join pending preparation tasks and release their
unpublished shared fences before sending Revoked. This helper does not own
those tasks. The socket owner must remain free to read control messages.
Neither it nor native cancellation may wait for the GUI thread.

`pending(stamp)` is a join-preparation guard. Write permits need an independent
live connection/request guard. A local store mutation or explicit
invalidation also closes peer readiness and changes this stamp. Such a change
must not cancel its own valid writer permit merely because read admission
has closed.

## Lifetime rules

The peer owns the shared fence. Tokens and watches do not keep a fence after
native cancellation ends. They can stay in memory as permanently closed
objects. Their retained quota still follows the existing native-capture
rules.

Close and pending handles are weak. They do not keep the peer or its fence
alive. A closed or expired owner makes those handles return zero or false.

Peer destruction also closes admission, drains callbacks, and releases its
fence. Destruction belongs on joined background work. Never destroy, admit,
or drain a peer from a Store callback. The existing no-reentry rules apply.
A replacement cannot attach while the old peer's drain still owns its state.

Local `mutation()` and `invalidate()` keep peer admission closed after their
work ends. A fresh server confirmation is required to admit again. The
existing ungated mode keeps its behavior until the app mounts the peer gate.
Runtime integration must also refuse unapproved ordinary write entry points;
this helper alone does not prevent an old caller from writing without the
coordinator ticket and exclusive fence.

## Verification

The new suite checks immediate closure, callback and capture cleanup,
retained-fence order, refused callback registration, stale join attempts,
weak handles, owner loss, local mutation and invalidation, retained old
tokens, and racing close/admit operations. A real child process uses the new
gate and a checked shared fence. The parent cannot pass that fence while the
child's native cancellation is paused. After drain, the parent can complete
a real generation transaction. The child rejoins the next epoch; its old
tokens and watch stay closed.

That child uses a controlled standard-input/output test channel. It is not
the production socket transport or a same-user authentication test. Native
Core final Apply, the full consumer cohort, and app performance still need
verification after integration.

Three normal-build control variants ignore readiness in token checks, release the fence
before cancellation, or admit a stale stamp without compare/exchange. Each
must fail its selected regression. Their sources, objects, programs, and
results are retained. They are changes to the current producer for these
checks, rather than historical release binaries.

The final results are:

| Suite | Normal | ASAN/UBSAN | Native Windows |
| --- | ---: | ---: | ---: |
| Native peer readiness | 16 | 16 | 16 |
| Existing store authority | 13 | 13 | 13 |
| Protocol and coordinator | 104 | 104 | 104 |
| Generation and writer scope | 60 | 60 | 60 |
| Total | 193 | 193 | 193 |

All checks pass with no failed or skipped cases, compile warnings, runtime
warnings, or sanitizer reports. Leak and vptr checks stay on. Windows uses
the existing Qt 6.8.3 and GCC 14 cache read-only, with host MOC 6.8.0 as in
the app. The child-process close, drain, and next-epoch join pass on Linux
and native Windows. The immediate-close check also stays below 100 ms on
these hosts. That is a component observation, not a full app latency bound.

The proof is `/tmp/hakone-profile-peer/final-proof.json`, SHA-256
`8f922b81aabd3f27b0a2df528504d2c52c91f5b05f4a212490f7388110050f4c`.
It pins 122 source files, 569 normal inputs, 579 sanitizer inputs, and 735
Windows inputs. The real five-unit native closure is compiled in each mode.
The source and new test are registered in all three build definitions.
The complete app is not built or mounted by these component checks.

The Windows space guard requested a stop as the final targets finished.
The build returned exit zero, and the log and all four programs confirmed
completion. The build was not restarted. Before native tests, four duplicate
link archives were checked member by member against retained objects, then
removed. All four programs then passed their actual Windows tests. This
sequence is recorded in `pretest-duplicate-cleanup.json`.

Completed objects and programs are retained in verified compressed archives;
raw outputs are removed after verification. Four older completed archives
were also compressed again without loss. Their complete uncompressed hashes
match. Replacement pointers remain beside the old paths, and the three
`archive-recompression*.json` records retain the mappings. This recovered
34,510,016 bytes. No Docker cache or user profile was removed.

The minimum observed free space was 7,512,313,856 bytes. The final check left
7,585,787,904 bytes free. Protected Core and local Windows package hashes
match. The current Windows review package is unchanged.

## Remaining integration

Implement the authenticated socket owners and client protocol state. Bind
close and grant controls to their live connection and request. Join all
pending preparation work before acknowledgement. Refuse unapproved ordinary
write entry points once runtime peer mode is active. Complete whole-profile
recovery, then mount the owner before consumers. Verify all native final
Apply paths and preserved providers. App idle cost, response time, cursor,
quit behavior, and supported-platform checks remain open. See
[profile coordination](profile-authority-coordination.md).
