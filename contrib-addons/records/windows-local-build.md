# Local Windows add-on build

The latest local build is in `build-windows/vlc-win64`, from clean source
`11adca2a52`. It includes the shared display catalogue and cached consumers.
See [the 29 October build record](windows-performance-build-2026-10-29.md)
for current verification and remaining manual checks. The sections below
retain the earlier evidence for their stated source commits.

## Earlier fd3 checkpoint

The previous Windows x86_64 test build was in `build-windows/vlc-win64`.
That checkpoint includes the Windows add-on loader repair, background
package summaries, revised General and Extensions settings screens, and the
playback-stop ordering fix and remote-file read cancellation. The current Windows
console check imports all seven bundled add-ons, reads the complete provider
snapshot, and checks the memory-only snapshot factory. The earlier private
GUI startup check applies to `2a689c3a11`. The actual Extensions click still needs
a user retest. See
[the loader repair](windows-addon-loader-repair.md) and
[the background list](package-summary-ui.md).

The user later reported a separate Close delay while an audio-only network file was playing.
The [playback-stop ordering fix](windows-close-ordering.md) passes 22 normal and
22 sanitizer cases and is included in this published build. The exact one-minute
blocker has no captured stack; the user's network-file Close still needs a retest.
The [remote-file read repair](windows-network-file-close.md) also passes its
Windows fixture and is included in the current package.

`BUILD-INFO.json` records app and Core source
`fd3fed417330f16bb413f6fda71e77c47250b8a6`, with a clean source snapshot.
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

The earlier build, package, and console checks below apply to `fd3fed4173`. GUI window and
startup observations below were collected from `2a689c3a11`; they are retained
as that earlier build's evidence.

- The complete Core, Qt interface, compiled UI resources, media modules, and
  launcher build and link.
- All 868 owned output files have recorded hashes. All 416 PE files pass the
  x86_64 and dependency inventory check.
- The fresh Windows console probe uses the current host archive. It imports all
  seven packages and captures seven packages and seven raw contributions in
  joined work. The snapshot has a current 32-byte digest. TheSubtitleDB requires
  its protected key and is unconfigured with no saved key in this temporary
  profile. This probe performs no GUI action or playback.
- The current Manager snapshot factory returns owned readers on its owner
  thread. Its joined read matches the raw catalogue digest and preferences.
  Credential presence is empty in the private profile, and no credential reader
  is called. This checks the current source and host archive.
- The network-file fixture passes 15 groups and 298 checks twice against the
  current published Core and LibVLC. All three deliberate race defects fail
  their expected assertions. This is a native read test; actual app Close still
  needs the user retest.
- The same worker passes all seven unit tests on Windows.
- The packaged worker loads the packaged TheSubtitleDB module on Windows.
  With no key, it returns `auth_required`, makes no host call, and exits cleanly.
- The build export retains the existing portable profile.
- The current complete build records a minimum of 8,459,743,232 bytes free. The helper
  passes the debug-strip link option directly to avoid large debug DLL copies.
- The earlier private Windows app creates all seven enabled distribution
  installs. TheSubtitleDB has its protected required key field and no saved
  key in that new profile. The test does not enter an account key.
- Debug logs confirm that the Qt module, settings, configuration directory
  and add-on store all use the complete private runtime copy.
- After startup, the owned window passes five consecutive responsiveness
  samples. It accepts a close request and exits with code zero. No forced
  termination is needed.
- That earlier portable profile has a byte backup. Its file names and bytes
  match after this private test. No profile restoration is performed.

The existing repository job suite also passes 11 normal and 11 focused
sanitizer cases on Linux after the cancellation TLS change.

The earlier `600d3158b0` full app probe displayed a window and closed normally. It did not
create the seven expected package states in its temporary profile, so the
startup check failed. That test also changed the existing portable Qt settings
file. Its profile-isolation claim is withdrawn. No settings were restored,
because the old file bytes were not retained and the user has since used the
app. The current test uses the complete private copy and a current byte backup.

The user then opened the app and reported a hang on the Extensions tab.
Windows recorded an application-hang event. The process was no longer
available when inspected; no user process was closed by the diagnostic work.
An actual Windows probe measured a synchronous package read at about 421 ms
on a local path and 1,598 ms on a WSL network path. The new screen uses joined
background reads and does not call the synchronous list API. Its UI checks
pass 149 cases; the reader passes 19 normal and 19 focused sanitizer cases.
The reported hang has no captured stack, so its exact cause is not confirmed.
A comparison test of the old theme cache key passes with the installed Qt;
the explicit QString key is a value boundary, not a proven hang repair.

The fresh private startup records one initial hung-window sample at about
16 seconds, followed by responsive samples from about 17 to 22 seconds.
First-start setup is therefore still a performance concern. This evidence
does not establish an instant or stall-free cold launch. The existing player
width-binding warning is also present. Neither issue is hidden by the passing
stable-startup check.

A second launch uses that same private runtime and installed profile. Its
first window appears at about 15.28 seconds and first responds at about
15.56 seconds. It then has 205 consecutive responses, no hung-window sample,
all seven matching IDs and normal exit. Its live log gate times out because
the final store-path line is not visible during polling. The raw failed
driver result is retained. Independent checks of the closed log confirm all
four private paths. Buffered output is a supported explanation for the live
observation failure, not an observed flush event.

Both runs disable the plug-in cache and use the software renderer. These
startup times do not benchmark an ordinary cached launch. The warm run uses
a bounded WM_NULL query; the first run uses Process.Responding, which can
block. Their first-response times are not a controlled speed comparison.

The exact startup evidence is in
`/tmp/hakone-windows-private-startup/evidence/run-91872fefb5c4418289effda8eb20e300`.
The second observation and its independent final-log check are in
`/tmp/hakone-windows-private-startup/evidence/warm-5df960dbffdd40ae812b9240c1e88aac`.
The Extensions click is not automated: the Computer Use runtime cannot start
from this WSL workspace. Production-control tests do not replace that check.

Full Windows UI, rendering, media-library runtime, account, Git, and shutdown
conformance remain open. The complete architecture backlog remains active.

The current complete build and publication records are in
`/tmp/hakone-windows-network-file-rebuild`. Publication checked for a running VLC
instance, then replaced only managed runtime files and metadata. All 868 owned
file hashes match the verified staging copy. It did not traverse portable
settings. The fresh Windows console evidence is in
`/tmp/hakone-win-network-file-catalogue-probe/final-proof.json`.
The current-Core read checks are in
`/tmp/hakone-win-network-file-current-core/final-proof.json`.
After all 868 published files matched, the extra staging copy was removed. Six
key PE files have verified lossless archives. Portable settings were not read
or traversed during publication or staging cleanup.

## Build fixes

The build includes the reference-package export, Qt Quick Test guards,
JavaScript resource prerequisites, one repository cancellation TLS definition,
and separate convenience-archive linker flags. Generated QML resource rules
now compare the current file list on each Make parse. This repairs a stale
include after Makefile regeneration while keeping unchanged outputs stable.
