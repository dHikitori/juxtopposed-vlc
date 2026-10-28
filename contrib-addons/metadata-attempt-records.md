# Host metadata completion records

The host can record one complete `no_match` result from an exact provider
invocation. A guest result, cached value, or public media reference cannot
create this record. The native provider runner and owner activation remain open.

The admission binds the private child subject, ordinal, contribution, contract,
feature, operation, release digest and generation, settings revision, account,
approved permissions, requested fields, and actual projected context. The
Manager checks these facts against the installed package. The Worker then binds
its exact instance, invocation, and dispatch. Only that invocation can supply a
result. The host can consume a completion record once.

The Manager projects identity fields through both the manifest declaration and
the user's grants. It removes private consumer properties from the portable
request. The record hashes that actual projected context. A caller must compare
the per-provider context digest, not a digest of the original unfiltered request.
The ordinary Coordinator and the completion checks use one shared identity
normalizer. Existing positive result behavior keeps its current rules.

The Worker records every host call for the invocation. Pending, failed, denied,
unavailable, canceled, incomplete, or malformed calls prevent a record. A later
retry cannot remove an earlier failed call. A finite provider can return a valid
empty result without a host call. It still needs a fresh dispatch.

The network broker reports the actual transport outcome before it delivers the
response. A body that ends cleanly below a requested prefix limit can qualify.
A body at the limit, a truncated response, HTTP 206, an aborted transfer, or an
unfinished connection cannot qualify. An installed contribution can declare
HTTP 404 or 410 as an empty lookup for an exact feature and operation. An absent
policy permits neither status. Guest output cannot change that policy. See
[the SDK guide and schema](sdk/metadata-completion.md).

Only strict `NoMatch` records are implemented. The C++ enum reserves positive
outcomes; those outcomes cannot currently create a record. A guest `unsupported`
result is not trusted applicability evidence. An unavailable or failed provider
cannot become a negative result through this interface.

## Authority and lifetime

Cancellation and app retirement close the native admission. Repeated revocation
must join any admitted native commit. The record also checks its app lifetime
and captured memory-only authority. Full saved-state checks belong in joined
work before admission.

The process permits at most 1,024 admitted attempts and retained records in total,
with 16 MiB of charged data. An individual result is limited to 1 MiB. The charge remains held through the last owned
capture. The record holds Qt values and an exact native child hold; it stays on
the owner side. It must not enter the native final-commit keeper.

One record proves only one executed attempt. A later aggregate must account for
the complete provider plan, retain its native authority, and use the Core
transaction boundary. This code does not set an artwork `NOTFOUND` bit or mount
the prepared metadata owner.

## Verification

The copied receipt and shared-normalizer closure passes 153 normal and 153
sanitizer cases, with no failures, skips, or runtime diagnostics. It compiles
19 current production files, fresh metadata, and the current Coordinator. It
uses no old host archive or Core library. The fixture includes the actual Rust
worker, committed French-TV Wasm table, and local Qt TLS responses.

After integration with the complete catalogue reader, a fresh current-source
build passes 200 cases: 74 completion, 79 Coordinator and identity, 34 catalogue,
and 13 store-authority cases. It uses the registered test files and explicit
worker and Wasm paths. All four suites have zero failures, errors, skips, and
runtime stderr. The production input hashes stay unchanged during the build
and tests. The SDK policy schema also passes 43 cases with Draft 2020-12
validation. The full manifest parser remains the authority for cross-field
policy rules.

The separate receipt sanitizer proof is in
`/tmp/hakone-metadata-attempt-tests/final-current-proof.json`. The combined
normal build, source pins, and test reports are in
`/tmp/hakone-metadata-live-integration`. Two early configuration failures are
retained separately; they are not successful builds. The focused proof does not
replace a full app link, native aggregate proof, Windows runtime check, or
owner activation check.
