# Add-on SDK

See [visualizer parameters](visualizer-parameters.md) for declared numeric scene updates and their current integration limits.

Use these guides for the current host contracts. The [architecture documents](../README.md) also describe future functions.

| Guide | Current status |
| --- | --- |
| [Metadata completion](metadata-completion.md) | Strict NoMatch receipts, complete host observations, and optional HTTP 404/410 policy. Native aggregate admission remains separate. |
| [Rust message SDK](message-v1/README.md) | Typed input views, owned ABI buffers, pending-call ownership, and bounded output. |
| [UI document](ui-document.md) | The host renders bounded JSON views at declared hooks. Packages do not supply QML or JavaScript for these views. |
| [Static UI images](static-ui-images.md) | Declared package raster images use bounded decode jobs and revocable view tickets and image-owner claims. |
| [Visualizer scene](visualizer-scene.md) | The host renders data-only music scenes. Core supplies bounded audio analysis and playback timing. |
| [Discovery source](discovery-sources.md) | Browse, search, and pages use host references. The default application gate keeps Play and Enqueue disabled. |
| [Typed UI selections](typed-ui-selections.md) | Typed subtitle and playlist bodies, core confirmation, and recovery. The normal app build and compiled UI tests pass. Core keeps the built-in bodies available. |
| [Shared playback sessions](sessions.md) | MainUI connects the native bridge, controller, and host review panel. Focused UI and actual-player checks pass. The fresh full app and platform runs remain open. |
| [Guest private storage](../guest-storage-contract.md) | Mounted private storage, account partitions, pure migrations, and recovery snapshots. All 18 focused tests pass. |
| [Accounts, picked files and temporary settings](settings-accounts.md) | Device-code account sign-in with refresh, file and folder handles, and session settings. Handles have no guest operation yet. |
| [Terminology](terminology.md) | Technical terms and English text rules for this project. |

Declare each contract and operation that the package uses. A declared permission does not mean that the host has a handler for that function. The host capability view reports the available functions. Unsupported functions fail before a core operation starts.

Use the [reference packages](../examples/) to inspect complete package files. Keep credentials out of package files, ordinary settings values, worker state, and saved media references. Use the generated host settings form for secret replacement or deletion.

## Host package names

The host reserves `hakone.builtin`, `vlc.builtin`, and every package ID below
either dotted prefix. An installable package cannot use these names. The old
prefix remains reserved so that a package cannot replace a host provider.
New project add-ons use `org.hakone`. Extracted add-ons keep their approved IDs.
See [host provider names](../host-provider-namespaces.md) for the import checks.

## Session schema checks

Use Python 3.10 or later and `jsonschema==4.23.0` in a separate tool environment.
From the repository root, run:

```sh
python3 contrib-addons/sdk/test-session-schema.py
```

The check validates the Draft 2020-12 schema and 58 request and continuation
examples. It includes refused operations, extra fields, old session names,
integer encoding, and parameter limits. The native host must also check current
authority, exact invocation, native stamps, byte limits, and transport rules.
