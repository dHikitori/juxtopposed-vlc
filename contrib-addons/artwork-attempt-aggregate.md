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

A separate cancellation control holds only native admission. It can revoke
that admission after the aggregate and Qt application are gone. Normal sealed
cleanup does not revoke the transferred admission. An unfinished aggregate
cancels on retirement.

## Evidence

Current source passes 38 normal and 38 ASAN/UBSAN checks on WSL with Qt 6.8.3.
There are no failures, skipped cases or runtime warnings. Each build has 42
fresh compiled objects, including the test and C fixtures, and pins 1,464
input files. The Core libraries are unchanged.

Valid portable receipt tests run `metadataInvocationPreparer` on a joined
test thread, then call `invokePreparedMetadata` on the owner thread. The adversarial tests retain the legacy host
path only to produce otherwise valid receipts with the wrong binding, or for
a blocked slot. Production preparation refuses a blocked slot before dispatch;
the aggregate also refuses a receipt obtained through that older path.

Tests use the real Rust worker and French TV Wasm module, the actual native
context and current native bridge source, and real Core jobs through child
completion, Apply, PrepareNotFound and final Apply. Native lookup execution
uses the existing deterministic fixture. This does not verify each shipped
provider's network parser or the full app runner.

The record is `/tmp/hakone-artwork-aggregate-current/final-proof.json`, SHA-256
`bf7f9ee92d2b54b179f1a0ec0357d1b538097a3f8bb85977c0bb9b10a56c7a17`.
Completed test objects and binaries are retained in verified compressed
archives. Free space after retention was 7,762,964,480 bytes.

## Remaining integration

The shared Service still needs one serial artwork runner. That runner must
capture the registry and catalogue, prepare each selected portable call on
joined work, obtain native outcomes, validate positive identity evidence,
approve the image and retain current provider/account authority through the
final Core commit. It must then use this aggregate for a final not-found
result. Image failure must permit later providers without permitting a false
not-found result.

Native owner activation remains off. Before activation, verify every retained
provider, local metadata work, cancellation, status bits and the rule that an
owned lookup cannot bypass this path through legacy fetching. The current
Windows review package remains the separately verified `335ac989d0` build.
