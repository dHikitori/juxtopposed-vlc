# Metadata identity choice store

`AddonMetadataChoices` retains offered identities and explicit host selections.
The metadata Service now connects it to native media targets and identify
results. The dialog, persistent accepted identity and native Apply mount
remain work. See [the Service contract](metadata-identify-service.md).

The caller supplies normalized, provider-tagged coordinator candidates. The
store uses the app's `MediaIdentity` format, including `ids` and `type` or
`kind`. The identify coordinator converts SDK `externalIds` and structured
episode numbering to that format. Raw guest results cannot enter the store
directly.

## References and bindings

The store creates a choice-set reference, one reference per offered choice,
and a separate selection reference. Every offer requires an explicit host
selection, including an offer with one candidate. Selection does not change
the media item.

Each choice binds the exact media reference, context generation, target
revision and field revisions. It also binds a private native subject snapshot
and the selected provider's release, account and settings partition.

The backend must bind the native URI, policy, owner scope and revision
tokens in that private subject. A held native anchor with a fresh private
nonce can supply that binding if each subject read checks the anchor again.
Use decimal strings for copied 64-bit tokens. A public title or external ID
alone is not a sufficient native subject. The private snapshot enters a host
hash. It does not enter provider requests or the offered rows.

The store checks the media and provider again before and after it retains
the media. It checks them again when a selection is read. The final consumer
must still validate the native target and revisions at Apply.

## Bounds and lifetime

The default limits are 64 sets, 8,192 choices, 256 selections and 16 MiB of
serialized choice and selection payloads. An offer has at most 500 rows.
References expire after five minutes. Host options can reduce these limits.

A retained selection stays charged after its original set is released. One
sweep timer serves the store. Admission also removes expired entries, so
quota recovery does not need timer delivery.

Use the store on its host thread. Backend calls can delete or invalidate it.
Each operation copies its data and callbacks before those calls. A guarded
owner identity and mutation generation refuse stale continuation. A pending
media hold is balanced even if the backend deletes the store.

Invalidation and expiry detach entries before they call the release backend.
Every detached hold ends even if the first release deletes the store. The
destructor refuses new admissions while it releases existing holds.

## Verification

The unit suite passes 33 normal and 33 focused sanitizer cases. Both runs
have no failures, skipped cases or sanitizer diagnostics. The current store,
unit source and identity helper stayed fixed across the runs. Qt itself was
not instrumented.

The cases cover callback deletion, invalidation and release reentry, complete
hold cleanup, private native subject changes, provider partition changes,
selection byte limits and expiry without timer delivery. The added cases
cover scoped media invalidation and destruction during selective release
with other sets and selections still held. These store checks are separate
from Service integration and dialog checks.

## Remaining integration

The store and its unit suite are registered in the CMake, Meson and Automake
inventories. The identify coordinator and native Service binding now use the
store. The trusted choice dialog, accepted-identity revision, persistent
identity and later cache partition binding remain implementation work. The
native transaction remains the final write authority.
