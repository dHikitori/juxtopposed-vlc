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
Old readers cannot become current again after a same-process change. External
saved-state changes are checked by `current`; the memory check alone cannot
detect an external file edit.

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

The focused suite passes nine normal and nine sanitizer cases, including
concurrent credential reads, missing setup, disabled packages, safe mode,
changed package files, key rotation, manager destruction, saved-state changes,
unapproved fields, and a controlled blocked native file read. The numeric
case also refuses an integer outside the native conversion range.

The focused sanitizer build instruments the manager, credential store, and
test. It enables address, undefined-behavior, float-cast-overflow, leak, and
stack checks. Diagnostics are empty. Other host dependencies use the current
normal archive in this build.

Related normal checks pass runtime 17, packages 33, UI images 29, and store
authority five. The runtime check uses the current Hakone-compatible worker
and has no skipped case. Full Qt app and supported-platform checks remain open.
