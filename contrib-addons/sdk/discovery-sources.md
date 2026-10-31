# Discovery source contract

This page describes the current `discovery@1` host. The [architecture proposal](../03-providers-and-discovery.md) also describes future functions.

## Contribution

A source needs a `discovery.source` contribution, a `discovery@1` contract, and a `wasm-message-v1` runtime. Declare each supported operation in `operations`.

```json
{
  "id": "stations",
  "type": "discovery.source",
  "contract": "discovery@1",
  "operations": ["roots", "browse", "search", "resolve"]
}
```

An absent operation list permits no operations. The host does not infer support from the contribution type. Network access and credential use need separate permissions.

## Requests

The host invokes an operation through the common worker message protocol. The request context has these fields:

| Operation | Context fields |
| --- | --- |
| `roots` | Optional `cursor` from a previous root page |
| `browse` | `nodeRef`: the source item ID of a selected folder; optional `cursor` |
| `search` | `query`, `filters`, and optional `cursor` |
| `suggest` | `limit`, optional `identity`, and optional `language` |
| `resolve` | `itemRef`: the stable source item ID; `intent`: `play` or `enqueue` |
| `subscribe` | `collectionRef`: the folder's source item ID, or empty for the root; `revision`: the last revision the source sent, or empty |

A query has a maximum of 512 UTF-16 code units. Filters have a maximum of 32 scalar fields. Each filter value has a maximum string representation of 256 UTF-16 code units. A suggestion limit must be from 1 through 50. Identity uses the fixed host media identity fields.

The current browser supplies roots, folder navigation, search, and more pages. The coordinator has the separate suggestion API.

## Change feed (`subscribe`)

While the user has a root or folder open (not a search), the host checks it
again. A root, folder or change-feed page can add:

- `revision`: 1 to 256 visible ASCII characters. The host sends it back in
  the next `subscribe`.
- `nextCheckSeconds`: when to check again. The host uses 60 to 3600 seconds,
  and 300 when the page has no value.

A `subscribe` reply is a page of new and changed items, plus `removed`: the
IDs of items to take away (at most 200). Report no change with `no_match`
and the same `revision`. New items appear first. An item that is also in
`removed` stays. A failed check ends the feed for that view; the items shown
stay. The host stops checking when the view closes or shows another page.

## Keeping the queue filled

When a source declares `suggest` and the queue can take source items, the
page offers **Keep the queue filled**. The host then asks `suggest` (with
`limit` only) whenever at most one of its suggestions is still ahead of the
playing item, at most every 5 seconds, and appends up to five. It never
removes or reorders entries, and it does not add an item it already added in
the same session. Each entry records the source name. The user can pause or
stop it; it also stops when the source or its account changes or after two
answers with nothing new. Return only items that can play from the queue.

## Pages

Return `match`, `partial`, or `no_match` through the common result protocol. A page has an `items` array, a `nodes` array, or both. Its maximum combined size is 200 entries.

```json
{
  "items": [
    {"id": "station-42", "kind": "media", "title": "Station 42", "mediaKind": "audio"},
    {"id": "regional", "kind": "node", "title": "Regional stations"}
  ],
  "cursor": {"offset": 200}
}
```

The `kind` defaults to `media`. An entry in `nodes` always has the `node` kind. A page cannot contain duplicate item IDs. The host removes entries with an incorrect ID or kind. An ID must be a nonempty string of at most 512 UTF-16 code units. Control characters and incorrect Unicode surrogate pairs are not permitted.

The host displays `title` as plain text, with a maximum of 512 UTF-16 code units. It limits `mediaKind` to 32 UTF-16 code units. Optional fields are `identity` and `available`. The host removes URLs, playback options, and other fields from the displayed entry.

A cursor can be JSON data with a maximum encoded size of 4 KiB. The host retains the cursor. The interface receives a separate opaque reference. This reference is valid only for the source, operation, view, generation, request context, and active source configuration that created it. Root pages use the same rules as browse and search pages.

## View lifetime

The browser owner permits 16 concurrent views. Each view has a maximum of 1,000 displayed entries. The coordinator has limits of 64 requests, 4,096 item handles, and 512 cursors. A request has a time limit of 15 seconds.

Each view has its own item and cursor references. A reference from one view cannot act on another view. A new search cancels the previous request in that view. The host closes the view when it becomes hidden, its source changes, or its settings change. Late results cannot change a closed view.

## Saved source references

The host queue adapter accepts this internal format:

```json
{
  "format": "hakone-addon-source-v1",
  "source": "org.example.radio/stations",
  "itemId": "station-42",
  "accountReference": "<host account reference>",
  "snapshot": {"title": "Station 42", "mediaKind": "audio"}
}
```

The host supplies `accountReference`. The package cannot select an account reference. A missing account reference does not permit a production source to start. Credential reference changes and declared `settings.accountFields` changes produce a new account reference. A code update, title change, or display-language change preserves the account reference.

The locator consists of the source contribution ID, account reference, and stable item ID. Use the same item ID for the same source item. A title change must not change this ID. If a source has distinct item versions, its item ID must identify the required version.

The native URI starts with `hakone-addon-source:v1:`. It contains a canonical locator and a random admission ID. The private host admission record binds the exact locator. The record keeps the display snapshot separately. Repeated selection and title updates reuse the same URI. No resolved stream URL or credential is stored in that URI or snapshot.

Decoding an imported URI gives no playback authority. An unknown admission ID or changed locator fails validation. A known record still needs the current installed source, account, grants, and user playback intent. A copied admission record does not enable its source.

The real queue adapter takes a queue identity and revision before an action. It does the final comparison under the native queue lock. One append operation adds the full selection. A stale revision adds no items. Display refresh keeps the native queue item ID and any title that the user set. Queue export uses the admitted source URI.

## Playback status

The app supplies contained native preparation and a lazy resolver for admitted source references. Play and Enqueue need an active native route and the user's saved approval for the exact source and account. Browse and search do not grant that approval.

Enqueue stores the stable locator without a stream lookup. Play uses that same locator after the final source, account, and queue checks. Only a native primary input open creates the one-use playback intent and asks the source to resolve it. Stop and replay use a fresh input generation and source result. The package cannot supply native input pointers, Hakone options, output configuration, or an access module name.

The app also binds the native library adapter when the media library is available. The trusted picker supplies an opaque playlist choice. The adapter validates and appends the selection under one database transaction. Saving a reference does not resolve a stream or grant later playback approval.

The native owner, approval panel, queue route, and actual MainUI object have focused checks. The fresh full application link, compiled QML, and supported-platform app checks remain open.

## Source and tests

The [coordinator](../../modules/gui/qt/addons/addon_discovery_coordinator.cpp) validates pages and owns transient references. The [view owner](../../modules/gui/qt/addons/addon_discovery_service.cpp) handles browser lifetime. The [admission store](../../modules/gui/qt/addons/addon_source_admissions.cpp) binds saved locators. The [native queue adapter](../../modules/gui/qt/addons/addon_native_queue_store.cpp) uses the real Hakone queue.

The [queue tests](../../modules/gui/qt/tests/test_addon_native_queue_store.cpp) use real queue objects and the public add-on settings service. They cover atomic append, stale snapshots, playlist export, title refresh, credential changes, account-field changes, and code updates.
