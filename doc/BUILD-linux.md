# Building this fork on Linux (Meson)

> Status on CachyOS: configure, full default build,
> headless playback, native Wayland playback, and a staged
> install are validated (see "Validation" below). The Qt
> suite stands at 41 pass / 1 fail with one explicit
> remaining failure; no live installation has been
> performed. For portable release packaging with the media library, see
> [RELEASING.md](RELEASING.md). The source build below disables the media library,
> so expect that feature difference from release packages. Check the
> [Releases page](https://github.com/dsojevic/juxtopposed-vlc/releases) for available builds.

This guide uses **Meson** and the current build definitions
(root `meson.build` requires Meson `>= 1.10.0`). Note that
upstream itself prints "The Meson build system of VLC is
currently EXPERIMENTAL and INCOMPLETE", so this is the
practical path, not a claim of official support. The legacy
`INSTALL` file describes a different (autotools) flow and is
not these instructions.

## Prerequisites (Arch / CachyOS)

Starting point for toolchain, Qt 6, and the media stack:

```sh
sudo pacman -S --needed base-devel git meson ninja cmake pkgconf python \
    flex bison nasm \
    qt6-base qt6-declarative qt6-svg qt6-shadertools qt6-wayland \
    ffmpeg alsa-lib libpulse pipewire wayland wayland-protocols \
    libxkbcommon xcb-util-keysyms mesa lua gnutls libgcrypt \
    libxml2 sqlite libpng zvbi vulkan-headers
```

This list is a starting point, not a tested minimal
fresh-machine closure. Option handling falls into two kinds:

- `auto` (the majority): a missing dependency silently
  disables that module.
- `enabled` (must be present or explicitly disabled):
  `avcodec`, `avformat`, `swscale`, `png`, `zvbi`, `lua`,
  `screen`. Distribution `ffmpeg` covers the
  `libav*-based` ones; `libpng` and `zvbi` are listed above
  for the `png` and `zvbi` options.

Further notes:

- `vulkan-headers` is required, not just the loader.
  `vulkan-icd-loader` ships `vulkan.pc`, so configure
  detects Vulkan and enables the `placebo_vk` module — but
  compiling it needs `vulkan/vulkan.h` from the separate
  `vulkan-headers` package (first build attempt stopped at
  `video_output/vulkan/platform.c` without it). Alternative:
  `-Dvulkan=disabled` (`feature`, default `auto`).
- `nasm` is optional (`required: false` in `meson.build`);
  without it you lose x86 assembly acceleration paths but
  the build succeeds.
- `qt6-wayland` is optional, and build-time versus run-time
  needs differ. The Wayland compositor sources compile on
  the QtGui private headers plus the `wayland`/`xcb` pieces;
  actually *running* on Wayland additionally needs the Qt
  Wayland platform plugin from that package. Without it the
  build still succeeds and uses XCB when available (see
  below).
- The Qt interface needs Qt 6 `>= 6.2` (`Core`, `Gui`,
  `Widgets`, `Qml`, `Network`, `Quick`, `QuickControls2`,
  including private headers). `-Dqt=enabled` turns a missing
  or incomplete Qt into a configure error instead of a
  silently skipped UI.
- `medialibrary` needs an external `medialibrary >= 0.13`
  via pkg-config. It is not installed by this baseline and
  not in the package list above, so the baseline disables it
  explicitly; provide a compatible pkg-config package to
  enable it. Expect reduced library functionality without it
  (history, media-library views).

Other distributions are unverified; install the equivalents
of the packages above.

## Portable add-on runtime

Qt builds include `vlc-addon-worker`. This program runs executable Wasm add-ons
in a separate process. Use Rust 1.97 or later and Cargo to build the worker.
The `-Drust` option controls experimental native VLC modules. It does not
control this worker.

The worker build uses an installed `stable` Rust toolchain. Set
`RUSTUP_TOOLCHAIN` to use a different installed toolchain. The build does not
use the nightly toolchain from `rust-toolchain.toml`. The build does not
install toolchains or targets. Users use the worker program from the
application package without Rust or Cargo.

Cargo uses the versions in `Cargo.lock` and the `--locked --release` options.
The install step puts the worker next to VLC. The build-tree UI uses the
configured worker path. The add-on manager does not run Cargo or repository
build scripts during an add-on install.

For HTTPS Git installs, install a Git program on `PATH`. If Git is missing, the manager
shows an error. TheSubtitleDB reference package installs at
`share/vlc/addons/reference/thesubtitledb`. Each user must supply a separate API
key. The package does not contain a global TheSubtitleDB key.

Set `-Daddon-runtime=disabled` to omit the worker. Package checks, settings and
data-only UI resources operate without the worker. Executable providers cannot run
without the worker. The manager does not use Lua, QML or native libraries as a
worker replacement.

The current code has package installs, settings and subtitle providers.
[The add-on README](../contrib-addons/README.md) lists the bundled add-ons and
links to the SDK guides for add-on authors.

For a cross build, install the Rust target before you build VLC. Meson maps
Linux, Windows and macOS x86_64/aarch64 targets. Use
`-Daddon-runtime-target=<triple>` to set a different target. Use
`-Daddon-runtime-linker=<program>` to set a different linker. The build stops
if the target or linker is not available.

To set a free-space limit for the worker build, use these optional environment settings:

```sh
export VLC_ADDON_MIN_FREE_BYTES=7500000000
export VLC_ADDON_BUILD_RESERVE_BYTES=1500000000
```

The build starts only if the free space is at least the sum of these values.
The script does not remove files. The default values are zero, which omits this check.

Do these tests from a configured native build:

```sh
meson test -C build-linux test_qt_addon_packages test_qt_addon_runtime \
    test_qt_addon_metadata test_qt_addon_broker test_qt_subtitle_sources \
    test_qt_addon_ui_registry test_qt_addon_metadata_service \
    test_qt_addon_discovery test_qt_addon_theme test_qt_addon_sessions \
    test_qt_addon_audio_analysis test_src_misc_metadata_backend \
    --print-errorlogs
```

Meson supplies the worker to the runtime test. The QML tests include
`tst_AddonUi`, `tst_AddonTheme`, `tst_AddonIntegration`, `tst_AddonInstaller` and
`tst_AddonTrackPreparation`, `tst_AddonThemeSelection` and `tst_AddonThemeHost`.
Also do the full Qt tests and the release checks
for each target platform.

The standalone CMake tests accept `-DVLC_BUILD_DIR=<configured native build>`.
This setting adds the native metadata service tests, C bridge tests and provider probe.
The C bridge tests use the real core library. Build that library before the CMake tests.

## Configure and build

Always use a **separate build directory** and a **private
install prefix** (for example under your home directory).
Never install this development tree over the system VLC 3.x
libraries or plugins.

From the parent of your checkout, so the build directory
ends up beside (not inside) the source tree:

```sh
git clone https://github.com/dsojevic/juxtopposed-vlc.git
meson setup build-linux juxtopposed-vlc \
    --buildtype=debugoptimized --wrap-mode=nofallback \
    --prefix="$HOME/.local/vlc-dev" \
    -Dqt=enabled \
    -Dmedialibrary=disabled
meson compile -C build-linux -j2
```

`-j2` caps build parallelism for small machines (suggested
for a 7.4 GiB host); raise it to suit your own hardware,
never unlimited on memory-constrained hosts. `meson
configure build-linux` lists detected option values after
setup.

## Running from the build tree

Use the **`vlc-static`** binary, not `bin/vlc`: only the
`vlc-static` target is compiled with `TOP_BUILDDIR` /
`TOP_SRCDIR`, which point the plugin, data, library and
libexec paths at the build tree (`bin/vlc.c`,
`bin/meson.build`). The name marks it as the build-tree
launcher, not a fully statically linked executable.

Isolate test runs from your everyday VLC configuration with
a throwaway XDG root (VLC maps `XDG_CONFIG_HOME` /
`XDG_DATA_HOME` / `XDG_CACHE_HOME` to `<root>/vlc`, see
`src/posix/dirs.c`). The `env KEY=value` prefix works in
both POSIX `sh` and `fish`:

```sh
test_root="$(mktemp -d)"
mkdir -p "$test_root/config" "$test_root/data" "$test_root/cache"
env XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    ./build-linux/bin/vlc-static --version
```

(`fish` users: `set test_root (mktemp -d)` for the first
line; the `env`-prefixed invocations are unchanged.) Use the
same XDG wrapping for every later run, replacing
`--version` with your actual arguments. The build-tree
`--version` exits 0 and reports VLC 4.0.0-dev.

A reproducible headless playback check (validated flags;
generate the sample with `ffmpeg`, e.g. a couple of seconds
of FFV1 video plus PCM audio in Matroska):

```sh
env XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    ./build-linux/bin/vlc-static --ignore-config --no-media-library \
        --no-metadata-network-access --intf=dummy --vout=dummy \
        --aout=dummy --play-and-exit -vv sample.mkv
```

This exits 0 with the avcodec video decoder and the araw
audio decoder engaged through end-of-stream. No provider
streams have been tested.

Do not set `VLC_PLUGIN_PATH` to the system VLC 3.x plugin
directory: this is a VLC 4 (`libvlc` ABI 12.0.0) build and
must only load its own build-tree plugins. Close other VLC
instances before testing.

## Wayland: audio plays but video is black

[Issue #30](https://github.com/dsojevic/juxtopposed-vlc/issues/30)
reports black video with Intel `iris` on Mesa 26.2.5 / Hyprland,
including with software decoding. The reported working workaround is
`--vout=placebo`; it requires the Vulkan/libplacebo modules. On the
reported hybrid machine Vulkan selects NVIDIA while VAAPI decodes on
Intel, so this workaround includes frame copies between the GPUs.

To investigate, capture the **full** verbose log from a fresh process.
Use the throwaway XDG directories above and an ordinary local video file:

```sh
env XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    MESA_DEBUG=context EGL_LOG_LEVEL=debug \
    ./build-linux/bin/vlc-static --ignore-config --no-media-library \
        --no-qt-privacy-ask --intf=qt --vout=gl,none \
        --avcodec-hw=none -vv /path/to/video.mp4 >gl-software.log 2>&1
```

`--avcodec-hw=none` explicitly disables hardware decoding. `--vout=gl,none`
keeps the test on GL and disables fallback to another video output.
Repeat without `--avcodec-hw=none` to compare the hardware-decoding path.
Record which decoder, interop, EGL driver and video-output modules each
run actually selects.

The EGL log now reports the selected configuration's RGB/alpha sizes,
the first successful video buffer submission, and context/swap failures
with EGL error codes. A successful submission confirms that EGL accepted
the swap; it does not confirm that the compositor displayed the buffer.
Frame preparation and drawing failures are also reported, with repeated
messages suppressed until recovery. Include the display/GPU topology and
whether each run shows video with the logs.

`--glinterop=none` makes the GL output fail initialization and can select
`wl_shm` instead. Use the selected-module lines when interpreting that
result.

Local pixel checks with Mesa 23.2.1 llvmpipe and Mesa 26.2.5 softpipe,
including libplacebo 7.360.1 and an optimized GL build, render correctly.
They do not exercise Intel `iris`, cross-GPU DMA-BUF presentation, or the
reported Hyprland/Qt 6.12 combination. The hardware-specific black screen
therefore remains unconfirmed locally; the diagnostics and failure-path
fixes do not establish its root cause.

## Optional: install to a user prefix (staged only, live install unperformed)

The build definitions do install (per-target `install: true`
with user-prefix-relative destinations), and a staged
install into a scratch `DESTDIR` has been measured to
succeed: the installation payload was staged under
`DESTDIR` while the live prefix remained absent. (Meson
updates its normal build-directory install logs, and no
strace audit was done, so this is not a claim of zero
filesystem writes anywhere else.) Live prefix installation
was not part of validation; the tested paths are the
build-tree `vlc-static` runner above and the staged runner
below.

Measured staged-install facts (source reasoning confirmed,
not just predicted): the tree sets no RPATH/RUNPATH and
ships no install hooks — staged `bin/vlc` lists
`libvlc.so.12` / `libvlccore.so.9` as NEEDED with no
RPATH/RUNPATH, and runs without `LD_LIBRARY_PATH` fail
with missing `libvlc.so.12`. Launch per-process (never a
global export, never `ld.so.conf` changes) with both the
prefix library directory and its `vlc` subdirectory,
conservatively — the Qt plugins resolve with the first
path alone, with no claim about which specific libraries
require the second:

```sh
env LD_LIBRARY_PATH="<prefix>/<libdir>:<prefix>/<libdir>/vlc" \
    XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    "<prefix>/bin/vlc" --version
```

Find your actual libdir first — do not assume `lib` vs
`lib64` or multi-arch triplets:

```sh
meson configure build-linux | grep -E '^[[:space:]]*(libdir|prefix)[[:space:]]'
```

With those two loader paths, staged `--version` exits 0,
the same synthetic headless playback exits 0, and all five
staged Qt plugins show no missing dependencies under
`ldd` (`lib`-only also covered the Qt plugins, but other
outputs may need `lib/vlc`, so both paths are given
conservatively). The non-destructive staged form used was
`meson install -C build-linux --no-rebuild --destdir
<scratch-dir> --quiet`.

## Wayland notes

Recent `master` already contains the Linux fixes reported in
issue #4 (Wayland EGL setup, PipeWire device selection,
video embedding when Wayland integration is unavailable, and
the `libvlccore` export the Qt plugin links against).
Do not carry the old standalone patches by default.

Native Wayland playback is validated (see "Validation"):
the Qt interface opens its own Wayland window (not
XWayland) and plays video to end-of-stream. If video still
opens in a separate window on your compositor, the optional
fallback is the XCB platform with in-window video
(`embedded-video` defaults to true), still under XDG
isolation:

```sh
env QT_QPA_PLATFORM=xcb \
    XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    ./build-linux/bin/vlc-static --embedded-video
```

## Tests

`meson compile` does not build test executables (they are
`build_by_default: false`), while plain `meson test`
silently rebuilds prerequisites with an uncapped Ninja. So
build the test targets with the job cap first, then run with
`--no-rebuild`. Start with a small smoke subset:

```sh
meson compile -C build-linux -j2 base_model_test test_qt_track_folder_rules
env QT_QPA_PLATFORM=offscreen \
    XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    meson test -C build-linux --no-rebuild \
        base_model_test test_qt_track_folder_rules
```

Offscreen plus throwaway XDG dirs keeps this headless and
isolated; offscreen alone is not proof of either.

For the full Qt suite (`VLC:qt`, 42 registrations), build
all test prerequisites capped first — this builds every
test's prerequisites, not just Qt's, so it is the larger
build — then run with `--no-rebuild`:

```sh
ninja -C build-linux -j2 meson-test-prereq
env QT_QPA_PLATFORM=offscreen \
    XDG_CONFIG_HOME="$test_root/config" \
    XDG_DATA_HOME="$test_root/data" \
    XDG_CACHE_HOME="$test_root/cache" \
    meson test -C build-linux --no-rebuild --suite VLC:qt --num-processes 2
```

## Validation

Environment: CachyOS x86_64 (Hyprland), Qt 6.12.0,
GCC 16.2.1, Meson 1.12.1. Configure flags:
`--buildtype=debugoptimized --wrap-mode=nofallback
-Dqt=enabled -Dmedialibrary=disabled -Dtests=auto`
(800 targets, 164 tests registered, 42 Qt registrations,
both media-library tests correctly omitted).

| Check | Result |
| --- | --- |
| Full default build | Pass |
| Build-tree `--version` | Exit 0, VLC 4.0.0-dev |
| Headless playback (2 s FFV1 + PCM Matroska sample, dummy intf/vout/aout, `--play-and-exit`) | Exit 0, avcodec + araw decoders to end-of-stream |
| Same sample from staged install (per-process `LD_LIBRARY_PATH`) | Exit 0 |
| Staged `meson install --destdir` | Succeeds, payload staged under `DESTDIR` with live prefix absent; staged `bin/vlc` has no RPATH/RUNPATH and needs the loader paths |
| Native Wayland Qt playback (`--intf=qt`, dummy audio, silent 8 s FFV1 clip, `--play-and-exit`) | Exit 0, own Wayland window (not XWayland), `qt_wayland` + `egl_wl` in logs, end-of-stream |
| Qt suite (`VLC:qt`, 42) | 41 pass / 1 fail (see below) |

The GUI check used a first-run flag plus throwaway state,
e.g. `--no-qt-privacy-ask` with isolated XDG directories.
Silent video only: not an HDR, hardware-decode, or real
audio-hardware test, and no provider streams or media
library were exercised. The headless check likewise tests
decoding, not PipeWire or speaker output.

Remaining failure: `tst_RedesignLibrary ::
JuxtopposedLibrary :: test_breadcrumbsKeepHiddenAncestorsAvailable`
(`tst_RedesignLibrary.qml:426`, `menu.opened` false instead
of true); 51 other cases in that file pass, and the failure
reproduces in a sequential rerun with fresh isolated state.
Production and test QML sources are unmodified by this
work. Root cause is not confirmed, so no claim is made
about flakiness, environment, or production code — the
failure is recorded as-is and the assertion is left
untouched. A Meson test-registration fix for the subtitle
scanner test (plugin path plus module dependencies) is
included in this contribution; no production UI or codec
code is changed, and the baseline is therefore not fully
green.
