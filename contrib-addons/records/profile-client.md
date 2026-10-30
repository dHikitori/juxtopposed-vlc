# Profile client owner

Step 2.2 of the [continuation plan](../07-continuation-plan.md). Written
30 October 2026. Commit `7a8c17ed81`.

`StoreProfileClient` (`modules/gui/qt/addons/addon_store_profile_client.*`) is
each process's side of the [control protocol](../profile-control-protocol.md).
It owns the process's `StoreAuthorityPeer` and its control socket, on the
same control thread as the [coordinator](profile-coordinator.md). It is not
mounted in the app yet.

## Behavior

- **Join.** Close local admission and keep the stamp, drain on joined work,
  send Join, check the offered epoch under the shared native fence on joined
  work (guarded by the peer's `pending(stamp)`), Confirm, and admit only on
  a Ready with the same connection, request and epoch. Hold waits for
  Available.
- **Revoke, Recover, loss.** Close native admission at once, in memory. The
  socket's native callback does this for a loss, before the control
  callback. Then wait for any join preparation or admission task, so no
  fence copy remains, drain on joined work, and only then send Revoked (for
  Revoke) or wait for Available. A Recover or a loss drops a pending
  acknowledgement, because the arbiter would refuse it.
- **Reconnect.** After a loss, retry the endpoint every `retryInterval` ms
  for `reconnectLimit`, then report Unavailable. A ready client has no timer.
- **No GUI wait.** Every blocking step runs on joined work.

`StoreProfileControlWork` (`addon_store_profile_control.*`) now holds the
joined-work and native-wake plumbing for both owners.

Coordinator handover (another process taking over after a loss) is Step 2.5.

## Verification

Test `test_qt_addon_store_profile_client` (label `profile`), 7 cases, with the
real coordinator, sockets, fences and writer scope:

- a join admits native authority; neither owner has a timer when idle
- a Revoke closes the token at once; with a native cancellation that blocks,
  the writer gets no grant until it returns; the exclusive write then passes,
  and the client rejoins the next revision with a fresh token
- Recover drains without an acknowledgement, and the client joins the new
  generation
- a lost coordinator closes the token at once; the client joins the new owner
- Hold waits for Available
- with no coordinator, the client ends Unavailable with no timer
- a write that starts at five points of the client's join still gets its grant

| Build | Cases | Failed | Skipped |
| --- | ---: | ---: | ---: |
| Linux, meson debug, three runs with different `MALLOC_PERTURB_` | 7 each | 0 | 0 |
| `profile` label at `7a8c17ed81` | 741 (23 tests) | 0 | 0 |

| Control | Change | Failing assertion |
| --- | --- | --- |
| ack-before-drain | Revoked is sent before the drain | no grant while cancellation runs |
| no-close-on-revoke | Revoke does not close admission at once | the token closes at once |

Not run: standalone CMake, ASAN/UBSAN, Windows, macOS.
