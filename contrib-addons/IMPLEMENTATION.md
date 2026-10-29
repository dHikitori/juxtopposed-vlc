# Implementation status

This file records implementation progress and test evidence. Updated 30 October 2026.

The full scope in this directory remains the objective. SDK v1 still has open completion gates. The earlier effort estimate was about 80–85% complete, with about 15–20% left. That estimate came before the measured UI performance findings. It does not establish current readiness. The [completion backlog](completion-backlog.md) records the remaining requirements and the evidence for each gap. The open gates below determine release readiness.

Performance is the current priority. The **Online providers** freeze is traced
to synchronous execution-path package scans. Provider selection and portable
invocation preparation now run on joined work. The six related suites pass
349 normal and 349 ASAN/UBSAN checks, including a blocked-reader UI
regression. See [the diagnosis and fix](subtitle-online-responsiveness.md).
The [new Windows review package](windows-artwork-build-2026-10-30.md) at
clean source `74ae4267fc` is published in the usual local folder. Restart the
app before testing. Manual Windows GUI confirmation remains open.

The earlier published build repeated
synchronous package scans during basic UI work. The new local Windows build
at clean source `11adca2a52` first mounted the shared display path. See
[that build record](windows-performance-build-2026-10-29.md). The current
`74ae4267fc` package also includes asynchronous metadata selection and
invocation preparation.
Focused session host and session UI idle
timer fixes pass. The mounted source owner, resolver, and consent UI also have
verified idle timer fixes. Source TLS initialization is deferred until explicit
joined work. MainUI now mounts the shared display view and joined startup
batch before the display consumers. Transport hints and visualizer inactivity
are wired. Full app measurements remain open. See
[the performance requirements and evidence](performance-contract.md).

The default HTTP transport and artwork cache scan now wait for explicit work.
Their 73 and 13 normal cases pass. The shared display foundation is now in
application source. Its matching normal host passes 551 cases across 18 suites
with no warnings or skips, including the corrected reader unlock and Session
request cleanup. The UI passes 302 cases with two exact baseline Qt warnings.
The related native runtime suites pass 103 cases with no warnings or skips.
The configured MainUI object and 21 native producer objects compile against
the matching headers. Matching sanitizers pass all 551 checks. The Windows
build and package checks pass, and the usual local folder is updated. The
manual GUI checks and full app performance comparison remain open.

The user approved code edits, fixes, builds, staging, commits, and the native playlist transaction plan on 27 October 2026. Code staging now succeeds. The earlier scope hold has ended. See [the pending review](pending-implementation-review.md) for test results and required repairs.

The typed artwork aggregate and serial Service runner are in application
source and all three build definitions. The production portable image adapter
now prepares package, account, keys, and TLS on joined work. Cache reads,
decode, and writes also run on joined work. The old synchronous image API is
removed. The current five-suite cohort passes 271 normal and 271 ASAN/UBSAN
checks with no skips or diagnostics, including the actual
Manager TLS broker, managed cache, final Core Apply, and surviving-owner
shutdown. See [portable artwork background work](portable-artwork-background.md)
for the final verification record and its limits. The Store memory token
covers one process. Profile ownership or cross-process revocation remains a
gate. Native owner activation, preserved-provider checks, and full app cutover
remain open. The current Windows review package includes these changes.
See [the serial runner](artwork-serial-runner.md),
[the aggregate](artwork-attempt-aggregate.md),
[positive Core authority](artwork-positive-source-authority.md),
[portable positive transfer](artwork-positive-host-transfer.md) and
[native positive authority](artwork-native-positive-authority.md).

A real two-process control confirms that Store authority invalidation is local
to one process. The native profile fence now has shared peer and exclusive
replacement modes. The cohort passes 36 normal and 36 ASAN/UBSAN checks
on Linux, plus 32 native Windows checks. The helper inheritance and native
cancellation barriers pass. The stamped writer helper described below is
implemented. The cross-process control channel and production mount remain
open. See [profile coordination](profile-authority-coordination.md).

The durable profile generation and guarded writer helper now pass 70 normal
and 70 ASAN/UBSAN checks on Linux, plus 66 native Windows checks. Real process
replacement refuses old tickets. A killed writer leaves a recovery marker.
Recovery validates the store before a new ready generation. The callback
retirement control exposed a use-after-free in the initial helper; retained
state and a closed guard repair it. The peer control channel and locked
production store bindings remain open. See
[guarded profile generations](profile-generation-transactions.md).

