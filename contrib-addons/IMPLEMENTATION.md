# Implementation status

This file gives the maturity of each add-on requirement and points to its
evidence. Updated 30 October 2026, against `addons` after `dd43bfd9b8`.

The full scope in this directory remains the objective. The
[continuation plan](07-continuation-plan.md) gives the order of work. The
[completion backlog](completion-backlog.md) gives the detailed next step for
the larger open rows. Per-step records, with their test counts and limits,
are in [records/](records/).

## Maturity levels

| Level | Meaning |
| --- | --- |
| designed | The architecture describes it. There is no implementation, or only a stub. |
| component-tested | Implemented. Focused tests pass. The app does not construct it, or it is constructed but off. |
| mounted | The app constructs and uses it in source, and its object code compiles. No full app run of current source checks it. |
| app-verified (Linux) | A scripted full Linux app run of current source checks it. |
| platform-verified | Full app runs check it on Linux, Windows and macOS. |

**No row is app-verified yet.** The [Linux smoke test](records/app-smoke-linux.md)
starts the full app with every current owner mounted, prepares the bundled
add-ons, plays a local file, stays idle and quits, in one window and in two.
It does not yet exercise any one requirement end to end, so it does not raise
a level by itself. Earlier app runs at older commits are listed as evidence.
No row is platform-verified: there is no macOS build.

## How to run the evidence

Each add-on test has one label in [`test/addon-tests.txt`](../test/addon-tests.txt):
`profile`, `metadata`, `source`, `ui`, `session`, `visualizer`, or `app` for
the full app smoke tests. Source `.design-reference/native-build/env.sh`
first on this machine. Run one label with one command:

- meson (VLC build): `meson test -C <build> --suite <label>`
- standalone CMake (`modules/gui/qt/addons`): `ctest --test-dir <build> -L <label>`

`python3 test/check_addon_tests.py` checks that Automake, meson and CMake
register the tests that the table lists. It runs as the `addon_test_registry`
test in meson and CMake. Tests marked *(cmake)* below are registered only in
the standalone CMake project. Their fixtures need it.

`python3 contrib-addons/check-links.py` checks the relative links in this
directory.

### Label runs

Add one row for each label run that a record or a decision relies on.

| Commit | Label | Result | Platform and build |
| --- | --- | --- | --- |
| `81e29bc6d0` | `profile` | 21 tests, 720 cases. 0 failed, 0 skipped. | Linux, meson debug (`build-redesign`), Docker cross-user cases included |
| `375f9dba89` | `profile` | 21 tests, 720 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `375f9dba89` | `metadata` | 50 tests, 727 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `375f9dba89` | `source` | 22 tests, 289 cases. 0 failed, 1 skipped (needs a harness-only wrapper). | Linux, meson debug |
| `375f9dba89` | `ui` | 7 tests, 170 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `375f9dba89` | `session` | 4 tests, 28 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `375f9dba89` | `visualizer` | 12 tests, 116 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `375f9dba89` | `app` | 2 tests (one and two windows). 0 failed. | Linux, meson debug, offscreen Qt, Mesa llvmpipe |
| `7ddc4b28ee` | `source` | 22 tests, 291 cases. 0 failed, 1 skipped. | Linux, meson debug |
| `7ddc4b28ee` | `ui` | 7 tests, 170 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `7ddc4b28ee` | `session` | 4 tests, 28 cases. 0 failed, 0 skipped. | Linux, meson debug |
| `7ddc4b28ee` | `app` | 3 tests (one window, two windows, driver). 0 failed. | Linux, meson debug with `-Daddon-test-driver=true`, offscreen Qt, Mesa llvmpipe |

## Cross-cutting gates

| Gate | State | Evidence |
| --- | --- | --- |
| Full Linux app build and scripted smoke test | Builds and runs. Start, bundled preparation, playback, idle and quit pass in one and two windows. With the test driver (`-Daddon-test-driver=true`), the Extensions list and a discovery source through the real worker pass, and the worker numbers are recorded. Radio Browser playback and the prepared-invocation cost (F1) are open. Quit crashes in the WSL D3D12 GPU driver; the test uses llvmpipe. | [Linux smoke test](records/app-smoke-linux.md) |
| Performance: no idle work, no package I/O in basic UI, worker cold and repeated cost | Open. Focused idle regressions pass. No full app measurement. Plan findings F1–F3 apply. | [performance contract](performance-contract.md) |
| Windows GUI checks of the current review package (`74ae4267fc`) | Open, with the user. | [Windows artwork build](records/windows-artwork-build-2026-10-30.md) |
| Windows cursor report | Open, with the user. Plan F4 gives the hypothesis and the live check. | [cursor report](records/windows-cursor-feedback.md) |
| Multi-window profile authority before native owner activation | Open. Plan Step 2. | [profile coordination](profile-authority-coordination.md) |
| macOS build and runtime | Open. Never built. Plan Step 7. | — |
| ASD-STE100 audit of new app English | Open. Plan Step 7. | — |

