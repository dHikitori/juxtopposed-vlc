# Delivery plan and architectural review

This is the implementation roadmap. See [implementation status](IMPLEMENTATION.md) for current authoritative progress and verification; future acceptance criteria below are not claims that the corresponding functionality has already shipped.

## Current repository evidence

The redesign already offers useful internal boundaries. Reuse their behavior and tests behind the new contracts, without treating internal Qt objects or legacy Lua APIs as the public SDK.

| Area and current source | What exists | Proposed adaptation |
| --- | --- | --- |
| [include/vlc_addons.h](../include/vlc_addons.h), especially types/entry fields around lines 31–110 | One primary add-on type, archive/source/version fields and file list | Replace public package model with composed contributions, immutable source lock and lifecycle state |
| [src/misc/addons.c](../src/misc/addons.c), [modules/gui/qt/network/addonsmodel.cpp](../modules/gui/qt/network/addonsmodel.cpp) | Existing catalog/install model | New manager/backend for trust, grants, setup, disable/update/rollback and contribution health |
| [modules/lua/vlc.c](../modules/lua/vlc.c), around lines 179–231; [modules/lua/meta.c](../modules/lua/meta.c), around lines 197–274 | Directory-scanned reader/fetcher/art entrypoints, filename order and stop-on-success | Retire legacy online traversal on the new path; typed provider coordinator owns ranking and completeness |
| [modules/lua/extension.h](../modules/lua/extension.h), line 32; [extension.c](../modules/lua/extension.c), around line 734 | Functional event/menu flags plus broad application/IO APIs | New broker capabilities and explicit authority; no compatibility shim into unrestricted APIs |
| [src/preparser/fetcher.c](../src/preparser/fetcher.c), around lines 243–291 | Music-oriented completeness check and metadata/art module traversal | Media-kind/consumer-specific completeness and one coordinated remote path |
| [modules/gui/qt/util/media_identity.hpp](../modules/gui/qt/util/media_identity.hpp) and [doc/subtitle-identification.md](../doc/subtitle-identification.md) | Shared identity, provenance/conflicts and subtitle/artwork policies | Formalize a host-neutral transport schema and keep matching policy host-owned |
| [modules/gui/qt/util/subtitle_source_service.cpp](../modules/gui/qt/util/subtitle_source_service.cpp) | OpenSubtitles requests, matching, cancellation, caching and downloads | Start as a trusted provider adapter; move protocol-specific code into a reference add-on after broker parity |
| [modules/gui/qt/player/episode_metadata.cpp](../modules/gui/qt/player/episode_metadata.cpp), around lines 231–268 | Direct TVMaze HTTP behavior | Provider registration routed through the same coordinator |
| [modules/gui/qt/player/player_controller.cpp](../modules/gui/qt/player/player_controller.cpp), around lines 3172–3247 | Background metadata uses a copied input item and checks generations/user changes | Preserve and generalize transactional commit and stale-result rejection |
| [modules/gui/qt/player/series_model.cpp](../modules/gui/qt/player/series_model.cpp), [networkplaylistmodel.cpp](../modules/gui/qt/network/networkplaylistmodel.cpp) | Local series/podcast browsing and feed projections | Trusted discovery adapters sharing stable item/identity contracts |
| [include/vlc_services_discovery.h](../include/vlc_services_discovery.h), around lines 93–116 | Hierarchical items and native search capability | Explicit browse/search/resolve/subscription capability model |
| [RedesignTheme.qml](../modules/gui/qt/player/qml/redesign/RedesignTheme.qml), lines 17–44; [redesign_widget_theme.cpp](../modules/gui/qt/util/redesign_widget_theme.cpp), lines 33–69 | Separate hardcoded QML/QWidget palettes | Single semantic token service and adapters for QML, widgets and menus |
| [RedesignButton.qml](../modules/gui/qt/player/qml/redesign/RedesignButton.qml), around lines 49–71 | Shared theme-aware button/focus/accessibility behavior | Trusted implementation of stable `vlc.ui` components |
| [RedesignPlayerView.qml](../modules/gui/qt/player/qml/redesign/RedesignPlayerView.qml), lines 106–116 | Internal component slots for video/library/queue/settings/episodes | Insert the host renderer at selected boundaries; do not export raw component injection |
| [RedesignHomeView.qml](../modules/gui/qt/player/qml/redesign/RedesignHomeView.qml), lines 11–19 | Section models and semantic actions | First additive UI hook and first explicitly replaceable content surface |
| [RedesignLibraryNavigation.qml](../modules/gui/qt/player/qml/redesign/RedesignLibraryNavigation.qml), around lines 87–99 | Fixed categories, labels and icons | Stable route IDs and host-owned navigation entries |
| [RedesignPlayerLayout.qml](../modules/gui/qt/player/qml/redesign/RedesignPlayerLayout.qml), lines 12–34; [RedesignExtraControl.qml](../modules/gui/qt/player/qml/redesign/RedesignExtraControl.qml) | Bounded customization zones, validated controls and spec/state/action separation | Namespaced add-on commands in the palette, with host placement and persistence |
| [RedesignPreferenceRow.qml](../modules/gui/qt/player/qml/redesign/RedesignPreferenceRow.qml), [redesign_preferences.hpp](../modules/gui/qt/dialogs/preferences/redesign_preferences.hpp), lines 77–88 | Describe/value/stage/reset/discard/save pattern | Namespaced generated settings backend with secrets/accounts and staged migrations |
| [RedesignSubtitleSourcePreferences.qml](../modules/gui/qt/player/qml/redesign/RedesignSubtitleSourcePreferences.qml), [RedesignSubtitleProviderSetup.qml](../modules/gui/qt/player/qml/redesign/RedesignSubtitleProviderSetup.qml) | Provider-specific OpenSubtitles configuration | Registry-derived provider cards and per-feature ordering in the same core preferences |
| [RedesignAddons.qml](../modules/gui/qt/player/qml/redesign/RedesignAddons.qml), lines 17–29 and 109–116 | Add-on list, install/remove and discovery actions | Add source/tag/commit, compatibility, permission delta, setup/error and rollback views |
| [include/vlc_player.h](../include/vlc_player.h), around lines 2354, 2864 and 3457; [include/vlc_playlist.h](../include/vlc_playlist.h), around line 191 | Audio observation, locked callbacks and timer timeline data | Copy bounded snapshots, dispatch after locks; use scoped asynchronous command adapters |
| [modules/visualization/goom.c](../modules/visualization/goom.c), around lines 362–363; [projectm.cpp](../modules/visualization/projectm.cpp) | Native visualizers and clocked frame scheduling | Built-in adapters plus a new portable scene contract; no automatic binary compatibility |