## Verified work

| Area | Implementation and evidence |
| --- | --- |
| Portable worker | Pinned Wasmtime 50; no WASI imports; bounded memory, dispatch, messages, and fuel; process IPC; additional Linux process restrictions. All seven Linux Rust tests pass, including rejection of old guest imports and exports. The final encoded IPC limit includes number normalization, the envelope, and separators. |
| Windows worker | The current Hakone worker builds with the existing Rust 1.99.0 toolchain and Windows GNU target. It passes seven native Windows unit tests. The packaged worker also loads the packaged TheSubtitleDB module, returns `auth_required` with no key and no host call, and exits cleanly. See [the local Windows checkpoint](windows-local-build.md). |
| TheSubtitleDB | Portable Rust/Wasm package; individual API-key settings; typed subtitle search; episode scope; language and page controls; asset resolution. Fourteen unit tests and three worker fixture tests pass. |
| Real subtitle service | The native search and Save path returned four candidates and a 40,610-byte local subtitle through the host broker. The test used the supplied key in temporary session storage. No key entered source, saved settings, worker messages, or logs. |
| Package host | Strict manifests; repository, tag, and content identity; isolated Git object fetch; immutable release snapshots; permission approval; enable, disable, update, and rollback. SHA-1 and SHA-256 repositories use exact tag advertisement, matching object format, and one time budget. All 28 Git and seven parser cases pass normally and with sanitizers; 69 related package cases pass. See [Git object formats](git-object-format-contract.md). |
| Repository work lifetime | Staging and update checks use joined app work. Cancellation reaches each Git process without a Qt event. Eleven focused cases pass normally and with sanitizers. Package integration passes 33 cases. The runtime run passes 16 cases; a separate fresh-worker run covers its one skipped case. See [the repository lifetime contract](repository-worker-lifetime.md). |
| App package recovery | Bundled updates and restores use captured permission review. Removal can preserve data or delete the exact package data and keys. A shared retirement hold refuses fresh readers during removal. Current checks pass 187 normal, 59 focused QML, and 87 focused sanitizer cases. Windows/macOS wallet execution and the fresh full app check remain open. See [recovery and distribution](recovery-and-distribution-contract.md). |
| Credentials and network | OS credential store with an explicit temporary-storage option; exact origin and method scopes; DNS and public-address checks; pinned connections; redirect checks; credential removal on cross-origin redirects. Native tests pass. |
| Native media stream permission | Packages can request only `media.stream` with `public-https-media@1`. The host describes public playback and the separate source approval; wider scopes and visualizer use are refused. Current checks pass 117 host and 29 installer UI cases. WSS handshake header names are also reserved by prefix. Playback capability remains false until the production source route passes. See [the permission contract](native-media-stream-permission.md). |
| Guest private storage | Mounted `storage.get`, `storage.set`, and `storage.remove`; shared package quotas; account partitions; pure migrations; matching rollback snapshots. All 21 current cases pass normally and with focused sanitizers. See [the storage contract](guest-storage-contract.md) and [joined Qt work](qt-work-lifetime.md). |
| Metadata coordinator | Ordered providers; partial field patches; provenance; strong identity checks; protected fields; bounded cache partitions; independent cancellation. Native tests pass. |
| Prepared metadata invocation | A joined preparer captures the selected artwork slot, exact configuration and approved keys. Owner admission uses copied memory. The child must reach clean EOF before it transfers a receipt. A separate owner flag and shared callback capture protect shutdown. All 120 current normal and 120 sanitizer cases pass; the missing-owner-check control exposes the expected use-after-free. App cutover and general metadata-plan adaptation remain open. See [prepared invocations](prepared-metadata-invocations.md). |
| Provider preference transactions | Provider rank and builtin enablement use the shared store lock and authority. Each save merges into a fresh document. Synchronous native watches finish before publication. Current package and runtime suites pass 130 normal and 130 focused sanitizer cases. The complete attempt plan and final native negative outcome remain open. See [provider preferences](provider-preferences-contract.md). |
| Identity evidence boundary | Identify, enrichment and asset search use one bounded identity normalizer. Strong IDs retain their entity scope; podcast episodes need their own evidence. The current coordinator passes 79 cases and the matcher passes 178 cases in both normal and focused sanitizer builds. Persistent identity selection and native Apply integration remain open. See [identity evidence](metadata-identity-evidence.md). |
| Identity choice store and Service | A bounded host store supplies opaque offers and explicit selections. The Service binds them to retained native targets and current provider/account/settings partitions, then requests a draft for the selected identity. Coordinator 44, MediaIdentity 46 and Choices 33 cases pass normally and with focused sanitizers. The native Service passes 56 cases and the relinked metadata Owner passes 11 in each build. All five groups pass 190 normal and 190 focused sanitizer cases, with no failure or skip. The dialog, persistent accepted identity, accepted revision and final native Apply wiring remain open. See [the choice store](metadata-identity-choice-store.md) and [the Service contract](metadata-identify-service.md). |
| Episode consumer | Eleven real consumer parity cases pass. Host results retain their per-field source credit in a separate cache namespace. The old episode cache reader and local readers remain. |
| Subtitle integration | Native OpenSubtitles and portable providers use one provider order. Host handles resolve through the normal Save path. The latest native suite passes 53 cases. |
| Qt build and release | The previous Linux Qt module and compiled QML checkpoint passed 709 cases in 27 suites, with no failures or skips. The Windows build at 2a689c3a11 passes its complete build, 416 PE checks and seven exact bundle checks. Its private app run installs all seven IDs, confirms private settings/store/module paths and exits normally after stable responsiveness checks. One initial cold-start stall, the actual Extensions click and a fresh Linux app/UI run remain open. See [the Windows checkpoint](windows-local-build.md). |
| Settings and install UI | Host-rendered approval and generated settings preserve secret keep, replace and delete actions. General now follows the reference row layout and keeps the selected accent in high contrast. Extensions uses a compact list and lazy dialogs. Its async list passes 19 normal and 19 focused sanitizer cases; 149 QML cases cover loading, retry, stale actions and synchronous save/removal updates. See [General](settings-redesign-refinement.md), [Extensions](extensions-settings-ui.md) and [background reads](package-summary-ui.md). |
| Native metadata bridge | A Qt-free asynchronous bridge copies requests and native field patches. The original 14 symbols and explicit Apply have core exports. Apply compares field/identity/extra tokens, original URI, exact policy, cancellation, and owner state before writes. It fills missing values by default. Only native code can request replacement. All needed tokens are reserved together. Normal commit, limit, backend, and revision suites pass; focused sanitizer runs pass. Consumer mounts remain open. |
| Native artwork fetched outcome | An exact child Apply receipt stages approved local artwork for the final native transaction. Artwork, approval, native facts, and fetched status commit together after all token checks. Protected user artwork and existing status bits remain. Eight fresh-Core normal suites and 28 focused real-source sanitizer groups pass with no diagnostics. The private helper has a Core export. Trusted exhaustive remote not-found handling remains open before provider cutover. See [the artwork outcome](native-artwork-outcome.md). |
| Native artwork not-found transaction | A sealed native plan summary and admission gate bind a negative outcome to an exact child and working snapshot. Final Apply checks effective identity and extra data while it holds both items through publication. The coherent Core and LibVLC pass 12 normal suites; eight focused sanitizer suites also pass. Typed Qt aggregation, full provider authority checks and app activation remain open. See [the native transaction](native-artwork-outcome.md). |
| Qt native outcome bridge | The Qt context consumes exact sealed negative receipts into native-only authorities. Image routes require a positive match. The context passes 31 normal and 31 focused sanitizer cases, including conversion and repeated retirement races. The complete ranked aggregate and app activation remain open. See [the outcome bridge](native-provider-family-contract.md). |
| Native provider claims and scopes | One named native artwork family can issue an opaque claim for an exact pending job and immutable snapshot. Six groups over 18 contexts pass normal and focused sanitizer checks; all 12 private exports are present. A private coordinator scope prevents cross-anchor sharing and never enters portable context. The joined context, sealed image route, and pure family plan pass 90 normal cases: coordinator 34, plan seven, context/materializer 15, and original artwork API 34. Focused sanitizers pass the first 56 cases with no diagnostics. The manual image result retains its native consent permit through final Apply. The prepared service/factory owner has a later checkpoint below. Status-outcome parity, the identity-choice gate, and production activation remain open. See [native provider families](native-provider-family-contract.md). |
| Native lookup completion evidence | The named finder and sealed receipt have closed outcomes. Retained receipts keep their admission slot. Private stream observation, a finite filter profile and HTTP parser witnesses refuse incomplete negative results. The fresh native closure passes 13 normal suites, including real filter admission and byte/block sources. All 13 focused sanitizer suites also pass with no sanitizer finding. Qt typed aggregation, the final not-found transaction and owner activation remain open. See [completion evidence](native-artwork-completion.md). |
| Native metadata provider owner | The factory reserves one service owner, installs per-binding native artwork handlers, and selects one implementation for each family. The five-suite baseline passes 104 normal and 104 focused sanitizer cases. A later cleanup repair passes 11 owner cases in each build; the old-code control fails only the address-reuse case. Activation stays off by default. The full app and provider parity checks remain open. See [the owner contract](native-metadata-owner-contract.md). |
| Native metadata lifetime | Each VLC instance has one metadata owner. Shutdown retires that owner before interface teardown. The request list cancels work by caller and keeps callback state until callbacks return. Idle cancellation prevents later dispatch. Normal owner and request-list suites pass, including real instance cleanup, owner races, and cancellation between list insertion and Start. Both preparsers use guarded metadata transactions. The Qt provider owner and family cutover remain open. |
| Native metadata admission | A native gate can suspend new backend dispatch, cancel earlier direct work through its native interrupt, and resume only after that work ends. Six added owner groups cover queued and running work, shared-instance counts, interrupt acknowledgement, bounds, and stale leases. Normal backend, owner, request-list, and affected native suites pass; focused normal and ASAN/UBSAN runs pass. The fetcher stage passes 13 normal and sanitizer groups, including the real native executor transition. The Qt provider-owner activation remains open. |
| Native library transactions | The approved dependency patch adds fixed playlist snapshots, one atomic append, and exact source-record preparation. All 23 transaction/preparation groups and 13 existing playlist cases pass. The patch applies exactly after the previous six patches. The normal VLC module and C wrapper tests pass. The mounted picker/store passes all 21 cases through the real app object closure, including discovery append and active shutdown. The final service and QML suites each pass 11 cases. Source playback remains gated on its resolver and transport. |
| Native library work lifetime | Native work acquires a pending lease before submission and calls Begin on its worker. Shutdown refuses later Begin calls and waits only for running native calls. Pending leases can outlive the library and need no Qt completion. Four real-instance groups pass. All MediaLib task overloads now use this lease. The actual app dispatcher test proves that Core cleanup waits for active work, rejects queued work, and finishes without Qt completion delivery. |
| Joined library workers | The add-on library queue retains its app work and quota until its worker and Qt thread storage have ended. All 17 focused cases pass normally and with sanitizers. All 40 legacy runner cases pass. A new full-app dispatcher case still needs the fresh Qt module. See [the library lifetime contract](native-library-worker-lifetime.md). |
| App shutdown coordinator | The app retains its Qt event loop while joined work ends. Retirement hooks close mounted native owners before the drain. All 12 focused cases pass normally and with sanitizers. The full module build and the remaining worker mounts still need verification. |
| Session authority | Exclusive control leases; typed observations; media and revision checks; duplicate rejection; deadlines; local user priority; owned rate restoration. WSS passes 15 normal and 15 sanitizer cases; host/controller passes 32 in each build; real native player passes 11 in each scoped build. The old close order fails the new real-player revocation test. Two players reduce a known seek-target offset; output-clock synchronization is not proven. See [the session host contract](native-session-host-contract.md). |
| Session app owners and host panel | MainUI constructs the native adapter, Host, controller, and trusted review panel through one owner. App and Manager retirement close it before member teardown. The panel passes 18 normal, 18 sanitizer, and eight QML cases. The actual-player assembly passes ten normal and ten focused sanitizer cases. The actual MainUI object compiles. Full app linking, compiled QML, two-player correction, and platform checks remain open. See [session app owners](native-session-app-contract.md). |
| Native source lease retirement | Concurrent cancellation waits for the one native transport callback. Same-thread callback reentry returns without waiting for itself. The last release keeps a live cleanup reference. All 14 actual-source fixture groups pass normal and sanitizer checks; the old once-flag implementation fails the new completion assertion. The normal Core rebuild passes 54 actions, and its registered fixture passes all 14 groups against the fresh library. The full app build remains open. See [lease retirement](native-source-lease-retirement.md). |
| Native source watches and cancellation | Package and composite consent watches revoke exact pending and issued native work. Concurrent invalidation waits for transport cancellation even when Stop or backend Close started it. Native cancellation uses a bounded identity mailbox. All 27 consent and 41 resolver cases pass normally and with focused sanitizers. The owned source, queue, transaction, service, and separate pure-host checkpoint passes 57 normal and 57 sanitizer cases. Actual playback, replay, and authority withdrawal during queue admission pass. The fresh full app and platform checks remain open. See [source retirement](native-source-foundation.md) and [source ownership](discovery-source-owner-contract.md). |
| Source consent UI and app binding | The trusted facade owns one captured review, permits only the declared public HTTPS profile, and checks the same review before an explicit Allow or Remove action. All 20 facade cases pass normally and with focused sanitizers. The dialog passes nine QML cases; discovery passes 12. The real native binding passes nine normal and nine sanitizer cases, including saved Allow/Remove and retirement. The mounted capability passes all 39 runtime cases in each build, with the packaged worker and no skips. MainUI supplies the native owner, consent panel, and stable-reference factory; its actual object compiles. Full app linking, compiled QML, and platform checks remain open. See [source playback review](source-consent-ui.md). |
| Audio analysis | The callback copies bounded samples without allocation or locks. A separate worker calculates spectrum, waveform, and levels. Six cases pass for formats, time, media changes, and overload. |
| Audio observer lifetime | A sample listener retains the exact audio output that owns its meter. Removal uses that same output before release. The new normal native regression passes six public output resets, with listener removal in both orders and no extra test hold that could hide the defect. Native visualizer clock, playback, and rendering integration remain under work. |
| Visual scenes | Catalog reads, resource validation, Wasm parameter work, and rendering run outside the GUI thread. The normal app mounts the surface and selection UI. Registered normal tests pass: controller 13, resources 12, render queue 12, worker session 15, worker process six, native clock 11, real player six, and real window three. Tests cover media/seek/rate/format epochs, output reset, observer release, pixels, graph loss, and window recreation. A physical GPU failure was not tested. |
| Visualizer parameter schema | Named scene nodes expose only declared numeric fields and ranges. Updates have 64-entry/16-KiB limits and preserve aggregate particle limits. Seven parameter cases and both existing seven-case scene/controller suites pass. Worker scheduling and native renderer lifecycle integration remain open. |
| Host API dispatch | A closed operation map; permission and operation scopes; request limits; cancellation; handler lifetime; thread-safe completion. Eleven focused cases pass. Individual service adapters still need integration tests. |
| Native invocation guards | C++ owners can supply one memory-only guard per invocation. Worker dispatch, host calls, replies, and terminal results use that guard. HTTP and host adapters retain it through admission. A stale invocation ends without failing its siblings. The group passes 115 normal and 115 focused sanitizer cases: 14 guard, 70 broker, and 31 existing runtime cases. The packaged-worker runtime path executes with no skips. The fresh full app and native source owner checks remain open. See [native request guards](native-invocation-guard.md). |
| Host provider IDs | Live native registrations use `hakone.builtin.*`. Both old and new host namespaces are reserved against installable package IDs. Saved order and enablement move in memory, with explicit Hakone values taking priority. All 35 runtime cases pass normal and sanitizer checks; the installer UI passes 29 cases. The actual subtitle source object compiles with strict app definitions. The complete subtitle suite needs the next coherent metadata build. See [builtin name migration](builtin-provider-id-migration.md). |
| Rust author SDK | Owned ABI buffers, typed input views, pending-call ownership, and complete encoded output bounds. The 64-field credential fix passes nine Rust tests and four real-worker tests. All four artwork packages have been built with the fix; all five artwork worker fixtures pass. |
| Worker lifetime | Cancellation reaches the guest. Restart creates a new request-ID namespace. Failed objects cannot restart. Native authority is revoked before worker withdrawal, and only old requests are detached before callbacks can start a replacement. The coherent group passes 240 normal cases, including 70 broker and 31 runtime cases. Focused sanitizers pass 31 runtime and 13 host service cases. See [worker retirement](worker-retirement-contract.md). Credential packets contain presence flags only. |
| Session invocation limits | Host-call IDs belong to their active invocation, with a shared limit of 4,096 retained IDs. Terminal and canceled invocations release their entries. The current broker passes 70 cases, and the SDK passes ten tests, including late results across slices. The session app owner now has focused actual-player checks; the fresh full app run remains open. |
| Account scope | Workers and assets keep captured credential references, release, grants, and settings revision. External state changes revoke old work. Opaque account references preserve display-only changes and code updates. Declared account fields and key changes require a new account reference. |
| Native authority snapshots | Manager preparation reads memory only. Joined work verifies the exact package, contribution, settings, and account. Credential snapshots own a serialized cache, and each read checks the approved binding and current state. Native revocation precedes manager retirement, which refuses fresh readers. All ten focused cases pass normally and with sanitizers. The current related host group passes 185 cases with no skips. See [native authority snapshots](native-authority-snapshots.md). |
| Store authority epoch | Owners of the same store share a memory token. Authority writes invalidate it before file publication and again after finish or rollback. Bounded native watches cancel exact leases before publication, including across concurrent invalidations. Thirteen cases pass normally and with sanitizers, including 200 registration races and capture destruction at the quota limit. Data-only private storage writes keep the token. External file edits remain subject to background admission checks. |
| Artwork packages | MusicBrainz, Last.fm, French TV, and image search have portable packages. Eleven parser tests and five worker fixtures pass. Four public native image paths passed. Google and some old French URLs have incomplete live evidence; their core paths remain. See [the migration record](metadata-provider-migration.md). |
| Provider distribution | All four artwork packages and their 20 required files are in the normal install map. Startup imports missing distribution packages while it retains disabled and removed states. All 24 package cases pass. Existing core providers remain until the owned route and provider parity gates pass. |
| Themes and interfaces | Shared theme state reaches QML and native widgets. Contrast tests cover status text and accent surfaces. Known hooks support bounded JSON interface sessions, commands, home replacement, and typed subtitle/playlist selection shells. Nine native registry cases pass. The normal app build passes. Recovery and stale playlist target checks pass all 28 focused cases within the 707-case compiled UI run. |
| Static UI images | Declared package PNG/JPEG/WebP images use host tickets and native image claims. Reads, inventory checks, and decode work run in bounded background jobs. All 29 coherent image cases pass. The normal app build and 709 compiled UI cases pass. Shared image preflight has seven normal and sanitizer cases. |
| Image delivery authority | The image reader captures a retained memory-only store token before queueing. Cold delivery, warm-cache delivery, provider reads, and trusted native pixel snapshots check it. A short scope sweep withdraws old images. All 29 current image cases pass with the 25 package and five authority cases. These focused host changes still need the next full app rebuild. |
| Discovery browser | Roots, folders, search, page cursors, independent views, and stable source admissions are mounted. Real queue tests cover one-batch append, revisions, title refresh, export, and account changes. Production Play and Enqueue use the native source owner and trusted approval flow. The fresh full app check remains open. |
| Radio Browser reference source | The new `org.hakone.radio-browser` package supplies roots, browse, search and fresh UUID resolution. All 16 Rust and nine actual-worker cases pass. A coherent Linux live probe decodes 30 HLS audio blocks, preserves the queue count, and completes Stop and joined shutdown. The app distribution includes the package. Its current MainUI object compiles, and the registered normal worker test passes. Full app and other-platform playback checks remain open. See [the source contract](radio-browser-source.md). |
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

