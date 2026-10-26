# Native playlist transaction boundary

This document specifies the library boundary for an add-on playlist append.
The implementation needs separate approval if the active request prohibits code changes.

## Required result

The host captures a playlist snapshot before it prepares a group of items.
The host then appends the group only if that snapshot still matches the database.
The append must reject a changed, deleted, replaced, or read-only playlist.
An error must leave the playlist unchanged.

All native playlist writers must use the same database boundary.
These writers include the user interface, background import, playlist discovery, and library maintenance.
A mutex around the add-on adapter cannot give this guarantee.
The existing `acquirePriorityAccess()` reservation cannot give this guarantee either.
More than one priority thread can hold that reservation.

## Public VLC API

Use a separate header, `include/vlc_addon_medialibrary.h`.
Keep the main public media library header unchanged.

```c
typedef struct vlc_addon_playlist_snapshot vlc_addon_playlist_snapshot_t;

bool vlc_addon_playlist_cas_available(vlc_medialibrary_t *ml);

vlc_addon_playlist_snapshot_t *vlc_addon_playlist_snapshot_create(
    vlc_medialibrary_t *ml, int64_t playlist_id);

void vlc_addon_playlist_snapshot_destroy(
    vlc_addon_playlist_snapshot_t *snapshot);

int vlc_addon_playlist_append_if_unchanged(
    vlc_medialibrary_t *ml,
    const vlc_addon_playlist_snapshot_t *snapshot,
    const int64_t *media_ids, size_t count);
```

Create and append run synchronously on the media library thread.
Destroy can run on any thread.
The snapshot contains fixed data and an owner identity.
It does not hold a library, database, module, or Qt reference.
An owner mismatch must fail.

The adapter must stop a canceled task before it calls append.
After append starts, that call is one atomic operation.
The adapter must not report cancellation as proof that a completed append was reversed.

The host can append at most 1,000 media IDs in one operation.
Every ID must be positive.
The native library must validate all IDs within the transaction.
A missing media record must cause a complete rollback.

Use private control query values for these operations.
The public wrappers can call the existing `vlc_ml_control()` function.
An old module returns unsupported through its normal unknown-query result.
No new core export is necessary.

## Library dependency API

Add a free-function extension to the media library dependency.
Keep its existing public class layouts and virtual tables unchanged.
Publish its feature through the installed package metadata.
Enable the VLC module code only when that feature exists.
An unavailable feature must leave the add-on playlist target unavailable.

The extension uses `Connection::newTransaction()`.
That function obtains the library write context and starts `BEGIN EXCLUSIVE`.
The existing nested append joins the outer transaction.
Only the outer transaction commits the operation.

Each playlist receives a random 256-bit snapshot token.
Store the token in a small auxiliary table with a playlist foreign key.
Install database triggers for the following changes:

| Table | Changes that replace the token |
| --- | --- |
| `Playlist` | Insert, update, and delete |
| `PlaylistMediaRelation` | Insert, update, and delete; include old and new playlist IDs |
| `File` | Insert, update, and delete for an associated playlist |

These triggers run for every database connection.
They cover background and import writes without changes to each caller.
A token changes after a remove-and-restore sequence, even when the final item order matches.
A new token prevents a deleted playlist ID from accepting an old snapshot.
The auxiliary table must not change existing playlist columns or counters.

Each snapshot operation validates the expected trigger definitions within its write transaction.
A database reset or a missing trigger invalidates all old tokens.
The operation then creates the required triggers and new tokens.
An unexpected table or trigger definition must fail or receive an explicit compatible repair.

Snapshot creation reads the current token after it validates the schema.
It rejects a missing playlist or a playlist with an associated file.
The associated file is the library's read-only playlist marker.

Append validates the schema, owner, token, playlist, and read-only state within one write transaction.
It then adds the complete group and commits once.
A concurrent writer must finish before this validation or wait until the commit.
An append must not start inside another caller-owned database transaction.

## Verification

Use the actual dependency and a temporary SQLite database.
Do not replace its transaction with a mock mutex.

| Case | Required result |
| --- | --- |
| Matching snapshot and valid IDs | Append the complete group in order |
| User append after snapshot | Reject the stale snapshot |
| Background or second-connection append | Reject the stale snapshot |
| Remove and restore the same member | Reject the stale snapshot |
| Move and restore the same order | Reject the stale snapshot |
| Playlist deletion and ID reuse | Reject the old snapshot |
| Read-only transition | Reject the append |
| Missing media ID in the second position | Preserve the complete original playlist |
| Failure on the second insert | Roll back the first insert and all counters |
| Two concurrent appends with one snapshot | Commit exactly one group |
| Missing snapshot trigger | Invalidate old snapshots and restore the required boundary |
| Unavailable dependency extension | Keep the native target unavailable |

Use the existing media library tests for playlist behavior as regressions.
Build only the dependency, media library module, and focused adapter tests for this boundary.
Preserve all working core features.
