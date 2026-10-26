# French TV logos

This portable package retains all 27 channel names and the title suffix rules from `02_frenchtv.lua`. It supplies an image claim for an exact channel label. The host compares the identity and downloads the image within its limits. The package cannot replace existing local or user artwork.

The old core provider remains. Several old image URLs require repair. On 27 October 2026, the old TF1 URL returned HTTP 400. A 500-pixel TF1 URL returned HTTP 404. The old France Info URL returned HTTP 400. Its 500-pixel URL returned PNG data, so this package uses that URL. The shared host route downloaded and decoded that 9,670-byte image. This result does not prove that all 27 images work.

Build from this source tree:

```sh
RUSTUP_TOOLCHAIN=stable cargo build --target wasm32-unknown-unknown --release
cp target/wasm32-unknown-unknown/release/vlc_addon_french_tv.wasm dist/addon.wasm
```

The path dependency is `../../sdk/artwork-providers`. A separate release repository must include that source or use a fixed published SDK version. Do not run a build script during installation.
