# Guarded profile settings writes

Four `PackageStore` policy writes now have host-only guarded entry points:

- Provider order.
- Built-in provider enable or disable.
- Safe mode.
- Bundled add-on suppression.

Each entry point accepts the `StoreProfileWriteContext` from a guarded writer
callback. It uses the existing store lock and exclusive fence. It does not
take the store lock again. These are production store methods, tested with
real saved files. The peer control channel and application mount remain open.

## Commit and refusal

The store must match the context's canonical profile directory. A context for
another profile cannot write or invalidate that store. Provider updates retain
other features and built-in switches. The same validation and merge code
serves the existing ordinary methods.

Before publication, the local mutation gate closes old tokens and waits for
native cancellation callbacks. The guarded path checks the context after
those callbacks and again after the temporary file has been written, before
atomic commit. Direct-write fallback is off.

Invalid input or a profile mismatch returns RolledBack. Corrupt saved input
or an uncertain file write returns NeedsRecovery. The outer generation writer
keeps admission closed in that case. Cancellation also leaves the outer
writer in recovery. A successful write advances the saved revision once.
Treat a helper's saved output as provisional. Publish it only after the outer
writer returns Ready with `applied` set. A later stamp failure can still refuse
that outer result.

Guarded writes refuse a linked or special destination. Provider and bundled
policy reads use the bounded native catalogue reader. A FIFO is refused
before open. These checks do not grant an add-on a file path.

## Lock release and refresh

The generation layer now uses `CatalogueStoreLock`. Its destructor releases
`store.lock`, then sends the existing native display notice. The exclusive
fence remains held through that notice. Thus a refresh can retry after lock
release, while a peer still cannot pass an active writer's fence.

The regression pauses that notice in one thread. Another thread confirms
that the store lock is free and the peer fence is still blocked. The test
fails against the previous generation lock code, which sent no notice after
unlock. It passes with the current code.

No idle timer, polling loop, worker process or thread is added by this change.
All file work remains inside the explicit joined operation.

## Verification

| Suite | Linux normal | Linux ASAN/UBSAN | Native Windows |
| --- | --- | --- | --- |
| Guarded settings | 33 | 33 | 28 |
| Generation and writer | 34 | 34 | 34 |
| Native fence | 23 | 23 | 19 |
| Local Store authority | 13 | 13 | 13 |
| Total | 103 | 103 | 94 |

All cases pass, with no skip, runtime warning or sanitizer finding. Leak and
vptr checks are on. The extra Linux cases cover POSIX links, modes and FIFOs.
The separate previous-code control has the expected one failed refresh test.

The settings cases exercise saved files, all four guarded methods, ordinary
fixture setup, retained peer fences, a separate writer process, stale tickets,
paused native cancellation, cancellation during that pause, invalid input,
corrupt input, failed publication and canonical path aliases.

The focused Linux build compiles the actual store, generation, fence,
authority and notification units. The linker removes unused store functions. The Windows build also compiles the real manifest,
migration, private storage, repository, theme and lifetime dependencies
required by its linker. No test substitute supplies those implementations.
The Windows build uses the existing dependency cache read-only. Two existing
compiler warning types remain: the C++20 `requires` name and an unrelated
display-catalogue indentation warning. These are not runtime warnings.

The final records are in `/tmp/hakone-profile-store-settings/`. Source,
dependency, object, program and result hashes are retained. Completed outputs
are compressed and checked before raw files are removed. Duplicate library
archives are checked against their retained member objects before removal.
The earlier Windows review package and protected Core libraries are unchanged.
The final proof is `final-proof.json`, SHA-256
`746b1592e95c4e72ffd469990c376edfe6328e864a03ea99eb7efad9b6004a1c`.
The minimum recorded free space is 7,528,710,144 bytes. After cleanup, free
space is 7,607,640,064 bytes.

## Remaining integration

The current ordinary application callers still use their existing methods.
The guarded entry points are ready for the peer owner; they do not mount that
owner or establish cross-process invalidation by themselves. Native metadata
owner activation remains off.

Enable, rollback, package settings and package recovery now have guarded
bindings. See [package state writes](profile-package-writes.md). The remaining
store work now has [guarded lifecycle entry points](profile-lifecycle-writes.md)
for install, uninstall and distribution bootstrap. The
[retained writer scope](profile-writer-scope.md) now supports ticket negotiation.
Complete profile recovery, peer negotiation and the application mount remain open.
Guest data-only storage keeps its separate policy. The control channel must
bind each ticket to its connection and operation, revoke all joined and
joining peers, and close local readiness on channel loss. The application
must then mount startup, reconnect, refresh and drain without a GUI wait.

Full app performance, Windows cursor behavior and native Core final Apply
remain separate checks. See [profile coordination](profile-authority-coordination.md)
and [guarded generations](profile-generation-transactions.md).
