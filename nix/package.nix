# SPDX-License-Identifier: GPL-2.0-or-later
# Hakone built from this source tree. flake.nix calls this file.
#
# The meson options follow extras/package/release/build-native.sh (Linux),
# except that the update check is off: Nix, not the app, updates Nix installs.
{
  lib,
  stdenv,
  src,
  # The unfiltered source tree, for files read while evaluating.
  flakeRoot,
  version,
  medialibrary,
  rustToolchain,
  rustPlatform,
  symlinkJoin,
  runCommand,
  makeWrapper,

  # Build tools
  meson,
  ninja,
  pkg-config,
  python3,
  flex,
  bison,
  gettext,
  lua5_4,
  wayland-scanner,
  spirv-tools,
  patchelf,
  qt6,

  # Libraries
  alsa-lib,
  avahi,
  cairo,
  dav1d,
  dbus,
  faad2,
  ffmpeg,
  flac,
  fontconfig,
  freefont_ttf,
  freetype,
  fribidi,
  gnutls,
  harfbuzz,
  kdePackages,
  libGL,
  libarchive,
  libass,
  libbluray,
  libcddb,
  libdvbpsi,
  libdvdnav,
  libebml,
  libgcrypt,
  libgpg-error,
  libjpeg,
  libmatroska,
  libogg,
  libopus,
  libplacebo,
  libpng,
  libpulseaudio,
  librsvg,
  libsamplerate,
  libsecret,
  libssh2,
  libtheora,
  libva,
  libvorbis,
  libxml2,
  libxcb,
  libxcb-keysyms,
  libxkbcommon,
  libx11,
  libxext,
  libxinerama,
  libxpm,
  libsm,
  live555,
  mpg123,
  ncurses,
  pipewire,
  samba,
  speex,
  sqlite,
  srt,
  systemdLibs,
  taglib,
  vulkan-headers,
  vulkan-loader,
  wayland,
  wayland-protocols,
  zlib,
}:

