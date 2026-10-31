# Discovery change feeds and queue filling (Step 6.6)

Step 6, row 6 of the [continuation plan](../07-continuation-plan.md).
Written 31 October 2026. Implements `subscribe` and the "keep the queue
filled" rules of [providers and discovery](../03-providers-and-discovery.md)
(lines 149 and 170). Author guide: [discovery sources](../sdk/discovery-sources.md).

## Before

The coordinator removed `subscribe` from every source and refused it.
`suggest` returned one bounded page; nothing could keep the queue filled.

## Change feed

- **Coordinator.** `subscribe(source, node, revision, view, generation)`
  accepts a folder handle of the same view (or the root) and a revision of
  visible ASCII. The reply is processed as a page, plus `removed` (mapped to
  this view's handles, which are then retired), `revision` and
  `nextCheckSeconds` (clamped to 60 to 3600). A `no_match` reply keeps the
  revision. Root and folder pages pass `revision` and `nextCheckSeconds` too.
- **Service.** After a root or folder page, an open view with a declared
  `subscribe` schedules one check (300 s by default). New items go first,
  changed items are replaced in place and removed items go. Navigation, a
  search or closing the view ends the feed; a failed check ends it and keeps
  the rows. One timer exists only while a view is open and checks.

## Queue filling

`AddonDiscoveryService::startQueueFill(view, maxPending)` (1 to 10; the page
uses 5) starts one session for the view's source when it declares
`suggest` and the queue can take its items. The session:

- has its own coordinator view and a random fill tag;
- reacts to queue changes only (`AddonNativeQueueStore::revisionChanged` →
  `noteQueueChanged`, coalesced), with at most one `suggest` every 5 s;
- asks for `maxPending − pending` when at most one of its entries is ahead
  (`pendingAhead` counts entries after the current one that carry the tag);
- appends through the existing transaction (`append` with `fillTag` and
  `fillCredit`); the native store keeps both as item meta
  (`vlc:addon-fill`, `vlc:addon-fill-source`). A failed append returns its
  items to the next request; three failures end the session;
- skips items it already added, and items whose source reference is not
  ready;
- keeps the display demand while it runs, so it survives the page closing;
- stops on Stop, a source, account or add-on change, two answers with
  nothing new, or app close. Queued entries stay.

The discovery page shows **Keep the queue filled**, then a status line with
**Pause**/**Resume** and **Stop**.

## Tests

| Test | Label | Result |
| --- | --- | --- |
| `test_qt_addon_discovery` | source | 27 pass, 2 new: a feed with added, changed and removed items, clamps and no-change revision; a feed needs a folder of the same view and a correct revision |
| `test_qt_addon_discovery_service` | source | 21 pass, 3 new: an open view checks its feed until it closes (not for a search); a fill keeps a few suggestions ahead, skips repeats, pauses and ends when the source has nothing new; a fill ends on an account change and is not offered without a queue that can count its entries |
| `tst_AddonDiscovery` | (qml) | New: fill is an explicit choice with Pause and Stop |

All labels pass: profile 29, metadata 53, source 23, ui 7, app 6, session 4,
visualizer 12.

Not done: the queue does not yet show the source credit on filled entries,
a fill session does not seed `suggest` with the playing item, and there is
no app run. Not run on Windows.
