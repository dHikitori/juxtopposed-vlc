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

A query has a maximum of 512 UTF-16 code units. Filters have a maximum of 32 scalar fields. Each filter value has a maximum string representation of 256 UTF-16 code units. A suggestion limit must be from 1 through 50. Identity uses the fixed host media identity fields.

The current browser supplies roots, folder navigation, search, and more pages. The coordinator has the separate suggestion API. The current host does not support `subscribe`.

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
  "format": "vlc-addon-source-v1",
  "source": "org.example.radio/stations",
  "itemId": "station-42",
  "accountReference": "<host account reference>",
  "snapshot": {"title": "Station 42", "mediaKind": "audio"}
}
```

The host supplies `accountReference`. The package cannot select an account reference. A missing account reference does not permit a production source to start. Credential reference changes and declared `settings.accountFields` changes produce a new account reference. A code update, title change, or display-language change preserves the account reference.

The locator consists of the source contribution ID, account reference, and stable item ID. Use the same item ID for the same source item. A title change must not change this ID. If a source has distinct item versions, its item ID must identify the required version.

The native URI starts with `vlc-addon-source:v1:`. It contains a canonical locator and a random admission ID. The private host admission record binds the exact locator. The record keeps the display snapshot separately. Repeated selection and title updates reuse the same URI. No resolved stream URL or credential is stored in that URI or snapshot.

Decoding an imported URI gives no playback authority. An unknown admission ID or changed locator fails validation. A known record still needs the current installed source, account, grants, and user playback intent. A copied admission record does not enable its source.

The real queue adapter takes a queue identity and revision before an action. It does the final comparison under the native queue lock. One append operation adds the full selection. A stale revision adds no items. Display refresh keeps the native queue item ID and any title that the user set. Queue export uses the admitted source URI.

## Playback status

The default application gate does not yet supply a native discovery transport. Browse and search work with this gate. Play and Enqueue remain disabled. A saved source reference is not a stream URL.

A host that enables playback must supply both contained native preparation and a lazy resolver for admitted source references. The host consumes a one-use playback handle after the final current-item and queue revision checks. The package cannot supply native input pointers, VLC options, output configuration, or an access module name.

The current production mount does not append to media-library playlists. This path needs the library transaction API that validates and appends under one database transaction. A UI revision comparison or a mutex on one writer path is insufficient.

## Source and tests

The [coordinator](../../modules/gui/qt/addons/addon_discovery_coordinator.cpp) validates pages and owns transient references. The [view owner](../../modules/gui/qt/addons/addon_discovery_service.cpp) handles browser lifetime. The [admission store](../../modules/gui/qt/addons/addon_source_admissions.cpp) binds saved locators. The [native queue adapter](../../modules/gui/qt/addons/addon_native_queue_store.cpp) uses the real VLC queue.

The [queue tests](../../modules/gui/qt/tests/test_addon_native_queue_store.cpp) use real queue objects and the public add-on settings service. They cover atomic append, stale snapshots, playlist export, title refresh, credential changes, account-field changes, and code updates.
