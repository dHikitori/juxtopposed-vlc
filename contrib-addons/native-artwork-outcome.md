# Native artwork fetched status

The owned metadata pipeline stages a fetched result only after an exact child
job applies an approved local artwork file to the anchor's private working
item. `vlc_metadata_job_PrepareFetchedArtwork()` checks the child Apply token,
the exact field value, source, policy, and both private approval records.
Guest status text and an ordinary artwork URI cannot supply this proof.

The native replacement choice must match the final Apply choice. A protected
artwork field that the final transaction will keep adds no fetched outcome.
The helper changes no live item and reserves no token.

Final Apply checks the original artwork and status revisions again. It reserves
the field, extra-data, and combined status tokens before any write. It commits
the artwork, approval, native facts, and fetched bit under the existing locks.
A stale value, same-value edit, revoked policy, missing proof, cancellation, or
token limit refuses the complete transaction. It preserves all existing status
bits, including the independent not-found bit.

Eight normal native suites pass against the fresh Core. A focused sanitizer
build passes 20 functional local groups and eight allocation/token fault groups
with no diagnostics. That build instruments the actual item, policy, metadata,
artwork, parse, and backend sources. Other shared Core code is not instrumented.
Exact sources, binaries, logs, deadlines, and build reserves are recorded in
`/tmp/hakone-positive-art-validation.json`.

This helper proves a positive fetched result. A remote no-match result cannot
set native not-found status. That result needs a separate trusted aggregate
that proves an applicable search ended for every planned provider. This gate
remains open before the owned provider cutover.
