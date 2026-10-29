# Profile control protocol

`StoreProfileProtocol` and `StoreProfileArbiter` now implement the private
message format and coordinator policy for profile authority. They are
host-only code. They expose no add-on method and grant no native token.

The [native readiness helper](profile-native-peer.md) now binds Store tokens
to local readiness and joined cancellation. The socket owner, client protocol
owner, same-user authentication, complete profile recovery, and app mount
remain open. Native metadata
owner activation stays off. These classes do not close the existing
cross-process `StoreAuthority` gap by themselves. See
[the complete protocol](profile-authority-coordination.md).

## Message format

Each frame has exactly 96 bytes. All integers use big-endian byte order.
Revision uses the full unsigned 64-bit range. Time uses positive signed
64-bit monotonic microseconds. The transport owner supplies the current time;
it must not use a client field as the current time.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | `HAKONEP1`, including protocol version |
| 8 | 1 | Message kind, 1 through 14 |
| 9 | 7 | Reserved, all zero |
| 16 | 16 | Accepted connection identifier |
| 32 | 16 | Generation, or all zero when absent |
| 48 | 8 | Revision, or zero when absent |
| 56 | 16 | Request identifier, or all zero when absent |
| 72 | 8 | Deadline, or zero when absent |
| 80 | 16 | Reserved, all zero |

The kind values are `Welcome`, `Join`, `Candidate`, `Confirm`, `Ready`,
`Begin`, `Revoke`, `Revoked`, `Grant`, `Complete`, `Hold`, `Recover`, `Abort`,
and `Available`, in that order. Unknown kinds, versions, reserved values,
wrong sizes, and forbidden fields are refused.

| Kinds | Epoch | Request | Deadline |
| --- | --- | --- | --- |
| Welcome, Recover, Available | Absent | Absent | Absent |
| Join, Hold | Absent | Nonzero 16 bytes | Absent |
| Candidate, Confirm, Ready | Generation and revision | Nonzero 16 bytes | Absent |
| Begin, Revoke, Revoked, Grant, Complete, Abort | Generation and revision | Nonzero 16 bytes | Positive |

A generation is a nonzero 16-byte value. All messages have a nonzero 16-byte
connection identifier. Field presence is fixed by kind; extra fields cannot
change the meaning of another message kind.

The decoder accepts at most 32 frames' bytes per call. It retains at most 95
incomplete bytes between calls. It handles split headers, split payloads,
and several frames in one read. A bad or oversized batch fails the decoder
permanently and returns no messages from that batch. The socket owner must
close that connection. It must not apply a valid prefix from a failed batch.

## Coordinator policy

The arbiter starts in `Recovering`. Only a completed external recovery under
the owner lock, store lock, and exclusive fence can publish its first ready
epoch. Recovery must use a different generation at revision zero. A socket
message cannot publish a recovered epoch.

The owner supplies a fresh random instance nonce for its owner-lock lifetime.
The arbiter assigns connection identifiers from a 64-bit instance prefix and
a monotonic 64-bit counter. It does not reuse an identifier in that lifetime
or wrap the counter. At most 64 peers can be connected. The transport must
bind the returned identifier to its accepted socket, after same-user
validation. It must route input by that binding, never by the message's
claimed identifier. The identifier is not a substitute for authentication.

A join request gets a candidate only while the coordinator is ready.
The peer must acquire and check its actual shared native fence, then confirm
the exact candidate epoch and request. Only then can the server send `Ready`.
A candidate is not current native authority. A late confirmation during
revocation cannot send a new ready message.

The actual writer must hold the
[prepared writer scope](profile-writer-scope.md) before it sends `Begin`.
The coordinator accepts one pending write. It binds that write to its
accepted connection, exact generation and revision, request, and deadline.
The deadline must be ahead of current time and within 30 seconds. The
preparation deadline can impose a shorter bound.

`Begin` closes the ready phase and sends `Revoke` to every joined and joining
peer. New joins get `Hold`. A peer must close native admission, cancel native
work and pending join preparation, and release the shared fence before it
sends `Revoked`. The arbiter grants the writer only after every required
acknowledgement matches the pending operation.

