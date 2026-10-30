# Profile write permit and write refusal

Steps 2.3 and 2.4 of the [continuation plan](../07-continuation-plan.md).
Written 30 October 2026. Commits `a61031e03a` (permit) and `8808838eee`
(write refusal).

## Connection-bound write permit (2.3)

`StoreProfileWritePermit` (`addon_store_profile_client.*`) binds one prepared
writer scope to the live control connection.

1. The writer, on joined work, holds its prepared scope (and so
   `store.lock`), then calls the client's thread-safe `writePort()` with the
   scope's deadline.
2. The client sends Begin once it is joined and ready. A permit that arrives
   during a join waits for it. One write can be pending per client; a second
   permit is refused at once.
3. The client takes part in its own revocation, like every peer: its native
   admission closes, it drains, and it sends Revoked.
4. On the matching Grant, `wait()` returns true. `ticket()` gives the granted
   epoch and request, with a native-safe guard.
5. The guard closes at once on connection loss, Recover, client close or the
   deadline. A write after that is refused as stale, so the store is not
   changed.
6. `finish(result)` sends Complete for a ready result at the next revision,
   and Abort for any other result of a granted write. Abort starts recovery.
   A permit destroyed without `finish()` ends the same way.

## Refusing unapproved writes (2.4)

`PackageStore` has 12 ordinary write methods (11 names, two `saveSettings`
overloads). Once a peer is attached for the profile in this process
(`StoreAuthority::peerAttached()`, which never returns to false), each of
them refuses before it touches the store: "Other app windows share this
add-on profile. The change must go through the profile coordinator." The
guarded overloads, which take a `StoreProfileWriteContext`, keep working
inside a coordinator-permitted `StoreProfileGeneration` write.
`PackageStore::sharedProfile()` reports the mode.

`test_qt_addon_store_profile_writes` lists the ordinary writes. It checks
that the header's public non-static, non-const `bool` methods are exactly
that list, and that each definition calls the refusal or delegates to an
overload that does. A new unguarded write method fails the test.

Data-only private storage writes keep their separate policy. The app still
calls the ordinary methods; routing them through permits is the app mount
(Step 2.6).

## Verification

| Test | Cases | Result |
| --- | ---: | --- |
| `test_qt_addon_store_profile_client` (permit cases added) | 13 | pass, three runs with different `MALLOC_PERTURB_` |
| `test_qt_addon_store_profile_writes` | 3 | pass |
| All seven labels | 752 `profile` cases (24 tests); others unchanged | pass |

| Control | Change | Failing case |
| --- | --- | --- |
| ticket-ignores-connection | the ticket guard ignores the connection | `aLossEndsAGrantedPermitBeforeItsWrite` (the write lands) |
| loss-keeps-permit | a loss leaves the permit open | `aLossEndsAGrantedPermitBeforeItsWrite` |
| unguarded-setEnabled | one ordinary write lacks the refusal | refusal case and source check |
| new-unguarded-write | a new write method without the refusal | source check |

Not run: standalone CMake, ASAN/UBSAN, Windows, macOS.