- The session host no longer starts its timer while unused. Pending approvals
  use one expiry wake; player polling runs with active session work. The idle
  regression fails against the previous host, and all 38 normal and 38 focused
  sanitizer cases pass with the change. This is a call-count and lifecycle
  check. Full app performance, repeated UI reads, and startup work remain open.
  See [the performance requirements](performance-contract.md).

- Desktop Windows remote-file reads now have a native cancellation handshake
  with one sequence per read. Actual Windows checks pass 15 groups and 298
  assertions twice, including the real filesystem and UNC Read functions.
  Three deliberate race defects fail their expected assertions. The full
  Windows app builds and is published at `fd3fed4173`. The same read fixture
  also passes against its exact published Core and LibVLC. The user's Close
  retest remains open. See
  [Windows network-file Close](windows-network-file-close.md).

- The Service now places registration and owner changes behind the native
  publication barrier. Its private capture keeps raw unavailable rows and
  refuses foreign or expired plan owners. The complete current host/native
  build passes 115 targeted normal cases. The same 83 Service/native-owner
  cases pass focused sanitizer checks. The typed artwork runner and native
  owner activation remain open. See
  [the registration barrier](metadata-registration-barrier.md).

- The memory-only Manager catalogue factory and native registration barrier
  are in the source. Their copied checks pass 32 normal and 32 focused
  sanitizer cases. A fresh applied-source build passes all 32 normal cases.
  The Service mount and native runner remain open. See
  [registration barriers](metadata-registration-barrier.md) and
  [provider snapshots](metadata-catalogue.md).