Acknowledgement is not proof of physical cancellation. The actual writer
still needs the exclusive native fence. A retained native lease blocks that
writer even if a peer sends a false acknowledgement.

`Complete` must come from the granted writer and carry the exact next
revision, generation, request, and deadline. It starts `Checking`. It cannot
send ready messages. The host must verify the exact ready saved record under
the actual native fence, after the writer has released both locks. Only the
matching, coherent, still timely check can send `Available`. Peers then
repeat Join with fresh local admission. A duplicate completion cannot start
another check. A stale check cannot open admission.

A writer abort, lost writer, lost unacknowledged peer, timeout, bad clock,
failed saved-record check, or malformed participating peer requires recovery.
The coordinator does not reopen the old epoch. A peer that already drained
can disconnect without cancelling another writer. Socket EOF and timeout
never count as cancellation acknowledgements.

The owner must apply all returned close and recovery effects, including when
`accepted` is false. That flag does not cancel the other effects.

## Owner work still required

The arbiter is memory-only. It has no callback, file reader, socket, QObject,
thread, timer, or polling loop. An independent joined owner must apply its
effects, keep socket input and output bounded, and service a single deadline
wake only while a mutation is active. Handshake and join work also need
bounded active deadlines. Idle ready peers need no periodic wake.

Implement both transport roles with the same framing. Validate the profile
user through the local transport before admission. Keep local readiness
closed through socket loss and reconnect. Retain the shared fence until
native cancellation and any pending join work have ended. Bind grant
controls to the live connection and pending request. Mount the guarded write
methods and complete recovery. Do not enable native metadata cutover before
that owner and its native Core/Qt/consumer cohort pass.

## Verification

The new suite checks exact wire bytes, every message kind, all reserved byte
positions, large revisions, invalid fields, fragment boundaries, batch
limits, permanent decoder failure, peer limits, connection binding, join
confirmation, late messages, revocation, deadlines, completion checks,
abort, disconnect, and recovery rules. Three flows use the real generation
helper, store lock, shared and exclusive native fences, and writer scope.
The existing generation suite also stays in the cohort.

Three separate control variants skip joining peers, trust unchecked
completion, or retain messages from a failed decode batch. Each must fail
its selected regression. These are changes to the current producer for the
control runs; they are not historical release binaries.

The final results are:

| Suite | Normal | ASAN/UBSAN | Native Windows |
| --- | ---: | ---: | ---: |
| Protocol and coordinator | 104 | 104 | 104 |
| Existing generation and writer scope | 60 | 60 | 60 |
| Total | 164 | 164 | 164 |

There are no failed or skipped cases, compile or runtime warnings, or
sanitizer reports. Leak and vptr checks stay on. Windows uses the existing
Qt 6.8.3 and GCC 14 cache read-only. Its host MOC is 6.8.0, as in the app.
The test programs import system DLLs only. The new protocol flows use native
locks directly within one process; existing generation cases also use real
child processes. This does not establish an operating socket transport.

The proof is `/tmp/hakone-profile-protocol/final-proof.json`, SHA-256
`d6e03a7aa4aab991911b1f63970a29e0b6320866f6be637bff97f1b009bcf26a`.
It pins 120 source files, 558 normal inputs, 568 sanitizer inputs, and 726
Windows inputs. The actual producer closure has five units: protocol,
generation, process fence, store authority, and native notifications. All
three build definitions register the source and test. Full app builds are
not part of this component check.

Completed objects and programs are retained in verified compressed archives.
All raw completed outputs are removed. The three control producers, objects,
programs, commands, and results are also retained; their raw compiled outputs
are removed after verification. The minimum free space was 7,548,899,328
bytes. The final check left 7,584,313,344 bytes free. Protected Core and local
Windows package hashes match. No Docker cache or user profile is changed.

The native readiness binding now has its own
[checked component cohort](profile-native-peer.md). Same-user socket
authentication, client protocol state, cross-process Core Apply, macOS
runtime, and full app performance checks remain required.
The local Windows review package is unchanged.
