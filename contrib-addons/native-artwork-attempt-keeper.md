# Native artwork attempt admission and keeper

`CoreMetadataAdmission` connects the host completion-record interface to the
Core artwork admission gate. It owns only the native gate and a standard C++
quota. It holds no Qt object, Qt value, app handle, job, callback, or file reader.
The process permits at most 64 live plan admissions. The charge is released
after the last owned native capture.

The deadline uses the Core monotonic clock. Core refuses an expired deadline
or one more than 30 seconds ahead. Every `revoke()` call closes admission and
joins an admitted commit. A second caller cannot return early while another
revocation is waiting for that commit.

`NativeArtworkAttemptKeeper` retains the gate, bounded store-authority watches,
and native outcome authorities through final publication. It holds no provider
claim, native child job, completion record, Qt container, or full saved-state
reader. Store watches capture only the native admission. A store mutation must
close that admission before it publishes the new state.

The keeper accepts at most 32 watches and 16 native outcome authorities. It
releases the admission and its charge after those captures. Its destructor can
run on a native thread after the Qt application has been destroyed. The existing
Qt module unload pin keeps its release function loaded. Core retains its normal
parent and pipeline lifetime; this helper does not replace that contract.

## Sealing a result

The later typed aggregate must validate every selected attempt against its
immutable plan before it calls `sealPending()`. The helper requires a nonzero
plan digest and one to 16 native outcomes. Every outcome must be NoMatch or
trusted NotApplicable, and at least one must be NoMatch. Empty plans, all-
NotApplicable plans, and positive or error outcomes are refused.

The seal binds the exact pending native child through the existing Core API.
It transfers keeper ownership only on success. On refusal, the caller keeps
ownership and admission is revoked. The helper does not complete or apply a
job and does not write a metadata status bit. The normal child completion,
preparation, and guarded final Apply remain required.

These helpers are private host infrastructure. They do not activate the native
metadata owner. The full raw plan, exact portable completion records, typed
native outcomes, native registration gate, and serial runner must be connected
and verified before activation. A raw guest `no_match` remains insufficient.

## Verification

The copied component passes 11 normal and 11 AddressSanitizer and
UndefinedBehaviorSanitizer cases, including leak checks. The final working-tree
files then pass a fresh 11-case normal build. All runs have zero failures,
skips, and runtime diagnostics. Source and Core library hashes remain unchanged
through each proof.

The fixture uses actual Core child, seal, preparation, and final Apply APIs.
It compiles the current Core admission source in the test only, so it can
observe the private admission lock. It does not substitute a field or status
writer. The cases check failed-seal ownership, successful sideband ownership,
deadline refusal, the 64-plan quota, and destruction order.

One case holds the real final Apply after it enters native admission. A store
mutation and a second revocation both wait until that commit ends. Another
case revokes before Apply and verifies refusal without a partial status write.
A separate child process destroys its Qt application before it releases the
final keeper and verifies that all admission charges are released.

The copied normal and sanitizer proof is in
`/tmp/hakone-native-attempt-aggregate/final-proof.json`. The final live-source
normal proof is in `/tmp/hakone-native-attempt-live-proof/final-proof.json`.
The Core hash is
`b141fd06dcfe40295d0556648c18b2024e7b8c5b25d44f265a880121fcd7dfd8`.
The public library hash is
`062b55d89cd3f436ebea5b2206f8669624e90d89bc5dff77dc4682e64c5252e1`.
These focused Linux checks do not prove a full application build, Windows
behavior, or the complete provider aggregate.
