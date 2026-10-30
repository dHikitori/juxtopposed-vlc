# Background package list

The Extensions screen must not validate package files on the UI thread.
An actual Windows probe measured one existing `packages()` call at about
421 ms with a local temporary store and 1,598 ms with a WSL network path.
Repeated reads can freeze the screen for several seconds.

`AddonPackageSummaries` supplies a separate display list. The Manager captures
an owned reader in memory. Joined app work then reads and validates package
files and checks the captured credential store. The worker does not access a
Manager QObject. The existing synchronous package API and mutation checks stay
available to their current callers.

The service starts after an explicit view request. It runs one read at a time,
combines newer refresh requests, and refuses old generations or revoked store
authority. Loading clears the old display rows. Refusal has an error state;
it is distinct from a valid empty list. The display has a 256-row limit.

Package actions must call the memory-only `isCurrent()` check when requested
and again when a confirmation is accepted. Cached display rows do not grant
permission to change a package. Global temporary disable, request cancellation,
and prepared package review remain available while the list loads.

Manager retirement and app retirement close the service. Its worker keeps
owned data through the joined app drain. Service destruction does not wait
on a blocked file read and cannot publish a late result.

The focused current-source run passes 19 normal and 19 ASAN/UBSAN cases,
without failures or skips. Leak detection and stack-use-after-return checks
are enabled; runtime diagnostics are empty. Coverage includes exact
synchronous/asynchronous row parity for valid, disabled, unconfigured,
configured-secret, suppressed, saved-data and corrupt-state packages;
coalescing; peer revocation; deletion and callback reentry; quota refusal and
retry; and a real blocked state-file read while the GUI heartbeat continues.
That reader remains in the joined app drain after its view is closed.

The focused build compiles the actual Manager, PackageStore, StoreAuthority,
summary service, RepositoryJobs and app lifetime sources with instrumentation,
along with the test and fresh summary metadata. Other host dependencies and
Qt use pinned normal libraries. The unchanged Manager metadata is reused
from that archive; newly generated metadata matches after include-path
normalization. Both completed executables have verified compressed copies.
The source, recipe, log, metadata and archive pins are recorded in
`/tmp/hakone-package-summaries/final-validation.json`.

The first isolated link exposed an older archive's preference methods and
aggregate metadata/TLS definitions. Compiling the current Store and reusing
unchanged Manager metadata repaired the test closure. Its failed logs remain
separate. The controlled FIFO test's poll was made idempotent before its
first execution. A test-only failure-cleanup write emits an unused-result
build warning; it is not a sanitizer diagnostic.

The Windows rebuild and private app startup now pass. That driver confirms
private settings/store/module paths, seven installs and stable owned-window
responsiveness. It does not click Extensions. One initial cold-start stall
is recorded in [the build record](windows-local-build.md). Actual Windows
Extensions-click responsiveness remains open. No production source changed
during the focused proof.
