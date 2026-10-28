# Native artwork status transactions

## Fetched artwork

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

## Not-found artwork

The native backend now has a separate sealed not-found route. The host must
bind a closed plan summary to an exact pending child and a native admission
gate. The plan has one to 16 attempts. Each attempt must be `NO_MATCH` or
`NOT_APPLICABLE`, and at least one must be `NO_MATCH`. A guest result or cached
JSON cannot create this native proof.

The sideband retains the working identity, metadata, extra data, status and
native URI, name and option revisions. Its keeper holds native state only.
The native provider authority shares the existing 32-slot bound. It retains
the slot until its final native state ends, without retaining the child job.
Repeated Close and revoke calls join a commit that has already entered.

The exact successful child must first pass Apply. The parent can then prepare
the not-found result. Preparation changes no live item. Final Apply holds the
working item and target in address order through the revision checks, token
reservation and complete write. It checks the effective identity and extra
data that the chosen fill or replacement operation will publish. A patch that
would be skipped cannot supply different identity evidence.

The transaction adds `ITEM_ART_NOTFOUND` while it preserves existing status
bits. It preserves protected or fetched artwork. A stale identity, local edit,
policy change, revoked gate or token limit refuses the complete write. Final
Apply prepares allocations before admission. Keeper cleanup and callbacks
occur outside the native locks.

Twelve suites pass against the fresh coherent Core and LibVLC. Eight focused
sanitizer suites also pass. The focused build instruments the four affected
Core source files and each complete test source group. Other Core objects
remain normal dependencies. The checks cover effective identity and extra
data, same-value and away-and-back local edits, allocation and token limits,
concurrent revoke and Close, and both locks through final publication.

The full Qt plan, typed portable attempt records and all-provider authority
checks still need integration. The app owner remains off. These native helpers
do not permit provider cutover by themselves. No Core provider was removed.
