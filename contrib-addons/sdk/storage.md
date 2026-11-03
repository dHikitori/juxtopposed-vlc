# Guest private storage

The host implements `storage@1` with three operations. Each operation needs the
`storage.private` permission. The host grants no file path or secret-store access.

| Operation | Request fields | Result fields |
| --- | --- | --- |
| `storage.get` | `key` | `found`, optional `value`, `revision`, `quotaBytes`, `usedBytes` |
| `storage.set` | `key`, `value`, optional `expectedRevision` | `found`, `revision`, `quotaBytes`, `usedBytes` |
| `storage.remove` | `key`, optional `expectedRevision` | `found`, `removed`, `revision`, `quotaBytes`, `usedBytes` |

`found` describes the value before the operation. A missing value differs from
JSON `null`. An identical set or a remove of a missing key does not change the
revision. `expectedRevision` must agree with the current package data revision.
The revision applies to all account partitions in that package.

The host captures the installed release, grants, settings revision, and account
reference from a live worker invocation. The host derives the account reference
from its protected credential references and declared account fields. The guest
cannot select another account. An old request fails after a release, grant,
settings, or account change. The host repeats this check before result delivery.

## Limits

The byte quota is the smallest of the declared quota, the approved quota, and the
host limit of 1 MiB. It applies to the encoded JSON namespaces for all accounts.
An empty namespace set uses zero quota bytes. Snapshot headers do not use quota.

Each data snapshot has these additional limits:

- 16 account partitions and 1,024 keys in total.
- 128 characters per key. Keys start with an ASCII letter. Other characters are
  ASCII letters, digits, `_`, `.`, and `-`.
- 64 KiB per encoded JSON value, 16 levels, and 4,000 value nodes.
- 1,024 entries per array and 256 fields per object.
- 65,536 value nodes across the complete data snapshot.

The host rejects path syntax, unknown request fields, and nonfinite numbers. A
new account does not get more quota or keys.

## Writes and cancellation

Each `storage.set` and `storage.remove` is atomic: the host saves all of it or
none of it. A cancellation can stop the result from arriving after the host has
saved the write. Read the value again when you need to know.

## Update and recovery

An optional manifest `storage` object declares a positive integer `version` and a
`migrations` list. Each step has `from`, `to`, and `resource` fields. Each step
advances one schema version. The resource must occur in the package file list.
The update must supply every step from the installed schema to the new schema.

A migration resource uses this data format:

```json
{
  "formatVersion": 1,
  "operations": [
    {"op": "rename", "from": "oldTitle", "to": "title"},
    {"op": "default", "key": "language", "value": "en"},
    {"op": "remove", "key": "oldCache"}
  ]
}
```

The resource limit is 64 KiB and 128 operations. Each operation applies to every
account partition. A rename fails if both keys exist. A default preserves an
existing value. Migration has no script, path, network, or secret access.

A missing step, a key collision, or a quota failure keeps the old release
active. Data migration increases the revision if it changes the data. Rollback
returns to the saved release, settings and data together. It does not run a
reverse migration.

Removal with retained data saves an archive outside the removable package state.
Reinstallation can restore it only for the same package ID, source repository,
and source class. Full removal deletes that archive and its data files. A new
credential reference does not acquire data from the previous account partition.
