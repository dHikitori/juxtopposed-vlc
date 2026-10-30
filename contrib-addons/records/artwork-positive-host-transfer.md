# Portable positive artwork completion

The typed host path can now carry current portable provider and account
authority into the [Core positive source seal](artwork-positive-source-authority.md).
The serial Service runner remains open. Native positive claims and image
consent now have their own [tested source path](artwork-native-positive-authority.md).
Native owner activation remains off.

## Receipt and acceptance

An actual dispatched `metadata@1` invocation can supply a move-only receipt
for `match` or `partial`, as well as the existing strict `no_match` receipt.
Every host call must have a valid complete observation. An error or pending
call prevents a receipt. A retry cannot erase a failed call.

Positive data must be a nonempty bounded object. The receipt records the digest
of its compact host JSON encoding. `matchesResult` checks that same object and
current authority. It does not establish that an identity or field is acceptable.
The shared consumer must still make that decision. A positive receipt cannot
serve as a negative receipt. No guest or QML code can create either type.

`ArtworkAttemptAggregate::acceptPortablePositive` checks the exact result,
private subject, slot, contribution, context, account partition and native
gate. The slot stays reserved while the caller uses shared consumer acceptance
and image resolution. A failed image must block that slot. A later provider can
still succeed; the plan cannot then publish a not-found status.

After consumer acceptance, a fresh configuration check and native image
approval, the runner can call `trySealPositive`. The aggregate rechecks the
receipt and plan. It transfers only native Store watches, admission and any
retained native outcome authorities into `NativeArtworkAttemptKeeper`.
Core must accept the exact pending child and already approved local image.
Failure revokes admission. The seal grants no image approval and completes
no job.

After success, the aggregate releases its Qt plan, receipt and child captures
on the owner thread. The native keeper stays attached through child Complete,
Apply, PrepareFetchedArtwork, parent Complete and final Apply. It holds no Qt
value, job, claim or reader. Cancellation, a late result after seal or a Store
change can still revoke the same admission. Destruction of the normally sealed
aggregate does not revoke the transferred authority.

The older `positive` marker only ends negative accounting. It grants no
source or image authority. The serial runner must use the source seal.

## Evidence

Both suites pass normally and with ASAN/UBSAN on WSL, using Qt 6.8.3:

| Suite | Normal | Sanitizer |
| --- | --- | --- |
| Completion receipts | 86 | 86 |
| Artwork aggregate | 58 | 58 |
| Total | 144 | 144 |

There are no failed or skipped checks, runtime warnings or compiler warnings
in the successful cohort. Each build compiles 44 objects and pins its inputs:
1,454 normal inputs and 1,452 sanitizer inputs. Host producers and the native
fixtures are fresh. The separate process fixture is a native protocol test
program. The sanitizer run also links the focused instrumented Core library
from the preceding 13-suite Core proof. Existing baseline libraries stay intact.

The portable success cases use the real Manager preparation factory on a
joined test thread and invoke the prepared recipe. They pass the certified
result through the actual shared consumer, then child and final Core Apply.
The Core fixture supplies a native local-image approval; it does not fetch or
decode an image. Existing cases also use the Rust worker and French TV Wasm
module for no-match results. Native lookups use the existing deterministic
fixture. This proof does not cover a live positive provider, each shipped
network parser, the final image broker integration or a mounted Service runner.

The checks cover changed bytes, another subject/ordinal/context/contribution/
gate, duplicate delivery, raw artwork text, missing positive receipts, wrong
children, image failure with later success, configuration changes before
preparation or final Apply, cancellation, late results and deadline expiry.
Both status types pass real Core publication. A Store watch still prevents a
write after the Qt aggregate has been destroyed.

The final record is `/tmp/hakone-artwork-positive-host/final-proof.json`, SHA-256
`395b32cbe8314af46cfe33042b400a7c6bd698361d9b4a234e4222e80c09d7d4`.
Completed outputs are retained in verified compressed archives. After all links
finished, compiler objects were pinned and retained before the sanitizer tests;
the test programs and Core libraries stayed in place. The lowest observed free
space was 7,511,711,744 bytes. Free space after final retention was 7,721,512,960
bytes. No baseline or Windows package was replaced.

## Remaining work

Native positive claims and image permits now bind to the same source gate.
Build the serial Service runner with fresh saved configuration checks on joined
work, actual image resolution and all typed outcomes. Keep every provider and
local path until the cutover, cancellation, status and legacy-bypass checks pass.
The local Windows review build remains clean source `335ac989d0`.
