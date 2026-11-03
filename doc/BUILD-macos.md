# Building this fork on macOS (Meson)

These steps build the redesigned Qt interface from source on macOS with
Homebrew dependencies and run it from the build tree. They were tested on
Apple Silicon (arm64) with macOS 27, the Xcode Command Line Tools (full
Xcode is not needed), Homebrew Qt 6.12.0, FFmpeg 8.0 and Meson 1.12.1.

This is a development build, not a distributable `VLC.app`: it uses the
Homebrew libraries on the machine that built it. On macOS the redesign is the
Qt interface (`-I qt`), not the Cocoa `macosx` interface.

For bundled release builds with Qt and non-system dependencies included, see
[RELEASING.md](RELEASING.md).

## Prerequisites

Install the Command Line Tools (`xcode-select --install`) and
[Homebrew](https://brew.sh), then:

```sh
brew install meson ninja cmake pkg-config python bison flex \
    qt ffmpeg lua gnutls libarchive libpng freetype harfbuzz fribidi \
    libass dav1d opus libvorbis flac mpg123 x264 \
    libmatroska libebml taglib libdvbpsi
```

Most dependencies are `auto` options: if one is missing, that module is
disabled and the build still succeeds.

- **Bison 3.0 or later is required** for the WebVTT and JSON parsers.
  macOS ships Bison 2.3 in `/usr/bin`; Homebrew's `bison` and `flex` are
  keg-only, so put them first on `PATH` (shown below). With only the system
  Bison, those modules are skipped at configure time.
- The Qt interface needs Qt 6 `>= 6.2` with private headers. Homebrew's `qt`
  provides this; `-Dqt=enabled` makes a missing Qt a configure error.
- `medialibrary` is not packaged by Homebrew, so the media library is
  disabled. Playback and the player UI work. The library sidebar does not
  list local media.

## Portable add-on worker

Qt builds include the add-on worker. Install Rust 1.97 or later. Install Cargo
before you build VLC. The worker build uses an installed `stable` toolchain.
Set `RUSTUP_TOOLCHAIN` to use a different installed toolchain. The build does
not use the experimental module's nightly toolchain. The build does not
install toolchains or targets. The `-Drust` option does not control this
worker. Users use the worker program without a compiler.

The worker installs next to VLC. TheSubtitleDB reference package installs
at `share/vlc/addons/reference/thesubtitledb`. Each user must supply a separate
API key in the protected settings form. The host uses Keychain for credentials.
Do not put user keys in source files or package files. For HTTPS Git installs,
install a Git program on `PATH`.

Set `-Daddon-runtime=disabled` to omit executable add-ons.
Package checks, settings and data-only UI resources operate without the worker.
Executable providers cannot run without the worker. See
[the Linux runtime section](BUILD-linux.md#portable-add-on-runtime) for target
options and test commands. The old macOS build results at the top of this
guide do not cover the new worker. Do the macOS release checks for this worker.

## Configure and build

```sh
export PATH="$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$(brew --prefix qt)/bin:$(brew --prefix qt)/share/qt/libexec:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix qt)/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig"

git clone https://github.com/hakone-org/hakone.git
cd hakone
meson setup build --prefix="$HOME/.local/vlc-dev" \
    -Dqt=enabled -Dtests=disabled -Dbuildtype=release
ninja -C build
```

`-Dtests=disabled` is currently needed because the test registration
requires the `medialibrary` plugin, which is not built here.

Set `--prefix` during `meson setup`, not `meson install`. For an existing
build, change it with `meson configure build --prefix=...` before installing.
Keep a private prefix: Homebrew's Meson otherwise defaults to the Homebrew
tree (`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel).

## Running from the build tree

Use `build/bin/vlc-static`. It sets the plugin, library and `vlc-preparser`
paths to the build tree itself. `build/bin/vlc` looks in the install prefix
and exits with `VLC cannot start any interface` when nothing is installed
there.

```sh
build/bin/vlc-static -I qt /path/to/video.mp4
```

A quick headless check that decoding works:

```sh
build/bin/vlc-static -I dummy --no-video --aout=dummy --play-and-exit /path/to/video.mp4
```

Use `--no-video` for headless runs: `--vout=dummy` with the dummy interface
falls back to the `minimal_macosx` window module, which can crash in AppKit.

On Apple Silicon, H.264/HEVC playback uses VideoToolbox hardware decoding,
the `samplebufferdisplay` video output and the `auhal` audio output.
