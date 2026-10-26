# Pending implementation review

Updated 27 October 2026. Code, build, and staging actions are on hold for direct scope approval in this chat. The original request permits Markdown documents. The active implementation goal requests code, but automatic approval review did not accept that goal as authorization.

## Verified work ready to record

- Guest storage: 18 cases pass. Package, runtime, broker, and host service regressions pass. The final host service cancellation test also passes. The code has not been staged: automatic approval review rejected that action.
- SDK credential capacity: nine Rust cases and four real-worker cases pass. The SDK accepts the host's 64 credential presence fields. The four artwork packages still need a build with that SDK change. The rejected build command did not run or change those artifacts.
- Typed interface and recovery: the normal app build, final host service link, and nine native registry cases pass. The broad interface run has 548 passes and one failure, with no skips. The failure is in `tests/tst_AddonTypedDialogs.qml:166`: the saved playlist ID is `playlist-24`; the expected ID is `playlist-23`. The cause is not confirmed. The earlier focused 17-case run passed. Keep this group open until the failure is explained and repaired.

The [implementation record](IMPLEMENTATION.md) lists the committed boundaries. The [playlist plan](native-playlist-cas.md) remains a separate approval item. No rejected database change ran.

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

Use a native retirement flag that belongs to the VLC instance and needs no registry-entry allocation. `Set` and the cleanup transition must use one consistent lock order. Keep the flag until the native instance ends, including after the registry entry is removed.

During `Forget`, keep a retiring entry with a detach flag while the final backend reference is released outside the registry lock. `Hold` must return NULL after detachment. Reentrant `Set` must fail. Reentrant `Forget` must return without another release. Remove the entry only after that release completes.

The private cleanup functions run on the instance cleanup thread. `Close` must run before interface destruction can wait for an input. `Forget` follows the native input and player users. These two calls are not a concurrent callback drain. Host callback state must remain valid until an active callback returns.

Add tests for an unbound Close followed by Set, destroy followed by Set/Forget, repeated cleanup, and a new instance after the previous one ends.

## Native open prerequisites

The source-only item policy must have an atomic get-or-create path. Concurrent inputs must use the same policy. Reject an unrelated or revoked policy. Accept a loopback result only through the core proxy profile, without source or attachment access.

The current helper drafts have a read-only review, but their new exports and native tests still need integration. Idle source-job cancellation must prevent Start without a terminal callback. This closes the interrupt-before-Start gap in the native waiter.

The native input owns the transport lease. Stop and playback end cancel it. A manifest, segment, or key EOF keeps it active. Keep the queue item's source-only policy intact for replay.

The [stream contract](native-discovery-stream-contract.md) specifies the remaining access bridge, source consent, native intent, public HTTPS transport, nested media, and revocation tests. The [artwork approval contract](native-artwork-approval-contract.md) specifies the separate native record and consumer gates for scoped artwork. Neither route can be enabled from its draft alone.

## Metadata handover

The committed revision API supplies field and extra-tag tokens. The owned remote patch path must use them under the target lock. Validate all proposed field tokens before writes can change the metadata object. Keep user and local values protected.

Also compare the original native item URI and source scope. URI, option, and policy changes are outside the current metadata snapshot token contract. A snapshot of metadata tokens alone is insufficient to commit a result from an old source.

Keep the existing providers and local readers until the new consumer routes pass preservation tests. The artwork approval record, owner mount, preparser request list, and identity choice store still have integration work.
