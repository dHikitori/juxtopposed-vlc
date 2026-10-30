# Native metadata registration barrier

`MetadataRegistryEpoch` supplies a native registration barrier for the private
artwork runner. Its state uses standard C++ values. It contains no Qt object,
provider, job, reader, callback, or strong reference to an admission gate.

Capture returns an exact registry token. Its current check reads memory only.
The 32-byte identity contains the registry and generation. Counters do not wrap.
Before dispatch, arm the token with the same native admission gate that the
runner and final artwork keeper retain. Stale or excess arms refuse and revoke
that gate. At most 64 gates can be armed. Duplicate gates use one slot.

Begin a mutation before a registration, lifetime, or provider-plan change. This
step prevents new capture, advances the generation, and revokes old gates. It
joins native publication outside the registry lock. Keep the mutation scope
through the last owner publication. Nested changes keep capture closed until
the outer change ends. Permanent Close joins concurrent revocation, including
on repeated calls. Destruction also calls Close.

The gate's current check, revoke call, and destructor must use native state and
must not call Qt or enter the registry again. Owner callbacks can change the
registry after the native barrier returns. Do not hold a provider-map or item
lock across the barrier. Close the registry before app drain and native parent
teardown.

## Service ownership

Service now acquires the mutation scope before registration replacement or
removal, provider-plan changes, owner changes, artwork-adapter changes, and
network-policy publication. Invalidation also uses the scope. Native owner
Close starts a scope before it closes the provider context, image service, or
registration lifetime. Service shutdown and destruction permanently close the
registry. No registry lock covers a native revoke or an owner callback.

`captureArtworkRegistrations()` reads memory on the Service thread. It retains
every built-in row and its exact binding, including unavailable rows. A row
that requires a native anchor is available only when its lifetime matches the
reserved native owner. A legacy plan hook with another or expired owner refuses
the private capture.
The production native owner must begin retirement before its subclass data
changes. A late QObject destruction signal alone is not this barrier.

The captured registry and token are native values. The future runner must arm
that token with its exact Core admission gate before provider dispatch and
include the token in its memory-only live check. A complete typed runner and
supported-platform checks remain open. Native-owner activation remains off.

## Verification

The copied helper passes 16 normal and 16 focused sanitizer cases. The tests
use the current Core admission, artwork keeper, store watch, and Core Apply
path. They cover 200 arm races, held publication, repeated Close, nesting,
moved scopes, quota, expired weak references, and cleanup after Qt app exit.
A deliberate repeated-Close early return fails the held-publication test.
The proof is `/tmp/hakone-native-registry-epoch/final-proof.json`.

The applied helper files match the tested bytes. The test changes only its
include and MOC file names to match the repository. Automake, Meson, and CMake
register the helper and the Linux native fixture. This checkpoint does not
claim Windows runtime coverage or owner activation. The Service mount has the
separate applied-source checks below.

A fresh build of the applied sources passes all 16 registry cases. The same
build passes all 16 Manager factory cases. Core and LibVLC hashes stay fixed.
No test fails or skips, and neither test emits a runtime diagnostic. The build
has one existing unused-function warning in the Linux credential backend. The
input hashes and results are in `/tmp/hakone-metadata-admission-live/proof.json`.

The complete current host and native metadata archives compile from the working
tree. Their targeted run passes 115 normal cases: Service 70, native owner 13,
registry 16, and Manager authority 16. The new cases cover capture on another
thread, raw unavailable rows, plan-owner refusal, ten change paths, nested
retirement, and the actual native owner's Close and destruction with a Core
gate armed. Existing callback reentry and owner-deletion cases still pass.
There are no failures, skips, warnings, or runtime diagnostics. Core and LibVLC
keep their recorded hashes. Evidence is in
`/tmp/hakone-metadata-service-registry/proof.json`.

The same 70 Service and 13 native-owner cases pass focused AddressSanitizer and
UndefinedBehaviorSanitizer checks, with leak and stack checks enabled. Service,
native owner, registry, Core admission wrapper, store authority, tests, and the
native-provider bridge and fixture are instrumented. Remaining dependencies
and meta-objects use the fresh pinned normal archives; Qt, Core, and LibVLC are
normal shared libraries. There are no failed or skipped cases and no runtime
diagnostics. The proof is
`/tmp/hakone-metadata-service-registry/sanitizer/proof.json`.
