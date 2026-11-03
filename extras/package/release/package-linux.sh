#!/usr/bin/env bash
set -euo pipefail
stage=$1 output=$2 tag=$3
root=$(cd "$(dirname "$0")/../../.." && pwd)
[[ $(uname -m) == x86_64 ]] || { echo 'Linux release target must be x86_64' >&2; exit 1; }
[[ -x "$stage/bin/vlc-addon-worker" ]] || { echo 'The stage directory has no executable add-on worker.' >&2; exit 1; }
appdir="$output/linux-x86_64/AppDir"
[[ ! -e "$appdir" ]] || { echo "Remove old AppDir first: $appdir" >&2; exit 1; }
mkdir -p "$appdir/usr" "$output/tools/downloads"
cp -a "$stage/." "$appdir/usr/"
mkdir -p "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
cp "$root/share/icons/256x256/vlc.png" "$appdir/usr/share/icons/hicolor/256x256/apps/vlc-juxtopposed.png"
cat > "$appdir/usr/share/applications/vlc-juxtopposed.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=VLC Juxtopposed
Exec=vlc
Icon=vlc-juxtopposed
Categories=AudioVideo;Player;
Terminal=false
EOF
# Keep library overrides out of the launched app's child processes.
cp "$root/extras/package/release/AppRun.linux" "$appdir/AppRun"
chmod +x "$appdir/AppRun"
python3 "$root/extras/package/release/fetch-linux-tools.py" "$output/tools/downloads" \
    > "$output/linux-packaging-tools.sha256"
# Extract the tools as data. Their static AppImage runtimes cannot run under
# Docker Desktop's Rosetta emulation, while the inner dynamic executables can.
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    extracted="$output/tools/$tool.AppDir"
    # Cached archives are checksum-verified above. Never trust an old extracted
    # executable tree, which can differ from its verified archive.
    rm -rf -- "$extracted"
    offset=$(python3 - "$output/tools/downloads/$tool-x86_64.AppImage" <<'PY'
import struct, sys
with open(sys.argv[1], 'rb') as stream:
    header = stream.read(64)
    if header[:6] != b'\x7fELF\x02\x01':
        sys.exit('Expected a little-endian ELF64 AppImage')
    table = struct.unpack_from('<Q', header, 40)[0]
    size, count = struct.unpack_from('<HH', header, 58)
    offset = table + size * count
    stream.seek(offset)
    if stream.read(4) != b'hsqs':
        sys.exit('AppImage has no SquashFS at the end of its ELF runtime')
    print(offset)
PY
)
    unsquashfs -q -d "$extracted" -o "$offset" "$output/tools/downloads/$tool-x86_64.AppImage"
    printf '#!/bin/sh\nexec "%s/AppRun" "$@"\n' "$extracted" > "$output/tools/$tool"
    chmod +x "$output/tools/$tool"
done
export PATH="$output/tools:$PATH"
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
export QML_SOURCES_PATHS="$root/modules/gui/qt"
export EXTRA_QT_MODULES=svg
export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so'
export LD_LIBRARY_PATH="$stage/lib:$stage/lib/vlc:${RELEASE_DEPS_DIR:-$root/build-release-linux-deps}/prefix/lib:${LD_LIBRARY_PATH:-}"
# linuxdeploy recursively scans the installed VLC modules in AppDir.
"$output/tools/linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/vlc" \
    --executable "$appdir/usr/bin/vlc-addon-worker" \
    --executable "$appdir/usr/libexec/vlc/vlc-preparser" \
    --executable "$appdir/usr/libexec/vlc/vlc-cache-gen" \
    --executable "$appdir/usr/libexec/vlc/vlc-qt-check" \
    --plugin qt
python3 "$root/extras/package/release/bundle-linux-runtime.py" "$appdir" \
    --qt-plugins "$("$QMAKE" -query QT_INSTALL_PLUGINS)"
# The directory bundle works without FUSE or a system VLC installation.
cp "$root/COPYING" "$root/COPYING.LIB" "$root/README.md" "$appdir/"
cp "$root/modules/gui/qt/fonts/LICENSE.txt" "$appdir/Inter-LICENSE.txt"
git -C "$root" rev-parse HEAD > "$appdir/SOURCE_COMMIT.txt"
tar -C "$(dirname "$appdir")" -czf "$output/vlc-juxtopposed-$tag-linux-x86_64.tar.gz" AppDir
cd "$output"
sha256sum "vlc-juxtopposed-$tag-linux-x86_64.tar.gz" > "vlc-juxtopposed-$tag-linux-x86_64.tar.gz.sha256"
python3 - "$tag" <<'PYMETA'
import hashlib, json, os, pathlib, subprocess, sys
metadata_path = pathlib.Path("linux-build.json")
metadata = json.loads(metadata_path.read_text())
archive = pathlib.Path(f"vlc-juxtopposed-{sys.argv[1]}-linux-x86_64.tar.gz")
digest = hashlib.sha256()
with archive.open("rb") as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
metadata.update(asset_name=archive.name, size=archive.stat().st_size, sha256=digest.hexdigest(),
                package_type="portable-tar", minimum_glibc="2.35")
runtime = json.loads(pathlib.Path("linux-x86_64/AppDir/LINUX-RUNTIME.json").read_text())
metadata["minimum_glibcxx"] = runtime["minimum_glibcxx"]
metadata["runtime_audit"] = {key: value for key, value in runtime.items() if key != "files"}
metadata["qt_version"] = subprocess.check_output([os.environ["QMAKE"], "-query", "QT_VERSION"], text=True).strip()
metadata["packaging_tools"] = {pathlib.Path(path).name: value for value, path in
    (line.split(maxsplit=1) for line in pathlib.Path("linux-packaging-tools.sha256").read_text().splitlines())}
metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
PYMETA
