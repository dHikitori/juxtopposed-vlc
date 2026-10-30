# Native positive artwork authority

The typed aggregate can now carry a native artwork result through image
resolution and final Core Apply. Provider closure and image-consent changes
revoke the same native admission. The serial Service runner and native owner
activation remain open.

## Source and image checks

`vlc_metadata_native_claim_AdmitSource` accepts an exact native `MATCH` claim
with an artwork address. It checks the child, family, snapshot and admission.
It shares the existing one-use authority and 32-slot reservation with the
negative API. `AdmitOutcome` still accepts only NoMatch or NotApplicable.
Neither function grants image approval or a target write.

`AddonNativeProviderContext::admitPositive` keeps its conversion inside the
counted native call. It accepts only a claim from `probeOutcome` and leaves
that claim on the context map for the trusted image adapter. It returns only
native provider authority. That authority retains no claim, child or Qt value.
Provider Close revokes it and joins an admitted commit. Delete can finish after
the claim and child are released, while closed authority remains in Core.

`acceptNativePositive` binds this authority to the reserved native plan slot.
The existing native image adapter consumes the claim, reads the image, decodes
it and publishes its local cache file. The runner must then obtain the normal
native image approval and call `trySealNativePositive` with the returned permit.
An empty, stale or closed permit cannot seal. A portable source seal cannot
stand in for this native consent step.

The permit arms the same concrete native gate in a bounded native epoch.
Network-consent changes begin a publication barrier before changing consent.
Close permanently closes that barrier. Both operations join admitted commits
without holding the consent or owner lock. A repeated Close also joins pending
revocation. Setting the same consent value does not revoke current work.

The epoch holds weak native gate references. The Core keeper retains no permit,
app lifetime, Qt value, claim, job or reader. It keeps native admission, Store
watches and source authorities through final Apply. The new epoch adds no
process, thread, timer or file read while idle. Existing native HTTP, account
and LAN behavior stays on the trusted native route.

## Evidence

The affected Core suites pass normally and with ASAN/UBSAN: native provider,
artwork admission and local transactions. The normal library has three fresh
Core translation units; the focused sanitizer library has eight. Tests cover
exact/foreign scope, once-only source admission, every native outcome, child
release, provider deletion and repeated Close joining both positive and
negative admission.

The host group passes on WSL with Qt 6.8.3:

| Suite | Normal | ASAN/UBSAN |
| --- | --- | --- |
| Artwork aggregate | 69 | 69 |
| Native image adapter | 36 | 36 |
| Total | 105 | 105 |

Each build has 47 fresh compiled objects. The final input records pin 1,472
normal and 1,473 sanitizer files. The sanitizer host uses `-O1` and stripped
executables. Both builds retain leak detection. There are no compiler warnings,
failed checks, skipped checks or sanitizer diagnostics in the final runs. The
image suite emits one expected information message with its HTTP address count.

All four native artwork families pass the actual context, image adapter,
decoder/cache, source seal, child Apply and final Apply path. Native lookup
execution uses the existing deterministic fetcher fixture. Image bytes are
controlled PNG data. Other cases check consent revocation, consent toggles,
provider closure, image-owner closure, invalid permits and unchanged consent.
The native HTTP test also reads real loopback and LAN responses through Core.
This is not a live test of each provider's service or the mounted app runner.

## HTTP fixture and shutdown control

The existing cold full-plugin scan exposed 20,918 bytes of process-global
allocations after library unload. Loader traces identify protobuf, pixman and
OpenMP. The two new consent checks pass in isolation. The same existing HTTP
test reproduces the allocations alone. These controls are retained in
`/tmp/hakone-native-positive-host/leak-trace/`.

The HTTP fixture now accepts an explicit fresh module-cache directory through
`HAKONE_NATIVE_HTTP_TEST_PLUGIN_PATH`. With that input it requires `plugins.dat`
and disables directory scanning. Without it, the original cold-scan mode
remains available. The Core instance has automatic cleanup on failed checks.

The final normal and sanitizer runs use a private tree with all 352 available
module files and a freshly generated cache. The original module directory and
cache stay unchanged. All HTTP assertions still run, and leak detection stays
enabled with no suppression. This does not establish a clean cold scan or a
complete app shutdown result for those unrelated library initializers.

## Records and remaining work

The final host record is `/tmp/hakone-native-positive-host/final-proof.json`,
SHA-256 `133e4dddcaa94724993c0be78f88799d1b78bc8b96ae5eeed5e1c4bed957af63`.
The Core producer record is
`/tmp/hakone-native-positive-core/producer-proof.json`, SHA-256
`b80e6e1c033e96925befaadd56f3b81120f09203b959c706ff5315bd07b99d44`.
Completed host objects and programs are in verified compressed archives.

The build stopped twice at its working reserve guard. Both stops stayed above
7.5 GB. The lowest recorded free space was 7,535,439,872 bytes. Final retention
left 7,793,573,888 bytes. Cleanup removed an obsolete 27 October Linux Qt backup
and obsolete standalone test programs. Their source, reports, producer archives
and test objects remain. Current Core libraries, donor objects and Windows
packages were preserved.

Connect the serial Service runner to prepared portable calls, `probeOutcome`,
actual image brokers and both source seals. Recheck saved configuration on
joined work before final host admission. Verify every retained provider, local
work, cancellation, status bit and legacy-bypass rule before enabling the owner.
The complete [completion backlog](../completion-backlog.md) remains active.
The local Windows review package remains clean source `335ac989d0`.
