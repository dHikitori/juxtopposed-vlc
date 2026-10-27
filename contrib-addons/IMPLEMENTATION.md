# Implementation status

This file records implementation progress and test evidence. Updated 28 October 2026.

The full scope in this directory remains the objective. SDK v1 still has open completion gates. The current effort estimate is about 85% complete, with about 15% of the effort left. This is an estimate of work, not a count of finished features. The open gates below determine release readiness.

The user approved code edits, fixes, builds, staging, commits, and the native playlist transaction plan on 27 October 2026. Code staging now succeeds. The earlier scope hold has ended. See [the pending review](pending-implementation-review.md) for test results and required repairs.

## Verified work

| Area | Implementation and evidence |
| --- | --- |
| Portable worker | Pinned Wasmtime 50; no WASI imports; bounded memory, dispatch, messages, and fuel; process IPC; additional Linux process restrictions. All seven Linux Rust tests pass, including rejection of old guest imports and exports. The final encoded IPC limit includes number normalization, the envelope, and separators. |
| Windows worker | The earlier worker built with Rust 1.97.0 and passed five Windows runtime cases plus missing-key IPC. The Hakone guest ABI still needs a fresh Windows build and run. |
| TheSubtitleDB | Portable Rust/Wasm package; individual API-key settings; typed subtitle search; episode scope; language and page controls; asset resolution. Fourteen unit tests and three worker fixture tests pass. |
| Real subtitle service | The native search and Save path returned four candidates and a 40,610-byte local subtitle through the host broker. The test used the supplied key in temporary session storage. No key entered source, saved settings, worker messages, or logs. |
| Package host | Strict manifests; repository, tag, and content identity; isolated Git object fetch; immutable release snapshots; permission approval; enable, disable, update, and rollback. Native tests pass. |
| Repository work lifetime | Staging and update checks use joined app work. Cancellation reaches each Git process without a Qt event. Eleven focused cases pass normally and with sanitizers. Package integration passes 33 cases. The runtime run passes 16 cases; a separate fresh-worker run covers its one skipped case. See [the repository lifetime contract](repository-worker-lifetime.md). |
| App package recovery | Bundled updates and restores use captured permission review. Removal can preserve data or delete the exact package data and keys. A shared retirement hold refuses fresh readers during removal. Current checks pass 187 normal, 59 focused QML, and 87 focused sanitizer cases. Windows/macOS wallet execution and the fresh full app check remain open. See [recovery and distribution](recovery-and-distribution-contract.md). |
| Credentials and network | OS credential store with an explicit temporary-storage option; exact origin and method scopes; DNS and public-address checks; pinned connections; redirect checks; credential removal on cross-origin redirects. Native tests pass. |
| Native media stream permission | Packages can request only `media.stream` with `public-https-media@1`. The host describes public playback and the separate source approval; wider scopes and visualizer use are refused. Current checks pass 117 host and 29 installer UI cases. WSS handshake header names are also reserved by prefix. Playback capability remains false until the production source route passes. See [the permission contract](native-media-stream-permission.md). |
| Guest private storage | Mounted `storage.get`, `storage.set`, and `storage.remove`; shared package quotas; account partitions; pure migrations; matching rollback snapshots. All 21 current cases pass normally and with focused sanitizers. See [the storage contract](guest-storage-contract.md) and [joined Qt work](qt-work-lifetime.md). |
| Metadata coordinator | Ordered providers; partial field patches; provenance; strong identity checks; protected fields; bounded cache partitions; independent cancellation. Native tests pass. |
| Episode consumer | Eleven real consumer parity cases pass. Host results retain their per-field source credit in a separate cache namespace. The old episode cache reader and local readers remain. |
| Subtitle integration | Native OpenSubtitles and portable providers use one provider order. Host handles resolve through the normal Save path. The latest native suite passes 53 cases. |
| Qt build and release | The previous Linux Qt module and compiled QML checkpoint passed 709 cases in 27 suites, with no failures or skips. A new full app build and compiled UI run are required after the Hakone contracts and remaining native integrations. Supported-platform release checks remain open. |
| Settings and install UI | Host-rendered package approval; generated settings; secret keep, replace, and delete actions; tagged repository update review; provider order; subtitle preparation and cancellation. Required-key deletion saves correctly and revokes the key. Focused QML tests pass. |
| Native metadata bridge | A Qt-free asynchronous bridge copies requests and native field patches. The original 14 symbols and explicit Apply have core exports. Apply compares field/identity/extra tokens, original URI, exact policy, cancellation, and owner state before writes. It fills missing values by default. Only native code can request replacement. All needed tokens are reserved together. Normal commit, limit, backend, and revision suites pass; focused sanitizer runs pass. Consumer mounts remain open. |
| Native metadata lifetime | Each VLC instance has one metadata owner. Shutdown retires that owner before interface teardown. The request list cancels work by caller and keeps callback state until callbacks return. Idle cancellation prevents later dispatch. Normal owner and request-list suites pass, including real instance cleanup, owner races, and cancellation between list insertion and Start. Both preparsers use guarded metadata transactions. The Qt provider owner and family cutover remain open. |
| Native metadata admission | A native gate can suspend new backend dispatch, cancel earlier direct work through its native interrupt, and resume only after that work ends. Six added owner groups cover queued and running work, shared-instance counts, interrupt acknowledgement, bounds, and stale leases. Normal backend, owner, request-list, and affected native suites pass; focused normal and ASAN/UBSAN runs pass. The fetcher stage passes 13 normal and sanitizer groups, including the real native executor transition. The Qt provider-owner activation remains open. |
| Native library transactions | The approved dependency patch adds fixed playlist snapshots, one atomic append, and exact source-record preparation. All 23 transaction/preparation groups and 13 existing playlist cases pass. The patch applies exactly after the previous six patches. The normal VLC module and C wrapper tests pass. The mounted picker/store passes all 21 cases through the real app object closure, including discovery append and active shutdown. The final service and QML suites each pass 11 cases. Source playback remains gated on its resolver and transport. |
| Native library work lifetime | Native work acquires a pending lease before submission and calls Begin on its worker. Shutdown refuses later Begin calls and waits only for running native calls. Pending leases can outlive the library and need no Qt completion. Four real-instance groups pass. All MediaLib task overloads now use this lease. The actual app dispatcher test proves that Core cleanup waits for active work, rejects queued work, and finishes without Qt completion delivery. |
| Joined library workers | The add-on library queue retains its app work and quota until its worker and Qt thread storage have ended. All 17 focused cases pass normally and with sanitizers. All 40 legacy runner cases pass. A new full-app dispatcher case still needs the fresh Qt module. See [the library lifetime contract](native-library-worker-lifetime.md). |
| App shutdown coordinator | The app retains its Qt event loop while joined work ends. Retirement hooks close mounted native owners before the drain. All 12 focused cases pass normally and with sanitizers. The full module build and the remaining worker mounts still need verification. |
| Session authority | Exclusive control leases; typed observations; media and revision checks; duplicate rejection; deadlines; local user priority; owned rate restoration. The actual native player fixture passes 12 scenarios. Qt host controls, relay integration, and two-player drift checks remain open. |
| Audio analysis | The callback copies bounded samples without allocation or locks. A separate worker calculates spectrum, waveform, and levels. Six cases pass for formats, time, media changes, and overload. |
| Audio observer lifetime | A sample listener retains the exact audio output that owns its meter. Removal uses that same output before release. The new normal native regression passes six public output resets, with listener removal in both orders and no extra test hold that could hide the defect. Native visualizer clock, playback, and rendering integration remain under work. |
| Visual scenes | Catalog reads, resource validation, Wasm parameter work, and rendering run outside the GUI thread. The normal app mounts the surface and selection UI. Registered normal tests pass: controller 13, resources 12, render queue 12, worker session 15, worker process six, native clock 11, real player six, and real window three. Tests cover media/seek/rate/format epochs, output reset, observer release, pixels, graph loss, and window recreation. A physical GPU failure was not tested. |
| Visualizer parameter schema | Named scene nodes expose only declared numeric fields and ranges. Updates have 64-entry/16-KiB limits and preserve aggregate particle limits. Seven parameter cases and both existing seven-case scene/controller suites pass. Worker scheduling and native renderer lifecycle integration remain open. |
| Host API dispatch | A closed operation map; permission and operation scopes; request limits; cancellation; handler lifetime; thread-safe completion. Eleven focused cases pass. Individual service adapters still need integration tests. |
| Rust author SDK | Owned ABI buffers, typed input views, pending-call ownership, and complete encoded output bounds. The 64-field credential fix passes nine Rust tests and four real-worker tests. All four artwork packages have been built with the fix; all five artwork worker fixtures pass. |
| Worker lifetime | Cancellation reaches the guest. Restart creates a new request-ID namespace. Reentrant native calls cannot restore a canceled invocation. The current broker suite passes 61 tests. Credential packets contain presence flags only. |
| Session invocation limits | Host-call IDs belong to their active invocation, with a shared limit of 4,096 retained IDs. Terminal and canceled invocations release their entries. The current broker passes 70 cases, and the SDK passes ten tests, including late results across slices. Native session app integration remains open. |
| Account scope | Workers and assets keep captured credential references, release, grants, and settings revision. External state changes revoke old work. Opaque account references preserve display-only changes and code updates. Declared account fields and key changes require a new account reference. |
| Native authority snapshots | Manager preparation reads memory only. Joined work verifies the exact package, contribution, settings, and account. Credential snapshots own a serialized cache, and each read checks the approved binding and current state. Native revocation precedes manager retirement, which refuses fresh readers. All ten focused cases pass normally and with sanitizers. The current related host group passes 185 cases with no skips. See [native authority snapshots](native-authority-snapshots.md). |
| Store authority epoch | Owners of the same store share a memory token. Authority writes invalidate it before file publication and again after finish or rollback. Bounded native watches cancel exact leases before publication, including across concurrent invalidations. Thirteen cases pass normally and with sanitizers, including 200 registration races and capture destruction at the quota limit. Data-only private storage writes keep the token. External file edits remain subject to background admission checks. |
| Artwork packages | MusicBrainz, Last.fm, French TV, and image search have portable packages. Eleven parser tests and five worker fixtures pass. Four public native image paths passed. Google and some old French URLs have incomplete live evidence; their core paths remain. See [the migration record](metadata-provider-migration.md). |
| Provider distribution | All four artwork packages and their 20 required files are in the normal install map. Startup imports missing distribution packages while it retains disabled and removed states. All 24 package cases pass. Existing core providers remain until the owned route and provider parity gates pass. |
| Themes and interfaces | Shared theme state reaches QML and native widgets. Contrast tests cover status text and accent surfaces. Known hooks support bounded JSON interface sessions, commands, home replacement, and typed subtitle/playlist selection shells. Nine native registry cases pass. The normal app build passes. Recovery and stale playlist target checks pass all 28 focused cases within the 707-case compiled UI run. |
| Static UI images | Declared package PNG/JPEG/WebP images use host tickets and native image claims. Reads, inventory checks, and decode work run in bounded background jobs. All 29 coherent image cases pass. The normal app build and 709 compiled UI cases pass. Shared image preflight has seven normal and sanitizer cases. |
| Image delivery authority | The image reader captures a retained memory-only store token before queueing. Cold delivery, warm-cache delivery, provider reads, and trusted native pixel snapshots check it. A short scope sweep withdraws old images. All 29 current image cases pass with the 25 package and five authority cases. These focused host changes still need the next full app rebuild. |
| Discovery browser | Roots, folders, search, page cursors, independent views, and stable source admissions are mounted. Real queue tests cover one-batch append, revisions, title refresh, export, and account changes. The production Play and Enqueue controls remain unavailable until the lazy resolver and contained transport are complete. |
| Native input policy | Item copies and native children retain policy. URL, connection, HTTP, FFmpeg secondary I/O, AVIO, and Live555 paths have guards. Native policy, HTTP, and direct AVIO controls passed. Core source/proxy profile checks and atomic source-item policy creation pass the normal native suite, including 16 concurrent callers. Live555 is not configured in this build. |
| Native source jobs | Bounded retained jobs and one-owner transport leases revoke pending and issued transports without a Qt event. The C suite includes 200 result races. The current resolver passes 33 normal and sanitizer cases; saved host consent passes 21. Exact admission readers are prepared on the owner thread and run on joined work. The production resolver mount and application shutdown connection remain open. |
| Native source input | The input requires source approval before options, exact item/policy admission, and primary playback. Seven bridge groups pass normally and with ASAN/UBSAN. Six real native groups pass for primary dispatch/end, Stop/late completion, URI mutation, preparse, thumbnails, and associated media. The separate contained transport fixture proves actual WAV and HLS AAC playback. The production mount and broader nested-stream checks remain open. |
| Adaptive manifest conversion | HLS and a restricted static DASH profile replace concrete media, initialization, and key URLs through one host resource batch. All 22 combined cases pass in coherent host, isolated normal, and ASAN/UBSAN builds. Dynamic DASH, templates, protection, and external XML features fail closed. Contained transport and actual adaptive playback remain open. See [the DASH contract](native-dash-manifest-contract.md). |
| Native source owner | One owner per native VLC instance; permanent retirement before interface shutdown; final release after native users. Seven normal C groups and 100 Set/Close races pass, including real Cleanup and Destroy. Only Set and Hold are public exports. The host source resolver still needs its mount. |
| Provider text | Five core UI consumers use plain text for provider titles, artists, track names, filenames, and tooltips. All 13 load-probe cases and 249 existing affected UI cases pass. The normal app build and compiled UI run also pass. |
| Native artwork record | A private approval binds the local URI, artwork token, source URI, and held policy. Seven record groups and seven metadata-transfer groups pass. Apply preallocates the proof and publishes it with the reserved field token. Preparation-time raw/approved edits refuse all writes. The transfer and token-limit suites also pass ASAN/UBSAN. All four record/URI exports are present. All nine actual Qt consumer cases and all 14 service cases pass, including the real asset-cache/service/Apply/getter chain. The native fetcher fixture passes normally and with sanitizers. The owned preparser/provider cutover remains open. |
| Native artwork materialization | A host-only adapter reads the original native artwork snapshot, checks consent and item policy before I/O, and produces a local approved file. Ten native adapter cases and all 10 actual Qt consumer cases pass. The shared decoder strips metadata for native artwork and rejects compressed metadata for add-on images. The installed native readers preserve pixels and orientation through the bounded adapter. The native pixel, materializer, guest UI image, and guest artwork suites pass 29, 27, 29, and nine cases. Selected-file consumer checks and owned-pipeline activation remain open. |
| Native artwork choices | Explicit user file preparation and native cover preview use joined app work. All 23 selected-image cases and 14 native helper cases pass normally and with sanitizers. Production consumers pass syntax checks. Their 15-case runtime check still needs the fresh Qt module. See [native cover image choices](native-artwork-selection-contract.md). |

