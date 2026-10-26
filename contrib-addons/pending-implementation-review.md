# Pending implementation review

Updated 27 October 2026. The user has approved code edits, fixes, builds, staging, commits, and the native playlist transaction plan. The earlier scope hold has ended. Code staging now succeeds.

## Verified work ready to record

- Guest storage: 18 cases pass. Package, runtime, broker, and host service regressions pass. The final host service cancellation test also passes.
- SDK credential capacity: nine Rust cases and four real-worker cases pass. The SDK accepts the host's 64 credential presence fields. All four artwork packages have now been built with that SDK change. All five artwork worker fixtures pass.
- Typed interface and recovery: the normal app build, final host service link, and nine native registry cases pass. The broad interface run has 548 passes and one failure, with no skips. The failure is in `tests/tst_AddonTypedDialogs.qml:166`: the saved playlist ID is `playlist-24`; the expected ID is `playlist-23`. The cause is not confirmed. The earlier focused 17-case run passed. Keep this group open until the failure is explained and repaired.

The [implementation record](IMPLEMENTATION.md) lists the committed boundaries. The [playlist plan](native-playlist-cas.md) is now approved. No earlier rejected database change ran.

## Playlist recovery: test and submission guards

The read-only trace found two separate repair needs. No new test ran during this review.

The earlier test changes the second row from `playlist-23` to `playlist-24`. Test setup then clears and fills the model with `playlist-23`. The recovery test waits for a non-null delegate and calls its `clicked()` signal directly. It does not first confirm the delegate's current key.

In Qt 6.8.3, `itemAtIndex()` reads the visible-item cache. Non-reset model changes schedule a later layout; `forceLayout()` applies pending changes. This supports a possible old-delegate timing cause. The model and delegate values at the failure were not captured, so this cause is still an inference. See the [Qt item-view source](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.3/src/quick/items/qquickitemview.cpp).

Repair the test precondition: apply pending layout, confirm that the model ID and delegate key are both `playlist-23`, then use a mouse event to select the row. Keep the final saved-ID assertion. Do not close the regression from a longer wait alone.

The product also needs a guard. The built-in delegate copies its key to `selectedKey`; `submitCore()` emits that key without a current model or revision comparison. Model signals invalidate add-on choices but leave the built-in selection intact. The native save path rejects a missing or read-only playlist. It can still accept a different writable playlist that exists.

Clear the built-in selection when its model revision changes. Reject a delegate selection that no longer agrees with the current row. Before submission, confirm the selected target and captured revision again. Add a separate regression that changes or removes a selected target before submission. Require a fresh selection and no save from the old target. The native [playlist transaction plan](native-playlist-cas.md) covers the separate change between UI validation and database append.

The recovery path does not supply an add-on result. This failure does not establish a defect in opaque choice-reference resolution.

## Plain-text metadata rendering

Untrusted provider text must remain plain text in labels, captions, choices, and tooltips. Qt AutoText can interpret image markup outside the add-on network broker. The narrow repair is an explicit plain-text format at each consumer.

The read-only audit found these consumers under `modules/gui/qt/player/qml/redesign/`:

| File | Consumer |
| --- | --- |
| `RedesignPlayerView.qml` | Audio title at line 401, artist at line 410, and header title at line 717 |
| `RedesignMiniPlayer.qml` | Title at line 176 and artist caption through the button at line 97 |
| `RedesignEpisodesView.qml` | Episode title tooltip at line 215 and filename choices at line 271 |
| `RedesignTracks.qml` | Track names and tooltips at lines 78 and 84 |
| `RedesignLibraryView.qml` | Title tooltip at line 593 |

The audit confirmed plain text in package approval, generated settings, discovery, queue, provider names, selectors, and visible episode titles. Native media-info text uses plain controls or escapes its text. The audit found no current artwork attribution-text renderer; that is still an integration requirement.

Repair tests must use title, artist, and filename text with image markup. Confirm that the text displays literally and that no image, file, or network request starts. Keep normal text, keyboard behavior, and layout intact.

## Source owner retirement

