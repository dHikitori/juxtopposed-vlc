# Implementation status

This file records implementation progress and test evidence. Updated 27 October 2026.

The full scope in this directory remains the objective. SDK v1 still has open completion gates.

The user approved code edits, fixes, builds, staging, commits, and the native playlist transaction plan on 27 October 2026. Code staging now succeeds. The earlier scope hold has ended. See [the pending review](pending-implementation-review.md) for test results and required repairs.

## Verified work

| Area | Implementation and evidence |
| --- | --- |
| Portable worker | Pinned Wasmtime 50; no WASI imports; bounded memory, dispatch, messages, and fuel; process IPC; additional Linux process restrictions. All six current Linux Rust tests pass. The final encoded IPC limit includes number normalization, the envelope, and separators. |
| Windows worker | Rust 1.97.0 built the Windows worker. All five runtime tests ran on Windows and passed. The real worker returned `auth_required` through IPC without a key. |
| TheSubtitleDB | Portable Rust/Wasm package; individual API-key settings; typed subtitle search; episode scope; language and page controls; asset resolution. Fourteen unit tests and three worker fixture tests pass. |
| Real subtitle service | The native search and Save path returned four candidates and a 40,610-byte local subtitle through the host broker. The test used the supplied key in temporary session storage. No key entered source, saved settings, worker messages, or logs. |
| Package host | Strict manifests; repository, tag, and content identity; isolated Git object fetch; immutable release snapshots; permission approval; enable, disable, update, and rollback. Native tests pass. |
| Credentials and network | OS credential store with an explicit temporary-storage option; exact origin and method scopes; DNS and public-address checks; pinned connections; redirect checks; credential removal on cross-origin redirects. Native tests pass. |
| Guest private storage | Mounted `storage.get`, `storage.set`, and `storage.remove`; shared package quotas; account partitions; pure migrations; matching rollback snapshots. All 18 focused cases pass. See [the storage contract](guest-storage-contract.md). |
| Metadata coordinator | Ordered providers; partial field patches; provenance; strong identity checks; protected fields; bounded cache partitions; independent cancellation. Native tests pass. |
| Episode consumer | Eleven real consumer parity cases pass. Host results retain their per-field source credit in a separate cache namespace. The old episode cache reader and local readers remain. |
| Subtitle integration | Native OpenSubtitles and portable providers use one provider order. Host handles resolve through the normal Save path. The latest native suite passes 53 cases. |
| Qt build and release | The normal Linux Qt target and QML runner build pass with the recorded typed-dialog, plain-text, and native metadata changes. The compiled runner passes 707 cases in 27 suites, with no failures or skips. It used a committed-source snapshot while the next UI image feature was edited. An earlier consolidated run passed 21 host suites. The earlier release suite passed 98 tests with one existing skip; release validation of the latest complete scope remains open. |
| Settings and install UI | Host-rendered package approval; generated settings; secret keep, replace, and delete actions; tagged repository update review; provider order; subtitle preparation and cancellation. Required-key deletion saves correctly and revokes the key. Focused QML tests pass. |
| Native metadata bridge | A Qt-free asynchronous bridge copies requests and native field patches. The original 14 symbols and explicit Apply have core exports. Apply compares field/identity/extra tokens, original URI, exact policy, cancellation, and owner state before writes. It fills missing values by default. Only native code can request replacement. All needed tokens are reserved together. Normal commit, limit, backend, and revision suites pass; focused sanitizer runs pass. Consumer mounts remain open. |
| Native metadata lifetime | Each VLC instance has one metadata owner. Shutdown retires that owner before interface teardown. The request list cancels work by caller and keeps callback state until callbacks return. Idle cancellation prevents later dispatch. Normal owner and request-list suites pass, including real instance cleanup, owner races, and cancellation between list insertion and Start. The preparser mount remains open. |
| Native library transactions | The approved dependency patch adds fixed playlist snapshots, one atomic append, and exact source-record preparation. All 23 transaction/preparation groups and 13 existing playlist cases pass. The patch applies exactly after the previous six patches. The normal VLC module and C wrapper tests pass. The mounted picker/store passes all 21 cases through the real app object closure, including discovery append and active shutdown. The final service and QML suites each pass 11 cases. Source playback remains gated on its resolver and transport. |
| Native library work lifetime | Native work acquires a pending lease before submission and calls Begin on its worker. Shutdown refuses later Begin calls and waits only for running native calls. Pending leases can outlive the library and need no Qt completion. Four real-instance groups pass. All MediaLib task overloads now use this lease. The actual app dispatcher test proves that Core cleanup waits for active work, rejects queued work, and finishes without Qt completion delivery. |
| Session authority | Exclusive control leases; typed observations; media and revision checks; duplicate rejection; deadlines; local user priority; owned rate restoration. Nine focused cases pass. Native player and relay integration remain open. |
| Audio analysis | The callback copies bounded samples without allocation or locks. A separate worker calculates spectrum, waveform, and levels. Six cases pass for formats, time, media changes, and overload. |
| Visual scenes | Trusted spectrum, waveform, spectrogram, particle, gradient, group, texture, and feedback primitives. Scene and controller suites pass. The normal app mounts the surface and selection UI. Real playback performance and device-loss tests remain open. |
| Host API dispatch | A closed operation map; permission and operation scopes; request limits; cancellation; handler lifetime; thread-safe completion. Eleven focused cases pass. Individual service adapters still need integration tests. |
| Rust author SDK | Owned ABI buffers, typed input views, pending-call ownership, and complete encoded output bounds. The 64-field credential fix passes nine Rust tests and four real-worker tests. All four artwork packages have been built with the fix; all five artwork worker fixtures pass. |
| Worker lifetime | Cancellation reaches the guest. Restart creates a new request-ID namespace. Reentrant native calls cannot restore a canceled invocation. The current broker suite passes 61 tests. Credential packets contain presence flags only. |
| Account scope | Workers and assets keep captured credential references, release, grants, and settings revision. External state changes revoke old work. Opaque account references preserve display-only changes and code updates. Declared account fields and key changes require a new account reference. |
| Artwork packages | MusicBrainz, Last.fm, French TV, and image search have portable packages. Eleven parser tests and five worker fixtures pass. Four public native image paths passed. Google and some old French URLs have incomplete live evidence; their core paths remain. See [the migration record](metadata-provider-migration.md). |
| Themes and interfaces | Shared theme state reaches QML and native widgets. Contrast tests cover status text and accent surfaces. Known hooks support bounded JSON interface sessions, commands, home replacement, and typed subtitle/playlist selection shells. Nine native registry cases pass. The normal app build passes. Recovery and stale playlist target checks pass all 28 focused cases within the 707-case compiled UI run. |
| Static UI images | Declared package PNG/JPEG/WebP images use host tickets and same-session native image claims. Package reads, full hashing, and decoding run in bounded background jobs. Global claims, encoded bytes, pixels, metadata, dimensions, and cache have limits. All 25 normal image cases pass, including actual painted pixels, external-I/O controls, and blocked read/close/destruction cases. The integrated app build and 709 compiled UI cases in 27 suites pass with no failures or skips. |
| Discovery browser | Roots, folders, search, page cursors, independent views, and stable source admissions are mounted. Real queue tests cover one-batch append, revisions, title refresh, export, and account changes. The production Play and Enqueue controls remain unavailable until the lazy resolver and contained transport are complete. |
| Native input policy | Item copies and native children retain policy. URL, connection, HTTP, FFmpeg secondary I/O, AVIO, and Live555 paths have guards. Native policy, HTTP, and direct AVIO controls passed. Core source/proxy profile checks and atomic source-item policy creation pass the normal native suite, including 16 concurrent callers. Live555 is not configured in this build. |
| Native source jobs | Bounded retained jobs and one-owner transport leases. Cancellation and Close revoke pending and issued transports without a Qt event. Eleven C groups pass, including cancel before Start and immediate revocation after Complete; the suite includes 200 result races. All 19 backend/job exports remain. The host resolver and stream proxy remain open. |
| Native source input | The input requires source approval before options, exact item/policy admission, and primary playback. Seven bridge groups pass normally and with ASAN/UBSAN. Six real native groups pass for primary dispatch/end, Stop/late completion, URI mutation, preparse, thumbnails, and associated media. The fixture does not establish actual media playback or child-stream EOF behavior. |
| Native source owner | One owner per native VLC instance; permanent retirement before interface shutdown; final release after native users. Seven normal C groups and 100 Set/Close races pass, including real Cleanup and Destroy. Only Set and Hold are public exports. The host source resolver still needs its mount. |
| Provider text | Five core UI consumers use plain text for provider titles, artists, track names, filenames, and tooltips. All 13 load-probe cases and 249 existing affected UI cases pass. The normal app build and compiled UI run also pass. |
| Native artwork record | A private approval binds the local URI, artwork token, source URI, and held policy. Seven record groups and seven metadata-transfer groups pass. Apply preallocates the proof and publishes it with the reserved field token. Preparation-time raw/approved edits refuse all writes. The transfer and token-limit suites also pass ASAN/UBSAN. All four record/URI exports are present. All nine actual Qt consumer cases and all 14 service cases pass, including the real asset-cache/service/Apply/getter chain. The native fetcher fixture passes normally and with sanitizers. The owned preparser/provider cutover remains open. |

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
- `283fcbef04`: episode consumer cache and provenance parity.
- `41174296b1`: native metadata field and extra-tag revision snapshots.
- `65e46b727a`: mounted private storage, account partitions, pure migrations, and recovery snapshots.
- `1065f63935`: SDK credential capacity and four matching artwork artifacts.
- `5c684f5a75`: core source-profile checks, atomic item policy creation, and idle source-job cancellation.
- `d7165da888`: native source-owner retirement and real instance cleanup tests.
- `816490fbea`: typed subtitle/playlist results, current-target checks, and core recovery.
- `560aa4ba79`: plain provider text with positive and zero-load regression probes.
- `d91755036c`: source-owner test definitions outside the Windows-only Automake condition.
- `203ef8e980`: native metadata commit admission with complete token reservation.