## Latest verified native checkpoint

- The public add-on contracts use Hakone names. All nine Wasm modules have the new ABI. SDK/provider tests pass 49 cases; the worker passes seven; worker fixtures pass 18; the fresh-helper runtime suite passes 16 with no skips.
- Renamed schema, UI, package, theme, discovery, and source-reference checks pass 117 host cases and 32 source QML cases.
- The normal Core build passes 14 source, policy, artwork, metadata, and private player-state groups. The actual native player fixture passes 12 scenarios. Full shared-session transport and host consent integration remain open.
- Fresh native adapters pass: resolver 28, consent 21, queue store seven, library store 19, metadata service 14, native artwork 10, and manifest conversion 22. The current library result supersedes the failed run that loaded the old dependency.
- The native media library patch recreates all eight source files without fuzz. Its transaction group and 13 existing playlist regressions pass with the rebuilt dependency.
- The isolated contained transport fixture passes six native cases, including actual WAV and HLS AAC decode, replay, authority refusal, and shutdown with its network loop blocked. The production mount still needs the application shutdown contract.

- The host-only native artwork service passes 19 focused normal and sanitizer cases. Automatic consent withdrawal reaches completed native anchors. The native provider cutover and materializer app-lifetime mount remain open.
- The native request guard, event collector, and affected metadata paths pass all eight normal Core target groups. Focused sanitizer checks pass guard six groups, collector three groups, and pipeline 16 groups.
- The full private local Parse transaction passes the normal Core checks. Its current local-result fixture passes 11 groups and 32 scenarios. It preserves native parse facts, sidebands, local/cache/attachment artwork, and field tokens. The limit, commit, pipeline, fetcher, and token-limit targets also pass with the sealed native artwork rule.
- The actual normal Lua module and compiled filename asset pass the pure selector and runtime targets. The runtime fixture has 27 probes. Shipped filename behavior and ordinary native playlists remain available; scoped inputs cannot execute arbitrary playlist scripts.
- Native artwork preserves the formats available from the installed Qt readers, including orientation and native metadata handling. The current build has no TIFF reader. Each supported platform still needs its own reader checks.
- The current internal and external preparser mount passes all 26 fresh normal targets. The real runtime fixture passes 16 WAV/M3U cases; the child lifecycle fixture passes nine cases. Existing checks pass 14 folder cases, two subtree cases, and parity with 999 tracks per media kind. Focused normal and sanitizer checks cover 21 controlled internal scenarios, 17 native IPC scenarios, and both retained-key and import allocation-failure cases. See [the external preparser contract](native-external-preparser-contract.md).
- The app shutdown coordinator passes 12 normal and 12 sanitizer cases. Repository work passes 11 focused cases in both builds. The joined add-on library runner passes 17 cases in both builds, and the legacy runner passes 40 normal cases. The full Qt module and its new dispatcher case remain open.
- Joined image, storage, and visualizer work passes 153 normal cases across 11 affected suites. Storage also passes 21 focused sanitizer cases.
- Native authority snapshots pass ten normal and ten sanitizer cases. Native revocation passes 13 normal and 13 sanitizer cases. The related normal host group passes 185 cases with no skips. Exact source admission readers pass 33 cases in both builds. These host checks do not activate the remaining production owners.
- Joined native artwork passes 34 normal and 12 targeted sanitizer cases. Metadata service passes all 27 cases in both builds. Native cancellation controls release Core references before joined retirement ends. MainUI binds the service to the app handle and closes metadata and queue owners at the early retirement hooks; its real object compiles. The native provider owner and full app checks remain open.

