# Play queue

An add-on can read the play queue and remove items from it. It never gets a
path, a URL or a playlist index that it could keep: each item has an opaque
ID, and its address is an opaque key.

| Operation | Permission | What it does |
| --- | --- | --- |
| `queue.snapshot` | `queue.read` | Reads one page of the queue, and can check its local files. |
| `queue.apply` | `queue.modify` with `operations` | Changes the queue. The app supports `remove`. |

```json
"permissions": {
  "required": {
    "queue.read": {},
    "queue.modify": { "operations": ["remove"] }
  }
}
```

The install review shows "Read the current play queue" and "Change the play
queue: remove media". The app refuses a `queue.apply` operation that the
grant does not list.

## queue.snapshot

Request (all optional):

| Field | Meaning |
| --- | --- |
| `offset` | The first item, 0 to 100000. Default 0. |
| `limit` | 1 to 100 items. Default 50. |
| `check` | `true`: the app also finds out whether each local file is there. |

Reply:

| Field | Meaning |
| --- | --- |
| `offset`, `total`, `current` | This page's first index, the queue length, and the index of the current item (-1 if none). |
| `items` | The items of this page, in queue order. |
| `checkIncomplete` | `true` when the check stopped at its time limit (see below). |

Each item has:

| Field | Meaning |
| --- | --- |
| `id` | `queue:` and 32 hex digits. Different for each add-on and app run; the same for one item while the app runs. |
| `title`, `artist`, `album`, `durationMs` | When known. |
| `location` | `location:` and 32 hex digits. Two items of the queue have the same key only when they play the same address. Not sent for an item without an address. |
| `file` | `true` for a file on this computer (or, on Windows, a UNC path). |
| `availability` | Only with `check`, only for a `file` item. See below. |

Use `location` to find duplicates. The app compares normalized addresses:
`.` and `..` path parts are resolved, and the percent encoding is the same.
A local file is compared by its path. On Windows the comparison ignores case,
and on macOS it ignores the Unicode composition of the name (NFC and NFD).
Other systems compare the exact path, because there two names that differ
only by case can be two files.

### File checks

`availability` is one of:

| Value | Meaning |
| --- | --- |
| `present` | The file is there. |
| `missing` | The file is not there, but its folder is. It was deleted, moved or renamed. |
| `folderMissing` | The file's folder is not there either. A disk, a memory card or a network share may be disconnected. |
| `unknown` | The app cannot tell: the system refused access, a network share did not answer, or the check stopped at its time limit. |

The app asks the file system for the name only. It does not open the file,
so a cloud placeholder (OneDrive, iCloud Drive) is not downloaded, and a
sleeping disk is not read. `missing` and `folderMissing` come only from an
answer that the name is not there (`ENOENT` or `ENOTDIR`; on Windows "file
not found", "path not found", "invalid drive" or "drive not ready"). Every
other error is `unknown`. A network address that is not a file path
(`smb://`, `nfs://`, `http://` and other streams) has no `file` flag and is
not checked.

The check runs on the app's background work, two checks at a time, and each
folder that is not there is checked once in a page. A page answers within 3
seconds. The items that the check did not reach by then are `unknown`, and
the reply has `checkIncomplete: true`. A share that does not answer can keep
a check busy for minutes, so after `checkIncomplete`, read the next pages
without `check` and tell the user that some files were not checked.

Do not treat `folderMissing` or `unknown` like `missing`. Show them apart, and
let the user choose to remove them: the media can come back when the disk or
share is connected again.

## queue.apply

```json
{ "operation": "remove", "items": ["queue:3f…", "queue:a0…"] }
```

`items` has 1 to 1000 IDs from `queue.snapshot`. The app removes the items
that are still in the queue and ignores the other IDs: an item that the user
removed after your read, or an ID of an earlier app run. It removes them in
one pass under the queue lock.

Reply: `operation`, `removed` (the number of items removed) and `total` (the
queue length after the change).

Removal changes only the queue. It does not delete files or change the media
library or saved playlists. If the current item is removed, the app does what
it does when the user removes it.

Read the queue again after a change before you show it. Ask for a removal
only after the user selects it in your view: the permission lets the add-on
change the queue, but the user expects a change only from their own action.