## Runtime and host services (`01-architecture.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Portable Wasm worker: no WASI, bounded memory, fuel, messages and IPC | mounted | `profile`: `test_qt_addon_runtime`; worker Rust tests; [Windows worker](records/windows-local-build.md) | Precompiled modules and warm worker (F1). Phase 0 start, footprint and hostile-computation numbers. |
| Worker lifetime: cancellation, restart namespaces, revocation before withdrawal | mounted | `profile`: `test_qt_addon_runtime`, `test_qt_addon_broker`, `test_qt_addon_host_services`; [worker retirement](worker-retirement-contract.md) | Full app check. |
| Crash backoff, quarantine and per-contribution health (`01-architecture.md:132,136`) | designed | — | Crash ledger, backoff, quarantine, health row (F3). |
| Host API dispatch, operation scopes and invocation guards | mounted | `profile`: `test_qt_addon_broker`, `test_qt_addon_invocation_guard` *(cmake)*; [request guards](records/native-invocation-guard.md) | Full app check. |
| Credentials, origin scopes and public HTTPS network policy | mounted | `profile`: `test_qt_addon_runtime`, `test_qt_addon_broker`, `test_qt_addon_native_authority` | Credential stores on Windows and macOS. |
| Guest private storage, quotas, account partitions and migrations | mounted | `profile`: `test_qt_addon_storage`; [storage contract](guest-storage-contract.md) | General blob namespace. |
| Account scope and native authority snapshots | mounted | `profile`: `test_qt_addon_native_authority`, `test_qt_addon_store_authority`; [authority snapshots](records/native-authority-snapshots.md), [store epoch](records/store-authority-epoch.md) | Full app check. |
| Joined app work and the shutdown coordinator | mounted | `profile`: `test_qt_addon_app_shutdown`, `test_qt_addon_repository_jobs`; `metadata`: `test_qt_ml_joined_runner`; [shutdown contract](app-shutdown-contract.md), [Qt work](records/qt-work-lifetime.md) | Full app shutdown with all owners. |
| Several windows share one profile, with live revocation (decided 30 October 2026) | mounted | `profile`: `test_qt_addon_store_process_fence`, `test_qt_addon_store_profile_generation`, `test_qt_addon_store_profile_protocol`, `test_qt_addon_store_profile_peer`, `test_qt_addon_store_profile_socket`, `test_qt_addon_store_profile_coordinator`, `test_qt_addon_store_profile_client`; [coordination](profile-authority-coordination.md), [protocol](profile-control-protocol.md), [native peer](records/profile-native-peer.md), [socket](records/profile-socket.md), [coordinator](records/profile-coordinator.md), [client](records/profile-client.md) | write permit, handover, app mount, crash matrix (Step 2). |
| Guarded profile writes: settings, package state, install, removal, bundles | mounted | `profile`: `test_qt_addon_store_profile_settings`, `test_qt_addon_store_profile_packages`, `test_qt_addon_store_profile_lifecycle`; [writer scope](records/profile-writer-scope.md), [settings](records/profile-store-settings.md), [package writes](records/profile-package-writes.md), [lifecycle](records/profile-lifecycle-writes.md) | Writes through permits in the app (Step 2.6). Ordinary writes refuse in a shared profile: [write permit](records/profile-write-permit.md). |
| Guest library, queue and selected-file fingerprint adapters | designed | Operation names only. [Backlog](completion-backlog.md) | Read-only projections first (Step 6). |

## Packages and updates (`02-packages-and-updates.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Manifests, Git fetch (SHA-1 and SHA-256), immutable releases, permissions, enable, update and rollback | mounted | `profile`: `test_qt_addon_packages`, `test_qt_addon_git_objects` *(cmake)*, `test_qt_addon_repository_updates`; [Git formats](git-object-format-contract.md), [repository workers](records/repository-worker-lifetime.md). Windows app run at `2a689c3a11` installed seven bundles. | Full app check. Supported-platform Git checks. |
| App distribution bundles, recovery, removal with data choice, safe mode | mounted | `profile`: `test_qt_addon_distribution_bootstrap`; `ui`: `test_qt_addon_display_integration`; [recovery contract](recovery-and-distribution-contract.md) | Full app check. Windows and macOS credential removal. |
| Built-in provider IDs and reserved host namespaces | mounted | `profile`: `test_qt_addon_runtime`; [namespaces](host-provider-namespaces.md), [ID migration](records/builtin-provider-id-migration.md) | — |
| Offline self-test and activation health transaction | designed | [Backlog](completion-backlog.md) | Step 6. |
| Signed curated install path (optional) | designed | Projections report `publisherVerified=false`. | Trust anchor and catalogue verifier. |
| Normative schemas, package validator, generated SDK types, templates, preview tool | designed | Session request and completion policy schemas only (`sdk/`). | Step 7. |

