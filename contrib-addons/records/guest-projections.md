# Read-only guest projections (Step 6.5)

Step 6, row 5 of the [continuation plan](../07-continuation-plan.md).
Written 31 October 2026. Implements the read parts of `queue.read`,
`library.read` and `media.fingerprint` in
[the architecture](../01-architecture.md) (capability table).

## Before

`addon_host_services.cpp` named `library.query`, `queue.snapshot` and
`queue.apply`, but nothing registered them, and there was no guest
fingerprint operation.

## Operations

`AddonGuestProjections` (`addons/addon_guest_projections.*`) registers an
operation only when the app supplies its source, so a missing source is not
advertised. `mountAddonGuestSources` (`maininterface/addon_guest_sources.*`)
connects the main queue, the media library and the player.

| Operation | Permission | Request | Response |
| --- | --- | --- | --- |
| `queue.snapshot` | `queue.read` | `offset` (0 to 100000), `limit` (1 to 100, default 50) | `total`, `current`, `offset`, `items` with `id`, `title`, `artist`, `album`, `kind`, `durationMs` |
| `library.query` | `library.read` with `collections` and `fields` | `collection`, `query` (at most 128 characters), `offset` (0 to 10000), `limit` (1 to 50, default 25) | `collection`, `offset`, `more`, `items` with `id` and the granted fields only |
| `media.fingerprint` | `media.fingerprint` with `algorithms` | `algorithm`: `opensubtitles-hash` | `algorithm`, `value` (16 hex digits), `size` |

- **IDs.** An item ID is `<kind>:` and a keyed hash (HMAC-SHA-256 with a
  random key for each app run) of the package ID and the app's own ID. Two
  add-ons get different IDs for the same item, and an ID does not survive a
  restart. No URI, MRL or path is sent.
- **Library.** Collections and fields are `tracks` (title, durationMs, year,
  trackNumber), `albums` (title, artist, year, trackCount, durationMs),
  `artists` (title, albumCount, trackCount), `videos` (title, durationMs,
  year) and `playlists` (title, trackCount, durationMs). The query runs on
  the media library thread. A manifest that names another collection, field
  or hash algorithm is refused at load.
- **Queue.** A bounded copy under the playlist lock; titles fall back to the
  item name.
- **Fingerprint.** Only for the playing media when it is a local file
  (including a file on a network share). The file is read on background
  work, never on the UI thread, and the host's call limit can cancel it.

`queue.apply` (queue proposals under a gesture or session grant) stays
unregistered and unadvertised.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_guest_projections` (new) | source | 18 pass: operations offered only with a source and removed with the owner; opaque queue IDs that differ for each add-on and stay stable; 7 refused requests; granted collections and fields only (no unknown field or MRL); fingerprint of a 256 KiB file, refused without the algorithm grant or for a remote stream; 5 manifest scope rows |

All labels pass: profile 29, metadata 53, source 23, ui 7, app 6, session 4,
visualizer 12. `test_qt_addon_ui_assets` once reached its 30 s limit in the
parallel `ui` run; alone it passes in 12 s, twice.

Not done: `queue.apply` through the existing compare-and-swap queue adapter,
an install review line for each read permission, and an app run. Not run on
Windows.
