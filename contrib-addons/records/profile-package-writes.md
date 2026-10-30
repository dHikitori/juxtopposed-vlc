# Guarded package state writes

Enable, disable, rollback, settings save and package recovery now have guarded
`PackageStore` entry points. They accept the context from a profile writer or
recovery callback. The existing ordinary methods share the same locked
implementations. These methods do not take a second store lock.

The package store must match the context's canonical profile directory. The
guarded path refuses a linked package parent. A missing package returns a
coherent refusal. A damaged package or uncertain publication requires recovery.
The peer channel and application mount remain open. Native metadata owner
activation remains off.

## State and commit rules

Guarded state reads use the bounded native reader. They refuse special files
before open. A valid backup can supply recovery data, but it does not permit
ordinary enable, rollback or configuration changes. Those changes require an
explicit package recovery first.

Enable and settings writes use the strict catalogue reader to check the
release, manifest, grants, schema and saved configuration. A bundled package
that was suppressed cannot regain enable approval through rollback. Package
generation counters cannot exceed the exact JSON integer limit.

A guarded settings save requires the captured stamp:
`content-digest:package-generation:settings-revision`. A release that changes
and then returns to the same content still has a different generation. An old
settings form cannot save through that change. New values must pass schema
and credential-reference validation. Fields marked as secrets use credential
references.

All four package methods use one state commit path. Native cancellation ends
before the primary or backup state can change. The context is checked after
cancellation and at each file's final atomic commit. Backup failure prevents
primary publication. Cancellation after backup publication keeps the primary
unchanged and leaves the profile in recovery. The backup contains the previous
complete state.

Rollback and package recovery retain release history. Recovery selects a
verified saved release and keeps it disabled. A damaged primary can be
restored from a valid backup inside the outer recovery callback. Recovery of
one package does not prove that the whole profile is coherent.

The outer writer still determines the final result. A helper's Applied return
does not permit a UI update before the outer stamp is Ready. The existing
refresh notice runs after store unlock and before the exclusive fence ends.

## Verification scope

The tests install real package releases into temporary profiles. They cover
ordinary and guarded methods, enable and disable, retained rollback, settings
revisions, stale forms, missing packages, bundled approval, corrupt saved
fields, retained-release recovery, backup failure, native cancellation and
the interval between backup and primary publication. Linux also covers linked
parents and a FIFO primary.

The publication-interval test calls the actual `QSaveFile::commit`, then closes
the native test guard after the backup commit. The wrapper supplies no fake
file result. The GNU component builds enable this test on Linux and Windows.

The corrupt-digest regression fails against the exact earlier store object.
That object returned Ready for a corrupt saved digest. The corrected path
returns NeedsRecovery. The other three corrupt-field control rows pass with
the earlier object. Source and object hashes identify both implementations.

All 13 source units in the PackageStore component compile in each build. The
tests use the real manifest, migration, private storage, repository, theme and
lifetime code. The builds retain the actual producer inputs and results.
No full app, native Core final Apply, macOS runtime or Windows cursor result
is claimed by this component cohort.

| Suite | Linux normal | Linux ASAN/UBSAN | Native Windows |
| --- | --- | --- | --- |
| Package state writes | 57 | 57 | 55 |
| Existing policy writes | 33 | 33 | 28 |
| Profile generation | 34 | 34 | 34 |
| Total | 124 | 124 | 117 |

All cases pass, with no skip, runtime warning or sanitizer finding. Leak and
vptr checks are on. The earlier-object control has one expected failed case.
The Windows runs use the existing dependency cache read-only. Existing
compiler warnings about the C++20 `requires` name and display-catalogue
indentation remain outside the changed behavior.

The final record is `/tmp/hakone-profile-package-writes/final-proof.json`,
SHA-256 `b6bb5a315daea2728fce0d282c10e450f1f420ac52464583e641e740987650da`.
The source snapshot has 116 files. Source, object, program, dependency, recipe
and result hashes are retained. Completed outputs are checked and compressed
before raw files are removed. Duplicate library archives are checked against
their retained objects before removal. The protected Core libraries and
Windows review package are unchanged.

Older completed sanitizer executables were retired after all matching source,
object and recipe inputs were checked. Their result records remain. An older
subtitle test was preserved as exact compressed bytes. These cleanup records
are in the same proof directory. The minimum recorded free space is
7,521,488,896 bytes. After cleanup, free space is 7,567,220,736 bytes.

## Remaining integration

Guarded install, uninstall and distribution bootstrap are now implemented.
See [lifecycle writes](profile-lifecycle-writes.md). Complete profile recovery
must still validate policy documents, all package state and transaction remnants.
The peer channel must bind a ticket to its connection and operation, revoke
joined and joining peers, and keep readiness closed through channel loss.
Then mount startup, reconnect, refresh and drain on joined work.

No idle timer, polling loop, thread or worker process is added. The current
Windows review package is unchanged. Full app performance and cursor checks
remain open. See [profile coordination](../profile-authority-coordination.md),
[guarded generations](profile-generation-transactions.md) and
[guarded policy writes](profile-store-settings.md).
