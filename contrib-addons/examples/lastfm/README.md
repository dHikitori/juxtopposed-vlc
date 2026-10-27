# Last.fm artwork

This portable package retains the album search from `03_lastfm.lua`. It accepts artist and album labels. It omits radio and TV items. It reads a bounded HTML prefix and accepts the current `og:image` attribute order. The old core provider remains until the replacement passes its tests.

The worker returns an image claim. The host compares the identity, applies provider order, and downloads a complete image within its limits. The package cannot replace existing local or user artwork.

The public album page returned HTTP 200 on 27 October 2026. The old Lua text pattern did not match that page. The package found its image link and requested the 500-pixel CDN form. The shared host route downloaded and decoded an 80,349-byte JPEG. This result proves the tested album path. It does not prove that each album has an image.

Build from this source tree:

```sh
RUSTUP_TOOLCHAIN=stable cargo build --target wasm32-unknown-unknown --release
cp target/wasm32-unknown-unknown/release/hakone_addon_lastfm.wasm dist/addon.wasm
```

The path dependency is `../../sdk/artwork-providers`. A separate release repository must include that source or use a fixed published SDK version. Do not run a build script during installation.
