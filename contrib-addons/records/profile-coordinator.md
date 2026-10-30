# Profile coordinator owner

Step 2.1 of the [continuation plan](../07-continuation-plan.md). Written
30 October 2026. Commit `5fd715edaa`.

`StoreProfileCoordinator` (`modules/gui/qt/addons/addon_store_profile_coordinator.*`)
is the owner that the [control protocol](../profile-control-protocol.md) calls
for. It is host only and not mounted in the app yet.

## Behavior

- **Claim.** `start()` tries the profile owner lock on joined work. The process
  that gets it removes a stale Unix endpoint and listens. Every other process
  reports `Peer` and serves nothing. Lock order is the owner lock, then
  `store.lock`, then the exclusive fence.
- **Recovery before Ready.** The owner recovers the profile to a new
  generation at revision zero before it publishes the first ready epoch. It
  listens during recovery, so a Join gets Hold until then. Recovery runs on
  joined work and retries a busy fence only while that recovery is active,
  up to `recoveryLimit` (at most 30 seconds). A failed or timed-out recovery
  closes the owner and releases the owner lock.
- **Routing.** Each accepted socket is bound to the identifier that the
  arbiter assigns. Input is routed by that binding. A message that claims
  another identifier is refused, and its socket is closed.
- **Effects.** The owner applies every arbiter effect, also when a message is
  refused: sends, closes, recovery and saved-record checks.
- **Checks.** After Complete, the owner reads the exact ready record under the
  shared native fence on joined work, releases that fence, and reports the
  result to the arbiter. A missing or different record starts recovery.
- **Timer.** One single-shot timer covers the pending write's deadline and each
  accepted connection's handshake: the time from accept to its first Join
  (`handshake`, default 5 seconds). This closes review note 3 of the socket
  step. An idle coordinator, with ready peers and no pending write, has no
  timer.
- **Results.** Joined work posts results to an inbox and signals a native
  wake. The control thread runs them. After `close()` nothing is posted.

The owner does not yet run on its own thread in the app, and no client owner
exists. The tests play the peer side with real sockets.

## Verification

Test `test_qt_addon_store_profile_coordinator` (label `profile`), 12 cases,
with the real generation helper, native fences, writer scope and sockets:

- one coordinator per profile; the second reports Peer
- a fresh generation after recovery; Join gets Hold until it is published
- a full write: Begin revokes every peer, Revoked from all, Grant, a real
  write, Complete, checked record, Available, and a Join at the next revision
- a Complete without the saved record starts recovery
- a lost revoking peer starts recovery with a new generation
- recovery waits while a peer retains its shared fence
- the write deadline ends a write that one peer never acknowledges
- a silent connection ends at the handshake deadline; a prompt peer stays
- a claimed connection identifier is refused
- close releases the owner lock, and a new owner publishes a new generation

| Build | Cases | Failed | Skipped |
| --- | ---: | ---: | ---: |
| Linux, meson debug | 12 | 0 | 0 |
| Same, five runs with different `MALLOC_PERTURB_` values | 12 each | 0 | 0 |
| `profile` label at `5fd715edaa` | 732 (22 tests) | 0 | 0 |

Four control variants each fail their selected case:

| Control | Change | Failing case |
| --- | --- | --- |
| no-handshake | the timer does not close silent connections | `aSilentConnectionEndsAtTheHandshakeDeadline` |
| skip-refused-effects | effects are not applied when a message is refused | `aClaimedConnectionIdentifierIsRefused` |
| trust-completion | the saved-record check always reports coherent | `aCompletionWithoutTheSavedRecordStartsRecovery` |
| claimed-identifier | input is routed by the message's identifier | `aClaimedConnectionIdentifierIsRefused` |

Not run: the standalone CMake build, ASAN/UBSAN, Windows and macOS. The unit
is registered in all three build systems.

## Next

Step 2.2, the client owner: socket, join preparation on joined work, immediate
native close on Revoke or loss, drain and fence release before Revoked, and no
GUI wait. Then the connection-bound write permit (2.3).
