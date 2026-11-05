# Add-on SDK

These guides describe the host contracts for add-on authors. Start with
[Author tools](authoring-tools.md) and the [templates](templates).

This folder is `contrib-addons/sdk` in
[hakone-org/hakone](https://github.com/hakone-org/hakone).
[hakone-org/addon-sdk](https://github.com/hakone-org/addon-sdk) is a
read-only mirror of it for add-on repositories, with the Rust crate, the
reusable workflows and the tools at its root. Send changes and issues to
hakone-org/hakone. `VERSION` is the SDK version; a release has the tag
`sdk-v<VERSION>` here and `v<VERSION>` on the mirror.

| Guide | What it covers |
| --- | --- |
| [Build and release](releasing.md) | The release tag, the built module, the reusable check and release workflows, and the package check. |
| [Author tools](authoring-tools.md) | The schemas, the generated Rust contract types, the package validator, the preview tool and the templates. |
| [Rust message SDK](message-v1/README.md) | Typed input views, owned ABI buffers, pending calls and bounded output for a Wasm worker. |
| [UI document](ui-document.md) | Bounded JSON views that the host renders at declared hooks. Packages do not supply QML or JavaScript. |
| [Player panels](player-panels.md) | A side panel in the player with its own button, view, state and commands. |
| [Static UI images](static-ui-images.md) | Declared package images for views. |
| [Visualizer scene](visualizer-scene.md) and [parameters](visualizer-parameters.md) | Data-only music scenes that the host draws from bounded audio analysis. |
| [Discovery source](discovery-sources.md) | Browse, search and pages with host references, and playback through the host. |
| [Metadata completion](metadata-completion.md) | NoMatch results, host observations and the HTTP 404/410 policy. |
| [Play queue](queue.md) | Read the queue with opaque IDs and address keys, check its local files, and remove items. |
| [Typed UI selections](typed-ui-selections.md) | Typed subtitle and playlist bodies with core confirmation. |
| [Shared playback sessions](sessions.md) | Sessions that follow playback across players. |
| [Private storage](storage.md) | Private key/value storage, account partitions, migrations and recovery snapshots. |
| [Accounts, picked files and temporary settings](settings-accounts.md) | Account sign-in, file and folder handles, and session-only settings. |
| [Terminology](terminology.md) | Technical terms and English text rules. |

Declare each contract and operation that the package uses. A declared permission does not mean that the host has a handler for that function. The host capability view reports the available functions. Unsupported functions fail before a core operation starts.

Use the [bundled packages](../README.md#bundled-add-ons) to inspect complete package files. Keep credentials out of package files, ordinary settings values, worker state, and saved media references. Use the generated host settings form for secret replacement or deletion.

## Package names

An installable package cannot use `hakone.builtin`, `vlc.builtin`, or any
package ID below either dotted prefix. See
[reserved provider names](provider-names.md).
