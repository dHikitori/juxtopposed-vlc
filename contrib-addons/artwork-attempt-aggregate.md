# Artwork attempt results

`ArtworkAttemptAggregate` combines the results of one immutable artwork plan.
It is host C++ code. Add-on code and QML cannot create its receipts or native
claims. The source and test are registered in CMake, Meson and Automake.

## Dispatch and completion

Create the aggregate with the exact pending artwork child and its
`CoreMetadataAdmission`. First bind the captured registry token to that same
admission. The plan's live check must include the registry token. Creation
adds watches for both Store tokens before any provider can run.

Reserve each selected ordinal before dispatch. Complete that ordinal once
before dispatch of the next provider. There are at most 16 selected attempts.

| Result | Rule |
| --- | --- |
| Portable no match | Requires a current, move-only completion receipt for the exact subject, ordinal, contribution, context, account partition and admission. |
| Native no match or not applicable | Requires the opaque claim from `AddonNativeProviderContext`. The context checks the exact child, native family and admission before it returns native authority. |
| Error, missing result, unsupported operation or blocked provider | Prevents a final not-found result. A later valid positive result can still succeed. |
| Positive result | Ends negative admission after the caller approves the image. This method grants no image or target-write authority. |
| Stale authority or a late result after seal | Revokes native admission. |

A final not-found seal requires every selected attempt to have a valid
negative result, with at least one actual no match. An empty plan, all-not-
applicable plan, duplicate result or incomplete plan cannot seal. A failed
attempt cannot be repaired by a later receipt for that ordinal.

The aggregate checks all retained portable receipts again before seal. It
then transfers native admission, Store watches and native outcome authorities
to `NativeArtworkAttemptKeeper`. It releases its Qt plan, receipts and child
holds on the owner thread. The caller completes the child through the ordinary
Core path. Core prepares the status change and applies it under its final
admission and item locks. JSON status or provenance cannot set the bit.

Portable `match` and `partial` receipts can now enter `acceptPortablePositive`.
The aggregate compares the exact result object and keeps the slot reserved
during consumer acceptance and image resolution. `trySealPositive` transfers
native source authority only after the caller has a native image approval.
See [positive completion and source transfer](artwork-positive-host-transfer.md).

A separate cancellation control holds only native admission. It can revoke
that admission after the aggregate and Qt application are gone. Normal sealed
cleanup does not revoke the transferred admission. An unfinished aggregate
cancels on retirement.

## Evidence

Current source passes 58 aggregate checks and 86 completion checks normally
and with ASAN/UBSAN. Each build has 44 fresh objects. Valid portable aggregate
receipts use joined preparation and the actual prepared Manager path. Both
negative and positive transactions run through real Core Apply. Positive
image approval comes from the Core fixture. Native lookup execution uses the
existing deterministic fixture. The Rust/French TV cases exercise no-match.

The [current positive transfer record](artwork-positive-host-transfer.md)
describes the inputs, source pins, retained outputs and limits. It does not
verify each shipped provider's network parser or the full app runner. The
earlier 38-case aggregate proof remains in
`/tmp/hakone-artwork-aggregate-current/final-proof.json`; its completed source
and build trees are now also in verified compressed archives.

## Remaining integration

The shared Service still needs one serial artwork runner. That runner must
capture the registry and catalogue, prepare each selected portable call on
joined work, obtain native outcomes, validate positive identity evidence,
approve the image and retain current provider/account authority through the
final Core commit. It must then use this aggregate for a final not-found
result. Image failure must permit later providers without permitting a false
not-found result.

Core now has a [positive source seal](artwork-positive-source-authority.md).
It passes 13 normal and 13 focused sanitizer suites. Portable positive receipts
and their host transfer now pass the combined 144-case group. Native positive
claims, actual image broker integration and the serial runner remain open.

Native owner activation remains off. Before activation, verify every retained
provider, local metadata work, cancellation, status bits and the rule that an
owned lookup cannot bypass this path through legacy fetching. The current
Windows review package remains the separately verified `335ac989d0` build.