The isolated `source_owner` draft passes its initial ownership and cleanup tests. It is not registered or mounted. Review found two additional cases that its current draft does not handle:

1. `Close` on an instance with no owner must still prevent a later `Set` during cleanup.
2. `Forget` must keep the retirement state through the final backend release. A destroy callback must not create another owner or drain the same entry twice.

Use a private `source_owner_retiring` flag in `libvlc_priv_t`. Initialize it to false in `libvlc_InternalCreate`, before the instance is published. No existing exit, interface, or playlist field supplies this state. Protect the new flag with the existing source `owner_lock`.

`Close` sets the flag before it looks for a registry entry. `Set` tests the flag before it allocates or attaches an entry. Never clear it for that instance. This works without an allocation when the registry is full or no owner was installed.

The persistent flag permits a simpler `Forget` path than the earlier entry-retention proposal: unlink the entry once under `owner_lock`, then release the detached backend outside that lock. During its destroy callback, `Set` fails from the instance flag; `Forget` finds no entry; `Hold` returns NULL. No separate tombstone allocation is required. Before `Forget`, `Hold` can still return the closed owner for an active completion callback.

The private cleanup functions run on the instance cleanup thread. Mount `Close` before `intf_DestroyAll` in `src/libvlc.c`. Mount `Forget` after the VLM, playlist, media-library, and media-source users end, before dialog, key-store, logger, and module cleanup. An idempotent `Forget` before final object deletion supplies a last guard. These calls are not a concurrent callback drain. Host callback state must remain valid until an active callback returns.

The current isolated tests create generic object roots. Change them to real `libvlc_InternalCreate`/`libvlc_InternalDestroy` instances before the code reads private instance fields. Cover an unbound Close followed by Set, allocation failure, full registry, destroy followed by Set/Forget/Hold, concurrent Set/Close, repeated cleanup, and a fresh instance after the previous one ends.

## Package images and theme resources

The read-only audit found no active package-image path that can load external image references. Themes accept hex colors and bounded numeric shape and effect tokens. UI icon names select fixed host SVG strings.

`RedesignAddonView.assetFor()` accepts only a key from the host asset map. The native registry currently supplies an empty map. A worker update changes the descriptor state; it cannot fill that map. Thus, declared UI Image and MediaCard artwork still need a mounted asset route.

The manifest permits package and contribution icon resource paths without an image-format restriction. The review found no current reader or renderer for those fields. Treat this as unfinished image support, rather than evidence of a current load outside the broker.

Before mounting package images, define the permitted formats, decode limits, and host-owned asset handles. Test an image with an external reference and confirm that no network or local-file load occurs. Also test account, release, and permission changes against old asset handles. Keep these tests separate from the metadata plain-text repairs above.

## Native open prerequisites

The source-only item policy must have an atomic get-or-create path. Concurrent inputs must use the same policy. Reject an unrelated or revoked policy. Accept a loopback result only through the core proxy profile, without source or attachment access.

The current helper drafts have a read-only review, but their new exports and native tests still need integration. Idle source-job cancellation must prevent Start without a terminal callback. This closes the interrupt-before-Start gap in the native waiter.

The native input owns the transport lease. Stop and playback end cancel it. A manifest, segment, or key EOF keeps it active. Keep the queue item's source-only policy intact for replay.

The [stream contract](native-discovery-stream-contract.md) specifies the remaining access bridge, source consent, native intent, public HTTPS transport, nested media, and revocation tests. The [artwork approval contract](native-artwork-approval-contract.md) specifies the separate native record and consumer gates for scoped artwork. Neither route can be enabled from its draft alone.

## Metadata handover

The committed revision API supplies field and extra-tag tokens. The owned remote patch path must use them under the target lock. Validate all proposed field tokens before writes can change the metadata object. Keep user and local values protected.

Also compare the original native item URI and source scope. URI, option, and policy changes are outside the current metadata snapshot token contract. A snapshot of metadata tokens alone is insufficient to commit a result from an old source.

Keep the existing providers and local readers until the new consumer routes pass preservation tests. The artwork approval record, owner mount, preparser request list, and identity choice store still have integration work.