The Qt metadata/provider owner, selected native artwork consumers, production discovery and session mounts, the normal app rebuild, and supported-platform checks remain open. Work in progress on those gates is separate from the verified checks above.

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
- `6df9069887`: native artwork approval records.
- `a22a5590a2`: metadata owner retirement, request-list lifetime, and idle cancellation.
- `11080d0144`: dependency playlist CAS and the native module boundary.
- `00beba6e13`: native source input lifetime and real playback gate tests.
- `99712472c1`: native library work leases through Core shutdown.
- `91ef46ff8d`: artwork proof transfer during atomic metadata Apply.
- `82bb7dfb95`: bounded static UI images and the normal app mount.
- `55bfc7c556`: native artwork consumers and the broker/service proof chain.
- `3a20f84770`: native library picker, guarded append, and real dispatcher tests.
- `4879a452b2`: complete artwork package distribution and startup inventories.
- `c133b6a883`: normalized build inventories and private fetcher test includes.
- `479b2d877c`: retained audio output for sample observer removal.
- `52c151e8fc`: native metadata owner admission and legacy work retirement.
- `a9e29c60a5`: bounded HLS conversion.
- `64de5ba1e6`: store authority invalidation before state writes.
- `1caba5d249`: current authority checks at image delivery.
- `0f4c257838`: declared numeric visualizer parameters.
- `40dd7c3ef5`: shared image preflight and native artwork materialization.
- `19148fe7f6`: saved source consent and native lazy resolver.
- `4c9d982cf6`: native local metadata anchors and automatic cancellation.
- `f677488eef`: concrete resources in static DASH manifests.
- `efc48cc5c7`: retained local results through commit admission.
- `1d26caa7c8`: native artwork consumer preservation with compressed metadata.
- `7bcf11cd51`: asynchronous visualizers, native clocks, and real lifecycle tests.

