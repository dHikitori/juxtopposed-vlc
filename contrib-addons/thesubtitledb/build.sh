#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")"
export RUSTUP_TOOLCHAIN="${RUSTUP_TOOLCHAIN:-stable}"
export RUSTUP_AUTO_INSTALL=0
target_directory="${CARGO_TARGET_DIR:-target}"
cargo build --locked --release --target wasm32-unknown-unknown
mkdir -p dist
cp "$target_directory"/wasm32-unknown-unknown/release/hakone_addon_thesubtitledb.wasm dist/addon.wasm