## Metadata providers (`03-providers-and-discovery.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Metadata coordinator: order, patches, provenance, identity checks, cache partitions | mounted | `metadata`: `test_qt_addon_metadata`, `test_qt_addon_metadata_catalogue`; [catalogue](records/metadata-catalogue.md), [identity evidence](records/metadata-identity-evidence.md) | Full app check. |
| Subtitle providers: native and portable in one order, handles through Save | mounted | `test_qt_subtitle_sources` (`qt` suite); [responsiveness](records/subtitle-online-responsiveness.md), [Windows subtitle build](records/windows-subtitle-build-2026-10-29.md) | Subtitle53 in the full app. |
| Prepared portable invocations for metadata and artwork | mounted | `metadata`: `test_qt_addon_prepared_metadata_invocation`, `test_qt_addon_metadata_attempt`, `test_qt_addon_artwork_attempt_plan`, `test_qt_addon_artwork_attempt_aggregate`; [prepared invocations](records/prepared-metadata-invocations.md), [image work](records/portable-artwork-background.md), [serial runner](records/artwork-serial-runner.md) | New process and compile per call (F1). 25 ms cancellation poll (F2). |
| Identification and explicit identity choice | component-tested | `metadata`: `test_qt_addon_metadata_choices`, `test_qt_addon_metadata_service`; [identify service](records/metadata-identify-service.md), [choice store](records/metadata-identity-choice-store.md) | Dialog and accepted revision (Step 6). |
| Result retention and expiry | designed | Fixed 15 s and 60 s TTLs in the coordinator. | Step 6. |
| Native metadata transactions: Apply tokens, revisions, preparsers, local parse | mounted | `metadata`: `test_src_misc_metadata_*`, `test_src_preparser_*`; [external preparser](native-external-preparser-contract.md), [local parse](native-prepared-local-parse-contract.md), [pipeline](records/native-metadata-pipeline.md) | Full app check. |
| Native metadata provider owner and family selection | component-tested (activation off) | `metadata`: `test_qt_addon_native_metadata_owner`, `test_qt_addon_native_provider_context`, `test_src_misc_metadata_owner`; [owner](native-metadata-owner-contract.md), [families](native-provider-family-contract.md), [barrier](records/metadata-registration-barrier.md) | Activation after Step 2 (Step 4). |
| Native artwork outcomes: fetched, not-found, positive authority, approval | component-tested | `metadata`: `test_qt_addon_artwork_attempt_keeper`, `test_qt_addon_native_artwork_assets`, `test_src_misc_metadata_artwork*`, `test_src_input_artwork_*`; [outcome](native-artwork-outcome-contract.md), [approval](native-artwork-approval-contract.md), [native positive](records/artwork-native-positive-authority.md), [completion](records/native-artwork-completion.md) | Owner activation. |
| Native artwork materialization and user image choices | mounted | `metadata`: `test_qt_addon_artwork_image`, `test_qt_artwork_selection`, `test_qt_artwork_selected_image`; [materializer](native-artwork-materializer-contract.md), [choices](native-artwork-selection-contract.md) | Per-platform image reader checks. |
| Artwork packages (MusicBrainz, Last.fm, French TV, image search) in the distribution | mounted | `contrib-addons/examples/*/tests`; `profile`: `test_qt_addon_distribution_bootstrap`; [migration](records/metadata-provider-migration.md) | Live evidence for image search and old French TV URLs. |
| Preserved-provider parity and removal of the legacy online bypass | designed | [Migration](records/metadata-provider-migration.md) | Parity matrix per family (Step 4). |
| Episode consumer parity (TVMaze) | component-tested | `tests/build_addon_episode_tests.py`. Not registered in a build system. | Register the episode and artwork consumer tests. |