Line numbers are observations from this design review and may move. The linked files and descriptions identify the enduring integration areas. This document does not audit unrelated existing work in the checkout.

## Recommended sequence

### Phase 0: prove the foundations

Build isolated prototypes before freezing public schemas:

- Run a tiny portable Wasm provider through a broker job on the actual supported Windows, Linux and macOS targets. Measure process startup, footprint, cross-compilation, runtime dependency packaging and interruption of hostile/infinite guest computation.
- Demonstrate no ambient IO/WASI imports, invalid-handle rejection, message/memory quotas, worker termination and late-completion rejection. Establish platform OS restrictions and document their actual guarantees.
- Render one declarative form and media list through existing redesign controls; switch theme live and exercise focus/RTL/scaling. No untrusted QML evaluation.
- Resolve one HTTPS playable descriptor through the native playback path with enforced redirects/nested segment/key destinations. If policy cannot be enforced, keep third-party stream resolution unavailable until the adapter exists.
- Prove the generated-settings/account recipe can satisfy the real OpenSubtitles protocol without handing arbitrary raw tokens to guest/UI state.

Exit: recorded benchmark results and integration decisions, plus a minimal reference add-on running in the intended isolation model. If Wasmtime packaging/footprint proves unsuitable, evaluate another bounded runtime behind the same message contracts; do not fall back to unrestricted main-engine QML.

### Phase 1: package lifecycle, themes and settings

Implement manifest validation, Git resolution/lock/store, source trust, compatibility negotiation, grants, disable/safe mode and install/update/rollback transactions. Add generated settings, protected secret editor and host account recipes. Build the token service across QML/QWidget/menu adapters, a theme preview and one data-only reference theme. Update the add-on manager UI.

