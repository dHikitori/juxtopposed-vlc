# Rust message SDK

This crate supplies the `wasm-message-v1` exports and message views. It has no WASI or network API. The host supplies all effects through messages.

Use the Cargo package `hakone-addon-message-v1` and Rust import `hakone_addon_message_v1`.

Implement `Addon::handle`. Use `Incoming::Invoke`, `Incoming::HostResult`, and `Incoming::Cancel`. Use `Pending` to bind each host call to its invocation. On cancellation, remove that invocation's pending state. Do not return a result after cancellation.

A host-call ID has one use in its active invocation. A completed or canceled invocation releases its retained IDs. The worker retains at most 4,096 IDs across all active invocations. Keep IDs unique among active entries in one `Pending` map. A reused ID in a later slice still needs that slice's exact invocation ID. A late result from an earlier slice cannot consume the new pending entry.

Call `export_addon!(YourState, YourState::new())` once in a `cdylib` crate. Build for `wasm32-unknown-unknown`. The macro exports `hakone_alloc`, `hakone_dealloc`, and `hakone_dispatch`, and imports only `hakone.host_send`.

The buffer registry accepts exact pointer and length pairs. It rejects unknown pointers, offsets, wrong lengths, and repeated release. Each buffer is at most 1 MiB. At most 32 buffers can exist. A dispatch can emit at most 64 objects. The complete IPC envelope must fit in 1 MiB. The full batch must fit before the first message is sent.

The credential view contains only `{ "present": true }` or `{ "present": false }`. It supports the host limit of 64 declared fields. Select a credential by its manifest name in a host request. The host inserts its value after permission and destination checks.

The SDK does not grant authority. The native host validates each effect, the installed release, the account, and the active invocation. The worker also applies memory, fuel, and message limits.

The fixture runner reads `HAKONE_ADDON_WORKER` and `HAKONE_ADDON_SDK_FIXTURE`. Set both to absolute paths. The second path selects the compiled `hakone_message_sdk_fixture.wasm` file.

Run `RUSTUP_AUTO_INSTALL=0 RUSTUP_TOOLCHAIN=stable cargo test --locked --manifest-path contrib-addons/sdk/message-v1/Cargo.toml` for the unit suite. The separate worker fixture exercises the real Wasm ABI. Refer to [the terminology list](../terminology.md) for English terms.