## Discovery and playback sources (`03-providers-and-discovery.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Discovery browser: roots, browse, search, cursors, stable admissions | mounted | `source`: `test_qt_addon_discovery`, `test_qt_addon_discovery_service`, `test_qt_addon_source_admissions`, `test_qt_addon_queue_transactions`; `app`: `addon_app_smoke_driver` opens, browses and searches the fixture source in the full app | Playback and queue actions in the full app. |
| Native source route: owner, resolver, consent, contained stream proxy, input policy | mounted | `source`: `test_qt_addon_discovery_source_owner`, `test_qt_addon_native_source_resolver`, `test_qt_addon_source_consent*`, `test_qt_addon_stream_*`, `test_src_input_*`; [source owner](discovery-source-owner-contract.md), [resolver](native-source-resolver-contract.md), [consent](native-source-consent-contract.md), [stream proxy](records/native-contained-stream-proxy.md) | Full app playback, revoke and quit. |
| Radio Browser reference source | mounted | `source`: `addon_radio_browser_worker`; [record](records/radio-browser-source.md). A Linux live probe decoded 30 HLS blocks. | Full app and other platforms. |
| Adaptive manifests: HLS and static DASH | component-tested | `source`: `test_qt_addon_stream_manifest`; [DASH](native-dash-manifest-contract.md), [manifests](native-stream-manifest-contract.md) | Actual adaptive playback in the app. |
| Native queue and library transactions | mounted | `source`: `test_qt_addon_native_queue_store`; `metadata`: `test_qt_addon_native_library_store` *(cmake)*, `test_src_misc_addon_medialibrary`; [playlist CAS](records/native-playlist-cas.md), [library targets](native-library-target-contract.md) | Full app check. |
| Subscriptions and continuous queue population | designed | `subscribe` returns unsupported. [Backlog](completion-backlog.md) | Step 6. |

## UI, themes and configuration (`04-ui-themes-and-configuration.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Themes and contrast | mounted | `ui`: `test_qt_addon_theme`. Earlier Linux app build passed. | Full app check. |
| UI hooks: interface sessions, commands, home replacement, typed modal shells | mounted | `ui`: `test_qt_addon_ui_registry`; [review record](records/pending-implementation-review.md) | Full app check. |
| Static UI images | mounted | `ui`: `test_qt_addon_ui_assets` | Full app check. |
| Extensions list, generated settings and install approval | mounted | `ui`: `test_qt_addon_catalogue_view`, `test_qt_addon_display_catalogue`, `test_qt_addon_package_summaries`; [Extensions](records/extensions-settings-ui.md), [package list](records/package-summary-ui.md) | Windows Extensions click (user). |
| File, folder and account pickers; account recipes; settings layers | designed | Controls emit signals with no consumer. [Backlog](completion-backlog.md) | Step 6. |
| Theme and UI preview harness | designed | — | Step 7. |

## Visualizers and sessions (`05-visualizers-and-sessions.md`)

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Audio analysis outside the audio callback | mounted | `visualizer`: `test_qt_addon_audio_analysis`, `test_qt_addon_native_player_analysis`, `test_src_player_audio_samples_lifecycle` | Full app check. |
| Visual scenes: worker, renderer, native clock, window lifecycle, parameters | mounted | `visualizer`: all 12 tests | Physical GPU loss. Supported rendering backends. |
| Session authority: leases, observations, rate, local priority | component-tested | `session`: `test_qt_addon_sessions`, `test_src_player_addon_session*`, `test_qt_addon_native_session_player` *(cmake)*; [session host](native-session-host-contract.md) | Full app link. |
| Session WSS transport | component-tested | `session`: `test_qt_addon_websocket_codec`, `test_qt_addon_session_websocket` *(cmake)*; [wire format](native-websocket-wire-contract.md), [transport audit](records/native-session-transport-audit.md) | Full app link. |
| Session app owners and host panel | mounted | `session`: `test_qt_addon_session_host`, `test_qt_addon_session_ui`, `test_qt_addon_player_sessions` *(cmake)*; [app owners](native-session-app-contract.md), [host panel](records/session-host-ui.md) | Full app link and compiled QML. |
| Two-player drift and rate experiment | designed | One seek-offset case only. | Only if v1 claims coordinated playback. |

## Windows-specific repairs

| Requirement | Maturity | Evidence | Open |
| --- | --- | --- | --- |
| Remote-file read cancellation and playback Stop before user-quit cleanup | mounted | Windows app at `fd3fed4173`; [network-file Close](records/windows-network-file-close.md), [close ordering](records/windows-close-ordering.md) | The user's Close retest. |
| Add-on loader repair | mounted | [loader repair](records/windows-addon-loader-repair.md) | Included in the GUI checks. |

## Rules that stay in force

- Keep each core provider until its add-on replacement passes the parity
  checks. Preserve local tags, sidecars, attachments, local artwork and user
  corrections.
- A shared metadata profile must not return silently to a direct HTTP or Lua
  provider path. Old and new remote paths must not run at the same time.
- Keep embedded native HTTP and LAN artwork separate from add-on public HTTPS
  assets, and respect the user's network preference.
- Do not widen the public HTTPS profile. Plain HTTP, LAN grants, inbound
  sockets, WebRTC, a watch-party service, sidecar write-back and
  cross-add-on imports are later work. See the [backlog](completion-backlog.md).
- `tests/addon_provider_probe.cpp` is an explicit live test. It reads the
  subtitle key on stdin. Ordinary test runs do not make that request.

The test counts and history from the earlier narrative are in the records and
in `git log`.