Exit: install a pinned tagged theme/provider skeleton, reject moved tags and permission expansion, preserve settings through a failed update and recover from a broken selected theme without starting a worker.

### Phase 2: metadata end to end

Implement the shared identity transport, coordinator, profiles, provenance/cache, broker networking and asset validation. Adapt the existing subtitle and episode services, then exercise a separately distributed reference provider. Route all automatic remote paths, including preparser/library requests, through this owner; remove direct online bypasses on the new path. Preserve local tags, artwork discovery and explicit user edits.

Exit: preferred-provider/partial-result fallback for at least two providers, correct target association, one logical request across consumers, generated provider setup and host-managed download/attachment. The old and new remote paths must not run concurrently. Enable the new subsystem only after routing/grant enforcement is complete.

### Phase 3: discovery and bounded UI

Implement stable routes, contribution registry, core media models, browse/search/resolve, playlist source references and queue transactions. Publish initial additive hooks, custom route/modal contracts, command palette/customization support and the Home replacement setting with built-in fallback. Add a radio/podcast reference source with a standard settings/account card and themed views.

Exit: browse a source, enqueue a selection, reload a saved playlist, refresh an expiring source and recover from a removed/crashed add-on. Replace Home and one eligible modal/action while preserving Back, focus, draft state and core recovery. This phase depends on the stream-policy proof in Phase 0.

### Phase 4: visualization

Implement bounded audio-analysis transport, scene renderer, lifecycle/quality controller and built-in visualizer adapters. Ship several visually distinct reference scenes and validate low-resource behavior on each rendering backend.

Exit: visualizer overload/worker failure never blocks playback; seeking and track switching reject old data; reduced motion and GPU-loss fallback work. Keep arbitrary shaders/native DSP outside the portable profile.

### Phase 5: sessions and general behavior

Implement explicit session registration, observable timeline snapshots, playback leases/arbitration, brokered WSS and rate/deadline handling. Prove the API with a small two-player synchronization experiment. The real watch-party service remains a later extension; the prototype establishes that the public contracts are sufficient.

Exit: duplicate/stale commands, local intervention, disconnect, lease expiry and update during a session behave as specified. No unrestricted “general” API is introduced to make the demo work.

### Phase 6: stabilize and publish v1

Freeze complete manifest/transport/UI/feature schemas and the oldest supported host profile. Publish generated SDK types, author templates, fixture harness, package validator, theme/UI preview and signed catalogue policies. Run conformance across reference add-ons and supported platforms before making compatibility promises.

Exit: a theme, multi-provider metadata setup, discovery source with custom UI, replacement Home/modal, portable visualizer and session prototype all use the same platform without privileged third-party escape hatches.

## Verification matrix for the implementation

These are future implementation acceptance criteria, not tests claimed to have run for this documentation change.

| Concern | Essential cases |
| --- | --- |
| Package supply | Moved tag, tag/manifest mismatch, invalid signature, source/ID collision, oversized repository, traversal/case collision, missing artifacts, forbidden runtime imports |
| Updates | Offline start, invalid candidate, new permission pending, failed migration, crash between data/pointer commits, rollback after later edits, secret reference preservation |
| Permission boundary | Forged/stale/cross-package handles, revoked grants, unauthorized redirects/private addresses, hidden core config access, denied optional permission and incompatible contract |
| Metadata | Per-feature ordering, partial field fallback, wrong entity, conflicting IDs/numbering, user edit during fetch, language-specific assets, ambiguity, quota/auth failures and one-consumer cancellation |
| Persistence | Attribution/retention, account disconnect, cache partitioning, profile change refresh, stale claims, uninstalled providers and local/user overrides |
| Assets/playback | Decompression/path attacks, invalid formats, nested stream URL escape, expired token refresh, unavailable source and credentials redaction |
| Queue | Stable-ID transactions, revision conflict, duplicate request, bounded continuous population, user entries preserved and cancellation after enqueue |
| UI/theme | Live theme switching, tokens across toolkits, narrow/enlarged/RTL layouts, keyboard/screen-reader behavior, required-secret keep/replace/delete and staged cancel |
| Replacements | Multiple candidates, explicit user selection, missing contract, crash/timeout, draft/focus restoration, no automatic duplicate command and reachable built-in recovery |
| Visualizer | Audio callback bounds, overload dropping, stale PTS/epoch, pause/minimize/seek/rate, reduced motion and graphics-device failure |
| Session | Local control priority, duplicate/stale commands, clock uncertainty, buffering/live/nonseekable media, disconnect, rate restoration and lease expiry |

