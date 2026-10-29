# Positive artwork source authority

Core can now keep provider and account authority attached to a successful
artwork result through final publication. The serial Service runner must still
supply that authority. Native owner activation remains off.

## Native contract

`vlc_metadata_job_SealArtworkSource` is a private native host function. It binds
one pending network child for `artwork/enrich`, with one `artwork` field, to an
already approved local image and the exact native admission gate. It creates
no image approval, completes no job and grants no add-on or QML API.

The approved image must match the child's source URI and input policy. Raw
artwork text, another source, a different image, duplicate seals, a completed
child and mixed positive/negative seals are refused. Ownership of the native
keeper transfers only on success. Its release function runs once, outside
native locks. The keeper must contain only native or standard-library state.
It must retain no job, claim, QObject, Qt value or file reader.

The seal captures bounded metadata, extra fields, native URI/name/options and
status revisions. A successful child Apply records the exact artwork revision.
PrepareFetchedArtwork checks that revision and the native image approval, then
keeps the same source authority on the parent. Final Apply checks the private
working item and the identity and extra fields that Core will publish. A fill
operation can preserve an existing title; a replacement that changes the image's
identity is refused. Same-value writes and changes followed by a reset to the
original value cannot reuse an old revision.

Core prepares image proofs and field values before it enters final admission.
The final order is admission, backend, then item locks in the existing total
order. The private working item stays locked through publication. Revocation
closes new admission and waits for an admitted commit. Failed allocation or
identity checks leave the target unchanged. A current-state query takes no
admission mutex. Existing paths without a source seal keep their behavior.

## Evidence

The affected 13 Core suites pass normally and with ASAN/UBSAN on WSL. Both
runs include local transactions, allocation limits, artwork, admission,
backend, commit, current-item checks, owner, pipeline, request guards,
preparser internals and native providers.

Five new local test groups cover successful publication, keeper release,
foreign/raw/duplicate/mixed seals, different completed results, revocation at
each handoff, private execution edits, and effective fill/replace identity.
Three new allocation/concurrency groups cover failed seals, failed final Apply,
and concurrent revocation plus a private working-item mutation. The latter
holds the real Core commit at revision reservation and verifies that both
operations wait for it.

The isolated normal build compiles the two changed Core translation units.
The sanitizer build compiles those two and five related Core translation units
with instrumentation. Each run also compiles 25 test objects. The other Core
objects are unchanged pinned donors. This is focused sanitizer coverage.
The existing short-delay fixture warnings and obsolete `quiet-synchro` option
warning remain; no sanitizer failure occurs.

The source, dependency, donor, object and program record is
`/tmp/hakone-artwork-source-current/final-proof.json`, SHA-256
`7b9f68467700ee4915289256e1ab0f83e9843ae4ed360cc28cf3f6e32a75f76b`.
Both runs link a new isolated Core library. The baseline Core library remains
unchanged. Completed test objects and programs are in verified compressed
archives. Free space after retention was 7,736,356,864 bytes.

## Remaining integration

The host aggregate and serial Service runner must supply typed positive
identity evidence, fresh configuration checks and native Store watches to this
seal. They must retain the same gate through image resolution and final Apply.
The portable completion receipt currently certifies only no-match results;
positive receipts and their exact result binding still need implementation.
The [typed aggregate](artwork-attempt-aggregate.md) therefore cannot yet claim
successful source transfer. Native owner activation remains off until the
preserved provider, consumer, cancellation and legacy-bypass checks pass.

The local Windows review package remains clean source `335ac989d0`. This Core
change is not in that package. See the [subtitle build record](windows-subtitle-build-2026-10-29.md).
