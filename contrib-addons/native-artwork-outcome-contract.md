# Native artwork outcome contract

This file defines the negative artwork result and its remaining aggregate.
The native receipt and input completion evidence are implemented. See
[completion evidence](records/native-artwork-completion.md). The final negative
transaction is not active in the app.

The positive native helper is separate. It accepts a changed artwork field
only after the child commits that field with a current native approval.
The parent then commits artwork, approval and `ITEM_ART_FETCHED` in one
transaction. A generic success result cannot set that bit.

## One family result

The native finder must report one closed outcome:

| Outcome | Required evidence |
| --- | --- |
| `UNKNOWN` | No accepted outcome exists. |
| `MATCH` | The selected family supplies an artwork address. The image still needs validation. |
| `NO_MATCH` | An applicable lookup ends with a valid response that contains no artwork. |
| `NOT_APPLICABLE` | The host fields do not meet the selected family's input requirements. |
| `UNAVAILABLE` | Transport, response size, response format or service access prevents a valid lookup. |
| `DENIED` | Native network policy refuses the lookup. |
| `ERROR` | Native execution or result validation fails. |

The trusted Lua scripts retain their first return value: an address or `nil`.
Only `fetch_art` has a second return value. That value uses the closed outcome
set. Only the exact app-shipped script path can supply this native evidence.
An unrecognized value, a Lua error or an incomplete response cannot mean
`NO_MATCH`. A prefix read with no pattern match does not prove an empty result.

The named finder initializes its output to `UNKNOWN` before module execution.
The native task copies the final value before it deletes the finder. Ordinary
legacy execution retains its first-result behavior.

## Sealed result receipt

`vlc_metadata_native_provider_ProbeOutcome` is an additive native entry point.
It retains the existing callback signature. The old `Probe` entry point keeps
its positive-only receipt behavior.

The new entry point can issue an immutable receipt for a completed outcome.
The receipt uses the existing claim lifetime and exact job, snapshot, family
and generation binding. `claim_GetOutcome` reads its closed value. Only a
`MATCH` receipt can contain an artwork address. The image adapter must require
both `MATCH` and a nonempty address.

The callback borrows the receipt. `Hold` retains it and `Release` ends that
reference. The existing 32-request bound also applies to negative receipts.
Close, cancellation, a stale native snapshot or a retired app invalidates the
receipt. No guest result, public media reference or address can create it.

## Ranked result aggregate

The host captures the selected contribution, family, release/account/settings
partition and order before it starts a request. It chooses one implementation
for each family. A portable miss cannot cause a Lua retry of that family.

The host records an outcome for each selected attempt. It can prepare
`ITEM_ART_NOTFOUND` only if all of these conditions hold:

1. The exact native job and target snapshot remain current.
2. The plan and every provider partition remain current.
3. Every selected applicable attempt ends with accepted `NO_MATCH` evidence.
4. At least one applicable lookup occurs.
5. No attempt is absent, canceled, denied, unavailable or in error.
6. No accepted address fails image validation or materialization.
7. No protected or current approved artwork would be replaced.

`NOT_APPLICABLE` does not count as a lookup. `MATCH` does not become `NO_MATCH`
after an image failure. A skipped provider or a stale cached result cannot
complete the aggregate. A successful image takes the separate positive route.

The aggregate is native host state. A guest `status`, provenance map or JSON
field cannot set a native status bit. A portable provider needs a typed host
attempt record from its exact admitted invocation. The host must validate its
completion against the captured plan and result contract. That portable
record and its complete plan integration remain implementation work. The
native sealed staging helper and final transaction now pass focused checks.
See [the native transaction](records/native-artwork-outcome.md).

The final helper must join the negative status with the parent's prepared
delta under the original artwork, status and identity tokens. It must prepare
allocations and tokens before any write. A stale token refuses all changes.
Callbacks run after admission and outside native locks.

## Required evidence before activation

Use the real Lua module to distinguish an empty valid result from a denied
request, an open/read failure, a truncated page and malformed content. Preserve
the existing first return value for all four shipped families. Test the old
and new probe entries with inline and delayed callbacks, cancellation, Close,
same-value writes and URI changes that return to the original value.

Test the aggregate with two ranked families, one chosen portable family,
inapplicable inputs, an unavailable higher provider, an image failure, stale
partitions and a completed child before consent withdrawal. Verify both status
bits and the final artwork getter through the real parent transaction.
Keep app owner activation off until the positive and negative routes pass.
