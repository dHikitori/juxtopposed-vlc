# Metadata identification service

This change connects metadata identification to the native media target and
the bounded identity choice store. The API is for host C++ code. It does not
add a guest host call or a QML method.

The service returns an identity choice and a metadata draft. The trusted
choice dialog, saved accepted identity, accepted-identity revision, and final
native write adapter remain work. The native transaction stays the final
write authority.

## Host flow

1. Keep the original `StartLocal` anchor and its exact target.
2. Call `bindIdentityTarget(anchor, target, hostContext)`. Keep the returned
   opaque target reference until the flow ends.
3. Call `identify(feature, targetReference)`. Wait for the service's ordinary
   `finished` signal. Its candidates have opaque choice references.
4. Show the offered identities through a trusted host view. Require an
   explicit user selection, including when the offer has one candidate.
5. Call `acceptIdentity(targetReference, choiceReference)`. The returned
   selection reference binds the selected identity to its original target
   and provider partition.
6. Call `enrichSelected(feature, targetReference, selectionReference, fields)`
   to request a metadata draft for the selected identity. The service checks
   the selection again before it delivers the draft.
7. A later write adapter can call `holdIdentityAnchor` to hold the original
   anchor. It must use ordinary native `Complete` and `Apply`. It must retain
   provider and account authority through the commit.
8. Release choice sets, selections, and the target reference when they are
   no longer needed. Releasing the target cancels its retained anchor.

Accepting a choice does not change the native item. A held anchor also does
not grant a write. Final Apply must still check the original target and all
native revision and policy guards.

## Exact native binding

The service holds the original anchor and target. It permits at most 64
identity targets. The host context is fixed at binding. To change the host
context, create a new binding.

The anchor must be current for that exact target. It can be pending or a
successfully completed retained `StartLocal` anchor before Apply. It must
have `VLC_METADATA_REQUEST_NETWORK`. An automatic request also needs the
app's network consent. A manual query does not add network permission.

Before an offer, acceptance, selection read, follow-up request, or draft
delivery, the service checks the native anchor again. Native URI, owner,
policy, identity, extra metadata, and field revisions remain part of that
check. A value changed away and back has a different revision.

The choice store's private subject is a fresh nonce for the held binding.
The service supplies that subject only while the native anchor is current.
The nonce, native URI, and native policy do not enter provider requests or
offered choice rows.

## Provider and identity rules

An identify provider must declare the exact feature and `identify`
operation. Providers use the configured rank. The coordinator tries the next
provider when the current provider has no usable candidate.

The coordinator converts SDK `externalIds` and structured episode numbering
to `MediaIdentity`. It preserves the namespace and scope of each external ID.
Known IDs use the host's existing keys. Other IDs keep namespaced keys and
do not become strong identity evidence by themselves.

Provider projections also restore structured `externalIds` from the host
keys. This includes unknown namespaces and their separate scopes. A selected
identity can thus keep those IDs in a later SDK enrichment request.

The service supports movie, series, episode, music, podcast, and audiobook
identity kinds. Series has its own kind. A season-scoped external ID keeps
its scope; it does not create a season kind.

Seasonal, absolute, and air-date episode numbering use bounded values.
Conflicting IDs, conflicting numbering, unknown kinds, and malformed
candidates are refused. Host limits also bound title, alias, ID, and candidate
sizes. Provider match scores cannot accept an identity.

An explicit nonempty manual query for `video.identity` can offer movies,
series, and episodes from that video identity family. Automatic requests
and later enrichment use the declared media kinds. This manual search rule
does not apply to other features.

Every offered identity requires explicit host selection. The choice store
checks the current provider, release/account/settings cache partition, media
binding, and expiry before it issues or reads a selection. A provider or
account change makes an old selection unusable.

## Private source locators

`hakone-addon-source:` is an opaque source reference. Its payload is not a
filename or a title. Identity inference also refuses the old locator scheme.
The shared identity helper uses explicit native display tags for these
locators. It does not parse the locator or its parent as media identity.

The service also refuses an old URI-derived context that would restore the
private payload as a title. Explicit public identity facts can keep their
declared kind and scoped work IDs. Provider requests contain the public
identity projection.

## Cancellation and teardown

Releasing one target removes that target's offers and selections. Global
invalidation, shutdown, and provider changes retire the related work.
Detached native obligations end even if a cancellation callback deletes the
service.

Service destruction drains coordinator work while its cancellation maps are
still alive. Choice-store destruction drains all retained media holds,
including when destruction starts inside a selective invalidation callback.
These paths do not require later Qt event delivery to release native holds.

Selective release also drains other retained identity targets if cancellation
deletes the service. This includes anchors from a different backend. The
service cancels those anchors without closing that external backend.

## Verification

| Group | Normal cases | Focused sanitizer cases |
| --- | ---: | ---: |
| Metadata coordinator | 44 | 44 |
| Media identity | 46 | 46 |
| Identity choice store | 33 | 33 |
| Native metadata Service | 56 | 56 |
| Native metadata Owner | 11 | 11 |
| Total | 190 | 190 |

All five groups pass with no failure, skip, stderr output, or sanitizer
finding. The native checks compile the current C++ producers and matching
generated Qt metadata against the fixed native library closure. The focused
sanitizer build instruments those producers and the fixture's native bridge.
Qt and the shared native Core are not instrumented.

The checks cover exact target and provider bindings, explicit acceptance,
scoped external IDs in later enrichment, URI changes away and back, private
source locator handling, network permission, bounded holds, callback deletion,
and shutdown. The native finder fixture supplies controlled positive results.
These checks do not prove exhaustive negative artwork results or the full
application choice flow.

## Remaining integration

Complete the trusted choice dialog and its app mount. Save an accepted
identity with a monotonic revision. Add that revision to later request and
cache bindings. Connect the selected draft to the ordinary native commit
under retained provider/account authority. Exercise that complete flow with
a real portable identify provider and in the full application.