Existing Qt tests around media identity, subtitle sources, series models, themes, add-ons, customization, dialogs and track assignment provide useful regression fixtures. Add new tests at contract/broker boundaries, including failure injection and parser fuzzing, rather than duplicating internal component implementations. Measure wrong automatic metadata assignments separately from missed matches.

## Decisions and tradeoffs

| Decision | Benefit | Cost / validation needed |
| --- | --- | --- |
| Portable Wasm + per-add-on worker | Explicit imports, enforceable guest execution limits, cross-platform package contents | Runtime footprint, author language ergonomics and target packaging must be measured |
| Host-rendered UI | Automatic theme/accessibility consistency and bounded integration | Less arbitrary UI freedom; vocabulary must cover real extensions before v1 |
| Token-only themes | Preserve the redesign and dependable extension layout | Full skins2-style control replacement is intentionally outside theme capability |
| Committed release artifacts in Git | Any HTTPS Git host, no local build scripts or runtime dependency fetch | Larger tagged repositories; transport quotas and author release tooling needed |
| Sequential automatic provider fallback | Predictable preference and lower request cost | Some latency; explicit aggregate search offers broader choice under limits |
| One remote metadata coordinator | Consistent grants, cache, matching and persistence | Requires core/preparser integration, not just a QML add-on screen |
| No cross-add-on runtime imports | Simple authority/versioning/rollback | Shared libraries must be bundled; optional interoperability uses host contracts |
| Portable visual scenes first | Diverse basic visualizers with bounded host execution | Complex shader ecosystems need a separately designed later profile |
| Session-scoped playback leases | Enables synchronization while preserving local control | Needs careful arbitration/timestamp handling and realistic precision expectations |

The architecture chooses defaults, rather than leaving runtime, UI freedom or update identity unspecified. Remaining implementation investigations are: runtime/library integration and supported target floor, exact CPU/GPU/IO budgets, stream-policy adapter feasibility, secret/account recipe coverage, final schema syntax, SDK language ergonomics and API support-window length. Treat these as explicit prototype gates before public v1, not reasons to weaken the boundaries opportunistically.

## Compatibility and rollout

Do not import or execute legacy add-on packages under the new manager. Show a clear new format/version boundary and keep legacy packages inactive on the redesigned add-on path. Native codecs, demuxers, parsers and built-in local discovery remain core implementation modules. Rollout must inventory all automatic online entry points and gate/remove their bypass behavior before advertising provider grants as comprehensive.

Preserve existing user-owned theme preferences, toolbar layouts, selected artwork, track assignments and metadata corrections where the new adapters can represent them. Provider-specific existing configuration should be migrated deliberately into a trusted built-in provider/account card; do not copy plaintext credentials into add-on settings. Because compatibility with old add-ons is not required, there is no reason to expose their broad APIs to new packages.

The initial Qt desktop release can report unavailable feature profiles on hosts without a UI/visualizer/session adapter. Do not claim native macOS-interface, mobile or arbitrary libVLC-host support merely because the metadata transport is host-neutral.

## External references checked for this proposal

- [Qt: Handling Untrusted Data](https://doc.qt.io/qt-6/untrusteddata.html) explains why shared QML scope is not an add-on privacy boundary.
- [Wasmtime: Security](https://docs.wasmtime.dev/security.html) describes explicit imports and the runtime sandbox model.
- [Wasmtime: Interrupting Wasm Execution](https://docs.wasmtime.dev/examples-interrupting-wasm.html) describes fuel/epoch mechanisms; host IO still needs separate bounds.
- [Git: git-tag](https://git-scm.com/docs/git-tag.html) documents annotated/signed tags, verification and force replacement; tags alone are not immutable locks.

These references support specific design constraints. They do not certify this proposed system or replace the platform prototypes and implementation review above.
