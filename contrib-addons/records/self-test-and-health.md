# Offline self-test and update health period (Step 6.2)

Step 6, row 2 of the [continuation plan](../07-continuation-plan.md).
Written 31 October 2026. Implements install steps 5 and 8 of
[packages and updates](../02-packages-and-updates.md).

## Before install: offline candidate check

`AddonCandidateCheck` (`addons/addon_candidate_check.*`) runs in the same
background repository job that fetches a release, for a new install and for
an update, before the app offers it:

- A package without a runtime passes (themes and other data-only packages
  stay independent of a worker).
- The worker starts on the release's module from a private temporary
  folder. It must compile the module and send `ready`.
- A module that declares `runtime.selfTest` also gets
  `{"type": "self-test"}`. It must answer in its direct reply with an `ok`
  result. A host call (network or anything else), another status or no
  answer refuses the release.
- The module gets no settings, secrets, network or playback. The check
  stops after 15 s, or within about 50 ms of a cancelled job.
- A refused release is not offered, so the active release stays. The
  review shows a fixed message, never text from the package.

Local developer installs (`installLocalPackage`) and the bundled
distribution packages are not checked here.

TVMaze 0.2.0 declares a self-test: inside the module it runs its show search
parsing on a built-in sample, with no host call.

## After an update: health period

When an install replaces an existing release (the store keeps the previous
one), the package is on trial until its first answered call. If its worker
crashes during the trial, the manager restores the previous release through
the guarded store rollback, instead of counting the crash toward quarantine.
The Extensions row shows "New version on trial", and `health()` reports
`observing`. The trial is held in memory; a restart ends it.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_candidate_check` (new, real worker) | profile | 5 cases pass: TVMaze self-test, a module that does not start, an unanswered self-test (refused in under 5 s), a data-only package, cancellation |
| `test_qt_addon_worker_health` | profile | New `anUpdateThatCrashesRestoresThePreviousRelease`: a crashing update is rolled back, stays enabled, no quarantine |
| `tst_AddonInstaller` | (qml) | New trial row text |
| TVMaze `cargo test` | — | 20 pass, including the self-test |

Not done: settings and private-data migrations with a staged data
generation (install step 6), a commit journal for an interrupted switch
(step 7), and a trial that lasts across restarts. Not run on Windows.
