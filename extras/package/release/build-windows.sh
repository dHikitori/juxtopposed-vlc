#!/usr/bin/env bash
# Run inside the pinned VLC Debian cross-build image, from a separate work dir.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
RELEASE_TAG=$(python3 "$root/extras/package/release/version.py")
export RELEASE_TAG
# Keep contrib Rust packages below the root Cargo workspace's excluded contrib/.
work="$root/contrib/contrib-release-win64"
cache="$root/extras/package/release/dependency-cache.py"
mode=${1:-build}
[[ "$mode" == build || "$mode" == --dependencies-only ]] || { echo 'Expected --dependencies-only or no arguments' >&2; exit 1; }
if [[ "$mode" == build ]]; then
    source "$root/extras/package/release/setup-addon-rust.sh"
    vlc_setup_addon_rust x86_64-pc-windows-gnu
    addon_linker=${ADDON_RUST_LINKER:-x86_64-w64-mingw32-gcc}
    command -v "$addon_linker" >/dev/null || { echo 'The Windows worker target linker is not available.' >&2; exit 1; }
    export MCONFIGFLAGS="${MCONFIGFLAGS:-} -Daddon-runtime=enabled -Daddon-runtime-target=x86_64-pc-windows-gnu -Daddon-runtime-linker=$addon_linker"
fi
mkdir -p "$work"
cd "$work"
# Avoid -i: package.py builds the NSIS installer from the portable package instead.
# Build patched contribs from source; upstream prebuilt contribs lack fork patches.
# Target executables need a Windows host; Wine may not work under ARM emulation.
export VLC_NO_EXE_WRAPPER=1
export VLC_BUILD_TRACE=0
export MCONFIGFLAGS="${MCONFIGFLAGS:-} -Dmedialibrary=enabled -Dtests=disabled -Dnsis=enabled -Dwix=disabled"
unset VLC_USE_CACHED_DEPENDENCIES VLC_BUILD_DEPENDENCIES_ONLY
if [[ "$mode" == --dependencies-only ]]; then
    : "${RELEASE_DEPS_CACHE_KEY:?A cache key is required for the separate dependency phase}"
    if python3 "$cache" check; then
        echo 'Reusing the validated Windows dependency SDK'
        exit 0
    fi
    python3 "$cache" clean
    export VLC_BUILD_DEPENDENCIES_ONLY=1
elif [[ -n "${RELEASE_DEPS_CACHE_KEY:-}" ]] && python3 "$cache" check; then
    export VLC_USE_CACHED_DEPENDENCIES=1
fi
"$root/extras/package/win32/build.sh" -a x86_64 -r -i '' -m -l -o "$work/install"
if [[ "$mode" == --dependencies-only ]]; then
    if ! python3 "$cache" seal; then
        echo 'Dependency cache could not be sealed; continuing with normal dependency checks' >&2
    fi
    exit 0
fi
# Unlike Windows API DLLs, the Vulkan loader is a redistributable dependency.
cp "$work/contrib/x86_64-w64-mingw32/bin/vulkan-1.dll" "$work/install/"
mkdir -p "$work/install/licenses/vulkan-loader"
cp "$work/contrib/contrib-win64/vulkan-loader/LICENSE.txt" \
    "$work/contrib/contrib-win64/vulkan-loader/LICENSES/"*.txt \
    "$work/install/licenses/vulkan-loader/"
python3 "$root/extras/package/release/package.py" windows "$work/install" --output "$root/build-release-output"
