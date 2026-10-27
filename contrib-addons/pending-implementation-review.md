# Pending implementation review

Updated 27 October 2026. The user has approved code edits, fixes, builds, staging, commits, and the native playlist transaction plan. The earlier scope hold has ended. Code staging now succeeds.

## Verified work ready to record

- Guest storage: 18 cases pass. Package, runtime, broker, and host service regressions pass. The final host service cancellation test also passes.
- SDK credential capacity: nine Rust cases and four real-worker cases pass. The SDK accepts the host's 64 credential presence fields. All four artwork packages have now been built with that SDK change. All five artwork worker fixtures pass.
- Typed interface and recovery: nine native registry cases pass. The normal app and QML runner builds pass. The compiled runner passes 707 cases in 27 suites, including all 28 typed-dialog cases, with no failures or skips. It used a committed-source snapshot to exclude the next image feature's edits. The prior 548-pass/one-failure result remains historical evidence.

The [implementation record](IMPLEMENTATION.md) lists the committed boundaries. The [playlist plan](native-playlist-cas.md) is now approved. No earlier rejected database change ran.

The latest integrated app build passes. Its compiled UI run passes 709 cases in 27 suites, with no failures or skips. The normal image suite passes 25 cases; the metadata service passes 14; actual artwork consumers pass nine; the real library dispatcher suite passes 21. These results include the image and library mounts that were excluded from the earlier snapshot.

## Playlist recovery: test and submission guards

The initial read-only trace found separate test and product repair needs. Both now have source changes and passing regressions.

The earlier test changes the second row from `playlist-23` to `playlist-24`. Test setup then clears and fills the model with `playlist-23`. The recovery test waits for a non-null delegate and calls its `clicked()` signal directly. It does not first confirm the delegate's current key.

In Qt 6.8.3, `itemAtIndex()` reads the visible-item cache. Non-reset model changes schedule a later layout; `forceLayout()` applies pending changes. This supports a possible old-delegate timing cause. The model and delegate values at the failure were not captured, so this cause is still an inference. See the [Qt item-view source](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.3/src/quick/items/qquickitemview.cpp).

The recovery test now applies pending layout, confirms that the model ID and delegate key are both `playlist-23`, then uses a mouse event to select the row. It retains the saved-ID assertion. This establishes a current visible target before the click.

The old product path copied a delegate key to `selectedKey` and emitted it without a current model or revision comparison. Model signals invalidated add-on choices but left the built-in selection intact. The native save path rejected a missing or read-only playlist, but could still accept a different writable playlist that existed.

The repair clears selection on a model revision change. Both click and Save read the current target and compare its key, row, dialog generation, and model revision. Old asynchronous callbacks cannot submit. Separate regressions cover target replacement, removal, read-only state, move, reset, an old presented key, a late callback, and dialog closure. The native [playlist transaction plan](native-playlist-cas.md) covers the separate change between UI validation and database append.

The recovery path does not supply an add-on result. This failure does not establish a defect in opaque choice-reference resolution.

## Plain-text metadata rendering

Untrusted provider text must remain plain text in labels, captions, choices, and tooltips. Qt AutoText can interpret image markup outside the add-on network broker. The narrow repair is an explicit plain-text format at each consumer.

The repair covers these consumers under `modules/gui/qt/player/qml/redesign/`:

| File | Consumer |
| --- | --- |
| `RedesignPlayerView.qml` | Audio title, artist, and header title |
| `RedesignMiniPlayer.qml` | Title, artist caption, and folder label |
| `RedesignEpisodesView.qml` | Show and episode title tooltips, and filename choices |
| `RedesignTracks.qml` | Track names and tooltips |
| `RedesignLibraryView.qml` | Title tooltip |

The audit confirmed plain text in package approval, generated settings, discovery, queue, provider names, selectors, and visible episode titles. Native media-info text uses plain controls or escapes its text. The audit found no current artwork attribution-text renderer; that is still an integration requirement.

The repair sets an explicit plain-text format. Its 13 focused cases pass with no warnings or skips. Positive AutoText and RichText controls prove that the image provider, reserved HTTPS origin, and fixture-file read probes detect loads in WSL. All five repaired consumer cases retain literal text and report zero loads on these routes. The HTTPS test returns embedded image bytes without a connection. The file probe observes only its own temporary PNG. Other platforms retain literal-text assertions but do not claim the Linux file-read observation.

The existing player, episodes, chrome, and library suites also pass all 249 cases. The normal app build with the final test helper passes. The compiled QML run confirms these cases and the probes within its 707 total passes.

## Source owner retirement

The native `source_owner` registry is now registered and mounted in instance cleanup. Its normal C suite passes seven groups and 100 Set/Close races. Review led to fixes for these cases:

