#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the module, then check the package with vlc-addon-validate.
set -eu
cd "$(dirname "$0")"
target_directory="${CARGO_TARGET_DIR:-target}"
cargo build --release --target wasm32-unknown-unknown
mkdir -p dist
cp "$target_directory"/wasm32-unknown-unknown/release/example_addon_discovery.wasm dist/addon.wasm