let
  # Every Cargo project the build compiles: the native add-on worker and the
  # bundled WebAssembly add-ons (see modules/gui/qt/meson.build). None has git
  # dependencies, so each lock file is vendored without a hash to maintain.
  # Read them from the flake's own tree, not from `src`: `nix flake check
  # --no-build` only computes the filtered copy's store path, so reading
  # from it fails until something has built it.
  cargoLockFiles = [
    (flakeRoot + "/modules/gui/qt/addons/worker/Cargo.lock")
    (flakeRoot + "/contrib-addons/episodes/Cargo.lock")
    (flakeRoot + "/contrib-addons/thesubtitledb/Cargo.lock")
    (flakeRoot + "/contrib-addons/tvmaze/Cargo.lock")
  ];
  cargoVendor = symlinkJoin {
    name = "hakone-cargo-vendor";
    paths = map (lockFile: rustPlatform.importCargoLock { inherit lockFile; }) cargoLockFiles;
  };

  hakone = stdenv.mkDerivation {
    pname = "hakone-unwrapped";
    inherit version src;

    nativeBuildInputs = [
      meson
      ninja
      pkg-config
      python3
      flex
      bison
      gettext
      lua5_4 # luac compiles the bundled Lua scripts
      wayland-scanner
      # Lets qsb find spirv-opt so the Qt shaders are optimized.
      spirv-tools
      rustToolchain
      patchelf
      qt6.wrapQtAppsHook
      qt6.qttools
      qt6.qtshadertools
    ];

    buildInputs = [
      alsa-lib
      avahi
      cairo
      dav1d
      dbus
      faad2
      ffmpeg
      flac
      fontconfig
      freetype
      fribidi
      gnutls
      harfbuzz
      libGL
      libarchive
      libass
      libbluray
      libcddb
      libdvbpsi
      libdvdnav
      libdvdnav.libdvdread
      libebml
      libgcrypt
      libgpg-error
      libjpeg
      libmatroska
      libogg
      libopus
      libplacebo
      libpng
      libpulseaudio
      librsvg
      libsamplerate
      libsecret
      libssh2
      libtheora
      libva
      libvorbis
      libxml2
      libxcb
      libxcb-keysyms
      libxkbcommon
      libx11
      libxext
      libxinerama
      libxpm
      libsm
      live555
      lua5_4
      medialibrary
      mpg123
      ncurses
      pipewire
      samba
      speex
      sqlite
      srt
      systemdLibs
      taglib
      vulkan-headers
      vulkan-loader
      wayland
      wayland-protocols
      zlib
      qt6.qtbase
      # nixpkgs' Qt Quick lacks contrib/src/qtdeclarative's pixmap cache patch,
      # which only silences a timer warning when the player exits. Patching
      # it would rebuild Qt Declarative and every Qt module that uses it.
      qt6.qtdeclarative
      # WebP and other artwork formats, as in the release builds.
      qt6.qtimageformats
      qt6.qtsvg
      qt6.qtwayland
      qt6.qtshadertools
      kdePackages.kwindowsystem
    ];

    strictDeps = true;

    postPatch = ''
      patchShebangs buildsystem modules/gui/qt/scripts extras/package/release
      substituteInPlace modules/text_renderer/freetype/platform_fonts.h \
        --replace-fail /usr/share/fonts/truetype/freefont ${freefont_ttf}/share/fonts/truetype
    '';

    # nixpkgs defaults to auto_features=enabled, which turns every optional
    # probe into a hard error. Let meson detect what buildInputs provide.
    mesonAutoFeatures = "auto";
    mesonBuildType = "release";

    mesonFlags = [
      (lib.mesonEnable "qt" true)
      (lib.mesonEnable "addon-runtime" true)
      (lib.mesonEnable "medialibrary" true)
      (lib.mesonEnable "nls" true)
      (lib.mesonEnable "lua" true)
      (lib.mesonEnable "xcb" true)
      (lib.mesonEnable "wayland" true)
      (lib.mesonEnable "dbus" true)
      (lib.mesonEnable "update-check" false)
      (lib.mesonEnable "skins2" false)
      (lib.mesonEnable "zvbi" false)
      (lib.mesonEnable "rust" false)
      # protobuf's abseil headers need C++20; Hakone builds as C++17.
      (lib.mesonEnable "chromecast" false)
      (lib.mesonEnable "tests" false)
    ];

    # Cargo runs inside meson's build steps, so point it at the vendored
    # crates here. A copy keeps store paths out of the add-on modules.
    preConfigure = ''
      export CARGO_HOME="$NIX_BUILD_TOP/cargo-home"
      mkdir -p "$CARGO_HOME"
      cp -rL --no-preserve=mode ${cargoVendor} "$NIX_BUILD_TOP/cargo-vendor"
      cat > "$CARGO_HOME/config.toml" <<EOF
      [source.crates-io]
      replace-with = "vendored-sources"

      [source.vendored-sources]
      directory = "$NIX_BUILD_TOP/cargo-vendor"

      [net]
      offline = true
      EOF
    '';

    # The meson install has no desktop entries or icons yet (the Autotools
    # build in share/Makefile.am does), so make them from the same templates.
    postInstall = ''
      mimetypes="$(sed 's/\s*#.*$//g' ../share/hakone.desktop.mimetypes | grep -v '^$' | tr '\n' ';')"
      for template in ../share/hakone*.desktop.in; do
        name="$(basename "$template" .in)"
        sed -e "s,@bindir@,$out/bin,g" -e "s,@MIMETYPES@,$mimetypes,g" "$template" \
          | install -Dm444 /dev/stdin "$out/share/applications/$name"
      done
      for size in 16x16 32x32 48x48 128x128 256x256; do
        install -Dm444 ../share/icons/$size/hakone.png "$out/share/icons/hicolor/$size/apps/hakone.png"
      done
      install -Dm444 ../share/icons/hakone.svg "$out/share/icons/hicolor/scalable/apps/hakone.svg"
    '';

    # Wrap only the player. The add-on worker is started with an empty
    # environment and must stay a plain executable beside it.
    dontWrapQtApps = true;
    postFixup = ''
      wrapQtApp "$out/bin/hakone"

      # The xcb plugins link libvlc_xcb_events.so from lib/vlc, which is not
      # in their RUNPATH.
      for plugin in $out/lib/vlc/plugins/video_output/libxcb_*_plugin.so; do
        patchelf --add-rpath "$out/lib/vlc" "$plugin"
      done

      # The plug-in cache records each file's time, and the store sets every
      # time to 1. Write it last, after patchelf, with the store's time.
      find $out/lib/vlc/plugins -type f -exec touch -d @1 '{}' +
      $out/libexec/vlc/vlc-cache-gen $out/lib/vlc/plugins
      test -s $out/lib/vlc/plugins/plugins.dat
    '';

    passthru = { inherit cargoVendor; };

    meta = {
      description = "Hakone media player (unwrapped build with its private libraries)";
      homepage = "https://github.com/hakone-org/hakone";
      license = with lib.licenses; [
        gpl2Plus
        lgpl21Plus
      ];
      mainProgram = "hakone";
      platforms = lib.platforms.linux;
    };
  };
in
# The build installs libvlc, its plug-ins and share/vlc under VLC's own
# names. Expose only the player, desktop entries and icons, so Hakone can sit
# in the same profile as nixpkgs' vlc. Settings already live in ~/.config/hakone.
runCommand "hakone-${version}"
  {
    pname = "hakone";
    inherit version;
    nativeBuildInputs = [ makeWrapper ];
    passthru = {
      unwrapped = hakone;
      inherit medialibrary;
    };
    meta = hakone.meta // {
      description = "Media player based on VLC 4 with the Hakone interface";
    };
  }
  ''
    mkdir -p $out/bin $out/share
    ln -s ${hakone}/bin/hakone $out/bin/hakone
    cp -r --no-preserve=mode ${hakone}/share/applications $out/share/applications
    substituteInPlace $out/share/applications/*.desktop --replace-quiet ${hakone}/bin $out/bin
    ln -s ${hakone}/share/icons $out/share/icons
  ''
