# Cutting a release

The release pipeline builds four native targets:

| Target | Download |
| --- | --- |
| Windows 10 (1809+) / 11, x86_64 | `hakone-<tag>-win64.zip` (portable `hakone-win64` folder with `hakone.exe`) |
| macOS Apple Silicon | `hakone-<tag>-macos-arm64.zip` (`Hakone.app`) |
| macOS Intel | `hakone-<tag>-macos-x86_64.zip` (`Hakone.app`) |
| Linux x86_64, glibc >= 2.35, GLIBCXX >= 3.4.30 | `hakone-<tag>-linux-x86_64.tar.gz` portable directory bundle |

Releases up to `4.0.0-beta.5` used the `vlc-juxtopposed-<tag>-...` names and a
`VLC Juxtopposed.app` bundle.

The Mac packages are separate native apps, rather than a universal binary.
Both include Qt, VLC plugins and their non-system dynamic libraries. Packaging
checks every Mach-O dependency and architecture, rejects links outside the app,
and signs the bundle ad-hoc. This supports execution on Apple Silicon but does
not provide a trusted publisher signature or notarization. All release targets
can be built without signing certificates, Apple accounts or signing secrets.
macOS ad-hoc signing uses the local `codesign --sign -` facility, with no timestamp
service; a missing Developer ID identity is not an error. Packaging signs nested
code before its containing bundle and verifies the completed app. Invalid ad-hoc
signatures still fail packaging because they can prevent execution on Apple
Silicon. Downloaded Mac apps remain subject to Gatekeeper's checks for unverified
publishers.

The maximum deployment version found across the included binaries becomes
`LSMinimumSystemVersion` and
is recorded in the build metadata. Homebrew dependencies determine that minimum;
a build made on a newer Mac can require a newer macOS than the CI build.
The Intel CI job permits Homebrew source builds because many current formulas
no longer provide Intel macOS bottles. Its first build can take substantially
longer than the ARM build.

