# Add-on SDK

See [visualizer parameters](visualizer-parameters.md) for declared numeric scene updates and their current integration limits.

Use these guides for the current host contracts. The [architecture documents](../README.md) also describe future functions.

| Guide | Current status |
| --- | --- |
| [Rust message SDK](message-v1/README.md) | Typed input views, owned ABI buffers, pending-call ownership, and bounded output. |
| [UI document](ui-document.md) | The host renders bounded JSON views at declared hooks. Packages do not supply QML or JavaScript for these views. |
| [Static UI images](static-ui-images.md) | Declared package raster images use bounded decode jobs and revocable view tickets and image-owner claims. |
| [Visualizer scene](visualizer-scene.md) | The host renders data-only music scenes. Core supplies bounded audio analysis and playback timing. |
| [Discovery source](discovery-sources.md) | Browse, search, and pages use host references. The default application gate keeps Play and Enqueue disabled. |
| [Typed UI selections](typed-ui-selections.md) | Typed subtitle and playlist bodies, core confirmation, and recovery. The normal app build and compiled UI tests pass. Core keeps the built-in bodies available. |
| [Guest private storage](../guest-storage-contract.md) | Mounted private storage, account partitions, pure migrations, and recovery snapshots. All 18 focused tests pass. |
| [Terminology](terminology.md) | Technical terms and English text rules for this project. |

Declare each contract and operation that the package uses. A declared permission does not mean that the host has a handler for that function. The host capability view reports the available functions. Unsupported functions fail before a core operation starts.

Use the [reference packages](../examples/) to inspect complete package files. Keep credentials out of package files, ordinary settings values, worker state, and saved media references. Use the generated host settings form for secret replacement or deletion.