1. `Close` on an instance with no owner must still prevent a later `Set` during cleanup.
2. `Forget` must keep the retirement state through the final backend release. A destroy callback must not create another owner or drain the same entry twice.

The private `source_owner_retiring` flag in `libvlc_priv_t` starts false in `libvlc_InternalCreate`, before the instance is published. No existing exit, interface, or playlist field supplies this state. The existing source `owner_lock` protects the flag.

`Close` sets the flag before it looks for a registry entry. `Set` tests the flag before it allocates or attaches an entry. The flag is never cleared for that instance. This works without an allocation when the registry is full or no owner was installed.

`Forget` unlinks the entry once under `owner_lock`, then releases the detached backend outside that lock. During its destroy callback, `Set` fails from the instance flag; `Forget` finds no entry; `Hold` returns NULL. No separate tombstone allocation is required. Before `Forget`, `Hold` can still return the closed owner for an active completion callback.

The private cleanup functions run on the instance cleanup thread. `Close` runs before `intf_DestroyAll` in `src/libvlc.c`. `Forget` runs after the VLM, playlist, media-library, and media-source users end, before dialog, key-store, logger, and module cleanup. An idempotent `Forget` before final object deletion supplies a last guard. These calls are not a concurrent callback drain. Host callback state must remain valid until an active callback returns.

The tests use real `libvlc_InternalCreate`/`libvlc_InternalDestroy` instances. They cover an unbound Close followed by Set, allocation failure, full registry, destroy followed by Set/Forget/Hold, concurrent Set/Close, repeated cleanup, and a fresh instance after the previous one ends. Separate cases exercise the registry through actual native Cleanup and Destroy. Only Set and Hold have public exports; cleanup functions stay private. The native source access bridge is still open work.

## Package images and theme resources

The read-only audit found no active package-image path that can load external image references. Themes accept hex colors and bounded numeric shape and effect tokens. UI icon names select fixed host SVG strings.

`RedesignAddonView.assetFor()` accepts only a key from the host asset map. The mounted registry supplies tickets for declared package PNG, JPEG, and WebP images. A native claim binds each image to the same live session and image owner. A worker can select an alias; it cannot create a ticket or supply a URL.

The manifest permits package and contribution icon resource paths without an image-format restriction. The review found no current reader or renderer for those fields. Treat this as unfinished image support, rather than evidence of a current load outside the broker.

All 25 image cases pass, including painted pixels, external-I/O controls, static format limits, old-handle invalidation, slow reads, and Close/destruction while work is blocked. Full package reads, hashing, and decoding run in bounded background slots. Global resource reservations remain charged until those jobs finish. These tests remain separate from the plain-text repairs above.

## Native open prerequisites

The source-only item policy must have an atomic get-or-create path. Concurrent inputs must use the same policy. Reject an unrelated or revoked policy. Accept a loopback result only through the core proxy profile, without source or attachment access.

The core helper exports and normal native tests pass, including 16 concurrent source-policy callers. Idle source-job cancellation prevents Start without a terminal callback. The native input bridge now records approval before options and requires that approval for primary playback. Its seven bridge groups pass normally and with ASAN/UBSAN. Six real player/input groups also pass, including late URI mutation and non-playback refusal.

The native input owns the transport lease. Stop and playback end cancel it. A manifest, segment, or key EOF keeps it active. Keep the queue item's source-only policy intact for replay.

The [stream contract](native-discovery-stream-contract.md) specifies the remaining host resolver, source consent, native intent, public HTTPS transport, nested media, and revocation tests. Production Play and Enqueue remain unavailable. The [artwork approval contract](native-artwork-approval-contract.md) records the tested native record, consumers, and proof transfer; its owned preparser/provider cutover remains open.

## Metadata handover

The native Apply path now uses field and extra-tag tokens under the target lock. It validates all proposed fields and the identity baseline before any write. It fills missing values by default; only a native caller can request an explicit replacement. Existing user and local values stay protected.

Apply also compares the original native item URI and exact held policy, and refuses a revoked policy. A snapshot of metadata tokens alone cannot authorize a result from an old source. The backend and item locks define admission; a later cancellation or revocation does not reverse an admitted commit.

The complete set of new field tokens is reserved before writes. A failed reservation changes no target value or revision. Limit tests run the actual Apply code with a private test sequence; they cover a failed range, preserved fields, exhaustion between writes, and concurrent non-overlapping ranges. Normal core commit, limit, backend, and revision tests pass. Focused sanitizer tests pass. No production fault-injection API was added.

Keep the existing providers and local readers until the owned route passes preservation tests. The private artwork record, consumers, broker/service proof transfer, native owner retirement, and request-list lifetime have passing evidence. The preparser owner mount, direct legacy-network admission gate, and identity choice store still have integration work.
