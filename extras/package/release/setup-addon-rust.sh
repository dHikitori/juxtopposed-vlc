#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Use this script only for application builds and releases.

vlc_setup_addon_rust() {
    local setup_root rustup_version installer_host temporary installer
    setup_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    export RUSTUP_TOOLCHAIN="${RUSTUP_TOOLCHAIN:-$(cat "$setup_root/addon-rust-version.txt")}"
    export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
    export PATH="$CARGO_HOME/bin:$PATH"
    rustup_version=1.29.2
    if ! command -v rustup >/dev/null 2>&1; then
        case "$(uname -s)/$(uname -m)" in
            Linux/x86_64) installer_host=x86_64-unknown-linux-gnu ;;
            Linux/aarch64|Linux/arm64) installer_host=aarch64-unknown-linux-gnu ;;
            Darwin/x86_64) installer_host=x86_64-apple-darwin ;;
            Darwin/arm64|Darwin/aarch64) installer_host=aarch64-apple-darwin ;;
            *) echo 'Install rustup on this build machine.' >&2; return 1 ;;
        esac
        temporary=$(mktemp -d "${TMPDIR:-/tmp}/vlc-addon-rust.XXXXXX")
        installer="$temporary/rustup-init"
        local url="https://static.rust-lang.org/rustup/archive/$rustup_version/$installer_host/rustup-init"
        curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' --tlsv1.2 "$url" -o "$installer" || return 1
        curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' --tlsv1.2 "$url.sha256" -o "$installer.sha256" || return 1
        # Compare the installer checksum with the official checksum before use.
        if ! python3 - "$installer" "$installer.sha256" <<'PY'
import hashlib, pathlib, re, sys
binary, checksum = map(pathlib.Path, sys.argv[1:])
parts = checksum.read_text().split()
if not parts or not re.fullmatch(r"[a-fA-F0-9]{64}", parts[0]):
    raise SystemExit("The official rustup checksum is not correct.")
digest = hashlib.sha256()
with binary.open("rb") as stream:
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
if digest.hexdigest() != parts[0].lower():
    raise SystemExit("The rustup installer checksum does not agree with the official checksum.")
PY
        then
            rm -rf -- "$temporary"
            return 1
        fi
        chmod +x "$installer"
        "$installer" -y --no-modify-path --profile minimal --default-toolchain none || return 1
        rm -rf -- "$temporary"
    fi
    rustup toolchain install "$RUSTUP_TOOLCHAIN" --profile minimal --no-self-update || return 1
    # The build compiles each bundled add-on's module for this target on
    # every platform. $1 is an extra target, such as a worker cross target.
    rustup target add --toolchain "$RUSTUP_TOOLCHAIN" wasm32-unknown-unknown || return 1
    if [[ -n "${1:-}" ]]; then
        rustup target add --toolchain "$RUSTUP_TOOLCHAIN" "$1" || return 1
    fi
    # Do not let Cargo install another toolchain.
    export RUSTUP_AUTO_INSTALL=0
    cargo --version
}
