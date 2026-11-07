#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=${RELEASE_DEPS_DIR:-"$root/build-release-deps"}
mkdir -p "$work"
# contrib/src/medialibrary/rules.mak owns the version, checksum and patches.
version=$(sed -n 's/^MEDIALIBRARY_VERSION := //p' "$root/contrib/src/medialibrary/rules.mak")
[[ -n "$version" ]] || { echo "Cannot read the media library version" >&2; exit 1; }
archive="$work/medialibrary-$version.tar.bz2"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 "https://code.videolan.org/videolan/medialibrary/-/archive/$version/medialibrary-$version.tar.bz2" -o "$archive"
fi
python3 - "$archive" "$root/contrib/src/medialibrary/SHA512SUMS" <<'PY'
import hashlib, pathlib, sys
archive = pathlib.Path(sys.argv[1])
expected = pathlib.Path(sys.argv[2]).read_text().split()[0]
if hashlib.sha512(archive.read_bytes()).hexdigest() != expected:
    sys.exit('Media library archive checksum mismatch')
PY
if [[ ! -d "$work/medialibrary-$version" ]]; then
    tar -xjf "$archive" -C "$work"
    for patchfile in "$root"/contrib/src/medialibrary/000*.patch; do
        patch -d "$work/medialibrary-$version" -p1 < "$patchfile"
    done
fi
if [[ ! -f "$work/medialibrary-$version-build/build.ninja" ]]; then
    meson setup "$work/medialibrary-$version-build" "$work/medialibrary-$version" \
        --prefix="$work/prefix" --libdir=lib --buildtype=release \
        -Dlibvlc=disabled -Dtests=disabled
fi
meson compile -C "$work/medialibrary-$version-build" -j "${JOBS:-4}"
meson install -C "$work/medialibrary-$version-build" --no-rebuild
