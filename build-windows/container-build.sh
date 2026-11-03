#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Adapted from Hamido212/vlc e6146c4cc9daa3fda15300f557fed25ef52bcc16.
set -euo pipefail
mode=${1:-full}
case "$mode" in full|make|qt|repackage) ;; *) echo 'Invalid build mode.' >&2; exit 2 ;; esac
src=/work/src
build=/work/build
package=$build/vlc-local
: "${JOBS:=4}"
export JOBS
export WINEPREFIX=/work/wine
metadata_bundle=()
if [[ -d /host/src/ext/providers ]]; then
    metadata_bundle=(METADATA_PROVIDER_DIR=/host/src/ext/providers)
fi
mkdir -p "$src" "$build"
if [[ "$mode" != repackage ]]; then
    python3 /host/scripts/local_files.py sync /host/src "$src" /host/scripts/state/source-files.nul
fi
if [[ "${VLC_WINDOWS_VERIFIED_WORKER:-0}" == 1 && "$mode" != repackage ]]; then
    python3 /host/scripts/stage-addon-worker.py --source "$src" --build "$build" --bundle /host/verified-worker
fi
if [[ "$mode" != full && ! -f "$build/win64/Makefile" ]]; then
    echo 'No configured build exists; using full mode.'
    mode=full
fi
if [[ "$mode" == qt ]]; then
    python3 /host/scripts/local_files.py check-qt /work
elif [[ "$mode" != repackage && "$(cat "$src/src/revision.txt" 2>/dev/null || true)" != "$VLC_REVISION" ]]; then
    printf '%s\n' "$VLC_REVISION" > "$src/src/revision.txt"
fi
cd "$build"
export PATH="$build/extras/tools/build/bin:$PATH"
case "$mode" in
    full) "$src/extras/package/win32/build.sh" -a x86_64 -p -l -o "$package" ;;
    make)
        (cd win64 && python3 /host/scripts/enable-nls.py)
        make -C win64 -j"$JOBS" QT_LOW_MEMORY_LDFLAGS=-Wl,--no-keep-memory,--strip-debug
        # Avoid a temporary ~1.4 GiB debug-DLL copy during package install.
        # Keep the original debug DLL; other install operations use install.
        mkdir -p /work/runtime-install
        cat > /work/runtime-install/install <<'INSTALL'
#!/bin/bash
set -euo pipefail
args=("$@")
if (( $# >= 2 )); then
    source_file=${args[$#-2]}
    target_file=${args[$#-1]}
    if [[ "$source_file" == */libqt_plugin.dll && "$target_file" == */plugins/gui/* ]]; then
        [[ ! -d "$target_file" ]] || target_file="$target_file/libqt_plugin.dll"
        x86_64-w64-mingw32-strip --strip-debug --preserve-dates -o "$target_file.next" "$source_file"
        chmod 755 "$target_file.next"
        mv "$target_file.next" "$target_file"
        exit 0
    fi
fi
exec /usr/bin/install "$@"
INSTALL
        chmod +x /work/runtime-install/install
        make -C win64 package-win-common INSTALL_STRIP_FLAG=-s INSTALL=/work/runtime-install/install "${metadata_bundle[@]}"
        ;;
    qt)
        [[ -f "$package/hakone.exe" || -f "$package/vlc.exe" ]] || { echo 'Run full or make to create a complete package first.' >&2; exit 1; }
        make -C win64/modules/gui/qt -j"$JOBS" QT_LOW_MEMORY_LDFLAGS=-Wl,--no-keep-memory,--strip-debug
        # Keep the unstripped DLL in the build tree. Export only its runtime
        # contents, instead of copying ~1.4 GiB of debug data before stripping.
        x86_64-w64-mingw32-strip --strip-debug --preserve-dates \
            -o "$package/plugins/gui/libqt_plugin.dll.next" \
            win64/modules/gui/qt/.libs/libqt_plugin.dll
        mv "$package/plugins/gui/libqt_plugin.dll.next" "$package/plugins/gui/libqt_plugin.dll"
        ;;
    repackage) [[ -f "$package/hakone.exe" || -f "$package/vlc.exe" ]] || exit 1 ;;
esac
# Select the launcher declared by this checkout, rather than a previous brand.
programs=$(make --no-print-directory -s -C win64/bin --eval 'local-print-programs:; @echo $(bin_PROGRAMS)' local-print-programs)
case " $programs " in
    *" hakone.exe "*) launcher=hakone.exe ;;
    *" vlc.exe "*) launcher=vlc.exe ;;
    *) echo "Cannot identify the application launcher: $programs" >&2; exit 1 ;;
esac
installed="$build/win64/_win32/bin/$launcher"
if [[ -f "$installed" ]] && ! cmp -s "$installed" "$package/$launcher"; then
    cp -p "$installed" "$package/$launcher"
fi
for obsolete in vlc.exe hakone.exe; do
    [[ "$obsolete" == "$launcher" ]] || rm -f "$package/$obsolete"
done
python3 - /work/build-info.json "$launcher" <<'PYMETA'
from pathlib import Path
import json, sys
path = Path(sys.argv[1])
data = json.loads(path.read_text())
data['application_executable'] = sys.argv[2]
path.write_text(json.dumps(data, indent=2) + '\n')
PYMETA
# Remove compiled Lua files if their source files were removed.
python3 - "$package" "$src/share" <<'PY'
from pathlib import Path
import sys
package, source = map(Path, sys.argv[1:])
for path in (package / 'lua').rglob('*.luac'):
    if not (source / path.relative_to(package).with_suffix('.lua')).is_file():
        path.unlink()
PY
[[ -f "$package/$launcher" && -f "$package/plugins/gui/libqt_plugin.dll" ]] || { echo 'The build did not produce Hakone and its Qt plugin.' >&2; exit 1; }
# VLC deliberately searches System32 for plugin dependencies. Contrib pkg-config
# files that request a shared GCC runtime must use the static exception runtime.
runtime_changes=$(python3 /host/scripts/local_files.py normalize-runtime "$build")
if ((runtime_changes)); then
    echo "Correcting shared GCC runtime flags in $runtime_changes contrib metadata files."
    (cd win64 && ./config.status --recheck && ./config.status)
    make -C win64 -j"$JOBS"
    make -C win64 package-win-common INSTALL_STRIP_FLAG=-s "${metadata_bundle[@]}"
fi
rm -f "$package/libgcc_s_seh-1.dll" "$package/libwinpthread-1.dll"
strip_args=()
[[ ! -f /work/last-strip ]] || strip_args=(-newer /work/last-strip)
find "$package" -type f \( -name '*.dll' -o -name '*.exe' \) "${strip_args[@]}" -print0 |
    xargs -0 -r -n 32 x86_64-w64-mingw32-strip --strip-debug --preserve-dates
touch /work/last-strip
if [[ "${VLC_WINDOWS_VERIFIED_WORKER:-0}" == 1 ]]; then
    python3 - "$package/vlc-addon-worker.exe" /work/build-info.json <<'PYWORKER'
from pathlib import Path
import hashlib,json,sys
worker,metadata=map(Path,sys.argv[1:])
if not worker.is_file():raise SystemExit('The packaged add-on worker is missing.')
data=json.loads(metadata.read_text())
data['addon_worker']['packaged_sha256']=hashlib.sha256(worker.read_bytes()).hexdigest()
metadata.write_text(json.dumps(data,indent=2)+'\n')
PYWORKER
fi
if [[ "$mode" != repackage ]]; then
    python3 /host/scripts/local_files.py finish /work "$mode"
fi
