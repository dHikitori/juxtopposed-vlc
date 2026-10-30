# Store authority epoch

`PackageStore::authorityToken()` captures a same-process authority token from
the retained store. This call and the token's `current()` check use atomic
memory reads only. They have no file access, lock, allocation, or `QObject` call.
Capture the token before the background state reads. Attach it to the result.

`captureStoreAuthority(directory)` also normalizes the directory. That operation
can read the filesystem. Use it during initialization or on a background thread.
Use the retained store method on the GUI thread.

Each `PackageStore` shares one epoch for its normalized directory. A mutation
gate starts under the existing `store.lock`, before the first visible authority
write. It marks a write in progress and advances the epoch. Its destructor
advances the epoch again and clears that mark, after success or failure. An old
token cannot become current again. A capture during a write stays invalid.

The gates cover release, grant, settings, enablement, rollback, recovery, safe
mode, distribution suppression, and the uninstall rename. Private guest storage
data does not change source authority and does not advance this epoch.

Background readers still check the captured package and configuration against
the store. The GUI checks the memory token before it publishes a result or uses
a cached result. An image provider or trusted pixel accessor must also reject a
ticket whose token is no longer current. A previously published ticket does not
gain permanent authority.

This guard closes the same-process write and delivery gap. It does not observe
a separate process or a manual disk edit. Such edits need the existing store
checks at the next admission. Do not claim that this token provides a
cross-process transaction or can recall pixels already sent to a renderer.