## Open completion gates

- Finish supported-platform app/runtime/package tests and repeat release checks after the remaining implementation changes.
- Verify the implemented application-distribution upgrades, recovery UI, and safe mode in the fresh full app. Complete Git object-format negotiation. Keep publisher verification claims consistent with actual evidence.
- Keep typed host result adapters as the only path from modal replacements to core state. Repeat the app checks after the remaining feature integrations.
- Complete the owned metadata route for player, preparser, library, and artwork requests. Preserve each working provider as an add-on before removal.
- Complete discovery subscriptions and the native lazy playback route. Enforce stream policy through redirects, nested manifests, segments, and keys before playback.
- Keep the verified stable queue reference, native playlist CAS, and library shutdown boundary. Complete their use with contained playback and subscription updates.
- Keep the verified visualizer worker, renderer, native clock, and window lifecycle boundaries. Repeat supported-platform app and graphics checks after the remaining integrations.
- Connect the verified native session authority to the Qt host, consent UI, and outbound WSS. Exercise two-player drift correction and the broad native player regression suites.
- Complete host service adapters, author tooling, schemas, SDK text, and profile conformance tests.
- Audit all new app English against the requested ASD-STE100 rules and technical terminology.
- Retain the verified typed playlist recovery, target guards, and plain-text consumers. See [the review record](pending-implementation-review.md).
- Preserve the verified UI image admission and decode limits when new native consumers are mounted. Complete selected-file preparation and supported-platform native reader checks before the owned pipeline is enabled.
- Use the verified source-owner retirement boundary when mounting native source access.
- Compare the completed implementation with every requirement in the architecture documents.

