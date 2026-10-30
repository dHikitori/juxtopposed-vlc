# Profile session, handover and app mount

Steps 2.5 to 2.7 of the [continuation plan](../07-continuation-plan.md).
Written 30 October 2026. Commits `5760d62a9c` (session), `09f47c44fa` (mount) and
`3ad4ffc6a2` (crash matrix).

## Session and handover (2.5)

`StoreProfileSession` (`addon_store_profile_session.*`, commit `5760d62a9c`)
is the one owner that each app process runs per profile: the
[client](profile-client.md) always, and the [coordinator](profile-coordinator.md)
while the process holds the owner lock. Every window does the same thing.
When the client loses its coordinator, or cannot reach one, the session tries
the owner lock again. One process wins, recovers the profile to a new
generation and serves. The other clients reconnect and join it.

## App mount (2.6)

`AddonProfileHost` (`addon_profile_host.*`) mounts the session in the app.
MainUI creates it right after the `AddonManager`, before the bundled add-on
setup and before any native consumer of the profile.

- `start()` attaches the profile peer on the GUI thread (memory only). From
  then on, native admission waits for the coordinator, and the store's
  ordinary writes refuse.
- The session runs on its own control thread (`hakone-profile`). The host
  gives the store a `ProfileWriter`: a ready-wait and a write port.
- Every `AddonManager` store write, the bundled setup and the removal job now
  call `PackageStore::change(ordinary, guarded)`. In a shared profile it waits
  until the client is joined, without `store.lock`, then takes the prepared
  writer, asks for the permit, and runs the guarded method inside the
  permitted write. A refused permit releases `store.lock` and tries again
  until the 10-second limit.
- Profile recovery is `PackageStore::recoverProfile()`. It removes release
  staging that an interrupted install left. Packages flagged
  `recoveryRequired` keep that flag for the user's explicit Restore action.
- When the client rejoins at a new revision, the host calls
  `AddonManager::profileChanged()` on the GUI thread: it reloads the provider
  order, refreshes the display catalogue and bumps the revision. The
  metadata service's invalidation then cancels consumers without an error;
  `EpisodeMetadata` queues its automatic lookup again, and the coordinator
  caches only match, partial and no-match results, so a revoked lookup
  records nothing negative (plan §4.2).
- `MainCtx::addonWorkRetiring` closes the host: the session closes on its
  thread, native admission closes at once, and the thread is joined.

Manager writes are explicit user actions. In a shared profile they wait for
the coordinator on the GUI thread, bounded at 10 seconds. Nothing in that
chain needs the GUI thread, so it cannot deadlock. Moving these writes to
joined work, with an asynchronous result in the Extensions UI, is a
follow-up.

## Defects found while mounting

1. **Livelock between a writer and recovery.** The first mount held
   `store.lock` while waiting for a permit. The permit needed the join, the
   join needed recovery, and recovery needed `store.lock`. Every bundled
   install timed out. Writers now wait for the join before they take
   `store.lock`, and a permit requested while not joined is refused at once.
2. **No endpoint under a deep runtime directory.** With `$XDG_RUNTIME_DIR`
   so deep that the socket path did not fit `sun_path`, no endpoint was made.
   The endpoint now tries the next candidate (`/tmp/hakone-<secret>`). The
   order is fixed, so all processes choose the same one.
   `aDeepRuntimeDirectoryFallsBackToAFittingOne` covers it.

## Crash matrix (2.7)

`test_qt_addon_store_profile_session` runs app-like child processes. A test
hook in the child exits it at each writer boundary:

| Boundary | Outcome for the surviving process |
| --- | --- |
| Holding `store.lock`, before Begin | The stale lock is taken over; the next write passes |
| After the permit request | Recovery if Begin was pending; the next write passes |
| After the grant, before the write | Recovery; the next write passes |
| Inside the write, record marked writing | Recovery from the writing record; the next write passes |
| After the write, before Complete | Recovery; the next write passes |

- A peer killed during a native cancellation that never returns: until it
  exits, no write passes its retained shared fence, and the write does not
  report success. Its exit starts recovery at a new generation; the next
  write passes.
- A coordinator killed during a pending write: the write fails, the hung peer
  still blocks recovery, and after that peer exits the writer takes over as
  coordinator and writes.

Late messages, wrong generations, frame and queue limits and claimed
identifiers are covered at the component level by the protocol, socket and
coordinator suites. Native final Apply under a closed token is covered by
the `metadata` label (prepared invocations, artwork outcomes and the Core
metadata transactions). Native metadata owner activation stays off.

## Verification

| Test | Cases | Result |
| --- | ---: | --- |
| `test_qt_addon_store_profile_session`, real child processes | 12 (handover and crash matrix) | pass, three runs with `MALLOC_PERTURB_` |
| `test_qt_addon_profile_host`, one with a real child process | 3 | pass |
| `test_qt_addon_store_profile_socket` | 40 | pass, Docker cross-user cases included |

- A write in one process cancels a native lease in another before the write,
  and both rejoin the next revision. This is the two-process control from
  [profile coordination](../profile-authority-coordination.md), which
  failed before Step 2.
- A killed or quitting coordinator hands over to one remaining process.

Full app, Linux, `meson test --suite app`, mounted profile:

| Run | Result |
| --- | --- |
| One window | Bundled add-ons ready in 1.7–1.8 s; idle CPU 0.18 s / 30 s; quit 0.16 s |
| Two windows, both quit orders | Pass; idle CPU 0.19–0.26 s / 30 s |
| Driver | Extensions, install and discovery pass |
| Two windows with drivers (`addon_app_smoke_shared_profile`) | An install in window 1 shows in window 2 after 150–163 ms; idle CPU 0.13–0.14 s / 30 s per window; quit in both orders |

| Control | Change | Failing case |
| --- | --- | --- |
| no-claim | the session does not claim after a loss | `theNextProcessTakesOverWhenTheCoordinatorIsKilled` |

All seven labels pass at the mount commit: `profile` 26 tests and 772 cases, `app` 4 tests, the others unchanged.

Not run: standalone CMake, ASAN/UBSAN, Windows, macOS.
