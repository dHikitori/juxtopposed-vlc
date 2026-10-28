# Private artwork provider plans

`ArtworkAttemptPlan` captures an immutable plan for one native artwork request.
It is private host data. The native Service runner and owner activation remain
open.

Capture runs in joined background work. It uses the complete catalogue reader,
current credential-presence evidence, all native registration descriptors, and
an exact registration epoch. It retains every raw row before selection. A
missing cached credential entry uses the owned background reader. The resulting
plan retains presence flags and references, with no credential reader or value
read from the credential store.

Saved settings must pass the same declared-secret and credential-reference
checks as a new settings save. A declared protected key in ordinary settings,
including a nested object or array, refuses the whole catalogue. Malformed
references also refuse it. Unknown ordinary strings cannot be classified as
credentials by their contents; this check does not claim to do that.

## Selection and binding

The plan selects one implementation per provider family. Its default family
order is MusicBrainz, Google Images, French TV, then Last.fm. Saved feature,
media-kind, and language order can change the rank and implementation. A selected
preferred provider that needs setup or is unavailable remains a blocking
attempt. A miss or error cannot select another implementation of that family.

Disabled and statically omitted rows remain in the plan's raw decisions. An
absent saved preference blocks a final negative result unless current state
proves removal or disable. A historical distribution-removal marker does not
exclude a restored and enabled package whose saved contribution is missing.
A portable removal marker cannot disable a missing native preference.

Each portable attempt binds its exact package, declaration, release, generation,
settings revision, grants, account, requested fields, and partition. Its context
passes through the same identity projection as the Manager. Each attempt has
its own projected-context digest. The original request, raw catalogue, and full
plan have separate digests. The shared configuration helper preserves the
Manager's existing value-validation and account-reference rules.

The limits are 256 raw rows across portable, native, and missing-preference
rows; 16 selected attempts; and 2 MiB of retained canonical descriptor data.
Overflow refuses the whole plan. Neither list is truncated. The byte limit
does not describe total Qt heap use or package verification I/O.

The deadline covers capture and later execution. Both captured store tokens and
the native registry epoch must remain current. The Service must arm the same
native gate before dispatch and revoke it before a registration, lifetime, or
policy change is published. An exact binding string alone cannot enforce that
barrier. The plan cannot write a metadata result or artwork status by itself.

## Verification

The copied plan passes 49 normal and 49 focused sanitizer cases, including the
actual distribution install, removal, approved restore, and missing-preference
regression. The catalogue settings check passes 77 normal and 77 sanitizer
cases: 54 catalogue, 13 authority, and 10 unchanged extracted store regressions.
Those extracted cases are not the full package suite.

The final combined working-tree build then passes all 359 cases: 74 completion,
79 identity and Coordinator, 54 catalogue, 13 authority, 49 plan, and all 90
package cases. This build compiles the Manager helper extraction and the shared
saved-settings check together. All suites have zero failures, errors, skips,
and runtime stderr; input hashes remain unchanged.

Proofs are in `/tmp/hakone-artwork-plan`,
`/tmp/hakone-metadata-catalogue-hygiene`, and
`/tmp/hakone-metadata-live-integration`. The earlier 200-case combined checkpoint
is retained separately. The full native runner, final aggregate, supported
platform checks, and native owner activation remain required.
