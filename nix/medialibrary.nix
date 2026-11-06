# SPDX-License-Identifier: GPL-2.0-or-later
# The VideoLAN media library with the patches Hakone carries in contrib.
# nixpkgs does not package it. Keep the version and patch list equal to
# contrib/src/medialibrary/rules.mak and extras/package/release/build-medialibrary.sh.
{
  lib,
  stdenv,
  fetchurl,
  meson,
  ninja,
  pkg-config,
  sqlite,
  libjpeg,
  xxhash,
  patchDir,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "medialibrary";
  version = "0.13.2";

  src = fetchurl {
    url = "https://code.videolan.org/videolan/medialibrary/-/archive/${finalAttrs.version}/medialibrary-${finalAttrs.version}.tar.bz2";
    # Same archive as contrib/src/medialibrary/SHA512SUMS.
    hash = "sha256-rNqV2ujzV88epscaw5eSNVVdrl/vXTPSGNFNHl1yloI=";
  };

  patches = map (name: patchDir + "/${name}") [
    "0001-preserve-windows-unc-storage-paths.patch"
    "0002-exclude-associated-tracks-from-media-lookup.patch"
    "0003-wait-for-transient-sqlite-locks.patch"
    "0004-replace-external-track-assignments.patch"
    "0005-persist-discovered-track-selections.patch"
    "0006-wake-eligible-priority-writers.patch"
    "0007-atomic-addon-playlist-append.patch"
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    sqlite
    libjpeg
    xxhash
  ];

  mesonBuildType = "release";
  mesonFlags = [
    # Hakone loads the media library, not the other way round.
    (lib.mesonEnable "libvlc" false)
    (lib.mesonEnable "tests" false)
  ];

  strictDeps = true;

  meta = {
    description = "Media library used by Hakone (VideoLAN medialibrary with Hakone patches)";
    homepage = "https://code.videolan.org/videolan/medialibrary";
    license = lib.licenses.lgpl21Plus;
    platforms = lib.platforms.linux;
  };
})