Linux packages include Qt/QML and VLC's plugin dependency closure using
linuxdeploy. Host graphics drivers, glibc and desktop services remain system
requirements. `AppDir/AppRun` sets VLC's relocated resource/helper paths and runs
the Qt redesign. The archive requires no FUSE installation.
The bundle includes the Wayland EGL client buffer integrations as well as the
QPA platform, shell and decoration plugins. The platform plugins alone are not
sufficient for Qt Quick/OpenGL (see [issue 26](https://github.com/hakone-org/hakone/issues/26)).
Every ELF has a relative runtime search path. Each Qt executable has a relative
`qt.conf`; AppRun clears inherited library/Qt search overrides so these paths do
not affect browsers, file managers or other launched applications. The packager
audits the dependency closure without `LD_LIBRARY_PATH`, rejects unexpected host
dependencies and build paths, and verifies the glibc 2.35 baseline. The shipped
`LINUX-RUNTIME.json` records required plugins, host ABI libraries and ELF hashes.
Host graphics drivers and their EGL/GL/Vulkan dispatch libraries stay together;
the bundle does not mix an older Mesa or libdrm with a newer host GPU driver.
The host also supplies the Wayland client, cursor, EGL and server libraries.
Host EGL vendors load after Qt and can require newer Wayland symbols; bundling
the build image's older Wayland libraries can prevent Mesa from loading and
produce `EGL not available`, even when the Qt integration plugin is present.
The packager removes and rejects versioned backing files and unversioned aliases
of host libraries as well as their SONAME filenames.
The host also supplies `libstdc++` and `libgcc_s`, since newer graphics drivers
can require C++ symbols absent from an older bundled runtime. The required
`GLIBCXX` ABI is recorded as `minimum_glibcxx` in the runtime/build metadata;
the standard updated Ubuntu 22.04 runtime satisfies this build's requirement.
Linux verification covers Ubuntu 22.04, 24.04 and 26.04, including repeated
OpenGL video playback with automatic, `wayland` and `wayland-egl` platform selection
on the newer runtimes without system Qt. The current graphics stack is needed
to catch dependencies loaded dynamically by EGL that an ELF dependency audit
cannot see.

These checks apply the runtime-plugin, relocation and environment-isolation
advice in the [pkgforge portable-AppImage guide](https://github.com/pkgforge-dev/Anylinux-AppImages/blob/main/HOW-TO-MAKE-THESE.md).
This release remains a FUSE-free directory archive with a documented glibc/host
graphics requirement. It does not bundle libc or use sharun, and does not claim
support for musl, non-FHS systems or all Linux distributions. Moving to a fully
bundled libc/graphics runtime needs separate compatibility and driver testing.
The Linux build requires both X11 and Wayland backends. Its image provides
checksum-pinned Wayland scanner/protocol tools while retaining Ubuntu 22.04's
runtime libraries; missing backend prerequisites fail configuration.

All targets enable translations and use this fork's five media-library patches.
Windows builds the pinned contrib sources rather than using upstream prebuilt
libraries, which would omit those patches. Windows has no publisher signature.
Its portable ZIP includes imported MinGW runtime DLLs and the Vulkan loader,
with their license notices. Windows build metadata lists the external imports.
No signing credentials or OpenSubtitles app key are required.

The optional repository Actions secret `OPENSUBTITLES_API_KEY` is passed only to
the build steps for both macOS targets, Linux and Windows. Containers inherit it
by environment-variable name; the value is not included in command arguments.
The consumer-key generator writes a private build header without printing the
key, and Windows release builds disable shell tracing. GitHub masks registered
secret values in logs. The generated header is not uploaded as a separate
artifact or included in the source archive; the consumer key is embedded in the
distributed application and can be extracted from it.
Local builds do not receive GitHub repository secrets: supply the environment
variable or `OPENSUBTITLES_API_KEY_FILE` locally to include a key. Without a key,
the build still succeeds and users can configure their own key in settings.
Named assignment files and `.env` files must contain that exact variable.
The script rejects other user keys and does not print their values.
A raw key file can contain one value on one line. This value cannot be a named assignment.

## Review before pushing

Keep each suitable core feature available until its separate add-on replacement passes the tests.
Before core removal, move the feature into its own add-on. Do the replacement tests before core removal.
Do not remove a provider as part of a build change.
Record each feature move and its test results in the release notes.

Review [the 4.0.0-beta.5 notes](releases/4.0.0-beta.5.md), the packaging scripts in
`extras/package/release`, and `.github/workflows/beta-release.yml`. Keep release
notes in the published release format: platform summary and Git tag, full
changelog link, design attribution, changes since the previous release, usage
instructions and current limits. Assembly appends the final
`Package | Binary source commit | Download` table with the actual source commits
and `[download](#)` placeholders. Replace each `#` with the matching package's
download URL in the draft before publishing. Build run details and the packaging
commit remain in the workflow artifacts. Inspect the candidate release source
before creating a tag:

```sh
git fetch origin tag 4.0.0-beta.4
git log --oneline 4.0.0-beta.4..HEAD
git diff 4.0.0-beta.4..HEAD -- .github/workflows extras/package/release doc/RELEASING.md doc/releases README.md
```

Generated files go into ignored `build-release-*` and `contrib/contrib-*`
directories, plus the shared `contrib/tarballs` download cache. No script pushes
Git refs or publishes a release. Local builds can be reviewed before any push;
the workflow's optional final step creates a draft and may create its version tag.

Use **Run workflow** on the source branch with the default inputs (`all`, no
previous run ID, draft creation unchecked) to build and upload artifacts without
creating a GitHub release. This is available once the workflow is on the default
branch. Review the job logs and the `<version>-complete` artifact. The final job
requires both Mac builds, Linux relocation/playback checks, and native Windows
startup checks to pass, either in this run or in the original run when recovering.

If Apple Silicon never receives a runner and GitHub reports capacity constraints,
the application has not built or failed a test. Rerun the job when capacity is
available, or start a workflow with **macos_arm_runner** set to `macos-14` or
`macos-26` instead of the default `macos-15`. All three choices remain native
ARM64 builds with the same packaging and verification gates. A different runner
image can change the minimum supported macOS version; review the build metadata.
There is no automatic image switch or guarantee that another pool has capacity.

## Dependency caches

Release builds cache dependencies by default. Every run still compiles, packages
and verifies Hakone. On **Run workflow**, check **bypass_cache** to skip cache restore
and save operations and build the Linux environment without cached layers. The
first build for a new cache key remains a cold build; already-running jobs use
their original workflow and cannot populate these caches retrospectively.

Windows saves this fork's installed contrib libraries, installed native build
tools (including Qt's QML tools), and Vulkan license files. It does not save Hakone's
build tree, staged app, generated API-key header or dependency source/build trees.
The cache key includes the digest-pinned container image, target, fixed container
path, tracked contrib recipes/checksums/patches, build-tool sources and Windows
release configuration. Changing only Hakone's version or application code keeps the
dependency key. Compiled SDK caches require an exact key and a matching inventory
of file contents, permissions and symbolic links. An incomplete or modified SDK
is discarded and its dependencies rebuilt normally. Source archive downloads use
a separate cache; normal contrib checksum verification still applies.

The Windows dependency phase runs without the OpenSubtitles secret and saves its
cache before Hakone compilation. This preserves successfully built dependencies even
if later application packaging fails. The normal build then uses that validated
installed SDK; local builds without a cache key retain the original build path.

Linux uses BuildKit's GitHub Actions cache for the dependency image's intermediate
layers. Docker checks the Dockerfile, commands and base image when deciding which
layers remain usable. It pulls the base image before building; a changed base
invalidates dependent layers. The app and OpenSubtitles key are only supplied to a
subsequent container run, outside the cached image build. If the cached environment
build fails, the workflow retries without an external cache.

Each Mac target caches the pinned Qt SDK separately by runner, architecture, Qt
version, aqtinstall version and module selection. Restored SDKs must provide the
expected qmake version and ShaderTools configuration, otherwise they are replaced.
Homebrew's download cache uses a weekly key with fallback to older downloads;
Homebrew still validates downloads and installs the required packages. Installed
Homebrew Cellars are not restored, so Intel packages that require source builds
still take time to compile.

Cache restore/save failures are non-fatal, and unavailable caches fall back to
normal builds. GitHub may evict caches as storage fills or entries age. An exact
cache entry is immutable: to replace a damaged entry, delete it in the repository's
Actions cache settings (or bump its cache schema) before rerunning. **bypass_cache**
does not delete or replace existing entries. All release verification jobs remain
required regardless of cache hits. Use a normal full build with bypass enabled
when checking that a release can still be built from scratch.

## Recover failed targets

After fixing a release problem, you can rebuild one target or automatically
rebuild every failed target and reuse the verified packages from the original
full run:

1. Let the original run finish. The targets to reuse must have passed their
   verification jobs; Windows requires **windows-smoke**, not just a ZIP upload.
2. Commit and push the fix and this workflow to the branch you want to release.
3. Open **Actions → Build release → Run workflow** and select that branch.
4. Set **previous_run_id** to the original full run ID (for example,
   `39372878915`). Select **rebuild_target = failed** to rebuild targets whose
   verification failed or whose artifact is missing/expired. A successful Windows
   compilation with a failed **windows-smoke** check counts as a failed target.
   For that example run, this rebuilds **macos-x86_64 and win64**, reusing ARM Mac
   and Linux. To force a single target, select its name instead; every other
   target must already have passed its checks.
5. Optionally check **create_draft** to create the draft prerelease directly after
   recovery succeeds. Leave it unchecked to produce only the complete artifact.
6. Start the workflow. It verifies and copies the reusable packages into this
   run, builds and tests the selected targets, and assembles
   `4.0.0-beta.5-complete`. Inspect the downloads before publishing the draft.

Recovery requires the original run to use the same version and an ancestor of
the current commit. Only release tooling (including the Windows build wrapper),
release workflow, documentation and
release-script distribution-list changes may differ. Application, version or
dependency changes require a full build. Review the changed release tooling too:
if a fix is needed by several packages, rebuild all targets instead of reusing
packages affected by it. Expired or missing artifacts and failed verification
jobs stop single-target recovery when they affect another target; **failed**
selects those targets for rebuilding. The Windows-only launcher and plugin-loader
sources may differ when **win64 is rebuilt**; changes to shared application
sources still require a full build. A recovery run itself cannot serve
as the original full run for another recovery; keep using the original run ID.

The completed bundle includes `RELEASE_MANIFEST.json` with each package's source
commit, originating run, size, hash and reuse status. Existing package metadata
and embedded source commits are preserved. `SOURCE_COMMIT.txt` identifies the
current release packaging commit, not every binary's source: the release notes
and manifest list the sources individually. Metadata, manifests and dependency
audits remain in the workflow's `<version>-complete` artifact. The draft release
attaches only `SHA256SUMS.txt`, the four application archives and their individual
`.sha256` files. `SHA256SUMS.txt` covers only the application archives. No custom
source archives are generated; releases use GitHub's automatic source downloads.

**create_draft** is also available for full manual builds. It uses the checked-out
commit as the version tag's target if the tag does not exist. An existing tag must
already point to that exact commit; the workflow never moves tags or replaces an
existing release. Draft creation leaves publication to you. This avoids pushing
a tag yourself and starting another full build after a successful recovery.

### Collect builds after a reuse or aggregation failure

If the native builds pass but reuse/aggregation fails, let the existing run finish
and keep its artifacts. A new workflow can collect those completed builds without
compiling them again. Set:

| Input | Example |
| --- | --- |
| **previous_run_id** | `39372878915` (the original full run) |
| **rebuild_target** | `failed` |
| **completed_build_run_id** | `39413427739` (the run containing the rebuilt packages) |
| **create_draft** | Checked if a draft is wanted |

The planner obtains the already-verified packages from the original run and the
selected replacements from the completed build run. It starts no native build
jobs. The overall completed run may be red because reuse failed, but every
collected target must have a successful verification job and an available
artifact. Missing packages, failed checks or application changes since the
completed build stop collection; they never silently start another compilation.
The original full run remains the baseline, so a recovery run is not used as a
replacement baseline. Each artifact keeps its own run ID, source commit and hash
in the workflow artifact's `RELEASE_MANIFEST.json`.

The v4 download action nests artifacts downloaded by ID under their artifact
name unless `merge-multiple: true` is set. Reuse downloads explicitly use that
option so package metadata, archives and checksums are at the expected root.

GitHub's **Re-run failed jobs** uses the original commit. To pick up a committed
packaging fix, start a new recovery or collection run. The original failed run
remains failed.

## Create a release with a pushed tag

### Update metadata

Release assembly appends a hidden `vlc-update-metadata` JSON block to the GitHub
release notes and retains the same data in the workflow artifact's
`UPDATE_METADATA.json`. The updater reads the block from the existing releases
API response; it does not fetch the JSON file. Keep the block when editing the
published release notes. The app removes it from the displayed notes.

An optional `doc/releases/<version>.json` file supplies release display fields:

```json
{
  "product_name": "Hakone",
  "package_prefix": "hakone",
  "subtitle": "A short announcement for this release"
}
```

All three fields are optional. Without the file, the product name is `Hakone`, the
package prefix is `hakone`, and no subtitle is shown. Product names and
subtitles are plain single-line text, with limits of 128 and 512 UTF-16 code
units respectively. The prefix contains lowercase letters/numbers separated by
hyphens, up to 64 characters. It must agree with the packages actually built;
changing this display file does not rename build outputs. The subtitle appears
above the full release notes in the update window and notification pane.

Assembly adds `schema: 1`, the exact version tag, and the compiled update
generation automatically. These fields cannot be overridden by the JSON file.
`vlc_update_generation` in `meson.build` and `UPDATE_GENERATION` in `configure.ac`
must agree. Generation zero is the current value. Keep it unchanged for ordinary
releases. If a later release starts a new version sequence, increase both values
in its source before building. The updater compares generation first, then
normal semantic versions; never decrease or reuse a generation for an older
sequence. Stable builds continue to exclude prereleases across generations.

Releases with no metadata block are treated as generation zero with the
pre-rebrand `vlc-juxtopposed` package names, which is what releases published
before the metadata block used. Clients from `4.0.0-beta.5` on read the prefix
from the block, so they find the renamed `hakone-` packages. Invalid, duplicate, unsupported-schema or mismatched metadata
blocks make that release ineligible. Release-page and asset URLs must still
belong to this repository, and an uploaded package for the current platform and
architecture must exist. Metadata does not enable automatic installation.

The metadata reader first ships in `4.0.0-beta.5`; earlier clients need to install
that version to gain these capabilities. Keep the repository release location
stable so existing clients can continue checking it.

### Tagging

Before tagging, update `meson.build`'s project version and `configure.ac`'s
`AC_INIT` / `VERSION_*` fields together. Add `doc/releases/<version>.md`, then run:

```sh
python3 extras/package/release/version.py 4.0.0-beta.5
python3 -m unittest discover -s extras/package/release/tests
```

The validator rejects old `beta-N` tags and tags that disagree with the compiled
version. Use `4.0.0-beta.N`, `4.0.0-rc.N`, or `4.0.0` (stable). Published historical
`beta-1` through `beta-3` tags remain intact. Create the annotated tag only after
committing the reviewed release source:

```sh
git tag -a 4.0.0-beta.5 -m 'Hakone 4.0.0-beta.5'
git show 4.0.0-beta.5 --stat
```

Once the source/tag is acceptable, explicitly push the branch and tag:

```sh
git push origin HEAD
git push origin refs/tags/4.0.0-beta.5
```

A pushed `4.0.0-beta.5` tag runs the same builds and creates a **draft prerelease** named
`Hakone 4.0.0-beta.5`. It attaches only the four application
archives, their `.sha256` files and `SHA256SUMS.txt`. Build metadata stays in the
workflow artifacts; source downloads are provided automatically by GitHub.
The notes are taken from the tagged source and include its commit hash.
Inspect and publish the draft
manually only after downloading and trying the appropriate binaries.

With draft creation unchecked, the manual workflow run is a preflight build.
Pushing the tag starts a separate build of the tagged commit before creating the
draft; it does not promote the
manual run's artifacts. To reuse successful packages, use the recovery inputs
and **create_draft** above. Keep the candidate source unchanged between a successful
preflight and tagging, or repeat the preflight for the revised source.

Do not move a tag that has already been pushed. If a release source change is
needed after that point, increment the version and add new release notes. The
workflow derives artifact names and prerelease status from the validated version.
Stable tags create draft stable releases; prerelease tags create draft prereleases.

## Isolated add-on worker in release builds

Release builds include `vlc-addon-worker` next to VLC. The release also
contains the TheSubtitleDB reference package. The release setup script uses
`addon-rust-version.txt`. This file selects
[Rust 1.99.1](https://blog.rust-lang.org/releases/). Use Rust 1.97
or later for the worker. Cargo uses the versions in the committed lockfile.

The `setup-addon-rust.sh` script installs the Rust toolchain on the build
machine. The script downloads the official rustup installer. The script compares
the published checksum with the installer checksum before it starts the installer. An end-user add-on
install does not install Rust or run Cargo.

The native release script and macOS CI job install the selected toolchain.
The Linux dependency image includes this toolchain. The Windows build step
also installs `x86_64-pc-windows-gnu` and selects the MinGW linker.
The dependency-only cache step does not use the worker compiler.
Set `RUSTUP_TOOLCHAIN` to select a different toolchain. A normal developer build
uses an installed `stable` toolchain and does not install one automatically.
The worker does not use the experimental module's nightly setting.

The macOS package script copies the worker into `Contents/MacOS` before the
dependency checks and code signature. The Linux package script supplies the
worker to `linuxdeploy` for a dependency check. The Windows package script
examines the worker and its imported DLLs. The package step stops if the
worker is missing.

The release check starts the worker with the reference Wasm file. The check
supplies no API key. The add-on must return `auth_required`. The check rejects
host operations and stops after 20 seconds. The test uses no network request.
The tests for file paths and command order do not prove platform operation.
Do the release check on each target platform.

For HTTPS Git installs, install a Git program on `PATH`. TheSubtitleDB add-on uses
each user's own API key through protected settings. Do not put a TheSubtitleDB
user key in a CI secret, package artifact or worker environment. Data-only
resources and settings operate without the worker. Executable providers cannot
run without the worker.

## Local macOS build

Install the media prerequisites from [BUILD-macos.md](BUILD-macos.md), plus
`gettext` and `libgcrypt`. The release workflow installs Qt 6.8.3 separately, rather than
Homebrew's aggregate Qt package. To use the same SDK locally:

```sh
python3 -m venv build-release-tools
build-release-tools/bin/pip install aqtinstall==3.3.0
build-release-tools/bin/aqt install-qt mac desktop 6.8.3 clang_64 -O build-release-qt -m qtshadertools
export QT_ROOT_DIR="$PWD/build-release-qt/6.8.3/macos"
```

Use an Intel Mac for the Intel package or an Apple Silicon Mac for ARM. Optional
modules are detected from the installed libraries; inspect Meson's summary.

```sh
RELEASE_TAG=4.0.0-beta.5 JOBS=4 bash extras/package/release/build-native.sh
python3 extras/package/release/verify.py \
    'build-release-output/macos-arm64/Hakone.app/Contents/MacOS/hakone'
```

For an Intel build, replace `macos-arm64` with `macos-x86_64`. `QT_ROOT_DIR` can
select a Qt installation other than Homebrew. The script builds a private patched
media library and stages the installation before bundling; it does not install
Hakone into `/usr` or Homebrew. Remove the specific output app directory before
repackaging an existing build. To rebuild from clean dependency state, remove
the corresponding `build-release-macos-deps` directory too.

## Local Linux and Windows builds with Docker

Linux (x86_64 Docker host recommended; emulation on ARM is much slower):

```sh
docker build --platform linux/amd64 -t vlc-release-linux \
    -f extras/package/release/Dockerfile.linux .
docker run --rm --user root --platform linux/amd64 -v "$PWD:/src" -w /src \
    -e JOBS=4 -e RELEASE_TAG=4.0.0-beta.5 \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/src \
    vlc-release-linux \
    bash extras/package/release/build-native.sh
```

The dependency image rebuilds the matching Qt Quick library from verified
Qt sources to fix image-cache cleanup on VLC's interface thread. It keeps
the rest of the pinned Qt SDK and checks repeated application startup and
shutdown before installing the replacement library. This build step is cached
until its script, patch, test, or SDK inputs change.

Run the relocated `AppDir/AppRun` as a normal user, outside the build container.
For example, extract the tarball in a new directory and run `verify.py` against
that `AppRun`. The check needs Python and FFmpeg on the verification host.

Windows (the image includes MinGW and the cross-build toolchain):

```sh
docker run --rm --user root --platform linux/amd64 -v "$PWD:/src" -w /src \
    -e JOBS=4 -e RELEASE_TAG=4.0.0-beta.5 \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/src \
    registry.videolan.org/vlc-debian-win64-posix@sha256:8e83e800036f8f4a6da1031a6b80c2b5bad8337259c072ffa22398baaeaad880 \
    bash extras/package/release/build-windows.sh
```

Initial Windows contrib compilation is substantial. Preserve the ignored
`contrib/contrib-release-win64` build directory for repeat attempts. Check the
resulting ZIP on an actual Windows machine, including playback, the library,
settings and a path with spaces. The GUI targets the Windows versions supported
by [Qt 6.8](https://doc.qt.io/qt-6.8/supported-platforms.html#windows).
The release wrapper disables Wine execution of VLC configuration probes and
disables installer generation. This also permits cross-compilation under ARM
Docker emulation when Wine cannot run Windows programs. Native executable
validation remains a separate Windows CI job.
That job launches the same portable profile twice and requires both Qt startup
and audio decoding, covering first launch and cached graphics startup.
It first requires Qt, avcodec and the media library to appear in VLC's loadable
module list, so missing plugin dependencies fail before a GUI startup timeout.
The Windows loader permits dependencies shipped beside the executable as well
as Windows system DLLs, while excluding the working directory and `PATH`.

`verify.py` checks the installed version, required Qt/avcodec/media-library plugin
registration, and generated audio playback with VLC preferences ignored and
temporary XDG directories on Linux. `--gui` additionally checks Qt startup;
macOS Qt settings still use Qt's native preferences domain. `--gui --video`
also requires a Qt video window, video decoding and a working video output;
both Mac CI jobs and the Linux X11/Wayland checks use this mode. GUI checks start
twice using the same cache, to cover first launch and cached graphics startup.
The saved graphics backend must be applied before constructing any Qt Quick
windows. In particular, an Intel Mac runner can lack OpenGL 3.0 and use software
rendering on its first launch; its second launch must retain that fallback.
An aborted or timed-out verification prints the captured Hakone output before the
failure reason, including the GUI attempt number and signal. For a local check,
`--diagnostics-dir <directory>` also keeps each process's output in a separate
file. CI uses its normal build log and does not upload crash-report files.
Linux requires a working Qt Quick OpenGL context on both X11 and native Wayland,
using Mesa's software GPU driver in CI. This exercises EGL and the normal RHI
rendering path; `QT_QUICK_BACKEND=software` is a separate fallback check. The
verifier rejects graphics-context and missing-buffer-integration errors even
when Hakone exits with status zero. The runtime audit also runs after extraction
and relocation, without the build SDK's library paths. A required disposable
Ubuntu 24.04 runtime test repeats Wayland/OpenGL, X11/OpenGL and software playback
without system Qt or a build SDK. It uses the identical read-only package and
also checks compatibility with newer Mesa/LLVM/C++ libraries. This is a
smoke check, not a complete codec, GUI, GPU, HDR or hardware-device certification.
Mac packaging also verifies code signatures and all bundled dependency paths.

## Dependency sources

The repository source includes the contrib recipes, patches, source URLs
and SHA-512 sums. Retain build logs and exact dependency versions with release
artifacts. For dependency modifications, distribute the corresponding patched
source as required by each dependency's license; do not substitute an unrecorded
local library build. Linux deployment-tool SHA-256 digests are pinned in
`fetch-linux-tools.py` and verified before extraction, including cached downloads.
Extracted tool trees are recreated from those verified archives. Review the
upstream release asset and update its pin when adopting a newer tool; a changed
continuous-release download fails closed until then. Tool hashes are also
recorded alongside the artifact. VLC and design/font attribution remain in
`README.md` and the bundle.

## Update discovery and future installation

The Qt interface checks this fork's public GitHub Releases API. Automatic checks
are enabled by default in update-enabled builds and run at startup when the last
check date is at least the configured number of days ago (default: one day).
Existing `qt-updates-days` settings are preserved. Background failures are quiet;
manual checks display errors. There is no GitHub token embedded in the app.

The checker compares full semantic versions, ignores build metadata for ordering,
and treats a stable release as newer than its prereleases. Stable builds accept
only stable releases; prerelease builds accept both prereleases and stable releases
that compare higher. It interprets old `beta-N` tags as `4.0.0-beta.N`. Release
flags must agree with the tag. Drafts and unparseable tags are ignored.

Release selection filters platform packages before selecting the highest version;
publication order and GitHub's latest-release badge do not determine precedence.
An asset must be uploaded, nonempty, and named exactly:

- `<prefix>-<tag>-win64.zip` (Windows x86_64)
- `<prefix>-<tag>-win32.zip` (Windows x86)
- `<prefix>-<tag>-winarm64.zip` (Windows ARM64)
- `<prefix>-<tag>-macos-<arm64|x86_64>.zip` (macOS native)
- `<prefix>-<tag>-macos-universal.zip` (either supported macOS architecture)
- `<prefix>-<tag>-linux-<x86_64|arm64>.tar.gz` (Linux portable directory)

`<prefix>` is the release's metadata `package_prefix` (`hakone` by default), or
`vlc-juxtopposed` for a release without a metadata block.

The current workflow produces Windows x86_64, both native macOS packages, and
Linux x86_64. Recognizing other target names does not claim CI builds for them.
Source archives, SDKs, symbols and checksums are not eligible packages. Architecture
matching follows the running binary; automatic migration from emulated to native
builds is not enabled. Minimum OS/glibc requirements remain in release notes and
build metadata; filename matching does not certify all operating system versions.

The primary action opens the selected release's page in the system browser. The
model also retains the exact selected asset URL, size and digest for later use.
Release packaging records asset names, sizes, SHA-256 hashes, versions and source
commits in build metadata. These hashes are integrity metadata, not authenticated
publisher signatures. A future automatic installer must define authenticated
metadata/signing, platform installation, portable-profile handling and rollback.

The core `update.c` / `update_crypto.c` signed download backend is retained and is
built with `update-check` when libgcrypt is available. GitHub notifications need
only Qt Network and do not pass unsigned GitHub assets into that backend. The
release pipeline enables update checks explicitly. Other packagers can enable
`-Dupdate-check=enabled` (Meson) or `--enable-update-check` (Autoconf). The separate
native macOS Sparkle interface is unchanged; these release packages use Qt.

Existing installations need a manual upgrade to acquire this GitHub checker.
