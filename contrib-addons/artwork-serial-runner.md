# Serial artwork lookup

The automatic artwork path now has one host runner. The Manager-backed
Service uses it for automatic `artwork/enrich` jobs. It cannot return to the
ordinary coordinator if the Manager ends. Manual metadata and subtitle
requests keep their existing paths.

The Core owner is still not active in the app. This change does not remove a
Core provider or change the Windows review package.

## Request path

1. Capture the exact Service registration token and native child. Arm one
   native admission gate before any provider runs.
2. Read the catalogue and check the worker file on joined background work.
   Capture one immutable attempt plan with the saved provider order.
3. Reserve and run one attempt at a time. Portable attempts use a prepared
   invocation and its exact completion receipt. Native attempts use the
   closed outcome API.
4. Send a positive portable result through shared identity and field checks.
   This check uses one captured provider. It has no cached or shared result.
5. Resolve the image through the host adapter. Native resolution retains the
   exact provider claim and image consent.
6. Read the catalogue again on joined work. Require the same plan digest.
   A positive portable result also repeats invocation preparation to check
   the current package, grants, account, settings, and keys.
7. Seal the accepted source with the same gate. Complete the child through
   Core. The parent transaction still controls the final field and status
   write.

An error, denied attempt, unknown native outcome, or image failure prevents
an overall not-found result. A later valid positive result can still succeed.
An all-not-applicable plan cannot report not found. An ambiguous identity
ends automatic lookup without selecting an entity.

## Cancellation and work limits

The native cancel control contains a native gate and native cancel function.
It has no QObject, Qt value, catalogue reader, claim, or application Handle.
Core cancellation can revoke publication and interrupt native work without
waiting for a Qt event.

The owner clears its state before external cancel functions or completion
callbacks run. An immediate image callback cannot replace a newer cancel
handle. Late or repeated callbacks cannot complete another stage. Service
invalidation, shutdown, and Manager retirement end active runners.

The runner has one 25-second deadline. It uses the existing preparation,
process, and native admission limits. It starts no background work, process,
or timer when there is no request.

## Verification

The current WSL build passes 159 normal checks and 159 ASAN/UBSAN checks.
There are no failures, skips, warnings, runtime messages, or final sanitizer
findings. Leak checking is on:

| Suite | Cases |
| --- | --- |
| Typed aggregate, serial runner, and mounted Service | 89 |
| Shared metadata Service regressions | 70 |

The mounted cases use the actual Service, Manager, native owner registration,
Core jobs, and final Core Apply. They cover portable and native no-match,
native images, revocation after child completion, native cancellation, and
Manager retirement. The runner cases cover saved fallback order, immediate
and queued image callbacks, changed configuration, all four native families,
unknown outcomes, and all-not-applicable results.

The portable image case uses the actual host image decoder and cache with a
controlled network response. The native image cases use a controlled native
fetcher and PNG response. These are not live provider parser or remote-service
checks.

Normal input, object, program, and result records are in
`/tmp/hakone-artwork-runner-host/normal-proof.json`. The build compiles 14
objects for the changed sources and test fixtures. It reuses 39 verified host
objects from the preceding native-positive proof. Completed normal outputs
are retained in verified compressed archives.

The sanitizer build also compiles 14 objects and reuses the matching 39
instrumented host objects. Its Core dependency is the verified native-positive
sanitizer library. It does not link the uninstrumented host cohort.
The initial sanitizer run found an invalid application cast in two new test
callbacks. Both now use `QCoreApplication::instance()`. The corrected suites
pass with the normal sanitizer checks enabled.

The final combined record is
`/tmp/hakone-artwork-runner-host/final-proof.json`, SHA-256
`4810e965e5a778fcd89a00af16341a91ff7d41dfb10c90eb6580782c0e6f034a`.
Both builds share 520 source files. The normal dependency record has 975
files; the sanitizer record has 985. Verified output-retention records are in
that directory. The source snapshots, results, object
hashes, and compressed outputs remain available. Old test executables were
retired with separate digest records. Three completed source snapshots and
two Rust test programs were retained losslessly in compressed form.
Two space guards stopped preparation or linking before a test ran. The
confirmed stopped service link resumed from the same graph after cleanup.
Free space stayed above 7.5 GB.

## Remaining cutover work

- Move the portable image adapter's registration checks, package and key
  preparation, cache reads, decode, and writes off the UI thread. Its current
  Manager path still calls synchronous `providersForFeature`, `installed`,
  and credential preparation. A queued Qt callback is not background work.
- Exercise that prepared image path with the real Manager network broker,
  including account change, cache hit, cancellation, and final revocation.
- Finish preserved provider, local metadata, status-bit, no-legacy-bypass,
  subtitle, and complete app checks before native owner activation.
- Rebuild and test the supported app targets. The current Windows package
  does not contain this runner.