- The local Windows build now uses clean source `fd3fed4173` and includes playback Stop before user-quit cleanup plus remote-file read cancellation. Its full build, 868 managed file hashes, and 416 PE checks pass. A fresh Windows console probe imports seven packages, captures all seven raw contributions, and checks the owned-reader factory. The user's network-file Close and full GUI checks remain open. See [the Windows record](windows-local-build.md).

- The [private artwork plan](artwork-attempt-plan.md) retains all raw providers, fixes the restored-provider preference case, and shares exact configuration and identity projection with the Manager. The combined current-source host build passes 359 cases, including all 90 package cases. Separate plan and catalogue sanitizer groups pass 49 and 77 cases. Native runner and owner activation remain open.

- The [native artwork keeper](native-artwork-attempt-keeper.md) connects completion admission to the Core gate and retains native authority through final Apply. It passes 11 copied normal and 11 sanitizer cases, then 11 fresh working-tree normal cases. The typed aggregate, registration gate, and owner activation remain open.

- Host [metadata completion records](metadata-attempt-records.md) now bind one fresh provider invocation and its complete host calls. The combined catalogue, receipt, identity, and authority build passes 200 normal cases. Separate receipt and identity sanitizer checks pass 153 cases; the SDK policy schema passes 43. The native aggregate and owner activation remain open.

