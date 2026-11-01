# Synthetic discovery source

This package tests the portable discovery API. It supplies roots, folders, pages, search results, suggestions, and stable item IDs. All catalogue entries are synthetic. The package has no network, credential, or file access.

`resolve` returns `unsupported`. The package has no live stream. A real source needs a playback resolver and the host playback policy adapter before it can supply media.

Use Rust stable with the `wasm32-unknown-unknown` target.

```sh
RUSTUP_TOOLCHAIN=stable cargo test --locked
sh build.sh
python3 tests/test_worker.py --worker /absolute/path/to/vlc-addon-worker
```

`dist/addon.wasm` is the package module. Install the folder with the add-on manager. Then enable the package. The host supplies private references for folders, items, and page cursors. A saved queue reference contains the source and item identity. A separate user command must accept a suggestion before it changes the queue.

The manifest identifies the app development repository. For a separate Git release, put the complete source and build files at its repository root. Set `source` to that repository. The host installs only the declared package files. This package uses the `wasm-message-v1` ABI.
