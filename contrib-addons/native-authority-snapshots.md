# Native authority snapshots

`AddonManager::nativeAuthorityReader` prepares a reader on the manager thread.
Preparation copies memory only. It does not open a file, read the secure store,
or retain the manager. Invoke the reader on joined background work.

The reader verifies the installed package, content digest, exact contribution,
enabled state, recovery state, safe mode, settings, and required account data.
It returns copied native host data and an opaque account reference. The account
reference uses the same rules as the existing manager UI.

The returned `live` check reads the shared store epoch in memory. Use it in
native cancellation and result delivery. The `current` check also compares the
saved release, source identity, grants, settings, and revision. Run that check
on background work before access. It checks the memory epoch again after the
file read. A blocked file read cannot block the memory check or revocation.

Manager destruction invalidates the shared store epoch before service teardown.
Package, account, settings, and permission writes also invalidate that epoch.
Executable withdrawal invalidates it before Worker stop and old request
completion, even when no saved-state write is needed.
Old readers cannot become current again after a same-process change. External
saved-state changes are checked by `current`; the memory check alone cannot
detect an external file edit.

## Native revocation

`onInvalidated` registers a native cancellation callback for the captured
epoch. Register it before a native lease is granted. Keep the returned
`StoreAuthorityWatch` until that lease ends. Check the watch and `live` again
after the grant. If either check fails, revoke that exact lease before it can
be used. A missing or refused registration must refuse the lease.

A store change invalidates the epoch, then calls native cancellation before
file publication. Concurrent invalidations also wait for earlier cancellation
callbacks. A refused registration calls its callback before return. The shared
limit is 256 retained callbacks per store. A callback keeps its slot until its
last capture has been destroyed, including after cancellation.

Callbacks run outside the callback, package, and registry locks. They must use
native state only. They must not throw, wait for Qt, or call back into the
store, except to read `current`. A store change must not start while its caller
holds a native lock that the callback needs. Use an immutable cancellation
handle for the exact native lease. A separate UI cancel action cannot replace
that handle while native revocation is in progress.

Manager retirement first refuses new readers and workers, then invalidates
native authority. Its `retiring` signal follows native revocation and precedes
worker shutdown. Owners can close their Qt services at that point. This signal
does not replace the native callback or the application work drain.

Credential reads use an owned `CredentialStore` snapshot with a private mutex.
Each snapshot serializes its cache and platform-store reads. It has no QObject.
The snapshot clears its cached bytes when its last reader ends. This low-level
snapshot does not grant permission to use a credential.

The native authority resolver permits only fields from the exact intersection
of declared and approved credential bindings. It checks current configuration
before and after a secret read. The network adapter must also match the exact
approved origin and header binding. Never pass this native resolver or its
values to a guest. Worker messages continue to carry presence flags only.

Protected settings recognize the same three schema forms in the manager and
the store: `x-hakone-secret`, `writeOnly`, and password format. A protected field
can be saved without a network binding. A native resolver cannot read that
field until an exact declared binding has permission.

## Validation

The focused suite passes ten normal and ten sanitizer cases, including
concurrent credential reads, missing setup, disabled packages, safe mode,
changed package files, key rotation, manager destruction, saved-state changes,
unapproved fields, and a controlled blocked native file read. The numeric
case also refuses an integer outside the native conversion range. Native
revocation precedes manager retirement, which refuses fresh readers.

The focused sanitizer build instruments the manager, credential store, and
test. It enables address, undefined-behavior, float-cast-overflow, leak, and
stack checks. Diagnostics are empty. Other host dependencies use the current
normal archive in this build.

The current normal group passes 185 cases: native authority ten, store
authority 13, runtime 27, packages 54, broker 70, and host services 11. The
runtime check uses the current Hakone-compatible worker. No case is skipped.
Full Qt app and supported-platform checks remain open.

The new native revocation suite passes 13 cases in both normal and sanitizer
builds. It covers publication ordering, concurrent invalidations, refused
registrations, the retained quota, capture destruction, shared epoch lifetime,
and 200 registration races. Address, undefined-behavior, leak, and stack checks
have no diagnostics. The manager sanitizer build uses the normal archive for
other host dependencies; the separate store suite instruments the revocation
implementation itself.
