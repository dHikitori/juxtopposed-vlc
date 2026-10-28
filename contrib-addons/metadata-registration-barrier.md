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

The helper alone does not protect a Service registration. Service must acquire
the mutation scope before it changes the registration map and must arm each
attempt before dispatch. Native-owner activation remains off while the typed
runner and these mounts are incomplete.

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
claim a Service mount, Windows runtime coverage, or owner activation.

A fresh build of the applied sources passes all 16 registry cases. The same
build passes all 16 Manager factory cases. Core and LibVLC hashes stay fixed.
No test fails or skips, and neither test emits a runtime diagnostic. The build
has one existing unused-function warning in the Linux credential backend. The
input hashes and results are in `/tmp/hakone-metadata-admission-live/proof.json`.
