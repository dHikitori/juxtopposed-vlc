# Portable artwork providers

The four artwork packages use this Rust source. It contains the provider state and bounded response parsers. Each package has its own manifest, portable module, and release identity.

The `../message-v1` crate owns the ABI exports and exact buffer registry. `Provider` implements its `Addon` trait. Each package calls `export_provider!`, which selects the provider and calls `export_addon!`. Cancellation removes the invocation's pending response state.

The host supplies HTTP results through `wasm-message-v1`. This code has no network or file import. Image claims pass through the host image adapter. The adapter applies origins, redirects, account scope, byte limits, and image decode limits.

Run the source tests:

```sh
RUSTUP_TOOLCHAIN=stable cargo test --offline
```

Run the portable module tests from the repository root:

```sh
python3 contrib-addons/sdk/artwork-providers/tests/test_worker.py --worker build-redesign/modules/gui/qt/vlc-addon-worker
```

The portable module tests use synthetic service responses. They do not prove that a public service works. Optional native probes use the real host broker and image adapter.
