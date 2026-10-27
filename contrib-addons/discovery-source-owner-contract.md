# Discovery source playback owner

The native host owns source activation and playback approval. A package cannot create a playback intent. A saved source URI does not give playback authority.

## App construction and retirement

Create one `AddonDiscoverySourceOwner` on the Manager thread. Give it the current Manager, the native instance, the shared `AddonSourceAdmissions`, and the app work handle. Call `activate()` explicitly. Activation does not approve a source.

Use `createPlayerDiscoveryWithSources()` with the same owner and native queue backend. The older playable-handle factory remains available for its existing callers. The new factory does not call the provider's `resolve` operation from a browser action.

Connect app retirement to `close()` before native Core or the app is destroyed. The owner closes native authority first. Its counted app work releases file readers and Qt data before app drain ends. Keep the native instance and the Qt app valid through that drain. A publication gate alone does not meet this requirement.

## Core approval UI

`requestApproval(source)` returns a private request ID. A successful capture emits `approvalReady(id)`. It does not emit a successful `actionFinished` signal. A failed capture emits `actionFinished(id, false, error)`.

`approval(id)` returns these captured properties:

| Property | Meaning |
| --- | --- |
| `id`, `name` | Contribution ID and package display name |
| `source`, `kind` | Approved repository, developer source, or app distribution identity |
| `accountLabel`, `accountFields` | Bounded nonsecret account labels |
| `accountReference` | Opaque account identity for technical details |
| `profile`, `profiles` | Exact native media profile |
| `publisherVerified` | False; this owner does not verify a publisher |
| `approved` | Current saved playback approval |

No key, credential reference, provider API address, or resolved stream address enters this map. Render package labels as plain text.

The review expires after 30 seconds of steady time. The UI must reread and compare the exact displayed map before it calls `approve(id)` or `revoke(id)`. Each accepted action consumes the review. A successful saved action emits `actionFinished(id, true, {})`; a failed action supplies a host error. `discardApproval(id)` closes the review and preserves saved approval. An immediate false action result means no save task was admitted.

`refreshSource(source)` reads current saved approval on joined work. `canUseSource(source)` reads memory only and remains false until that capture succeeds. It is false while the owner is inactive or closing. `canUseReference(reference)` also compares the exact account. `changed()` updates readiness in open browser views.

## Saved reference and native open

The queue stores only the admitted source/account/item locator and bounded display metadata. Enqueue does not obtain a stream URL. Play uses the same saved locator and adds the native source-only policy before native input creation. It reuses an existing queue entry for that locator. A stale queue identity or revision refuses the action. The factory also captures a memory-only source authority query before private admission. The native adapter does a check of this query under its final queue lock. A source or account change before that point refuses the commit.

Only native primary playback dispatch captures an authority reader and mints a one-use `PrimaryPlayback` intent. The joined reader uses `admittedReference()`; it does not call the admissions object's GUI validator. It requires the exact enabled, configured discovery source, account, declared resolve operation, and declared plus approved `media.stream` profile `public-https-media@1`.

The owner calls the current Manager's guarded resolve overload on the Manager thread. The provider can return only a bounded HTTPS media address, supported format, and optional future expiry. The contained transport replaces the caller's authority with the captured admission. It remains dormant until Core attaches its native lease and policy. Every remote request and redirect uses current source, account, saved approval, and network policy checks.

Stop and replay use a new native input generation and a fresh provider result. No resolved URL or key is persisted in the queue or playlist.

## Native invalidation

Package/configuration authority and saved source approval have separate native watches. Approval does not invalidate its own package capture. Revoke, reset, owner retirement, and package authority changes revoke admitted leases without a GUI event or file read. Concurrent native Stop and authority withdrawal wait for the same native cancellation callback to end before a writer can publish new authority.

The native cancellation mailbox contains only job pointer identities and generations. It never dereferences a retired pointer. Overflow closes admission and the native backend before further dispatch. It does not retain a job/backend/mailbox cycle.

The same-process store and approval epochs give the immediate invalidation boundary. A cross-process file change requires the full background check before the next request. This design does not claim an instantaneous cross-process revocation lease.

## Evidence boundary

The consent/resolver foundation has 27 consent and 41 resolver cases in normal and sanitizer runs. The current owner/queue cohort also passes these cases in both modes:

| Suite | Cases |
| --- | ---: |
| Source owner, including real queue, Manager, contained TLS, and native WAV decode | 12 |
| Native queue, including authority withdrawal during admission | 10 |
| Queue transactions | 11 |
| Discovery service | 12 |
| Separate host service build without native headers or Core libraries | 12 |

No sanitizer report occurs in these runs. The queue-only native fixture retains one ordinary Core message about an unavailable default audio output in the limited module set. The real playback fixture selects the dummy output and has no diagnostic output.

The native sanitizer fixture uses the observed HTTP/WAV decoder modules, their configuration modules, and the native fallback resampler. A separate no-owner control reproduces the full module scan's 20,918-byte exit leak in 145 allocations. A per-module no-owner control isolates an additional 8-byte exit leak to the optional `soxr` module. These controls do not change production codec selection. The limited module run uses `ugly_resampler`, proves real native decode, and has no leak report.

The app mount and truthful Manager capability flag remain a separate integration checkpoint. Do a check of that proof before the app enables this platform.

The C++ fixture friend can install synthetic DNS, TLS trust, and a loopback connection for native tests. The production constructor and factory expose no endpoint override. No public service or account key is required for these tests.
