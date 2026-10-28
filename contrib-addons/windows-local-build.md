# Local Windows add-on build

The local Windows x86_64 test build is in `build-windows/vlc-win64`.
The current checkpoint includes the Windows add-on loader repair. Its actual
Windows import probe passes, but the full app startup check fails. The user
also reported a hang when opening Extensions. This folder is not yet a
verified repair build. See [the loader repair](windows-addon-loader-repair.md).

`BUILD-INFO.json` records app and Core source
`600d3158b0f61de44be3fdb755a2dda129263e94`, with a clean source snapshot.
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
- The build export retains the existing portable profile.
- A disk guard stopped the first packaging attempt. The completed Qt module
  had accumulated about 1.7 GB of debug data. Removing that debug data kept
  its code section unchanged and allowed packaging to finish. The free-space
  floor stayed above 7.5 GB. The resumed build kept at least 9,278,574,592
  bytes free. The build helper now passes the debug-strip link option directly.

The existing repository job suite also passes 11 normal and 11 focused
sanitizer cases on Linux after the cancellation TLS change.

The first full app probe displayed a window and closed normally. It did not
create the seven expected package states in its temporary profile, so the
startup check failed. That test also changed the existing portable Qt settings
file. Its profile-isolation claim is withdrawn. No settings were restored,
because the old file bytes were not retained and the user has since used the
app. Further GUI tests require a complete private runtime copy and a current
profile backup.

The user then opened the app and reported a hang on the Extensions tab.
Windows recorded an application-hang event. The process was no longer
available when inspected; no user process was closed by the diagnostic work.
Theme cache construction and repeated synchronous package reads are under
review. Neither is confirmed as the cause of that reported hang. A comparison
test of the old theme cache key also passes with the installed Qt version.

Full Windows UI, rendering, media-library runtime, account, Git, and shutdown
conformance remain open. The complete architecture backlog remains active.

## Build fixes

The build includes the reference-package export, Qt Quick Test guards,
JavaScript resource prerequisites, one repository cancellation TLS definition,
and separate convenience-archive linker flags. The Windows cache also needed
full build-file regeneration before it could compile the added native sources.
