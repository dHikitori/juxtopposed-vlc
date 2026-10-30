# Native source retirement

The source resolver registers a package invalidation watch before saved
admission work starts. An admitted source also retains a composite consent
watch. That watch covers package, owner, receipt, profile, and exact binding
authority. An approval-only token can report saved permission. It cannot create
a playback input intent.

Each composite watch calls one retained native revoker. Repeated and concurrent
invalidators wait for that call to finish before they return. Its callback and
captured state use native or standard C++ operations. They must not wait for a
Qt event or change authority. Watch quotas remain held through capture cleanup.

Resolver revocation invalidates the exact native policy, cancels the attached
transport, and ends a pending native input wait. The Core lease completion
barrier also applies when Stop or backend Close already started cancellation.
The source guard becomes false before later provider host calls can enter.
The worker's per-invocation guard enforces that check at each request boundary.

Native cancellation uses a bounded mailbox of job identities and input
generations. It posts no Qt event and retains no job through that mailbox.
The owner drains it on its thread. Overflow closes native admission before
backend cancellation. The backend reference in the gate is weak.

`AddonSourceConsent::retire()` withdraws native authority without taking the
consent-file mutex. Joined cleanup later calls `close()`. The app must retire
the owner before it drains joined work and releases Core. A view closing does
not revoke saved consent.

The final focused suites pass 27 consent and 41 resolver cases in each normal
and sanitizer build. There are no failures, skips, or sanitizer diagnostics.
The two added actual-Core cases hold Stop or backend Close inside a transport
callback and prove that concurrent invalidation waits for completion. Other
cases cover repeated retirement, early package invalidation, pending and
issued transports, and retained capture cleanup.

The proof is `/tmp/hakone-source-foundation-validation.json`. The six consent,
resolver, and test sources are the frozen production checkpoint. Its queue
support hashes describe the code used for those builds. Queue playback and
reentry changes continue in the separate owner group and need a coherent
producer/consumer build. The shared Core hash is
`dbcc86575828c2bfbe8ac04df42f1f2b0789c73f4ffab59eff03dcb38ca1cac4`.
Only the focused sources and tests use memory instrumentation. The production
source owner, queue assembly, consent panel, and full app run remain open.
