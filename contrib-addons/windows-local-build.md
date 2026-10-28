# Local Windows add-on build

The local Windows x86_64 test build is in `build-windows/vlc-win64`.
Run `vlc.exe` from that folder. Keep the complete folder together.

`BUILD-INFO.json` records app and Core source
`c1cbc65816e73fe9d21abe4e82d6e385ae4c70d7`, with a clean source snapshot.
The build uses the existing dependency cache at
`4ca4eb0c3e3d1181a48f1e216f4324c6e2670ef7`. Its media library has all seven
current patches and the playlist transaction capability.

## Included add-ons and worker

The package has all seven declared reference add-ons. All 35 declared files
match their source bytes. This includes TheSubtitleDB, its key settings, and
its portable module. Windows packaging copies these files after the general
line-ending conversion.

The worker uses the current Hakone guest ABI. It was built with the existing
Rust 1.99.0 toolchain and its Windows GNU target. The build checks the worker,
source, manifest, lockfile, and build-script hashes before it stages that
worker into the app.

## Verification

- The complete Core, Qt interface, compiled UI resources, media modules, and
  launcher build and link.
- All 868 owned output files have recorded hashes. All 416 PE files pass the
  x86_64 and dependency inventory check.
- The same worker passes all seven unit tests on Windows.
- The packaged worker loads the packaged TheSubtitleDB module on Windows.
  With no key, it returns `auth_required`, makes no host call, and exits cleanly.
- All 544 existing portable profile files remain unchanged. No profile file
  was added or removed by the build.
- The build kept at least 8,005,607,424 bytes free. Completed build-output
  cleanup restored more than 9.7 GB of free space.

The existing repository job suite also passes 11 normal and 11 focused
sanitizer cases on Linux after the cancellation TLS change.

These checks cover a local test build. The app GUI was not launched. Full
Windows UI, rendering, media-library runtime, account, Git, and shutdown
conformance remain open. The complete add-on architecture backlog remains
active.

## Build fixes

The build includes the reference-package export, Qt Quick Test guards,
JavaScript resource prerequisites, one repository cancellation TLS definition,
and separate convenience-archive linker flags. The Windows cache also needed
full build-file regeneration before it could compile the added native sources.