Successful subtitle tests do not close the other gates. The full implementation remains active while these items are open.

## Latest progress commits

- `b8bb71e18d`: one public-address policy for add-on requests and contained streams.
- `1c1d0f673b`: refuse scoped media in the external download helper.
- `c82fe845e2`: Hakone public contracts, SDKs, guest ABI, schemas, and example package identities.
- `498b9edce0`: native metadata transaction and fetcher stages.
- `fae3662334`: native shared-session player authority and real input tests.
- `8d291c7638`: Hakone source references in the retained native media library patch.
- `780a643649`: owned bytes in strict string-builder manifest fixtures.
- `1b6a4fea3c`: memory-only consent receipts and background source admission.

- `eb9eaffaa3`: complete native local Parse snapshots and guarded deltas.
- `e4cc8740c7`: installed native artwork formats and orientation.
- `c69ab0114a`: native directory sort callback values and public regression tests.
- `0376641b92`: shipped native Lua metadata scripts and scoped playlist refusal.
- `11754a491b`: sealed ordinary-native artwork through local metadata commits.
- `db4424d266`: contained TLS media transport, actual WAV/HLS playback, and joined work primitives.
- `a35bb02651`: private native artwork service and completed-anchor consent withdrawal.
- `94182cc079`: native request publication latch, local-only request fallback, and bounded sideband collector.
- `9765ab54ac`: guarded metadata transactions in the internal preparser.
- `b73aa857e5`: joined app work and the Qt shutdown coordinator.
- `9c6a7a599b`: joined repository staging and update checks.
- `c61b87722b`: joined add-on library workers and shared task IDs.
- `be1ae5297f`: joined image, storage, and visualizer app work.
- `4492f82dca`: native selected-artwork preparation and preview.
- `066566341e`: owned native authority and credential snapshots.
- `48df31052b`: guarded external preparser transactions and native IPC facts.
- `3a633d1f30`: exact source admission reader preparation.
- `3ee20bc633`: bounded host-call IDs for active invocations.
- `087ed8f117`: explicit WSS credential origin bindings.
- `e309db2ff3`: captured app package review, removal, and saved-data cleanup.
- `affdfe51ea`: bounded native authority revocation before store publication.
- `fe1191e639`: mounted metadata and queue retirement connections.
- `0546969a5d`: joined native artwork workers and metadata callback retirement.
