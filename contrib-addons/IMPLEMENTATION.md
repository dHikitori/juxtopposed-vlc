# Implementation status

This file records implementation progress and test evidence. Updated 27 October 2026.

The full scope in this directory remains the objective. SDK v1 still has open completion gates.

## Verified work

| Area | Implementation and evidence |
| --- | --- |
| Portable worker | Pinned Wasmtime 50; no WASI imports; bounded memory, dispatch, messages, and fuel; process IPC; additional Linux process restrictions. Five Rust tests pass. |
| Windows worker | Rust 1.97.0 built the Windows worker. All five runtime tests ran on Windows and passed. The real worker returned `auth_required` through IPC without a key. |
| TheSubtitleDB | Portable Rust/Wasm package; individual API-key settings; typed subtitle search; episode scope; language and page controls; asset resolution. Fourteen unit tests and three worker fixture tests pass. |
| Real subtitle service | The native search and Save path returned four candidates and a 40,610-byte local subtitle through the host broker. The test used the supplied key in temporary session storage. No key entered source, saved settings, worker messages, or logs. |
| Package host | Strict manifests; repository, tag, and content identity; isolated Git object fetch; immutable release snapshots; permission approval; enable, disable, update, and rollback. Native tests pass. |
| Credentials and network | OS credential store with an explicit temporary-storage option; exact origin and method scopes; DNS and public-address checks; pinned connections; redirect checks; credential removal on cross-origin redirects. Native tests pass. |
| Metadata coordinator | Ordered providers; partial field patches; provenance; strong identity checks; protected fields; bounded cache partitions; independent cancellation. Native tests pass. |
| Subtitle integration | Native OpenSubtitles and portable providers use one provider order. Host handles resolve through the normal Save path. The latest native suite passes 53 cases. |
| Qt build and release | The normal Linux Qt target passed after the first UI integration. The release suite passed 98 tests with one existing skip. A relocated Linux worker passed the release smoke test. |
| Settings and install UI | Host-rendered package approval; generated settings; secret keep, replace, and delete actions; provider order; subtitle preparation and cancellation. Focused QML tests pass. |
| Native metadata bridge | A Qt-free asynchronous bridge copies requests and native field patches. The C suite passes cancellation/result races, reentrant close, and callback lifetime cases. All 14 symbols have core exports. |
| Session authority | Exclusive control leases; typed observations; media and revision checks; duplicate rejection; deadlines; local user priority; owned rate restoration. Nine focused cases pass. Native player and relay integration remain open. |
| Audio analysis | The callback copies bounded samples without allocation or locks. A separate worker calculates spectrum, waveform, and levels. Six cases pass for formats, time, media changes, and overload. |
| Visual scenes | Trusted spectrum, waveform, spectrogram, particle, gradient, group, texture, and feedback primitives. Seven cases pass for limits, output, media clock, reduced motion, and asset control. App integration is under test. |
| Host API dispatch | A closed operation map; permission and operation scopes; request limits; cancellation; handler lifetime; thread-safe completion. Nine focused cases pass. Individual service adapters still need integration tests. |

The standalone test build is in `modules/gui/qt/addons/CMakeLists.txt`. Set `VLC_BUILD_DIR` to add tests that need the native core library.

`tests/addon_provider_probe.cpp` is an explicit live test. It accepts the subtitle key on stdin. Normal tests do not run that network request.

The new theme adapters, mounted UI sessions, shared metadata owner, TVMaze package, and package recovery work have focused test evidence. Their latest combined app build is still in progress.

## Core feature preservation

Keep each core provider until its replacement add-on passes the relevant tests. Preserve local tags, sidecars, attachments, local artwork, and user corrections.

The TVMaze package passed a live episode lookup through the shared metadata service, worker, and broker. Its source credit remains in the result.

Other core artwork providers remain while their replacements are developed. A fixture test alone does not prove that an online replacement preserves the feature.

A shared metadata profile must not silently return to a competing direct HTTP or Lua provider path. Local metadata readers remain available.

## Meaningful commits

- `8e8b270c67`: portable Wasm worker.
- `c3b9895aa9`: TheSubtitleDB package and fixtures.
- `0d9aefd63c`: architecture contracts and completion gates.
- `fef07bbacd`: package, credential, network, and metadata host.
- `bac979e1cb`: native Qt string and provider resource limits.
- `489b7e4d94`: scoped UI registry.
- `eac14229cd`: Qt package/settings/subtitle integration.
- `401f541047`: worker release packaging and tests.
- `269bd38a5a`: controlled English in worker and network errors.
- `74a0f308f8`: bounded playback session authority.
- `7527e5ef15`: audio analysis outside the audio callback.
- `984b34aaaa`: asynchronous native metadata bridge.

## Open completion gates

- Build and test the latest combined Qt app. Finish supported-platform runtime and package tests.
- Complete update, migration, trust, retained-data, recovery, and safe-mode validation. Keep publisher verification claims consistent with actual evidence.
- Finish theme propagation and mounted UI regression tests. Add typed host result adapters before modal replacements can change core state.
- Complete the owned metadata route for player, preparser, library, and artwork requests. Preserve each working provider as an add-on before removal.
- Mount discovery browse, search, and subscriptions. Enforce stream policy through redirects, nested manifests, segments, and keys before playback.
- Complete stable queue references and atomic playlist/queue changes.
- Finish visualizer app tests, lifecycle and performance measurements, worker parameter updates, and device-loss fallback.
- Connect session authority to the native player, host consent UI, and outbound WSS. Exercise two-player drift correction and competing local input.
- Complete host service adapters, author tooling, schemas, SDK text, and profile conformance tests.
- Compare the completed implementation with every requirement in the architecture documents.

Successful subtitle tests do not close the other gates. The full implementation remains active while these items are open.
