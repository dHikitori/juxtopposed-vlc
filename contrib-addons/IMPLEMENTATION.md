# Implementation status

The architecture is being implemented against the full scope in this directory. This is a progress/evidence record, not a claim that all contracts are complete or that SDK v1 is stable. Updated 27 October 2026.

## Verified progress

| Area | Current implementation and evidence |
| --- | --- |
| Portable worker | `modules/gui/qt/addons/worker`: pinned Wasmtime 50, no WASI imports, bounded dispatch/memory/messages, fuel interruption, process IPC, additional Linux process restrictions. Five Rust runtime tests pass. |
| Reference provider | `examples/thesubtitledb`: portable Rust/Wasm package with individual API-key settings, typed subtitle lookup, scoped episode support, language/pagination and asset resolution. Thirteen provider unit tests and three real-worker protocol fixtures pass. |
| Real service exercise | The native host probe successfully used the explicitly supplied user key in temporary session storage: four candidates and a validated 40,610-byte subtitle download. The key was never written to source/configuration, passed to the worker, or printed. The API-to-file-server redirect went through the broker. |
| Host package/permission foundation | Strict manifests, source/tag/content identity, bounded isolated Git object fetch, immutable package snapshots, enable/disable, install/rollback and settings revision checks. Developer snapshots are explicitly distinguished from actual Git releases. Native package tests pass. |
| Broker and credentials | Approved origin/method/credential bindings, public-address checks and connection pinning, manual redirects and cross-origin credential stripping, cancellation/quotas, OS keychain integration with explicit session-only choice. Native broker/runtime tests pass. |
| Metadata coordinator | Ordered fallback, partial enrichment/provenance and host identity checks, aggregate search, protected fields, scoped cache partitions and independent consumer cancellation. Native coordinator tests pass. |
| Host UI vocabulary | Trusted declarative renderer, generated staged settings, protected secret keep/replace/delete, provider ordering, and validated theme snapshot helper. Focused QML tests pass. |

Native contract tests are available through the standalone `modules/gui/qt/addons/CMakeLists.txt`. Its four suites passed together with the actual worker executable. `tests/addon_provider_probe.cpp` is an explicit live probe taking the key on stdin; it is never automatically run by the normal test suite.

Meaningful commits so far:

- `8e8b270c67`: bounded portable Wasm worker.
- `c3b9895aa9`: portable TheSubtitleDB reference provider and fixtures.

Additional source work may be present in the checkout; verify its current tests/build rather than treating an in-progress file as finished functionality.

## Work in progress and outstanding completion gates

The following remain in scope. None is waived by the successful provider probe.

- Complete normal VLC build/packaging integration, supported-target builds and the isolated runtime dependency/runtime profile gates.
- Finish and verify real subtitle-screen integration, including native OpenSubtitles and portable providers in one saved ranking, manual correction, batch targets, downloads, attachment and cancellation.
- Finish the core-owned package install/review/update UI, tagged repository update selection, retained generations, permission changes, migration execution, trust/signature/catalogue policy, and recovery/safe mode.
- Propagate one resolved theme/token snapshot into the actual QML, QWidget and menu adapters; preview-only token resolution is insufficient.
- Implement and mount the published additive UI hooks, routes/modals, explicitly selected content/action replacements, customization persistence and built-in recovery.
- Route remaining direct remote metadata paths (preparser/library/episode services) through the single coordinator and retire competing legacy online traversal on that path.
- Implement service-discovery browse/search/subscriptions, stable source references, expiring playable resolution with end-to-end stream policy, and queue/playlist transactions.
- Implement the bounded portable visual-scene/audio-analysis controller and its lifecycle, quality and overload behavior.
- Implement session registrations, timeline observations, WSS/control leases and arbitration, with the two-player synchronization feasibility exercise. A real watch-party service is not required by the original request.
- Finish schema/SDK/author tooling and conformance for all supported profiles, update documentation to the implemented contracts, and perform requirement-by-requirement completion verification.

Existing local identity matching, artwork ownership and user corrections must survive cutover. Temporary native adapters are implementation infrastructure, not compatibility support for legacy third-party add-ons. Do not mark the overall goal complete while the items above are missing or only indirectly tested.