- The complete [metadata provider snapshot](metadata-catalogue.md) retains all packages and declarations before provider selection. It passes 34 catalogue and 13 authority cases in both normal and sanitizer builds on WSL Linux. Native owner activation and the Windows reader check remain open.

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
- The prepared native metadata provider owner has a five-suite baseline of 104 normal and 104 focused sanitizer cases. The later owner cleanup check passes 11 cases in each build. Native cancellation controls release Core references before joined retirement ends. MainUI binds the service to the app handle and closes metadata and queue owners at the early retirement hooks; its real object compiles. Factory activation and the full app checks remain open.

Metadata owner activation, complete artwork outcome parity, the normal app rebuild, and supported-platform checks remain open. The production discovery owner is mounted and has focused checks. The session app owner and panel now have focused checks. Their full app link and compiled UI run remain open. Work in progress on those gates is separate from the verified checks above.

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

- Meet the user's near-zero idle and basic UI performance requirement. Remove
  recurring inactive-feature work and synchronous UI package I/O. Measure
  startup, interaction latency, idle CPU, memory, and worker cold/repeated cost
  against controlled baselines. This gate precedes further owner activation.
  See [the performance requirements](performance-contract.md).

- Finish supported-platform app/runtime/package tests and repeat release checks after the remaining implementation changes.
- Verify the implemented application-distribution upgrades, recovery UI, and safe mode in the fresh full app. Repeat the verified Git object-format checks on supported platforms. Keep publisher verification claims consistent with actual evidence.
- Keep typed host result adapters as the only path from modal replacements to core state. Repeat the app checks after the remaining feature integrations.
- Complete the owned metadata route for player, preparser, library, and artwork requests. Preserve each working provider as an add-on before removal.
- Complete discovery subscriptions and the native lazy playback route. Enforce stream policy through redirects, nested manifests, segments, and keys before playback.
- Keep the verified stable queue reference, native playlist CAS, and library shutdown boundary. Complete their use with contained playback and subscription updates.
- Keep the verified visualizer worker, renderer, native clock, and window lifecycle boundaries. Repeat supported-platform app and graphics checks after the remaining integrations.
- Verify the connected native session authority, Qt host, consent UI, and outbound WSS in the fresh full app. Exercise two-player drift correction and the broad native player regression suites.
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
