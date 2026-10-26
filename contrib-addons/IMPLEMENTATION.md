# Implementation status

This file records implementation progress and test evidence. Updated 27 October 2026.

The full scope in this directory remains the objective. SDK v1 still has open completion gates.

## Verified work

| Area | Implementation and evidence |
| --- | --- |
| Portable worker | Pinned Wasmtime 50; no WASI imports; bounded memory, dispatch, messages, and fuel; process IPC; additional Linux process restrictions. All six current Linux Rust tests pass. The final encoded IPC limit includes number normalization, the envelope, and separators. |
| Windows worker | Rust 1.97.0 built the Windows worker. All five runtime tests ran on Windows and passed. The real worker returned `auth_required` through IPC without a key. |
| TheSubtitleDB | Portable Rust/Wasm package; individual API-key settings; typed subtitle search; episode scope; language and page controls; asset resolution. Fourteen unit tests and three worker fixture tests pass. |
| Real subtitle service | The native search and Save path returned four candidates and a 40,610-byte local subtitle through the host broker. The test used the supplied key in temporary session storage. No key entered source, saved settings, worker messages, or logs. |
| Package host | Strict manifests; repository, tag, and content identity; isolated Git object fetch; immutable release snapshots; permission approval; enable, disable, update, and rollback. Native tests pass. |
| Credentials and network | OS credential store with an explicit temporary-storage option; exact origin and method scopes; DNS and public-address checks; pinned connections; redirect checks; credential removal on cross-origin redirects. Native tests pass. |
| Metadata coordinator | Ordered providers; partial field patches; provenance; strong identity checks; protected fields; bounded cache partitions; independent cancellation. Native tests pass. |
| Episode consumer | Eleven real consumer parity cases pass. Host results retain their per-field source credit in a separate cache namespace. The old episode cache reader and local readers remain. |
| Subtitle integration | Native OpenSubtitles and portable providers use one provider order. Host handles resolve through the normal Save path. The latest native suite passes 53 cases. |
| Qt build and release | The normal Linux Qt target passed with discovery, native queue, metadata, themes, interface sessions, and visualizers. All 21 consolidated host suites passed without skips. The owned UI group passed 493 tests in 19 suites. The earlier release suite passed 98 tests with one existing skip; release validation of the latest complete scope remains open. |
| Settings and install UI | Host-rendered package approval; generated settings; secret keep, replace, and delete actions; tagged repository update review; provider order; subtitle preparation and cancellation. Required-key deletion saves correctly and revokes the key. Focused QML tests pass. |
| Native metadata bridge | A Qt-free asynchronous bridge copies requests and native field patches. The C suite passes cancellation/result races, reentrant close, and callback lifetime cases. All 14 symbols have core exports. |
| Session authority | Exclusive control leases; typed observations; media and revision checks; duplicate rejection; deadlines; local user priority; owned rate restoration. Nine focused cases pass. Native player and relay integration remain open. |
| Audio analysis | The callback copies bounded samples without allocation or locks. A separate worker calculates spectrum, waveform, and levels. Six cases pass for formats, time, media changes, and overload. |
| Visual scenes | Trusted spectrum, waveform, spectrogram, particle, gradient, group, texture, and feedback primitives. Scene and controller suites pass. The normal app mounts the surface and selection UI. Real playback performance and device-loss tests remain open. |
| Host API dispatch | A closed operation map; permission and operation scopes; request limits; cancellation; handler lifetime; thread-safe completion. Nine focused cases pass. Individual service adapters still need integration tests. |
| Rust author SDK | Owned ABI buffers, typed input views, pending-call ownership, and complete encoded output bounds. Eight Rust tests and three real-worker fixture tests pass. Four artwork packages use this SDK. |
| Worker lifetime | Cancellation reaches the guest. Restart creates a new request-ID namespace. Reentrant native calls cannot restore a canceled invocation. The current broker suite passes 61 tests. Credential packets contain presence flags only. |
| Account scope | Workers and assets keep captured credential references, release, grants, and settings revision. External state changes revoke old work. Opaque account references preserve display-only changes and code updates. Declared account fields and key changes require a new account reference. |
| Artwork packages | MusicBrainz, Last.fm, French TV, and image search have portable packages. Eleven parser tests and five worker fixtures pass. Four public native image paths passed. Google and some old French URLs have incomplete live evidence; their core paths remain. See [the migration record](metadata-provider-migration.md). |
| Themes and interfaces | Shared theme state reaches QML and native widgets. Contrast tests cover status text and accent surfaces. Known hooks support bounded JSON interface sessions, commands, home replacement, and result shells. Typed core dialog adapters remain open. |
| Discovery browser | Roots, folders, search, page cursors, independent views, and stable source admissions are mounted. Real queue tests cover one-batch append, revisions, title refresh, export, and account changes. The production Play and Enqueue controls remain unavailable until the lazy resolver and contained transport are complete. |
| Native input policy | Item copies and native children retain policy. URL, connection, HTTP, FFmpeg secondary I/O, AVIO, and Live555 paths have guards. Native policy, HTTP, and direct AVIO controls passed. Live555 is not configured in this build. |
| Native source jobs | Bounded retained jobs and one-owner transport leases. Cancellation and Close revoke pending and issued transports without a Qt event. Nine C case groups, 200 result races, sanitizers, and all 19 exports pass. Access binding and stream proxy work remain open. |

