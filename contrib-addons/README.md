# Add-ons architecture for the redesigned VLC

Architecture proposed 26 October 2026; implementation is now in progress. See [implementation status](IMPLEMENTATION.md) for current code, verification and remaining gates. Contracts and limits below are the intended design, not a declaration that SDK v1 is already stable.

## Recommendation

Build one capability-based add-on platform, with Git-backed packages, a host-owned UI and an isolated execution service. An add-on is a package that can contribute several features. It is not forced into one mutually exclusive category such as “metadata” or “interface.” A podcast service, for example, can supply discovery, podcast metadata, a home section and account settings together.

Keep the redesigned application's structure authoritative. Themes change semantic design tokens and approved assets. Interface extensions add content at documented hooks or become the user's chosen replacement for an explicitly replaceable surface. All normal add-on UI is rendered using the application's own components and resolved theme.

Use portable WebAssembly for executable add-on logic, with brokered host services and a worker process per active add-on. Use declarative documents for themes, views and configuration. The recommended runtime candidate is Wasmtime, subject to the cross-platform prototype in [the delivery plan](06-delivery-plan.md). The message contract and contribution model should survive a change of runtime implementation.

This is a clean break from legacy Lua extensions, skins2 packages and the old add-on package format. Existing native playback, codec, demux, output and filesystem implementations remain application infrastructure. They are not third-party add-on entry points.

## Reading guide

| Document | What it decides |
| --- | --- |
| [Architecture and runtime](01-architecture.md) | Ownership, isolation, lifecycle, permissions, message contracts and compatibility |
| [Packages and updates](02-packages-and-updates.md) | Repository layout, example manifest, release identity, tag pinning, install/update/rollback and author workflow |
| [Metadata and discovery](03-providers-and-discovery.md) | Shared identity, ranked providers, merge rules, subtitles/lyrics, discovery, stream resolution and queue transactions |
| [UI, themes and configuration](04-ui-themes-and-configuration.md) | Named hooks, component reuse, screen/action replacement, theme bounds and generated settings |
| [Visualizers and collaborative sessions](05-visualizers-and-sessions.md) | Music visualization contract and a worked “watch with friends” feasibility design |
| [Delivery plan](06-delivery-plan.md) | Current source map, implementation order, acceptance criteria, tradeoffs and decisions to validate |

## Requirements mapped to the design

| Requested behavior | Proposed mechanism | Boundary |
| --- | --- | --- |
| Skin/theme the redesign | `theme` contribution with semantic tokens and approved assets | Cannot change layout hierarchy, routes, control meanings or UI hooks |
| Multiple metadata providers | `metadata.provider` contributions registered by feature and media kind | Host owns identity verification, selection and persistence |
| Prefer one provider then another | Per-feature ordered provider profiles, language overrides and bounded fallback | Provider order never overrides conflicting identity evidence |
| Subtitles and lyrics | Searchable asset candidates and host-managed download/attachment | Search and preview do not attach tracks or overwrite files |
| Movie, episode, podcast, music and other metadata | Versioned typed records, scoped external IDs, field provenance and negotiated metadata domains | Unknown custom data stays namespaced until a host contract supports it |
| Find/enqueue/stream services | `discovery.source`, lazy browse/search and expiring playable resolution | Browsing does not mutate the queue |
| Populate playlists/queues | Explicit user actions or a separate session-scoped automation grant | Mutations use validated queue transactions |
| Add sections and buttons | Versioned slot registry and namespaced commands | Only hooks published by the host accept contributions |
| Add new screens/modals | Namespaced routes and typed modal requests | Host owns shell, navigation, focus, placement and modal lifecycle |
| Replace a screen/modal/button | Explicitly replaceable surface/action contracts and a user-selected provider | One active replacement; built-in recovery always remains available |
| Music visualizers | `visualizer` contribution using a bounded visual scene and host FFT/waveform data | No add-on execution on playback/render-critical threads |
| General extensions and watch parties | Events, commands, scoped storage, timers and brokered networking | Playback authority and session consent are explicit and revocable |
| GitHub and other Git repositories | HTTPS Git transport; exact tag resolved to immutable object IDs and a package digest | Branch tracking is development-only; moved tags are rejected |
| Shared components that follow themes | Host-rendered `vlc.ui` vocabulary backed by redesign components | Internal QML files are not the public API |
| Consistent settings/account setup | Host-rendered schema forms, staged save/cancel, account broker and secret references | Add-ons cannot read arbitrary core settings or another add-on's secrets |

## Important product choices

1. **An exact tag is a pin.** Installing `v1.0.0` does not silently turn into `v1.1.0`. Users may separately choose compatible update notifications or automatic compatible updates. Every activated version remains locked to a commit and content digest.
2. **Rank by task.** Subtitle search, episode identity, episode artwork, podcast catalogue metadata, music metadata and lyrics can each have different provider orders. Do not put a single global priority slider on an add-on.
3. **Installation does not replace the interface.** A home replacement is offered in the relevant appearance/navigation setting. A new player command is offered in customization. The user chooses activation and placement.
4. **Use the same UI controls everywhere.** Generated settings and extension views share the host's theme, typography, accessibility and responsive rules. An author specifies “primary button” or “media list,” rather than copying app colors and QML.
5. **Retain local ownership.** User corrections, chosen artwork and manually selected tracks outrank automatic providers. Add-on failure cannot strand the user on a broken home screen or stop playback.
6. **Start with a narrow usable SDK.** Ship one provider, one theme and one discovery source with UI before declaring v1 stable. Watch parties and complex visualizers have explicit reserved contracts and later implementation gates.

## Scope of this proposal

The first host is this fork's redesigned Qt desktop interface. Platform support must be proven for the fork's actual Windows, Linux and macOS build targets. Other VLC interfaces, mobile applications and libVLC embedders would need their own host adapters and capability profiles; this document does not imply those adapters already exist.

The files in this directory are a reviewable design and implementation roadmap, not a shipped SDK, executable reference implementation or promise of a delivery date. Normative words such as “must” state proposed v1 invariants. Numeric budgets are starting values for measurement, called out where used.