## Open completion gates

- Finish supported-platform app/runtime/package tests and repeat release checks after the remaining implementation changes.
- Complete application-distribution upgrades, recovery UI, safe mode, and Git object-format negotiation. Keep publisher verification claims consistent with actual evidence.
- Keep typed host result adapters as the only path from modal replacements to core state. Repeat the app checks after the remaining feature integrations.
- Complete the owned metadata route for player, preparser, library, and artwork requests. Preserve each working provider as an add-on before removal.
- Complete discovery subscriptions and the native lazy playback route. Enforce stream policy through redirects, nested manifests, segments, and keys before playback.
- Keep the verified stable queue reference and append path. Complete the host mount and shutdown checks for [native playlist CAS](native-playlist-cas.md).
- Finish visualizer app tests, lifecycle and performance measurements, worker parameter updates, and device-loss fallback.
- Connect session authority to the native player, host consent UI, and outbound WSS. Exercise two-player drift correction and competing local input.
- Complete host service adapters, author tooling, schemas, SDK text, and profile conformance tests.
- Audit all new app English against the requested ASD-STE100 rules and technical terminology.
- Retain the verified typed playlist recovery, target guards, and plain-text consumers. See [the review record](pending-implementation-review.md).
- Mount UI image handles only after format, decode, and reference tests pass. The recorded UI repair group still has an empty host asset map; the image feature is separate work.
- Use the verified source-owner retirement boundary when mounting native source access.
- Compare the completed implementation with every requirement in the architecture documents.

Successful subtitle tests do not close the other gates. The full implementation remains active while these items are open.