The standalone test build is in `modules/gui/qt/addons/CMakeLists.txt`. Set `VLC_BUILD_DIR` to add tests that need the native core library.

`tests/addon_provider_probe.cpp` is an explicit live test. It accepts the subtitle key on stdin. Normal tests do not run that network request.

The fresh production worker also passed all three TheSubtitleDB fixtures and the real missing-key host case. The latest artwork worker fixtures passed against that helper. These are separate from the earlier authenticated subtitle Save test.

## Core feature preservation

Keep each core provider until its replacement add-on passes the relevant tests. Preserve local tags, sidecars, attachments, local artwork, and user corrections.

The TVMaze package passed a live episode lookup through the shared metadata service, worker, and broker. Its source credit remains in the result.

Core artwork providers remain while the owned native route is completed. A fixture test alone does not prove that an online replacement preserves the feature. Keep embedded native HTTP/LAN artwork distinct from add-on public HTTPS assets and respect the user's network preference.

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
- `8d8ffb8433`: portable artwork packages and bounded native image assets.
- `d184d91c88`: captured configuration and stable account references.
- `69f45f1caa`: concrete native playlist and session plans.
- `08e8f09f76`: shared Rust SDK and worker cancellation.
- `d16c8536f7`: alternate native I/O guards.
- `65019a4ec2`: retained native discovery jobs and transport revocation.
- `a9856efaea`: final serialized IPC frame limits.
- `2fd6ca2651`: mounted discovery browser, stable admissions, and native queue transactions.

## Open completion gates

- Finish supported-platform app/runtime/package tests and repeat release checks after the remaining implementation changes.
- Complete application-distribution upgrades, private guest storage and migrations, recovery UI, safe mode, and Git object-format negotiation. Keep publisher verification claims consistent with actual evidence.
- Mount typed host result adapters before modal replacements can change core state. Theme and mounted UI regression tests have passed for the current scope.
- Complete the owned metadata route for player, preparser, library, and artwork requests. Preserve each working provider as an add-on before removal.
- Complete discovery subscriptions and the native lazy playback route. Enforce stream policy through redirects, nested manifests, segments, and keys before playback.
- Keep the verified stable queue reference and append path. Implement media-library playlist CAS only after the pending approval of [the prepared database plan](native-playlist-cas.md).
- Finish visualizer app tests, lifecycle and performance measurements, worker parameter updates, and device-loss fallback.
- Connect session authority to the native player, host consent UI, and outbound WSS. Exercise two-player drift correction and competing local input.
- Complete host service adapters, author tooling, schemas, SDK text, and profile conformance tests.
- Audit all new app English against the requested ASD-STE100 rules and technical terminology.
- Compare the completed implementation with every requirement in the architecture documents.

Successful subtitle tests do not close the other gates. The full implementation remains active while these items are open.
