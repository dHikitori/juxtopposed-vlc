# Local Windows performance test build, 29 October 2026

The local build is ready at `build-windows/vlc-win64/vlc.exe`. Its app and Core
source is clean commit `11adca2a5211d88149418d056e124268d0435552`.
The dependency revision is `4ca4eb0c3e3d1181a48f1e216f4324c6e2670ef7`.
This is a local test build. No release, tag, installer, or upload was made.

## Included changes

- One shared, retained display catalogue supplies add-on UI reads.
- Startup package checks run as joined background work before the first
  display capture.
- Theme, visualizer, settings, extension UI, discovery, session, and subtitle
  display consumers use that catalogue. Explicit actions retain their fresh
  authority checks.
- The HTTP transport and artwork cache scan wait for actual work. Inactive
  visualizers release their native player and audio observers.
- The earlier General and Extensions layout changes, playback-stop ordering,
  and remote-file read cancellation are included.

## Verification

The Windows build completed in 478.11 seconds with one compiler job. All 868
published file hashes and modes match the verified candidate. All 416 Windows
executables and libraries pass the x86_64 and DLL dependency checks. All seven
reference add-ons and their 35 declared payloads match their source bytes.

The packaged worker loads the packaged TheSubtitleDB module on Windows. With
no key it returns `auth_required`, makes zero host calls, writes no stderr,
and exits with code zero. The reused worker also retains its matching source
hashes and earlier native unit and ABI proofs.

The matching shared host passes 551 normal and 551 ASAN/UBSAN checks across
18 suites, with no warnings or skips. Related native checks pass 103 cases.
The QML checks pass 302 cases; two exact, previously reproduced Qt animation
warnings remain recorded. The actual MainUI object also compiles against the
matching headers. See [the performance contract](../performance-contract.md).

These checks do not measure full app responsiveness. GUI automation could
not initialize because its tool rejected this WSL workspace URI. No GUI app
was launched by that attempted check. The actual Extensions click, network
audio Close, and full app performance comparison still need a retest.

## Publication and evidence

Publication changed package-owned paths only. Unchanged files share inodes;
changed files use a hardlink followed by atomic replacement. This kept the
build within the space guard. Portable contents were not read, and the
existing portable directory identity stayed unchanged. Unrelated local files
were preserved. The earlier public `fd3` build is retained at
`build-windows/vlc-win64-fd3-baseline`; the pre-add-on build remains at
`build-windows/hakone-win64`. No Docker cache files were deleted.

- Build: `/tmp/hakone-windows-idle-build/proof.json`.
- Package checks: `/tmp/hakone-windows-idle-build/validation/`.
- Publication: `/tmp/hakone-windows-idle-build/publication-proof.json`.
- Shared sanitizer report:
  `/tmp/hakone-addon-display-host-proof/san-current-terminal-report.json`.

Published SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `BUILD-INFO.json` | `e8ceccdc78c11eb6ca6eaa0ebd4b4a1d44d967962712d8aa70e2ee6260cce4cd` |
| `vlc.exe` | `5876bc3e8a48bf8135f60be76b5d0f984313a0ecaf11ea27e5c0420df37a959e` |
| `plugins/gui/libqt_plugin.dll` | `8806bc1d3a8346aa79c631bb74c7a6c0217527a43a1dba4252fb980f1f88a024` |
