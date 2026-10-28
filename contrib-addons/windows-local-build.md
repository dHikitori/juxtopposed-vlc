# Local Windows add-on build

The local Windows x86_64 test build is in `build-windows/vlc-win64`.
The current checkpoint includes the Windows add-on loader repair, background
package summaries and the revised General and Extensions settings screens.
The private full-app startup check loads all seven bundled add-ons and closes
normally. The actual Extensions click still needs a user retest. See
[the loader repair](windows-addon-loader-repair.md) and
[the background list](package-summary-ui.md).

`BUILD-INFO.json` records app and Core source
`2a689c3a11f1cbe60410253312e2783ebd797335`, with a clean source snapshot.
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
- The current complete build kept at least 9,232,093,184 bytes free. The helper
  passes the debug-strip link option directly to avoid large debug DLL copies.
- The actual private Windows app creates all seven enabled distribution
  installs. TheSubtitleDB has its protected required key field and no saved
  key in that new profile. The test does not enter an account key.
- Debug logs confirm that the Qt module, settings, configuration directory
  and add-on store all use the complete private runtime copy.
- After startup, the owned window passes five consecutive responsiveness
  samples. It accepts a close request and exits with code zero. No forced
  termination is needed.
- The current portable profile has a byte backup. Its file names and bytes
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

## Build fixes

The build includes the reference-package export, Qt Quick Test guards,
JavaScript resource prerequisites, one repository cancellation TLS definition,
and separate convenience-archive linker flags. Generated QML resource rules
now compare the current file list on each Make parse. This repairs a stale
include after Makefile regeneration while keeping unchanged outputs stable.
